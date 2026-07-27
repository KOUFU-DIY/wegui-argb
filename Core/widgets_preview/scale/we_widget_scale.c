/**
 * @file  we_widget_scale.c
 * @brief 刻度尺控件（preview）：基线 + 主/小刻度 + 主刻度数字 + 三角指针
 *
 * 刻度线全部用 we_fill_rect 1px 竖/横条拼出（不走 AA 线，横平竖直且省事）；
 * 数字用 we_draw_string 绘制（内部小栈缓冲整数格式化，不依赖 stdio）；
 * 指针 = WE_SCALE_PTR_LEN 条递减宽度 fill_rect 拼出的实心小三角。
 * 指针滑动经单个中央动画节点推进（不占 GUI timer 槽），全程 int32 整数运算。
 */

#include "we_widget_scale.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _scale_draw_cb(void *ptr);
static uint8_t _scale_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _scale_class = {
    .draw_cb    = _scale_draw_cb,
    .event_cb   = _scale_event_cb,
    .set_pos_cb = NULL /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（setter 幂等判断用）。
 * @param a 颜色 A。
 * @param b 颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _scale_col_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#else
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 将数值钳制到量程内。
 * @param obj 控件对象指针。
 * @param v 输入值。
 * @return 钳制后的值。
 */
static int32_t _scale_clamp(const we_scale_obj_t *obj, int32_t v)
{
    if (v < obj->v_min)
        return obj->v_min;
    if (v > obj->v_max)
        return obj->v_max;
    return v;
}

/**
 * @brief 量程内数值线性映射为轴向像素偏移（相对刻度起点）。
 * @param obj 控件对象指针。
 * @param v 数值（应已钳制在量程内）。
 * @return 轴向像素偏移 0..len-1。
 * @note 纯 int32：len <= 320（len-1 < 2^9），要求量程跨度 < 2^22 防乘法溢出。
 */
static int32_t _scale_value_to_pos(const we_scale_obj_t *obj, int32_t v)
{
    int32_t span = obj->v_max - obj->v_min;

    if (span <= 0)
        return 0;
    return ((v - obj->v_min) * (int32_t)(obj->len - 1U)) / span;
}

/**
 * @brief 将 int32 格式化为十进制字符串（不依赖 stdio，支持 INT32_MIN）。
 * @param v 待格式化整数。
 * @param buf 传出缓冲区，容量至少 12 字节（"-2147483648" + '\0'）。
 * @return 无。
 */
static void _scale_fmt_i32(int32_t v, char *buf)
{
    char tmp[11];
    uint32_t u;
    uint8_t n = 0U;
    uint8_t i = 0U;

    if (v < 0)
    {
        buf[i++] = '-';
        u = (uint32_t)(-(v + 1)) + 1U; /* 先 +1 再取负，避开 INT32_MIN 取负未定义 */
    }
    else
    {
        u = (uint32_t)v;
    }

    do
    {
        tmp[n++] = (char)('0' + (u % 10U));
        u /= 10U;
    } while (u != 0U);

    while (n > 0U)
        buf[i++] = tmp[--n];
    buf[i] = '\0';
}

/* --------------------------------------------------------------------------
 * 绘制 / 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 绘制指针小三角（递减宽度 fill_rect 拼装，行/列均裁剪进包围盒）。
 * @param obj 控件对象指针。
 * @param pos 指针轴向像素偏移（0..len-1）。
 * @return 无。
 */
static void _scale_draw_pointer(const we_scale_obj_t *obj, int32_t pos)
{
    we_lcd_t *lcd = obj->base.lcd;
    int16_t i;

    if (obj->orientation == WE_SCALE_H)
    {
        /* 尖端朝下贴基线：第 0 行最宽，向下逐行收窄到 1px */
        int16_t px = (int16_t)(obj->base.x + (int16_t)pos);

        for (i = 0; i < WE_SCALE_PTR_LEN; i++)
        {
            int16_t half = (int16_t)(WE_SCALE_PTR_LEN - 1 - i);
            int16_t rx0  = (int16_t)(px - half);
            int16_t rx1  = (int16_t)(px + half);

            /* 横向裁剪进包围盒，防止端点处三角越界残留 */
            if (rx0 < obj->base.x)
                rx0 = obj->base.x;
            if (rx1 > (int16_t)(obj->base.x + obj->base.w - 1))
                rx1 = (int16_t)(obj->base.x + obj->base.w - 1);
            if (rx0 > rx1)
                continue;
            we_fill_rect(lcd, rx0, (int16_t)(obj->base.y + i),
                         (uint16_t)(rx1 - rx0 + 1), 1U, obj->pointer_color, 255U);
        }
    }
    else
    {
        /* 尖端朝右贴基线：第 0 列最高，向右逐列收窄到 1px */
        int16_t py = (int16_t)(obj->base.y + (int16_t)pos);

        for (i = 0; i < WE_SCALE_PTR_LEN; i++)
        {
            int16_t half = (int16_t)(WE_SCALE_PTR_LEN - 1 - i);
            int16_t ry0  = (int16_t)(py - half);
            int16_t ry1  = (int16_t)(py + half);

            if (ry0 < obj->base.y)
                ry0 = obj->base.y;
            if (ry1 > (int16_t)(obj->base.y + obj->base.h - 1))
                ry1 = (int16_t)(obj->base.y + obj->base.h - 1);
            if (ry0 > ry1)
                continue;
            we_fill_rect(lcd, (int16_t)(obj->base.x + i), ry0,
                         1U, (uint16_t)(ry1 - ry0 + 1), obj->pointer_color, 255U);
        }
    }
}

/**
 * @brief 控件绘制回调：基线 -> 主/小刻度与数字 -> 指针。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _scale_draw_cb(void *ptr)
{
    we_scale_obj_t *obj = (we_scale_obj_t *)ptr;
    we_lcd_t *lcd;
    int32_t span;
    uint32_t major_cnt;
    char num_buf[12];

    if (obj == NULL)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    span = obj->v_max - obj->v_min;
    if (span <= 0 || obj->len < 2U)
        return;

    /* 主刻度密度护栏：刻度比像素还密时整组跳过（基线与指针照画） */
    major_cnt = (uint32_t)(span / (int32_t)obj->major_step) + 1U;

    if (obj->orientation == WE_SCALE_H)
    {
        int16_t line_y = (int16_t)(obj->base.y + WE_SCALE_PTR_LEN);
        int16_t tick_y = (int16_t)(line_y + WE_SCALE_LINE_W);
        int16_t text_y = (int16_t)(tick_y + WE_SCALE_MAJOR_LEN + WE_SCALE_TEXT_GAP);

        /* 基线（水平通长） */
        we_fill_rect(lcd, obj->base.x, line_y, obj->len, WE_SCALE_LINE_W,
                     obj->line_color, 255U);

        if (major_cnt <= (uint32_t)obj->len)
        {
            int32_t v = obj->v_min;

            while (v <= obj->v_max)
            {
                int32_t pos = _scale_value_to_pos(obj, v);
                int16_t px  = (int16_t)(obj->base.x + (int16_t)pos);

                /* 主刻度长线（1px 竖条） */
                we_fill_rect(lcd, px, tick_y, 1U, WE_SCALE_MAJOR_LEN,
                             obj->line_color, 255U);

                /* 主刻度数字：居中于刻度下方，横向钳制进包围盒防越界残留；
                 * 数字比整尺还宽的退化场景直接跳过 */
                {
                    int16_t tw;
                    int16_t tx;

                    _scale_fmt_i32(v, num_buf);
                    tw = (int16_t)we_get_text_width(obj->font, num_buf);
                    if (tw <= obj->base.w)
                    {
                        tx = (int16_t)(px - tw / 2);
                        if (tx < obj->base.x)
                            tx = obj->base.x;
                        if (tx > (int16_t)(obj->base.x + obj->base.w - tw))
                            tx = (int16_t)(obj->base.x + obj->base.w - tw);
                        we_draw_string(lcd, tx, text_y, obj->font, num_buf,
                                       obj->text_color, 255U);
                    }
                }

                /* 小刻度短线：仅在完整主刻度区间内按像素等分插值 */
                if (obj->minor_div > 0U && (obj->v_max - v) >= (int32_t)obj->major_step)
                {
                    int32_t pos_next = _scale_value_to_pos(obj, v + (int32_t)obj->major_step);
                    uint8_t j;

                    for (j = 1U; j <= obj->minor_div; j++)
                    {
                        int32_t mpos = pos + (pos_next - pos) * (int32_t)j /
                                             (int32_t)(obj->minor_div + 1U);
                        we_fill_rect(lcd, (int16_t)(obj->base.x + (int16_t)mpos), tick_y,
                                     1U, WE_SCALE_MINOR_LEN, obj->line_color, 255U);
                    }
                }

                if ((obj->v_max - v) < (int32_t)obj->major_step)
                    break; /* 差值判断防 v += step 直接加法溢出 */
                v += (int32_t)obj->major_step;
            }
        }
    }
    else
    {
        int16_t line_x = (int16_t)(obj->base.x + WE_SCALE_PTR_LEN);
        int16_t tick_x = (int16_t)(line_x + WE_SCALE_LINE_W);
        int16_t text_x = (int16_t)(tick_x + WE_SCALE_MAJOR_LEN + WE_SCALE_TEXT_GAP);
        uint16_t line_h = we_font_get_line_height(obj->font);

        /* 基线（垂直通长） */
        we_fill_rect(lcd, line_x, obj->base.y, WE_SCALE_LINE_W, obj->len,
                     obj->line_color, 255U);

        if (major_cnt <= (uint32_t)obj->len)
        {
            int32_t v = obj->v_min;

            while (v <= obj->v_max)
            {
                int32_t pos = _scale_value_to_pos(obj, v);
                int16_t py  = (int16_t)(obj->base.y + (int16_t)pos);

                /* 主刻度长线（1px 横条） */
                we_fill_rect(lcd, tick_x, py, WE_SCALE_MAJOR_LEN, 1U,
                             obj->line_color, 255U);

                /* 主刻度数字：位于刻度右侧、垂直居中对齐刻度线；
                 * 超出数字区宽度的长数字、或尺长放不下一行文字的退化场景直接跳过 */
                {
                    int16_t tw;
                    int16_t ty;

                    _scale_fmt_i32(v, num_buf);
                    tw = (int16_t)we_get_text_width(obj->font, num_buf);
                    if (tw <= WE_SCALE_V_TEXT_W && (int16_t)line_h <= obj->base.h)
                    {
                        ty = (int16_t)(py - (int16_t)line_h / 2);
                        if (ty < obj->base.y)
                            ty = obj->base.y;
                        if (ty > (int16_t)(obj->base.y + obj->base.h - (int16_t)line_h))
                            ty = (int16_t)(obj->base.y + obj->base.h - (int16_t)line_h);
                        we_draw_string(lcd, text_x, ty, obj->font, num_buf,
                                       obj->text_color, 255U);
                    }
                }

                /* 小刻度短线：仅在完整主刻度区间内按像素等分插值 */
                if (obj->minor_div > 0U && (obj->v_max - v) >= (int32_t)obj->major_step)
                {
                    int32_t pos_next = _scale_value_to_pos(obj, v + (int32_t)obj->major_step);
                    uint8_t j;

                    for (j = 1U; j <= obj->minor_div; j++)
                    {
                        int32_t mpos = pos + (pos_next - pos) * (int32_t)j /
                                             (int32_t)(obj->minor_div + 1U);
                        we_fill_rect(lcd, tick_x, (int16_t)(obj->base.y + (int16_t)mpos),
                                     WE_SCALE_MINOR_LEN, 1U, obj->line_color, 255U);
                    }
                }

                if ((obj->v_max - v) < (int32_t)obj->major_step)
                    break;
                v += (int32_t)obj->major_step;
            }
        }
    }

    /* 指针最后画，压在基线与刻度之上 */
    if (obj->show_pointer)
        _scale_draw_pointer(obj, _scale_value_to_pos(obj, _scale_clamp(obj, obj->disp_value)));
}

/**
 * @brief 控件事件回调：装饰性控件，不消费事件，输入穿透给背后控件。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒为 0（穿透）。
 */
static uint8_t _scale_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

/* --------------------------------------------------------------------------
 * 指针滑动动画（中央动画引擎节点）
 * -------------------------------------------------------------------------- */

/**
 * @brief 推进一步指针滑动：Q8 进度 -> 缓入缓出正弦 -> we_lerp 更新显示值。
 * @param owner 控件对象指针（中央动画引擎透传）。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _scale_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_scale_obj_t *obj = (we_scale_obj_t *)owner;
    uint32_t delta;
    uint16_t eased;
    int32_t new_disp;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    if (obj->anim_t >= 256U)
    {
        we_anim_stop(obj->base.lcd, &obj->anim); /* 已就位：摘链停表 */
        return;
    }

    if (obj->anim_ms == 0U)
    {
        obj->anim_t = 256U; /* 零时长直接到位，避免除零 */
    }
    else
    {
        delta = (uint32_t)elapsed_ms * 256U / (uint32_t)obj->anim_ms;
        if (delta == 0U)
            delta = 1U; /* 慢主循环下也保证缓慢前进 */
        obj->anim_t = ((uint32_t)obj->anim_t + delta >= 256U)
                      ? 256U : (uint16_t)(obj->anim_t + delta);
    }

    eased = we_ease_in_out_sine(obj->anim_t);
    if (eased > 256U)
        eased = 256U;

    new_disp = (obj->anim_t >= 256U) ? obj->v_to
                                     : we_lerp(obj->v_from, obj->v_to, eased);

    if (obj->anim_t >= 256U)
        we_anim_stop(obj->base.lcd, &obj->anim); /* 本步到位，摘链停表 */

    if (new_disp != obj->disp_value)
    {
        obj->disp_value = new_disp;
        we_obj_invalidate((we_obj_t *)obj); /* preview：整包围盒标脏 */
    }
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_scale_obj_init(we_scale_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, uint16_t len, uint8_t orientation, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL || font == NULL)
        return;

    if (len < 2U)
        len = 2U;

    obj->font = font; /* 字体必传（上方守卫已拦 NULL） */
    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.class_p = &_scale_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    obj->orientation = (orientation != WE_SCALE_H) ? WE_SCALE_V : WE_SCALE_H;
    obj->len         = len;
    if (obj->orientation == WE_SCALE_H)
    {
        obj->base.w = (int16_t)len;
        obj->base.h = WE_SCALE_H_THICKNESS;
    }
    else
    {
        obj->base.w = WE_SCALE_V_THICKNESS;
        obj->base.h = (int16_t)len;
    }

    obj->v_min      = 0;
    obj->v_max      = 100;
    obj->major_step = 10U;
    obj->minor_div  = 4U;

    {
        colour_t line = RGB888_CONST(150, 168, 196);
        colour_t text = RGB888_CONST(196, 205, 220);
        colour_t ptr  = RGB888_CONST(255, 106, 90);
        obj->line_color    = line;
        obj->text_color    = text;
        obj->pointer_color = ptr;
    }
    obj->show_pointer = 1U;

    obj->value      = 0;
    obj->disp_value = 0;

    obj->anim.next    = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner   = NULL;
    obj->anim_ms      = 0U;
    obj->anim_t       = 256U; /* 空闲（无滑动在跑） */
    obj->v_from       = 0;
    obj->v_to         = 0;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scale_set_range(we_scale_obj_t *obj, int32_t v_min, int32_t v_max)
{
    if (obj == NULL || v_max <= v_min)
        return;
    if (obj->v_min == v_min && obj->v_max == v_max)
        return;

    obj->v_min = v_min;
    obj->v_max = v_max;

    /* 打断进行中的滑动：目标值钳制到新量程，显示值同步就位 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->anim_t     = 256U;
    obj->value      = _scale_clamp(obj, obj->value);
    obj->disp_value = obj->value;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scale_set_ticks(we_scale_obj_t *obj, uint16_t major_step, uint8_t minor_div)
{
    if (obj == NULL || major_step == 0U)
        return;
    if (obj->major_step == major_step && obj->minor_div == minor_div)
        return;

    obj->major_step = major_step;
    obj->minor_div  = minor_div;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scale_set_colors(we_scale_obj_t *obj, colour_t line_color,
                         colour_t text_color, colour_t pointer_color)
{
    if (obj == NULL)
        return;
    if (_scale_col_eq(obj->line_color, line_color) &&
        _scale_col_eq(obj->text_color, text_color) &&
        _scale_col_eq(obj->pointer_color, pointer_color))
        return;

    obj->line_color    = line_color;
    obj->text_color    = text_color;
    obj->pointer_color = pointer_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scale_set_value(we_scale_obj_t *obj, int32_t v)
{
    if (obj == NULL)
        return;

    v = _scale_clamp(obj, v);
    if (obj->value == v && obj->disp_value == v)
        return;

    /* 立即就位：打断进行中的滑动 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->anim_t     = 256U;
    obj->value      = v;
    obj->disp_value = v;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scale_anim_value(we_scale_obj_t *obj, int32_t v, uint16_t dur_ms)
{
    if (obj == NULL)
        return;

    v = _scale_clamp(obj, v);
    if (dur_ms == 0U || v == obj->disp_value)
    {
        we_scale_set_value(obj, v); /* 零时长或已在目标位：直接就位 */
        return;
    }

    obj->value   = v;
    obj->v_from  = obj->disp_value; /* 以当前显示值为新起点，滑动中可无缝改道 */
    obj->v_to    = v;
    obj->anim_ms = dur_ms;
    obj->anim_t  = 0U;
    we_anim_start(obj->base.lcd, &obj->anim, _scale_anim_step_cb, obj);
}

void we_scale_set_show_pointer(we_scale_obj_t *obj, uint8_t show)
{
    if (obj == NULL)
        return;
    show = (show != 0U) ? 1U : 0U;
    if (obj->show_pointer == show)
        return;

    obj->show_pointer = show;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scale_obj_delete(we_scale_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* 动画节点归控件所有，删除前必须摘链，否则中央动画链表留悬空指针 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    we_obj_delete((we_obj_t *)obj);
}
