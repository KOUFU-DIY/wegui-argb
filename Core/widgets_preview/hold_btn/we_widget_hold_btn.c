#include "we_widget_hold_btn.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * hold_btn —— 长按确认按钮（preview 孵化区）
 *
 * 状态机：
 *   idle（progress=0）
 *     -- PRESSED --> charging：中央动画节点按 elapsed 累计 charge_ms，
 *                    progress = charge_ms * 256 / hold_ms
 *     -- RELEASED（未满）--> decaying：charge_ms 以 2 倍速回退到 0 后摘链
 *     -- 充满 --> triggered：触发回调一次 + flash 闪亮衰减，进度锁定 256，
 *                 按住/点击不再响应充能，直到 we_hold_btn_reset
 *
 * 计时完全放在中央动画节点里（STAY 派发频率取决于输入轮询周期，
 * 不可靠；动画节点每调度周期都有稳定的 elapsed_ms）。充能/回退/闪亮
 * 三种过程共用同一个节点，由 pressed/triggered/flash 标志区分推进逻辑。
 * -------------------------------------------------------------------------- */

/* 按压态核心圆向白色增亮的混合量 */
#define _HB_PRESS_LIGHTEN 26U

/* 已触发态核心圆向环亮色靠拢的混合量（常驻点亮） */
#define _HB_LIT_BLEND 110U

static const colour_t _hb_white = RGB888_CONST(255, 255, 255);

/**
 * @brief 比较两个颜色值是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 表示相等，0 表示不等。
 */
static uint8_t _hb_color_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#else
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 将透明度按控件整体不透明度缩放。
 * @param a 传入：原始透明度（0~255）。
 * @param opacity 传入：控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _hb_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/**
 * @brief 触发充满：置锁定态、点燃闪亮反馈并回调一次。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _hb_fire(we_hold_btn_obj_t *obj)
{
    obj->triggered = 1U;
    obj->charge_ms = obj->hold_ms;
    obj->progress = 256U;
    obj->flash = 256U;
    if (obj->triggered_cb != NULL)
        obj->triggered_cb(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 中央动画引擎回调：推进充能 / 回退 / 闪亮衰减。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次调度经过的毫秒数。
 * @return 无。
 * @note 无事可做（非充能、进度归零、闪亮结束）时自行摘链。
 */
static void _hb_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_hold_btn_obj_t *obj = (we_hold_btn_obj_t *)owner;
    uint8_t changed = 0U;
    uint8_t still_busy = 0U;

    if (obj == NULL || elapsed_ms == 0U)
        return;
    if (elapsed_ms > 128U)
        elapsed_ms = 128U; /* 主循环长卡顿限幅，防一步跳满/跳空 */

    /* 1. 闪亮反馈：按时长线性衰减到 0 */
    if (obj->flash > 0U)
    {
        uint32_t dec = ((uint32_t)elapsed_ms * 256U) / WE_HOLD_BTN_FLASH_MS;
        if (dec == 0U)
            dec = 1U;
        obj->flash = (dec >= obj->flash) ? 0U : (uint16_t)(obj->flash - dec);
        changed = 1U;
        if (obj->flash > 0U)
            still_busy = 1U;
    }

    /* 2. 充能 / 回退（已触发态进度锁定 256，不再推进） */
    if (!obj->triggered)
    {
        uint16_t old_progress = obj->progress;

        if (obj->pressed)
        {
            /* 充能：按住时间累计，到点即触发 */
            uint32_t acc = (uint32_t)obj->charge_ms + elapsed_ms;
            obj->charge_ms = (acc >= obj->hold_ms) ? obj->hold_ms : (uint16_t)acc;
            obj->progress = (uint16_t)(((uint32_t)obj->charge_ms * 256U) / obj->hold_ms);

            if (obj->charge_ms >= obj->hold_ms)
            {
                _hb_fire(obj); /* 内部含标脏；flash 已点燃，节点继续跑闪亮 */
                return;
            }
            still_busy = 1U;
        }
        else if (obj->charge_ms > 0U)
        {
            /* 回退：2 倍速退到 0 */
            uint32_t dec = (uint32_t)elapsed_ms * WE_HOLD_BTN_DECAY_MUL;
            obj->charge_ms = (dec >= obj->charge_ms) ? 0U : (uint16_t)(obj->charge_ms - dec);
            obj->progress = (uint16_t)(((uint32_t)obj->charge_ms * 256U) / obj->hold_ms);
            if (obj->charge_ms > 0U)
                still_busy = 1U;
        }

        if (obj->progress != old_progress)
            changed = 1U;
    }

    if (changed)
        we_obj_invalidate((we_obj_t *)obj); /* preview：整控件包围盒标脏 */

    if (!still_busy)
        we_anim_stop(obj->base.lcd, &obj->anim); /* 全部静止，摘链停表 */
}

/**
 * @brief 绘制居中标签文字（PFB 收窄裁剪在控件矩形内）。
 * @param obj 传入：控件对象指针。
 * @param color 传入：文字颜色。
 * @return 无。
 */
static void _hb_draw_label_clipped(we_hold_btn_obj_t *obj, colour_t color)
{
    we_lcd_t *lcd = obj->base.lcd;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    uint16_t txt_w;
    int8_t y_top;
    int8_t y_bot;
    int16_t cx;
    int16_t cy;
    int16_t clip_x0;
    int16_t clip_y0;
    int16_t clip_x1;
    int16_t clip_y1;

    if (obj->label == NULL)
        return;

    txt_w = we_get_text_width(obj->font, obj->label);
    we_get_text_bbox(obj->font, obj->label, &y_top, &y_bot);

    cx = (int16_t)(obj->base.x + obj->base.w / 2);
    cy = (int16_t)(obj->base.y + obj->base.h / 2);

    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    clip_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
    clip_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
    clip_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
    clip_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

    if (clip_x0 <= clip_x1 && clip_y0 <= clip_y1)
    {
        lcd->pfb_area.x0 = clip_x0;
        lcd->pfb_area.x1 = clip_x1;
        lcd->pfb_y_start = (uint16_t)clip_y0;
        lcd->pfb_y_end = (uint16_t)clip_y1;
        lcd->pfb_gram = old_gram + (clip_y0 - (int16_t)old_y_start) * lcd->pfb_width
                                 + (clip_x0 - old_pfb_area.x0);

        we_draw_string(lcd, (int16_t)(cx - (int16_t)(txt_w / 2U)),
                       (int16_t)(cy - (y_top + y_bot) / 2),
                       obj->font, obj->label, color, obj->opacity);
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;
}

/**
 * @brief 控件绘制回调：充能环（分段辐条）+ 核心实心圆 + 居中标签。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 */
static void _hb_draw_cb(void *ptr)
{
    we_hold_btn_obj_t *obj = (we_hold_btn_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t size;
    int16_t cx;
    int16_t cy;
    int16_t r_max;
    int16_t lw;
    int16_t ro;
    int16_t ri;
    int16_t core_r;
    uint16_t seg;
    uint16_t lit_cnt;
    colour_t core_color;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->base.w <= 0 || obj->base.h <= 0)
        return;

    size = WE_MIN(obj->base.w, obj->base.h);
    cx = (int16_t)(obj->base.x + obj->base.w / 2);
    cy = (int16_t)(obj->base.y + obj->base.h / 2);

    /* 环几何：辐条圆头留白 lw/2+1，环带厚度约 size/8 */
    r_max = (int16_t)(size / 2 - 2);
    lw = (int16_t)(size / 20);
    if (lw < 2)
        lw = 2;
    if (lw > 5)
        lw = 5;
    ro = (int16_t)(r_max - lw / 2 - 1);
    ri = (int16_t)(ro - size / 8);
    if (ri < 1)
        ri = 1;
    core_r = (int16_t)(ri - lw / 2 - 3);
    if (core_r < 4)
        core_r = 4;

    /* 已点亮段数：progress Q8 -> 0..RING_SEGS */
    lit_cnt = (uint16_t)(((uint32_t)obj->progress * WE_HOLD_BTN_RING_SEGS) >> 8);
    if (lit_cnt > WE_HOLD_BTN_RING_SEGS)
        lit_cnt = WE_HOLD_BTN_RING_SEGS;

    /* 1. 充能环：整圈暗轨道 + 0..progress 亮段（512 步制，顶部起顺时针） */
    for (seg = 0U; seg < WE_HOLD_BTN_RING_SEGS; seg++)
    {
        /* 顶部（-90° = +384）起顺时针；先加 384 再归一化，全程非负无符号陷阱 */
        int16_t a = (int16_t)(((((int32_t)seg * 512) / (int32_t)WE_HOLD_BTN_RING_SEGS) + 384) & 0x1FF);
        int32_t ca = we_cos(a);
        int32_t sa = we_sin(a);
        int16_t xi = (int16_t)(cx + ((ca * ri) >> 15));
        int16_t yi = (int16_t)(cy + ((sa * ri) >> 15));
        int16_t xo = (int16_t)(cx + ((ca * ro) >> 15));
        int16_t yo = (int16_t)(cy + ((sa * ro) >> 15));
        uint8_t seg_opa = (seg < lit_cnt)
                        ? obj->opacity
                        : _hb_scale_opa(WE_HOLD_BTN_TRACK_OPA, obj->opacity);

        we_draw_line_round(lcd, xi, yi, xo, yo, (uint8_t)lw,
                           obj->ring_color, seg_opa);
    }

    /* 2. 核心实心圆：底色 -> 已触发常亮 -> 闪亮白光 -> 按压增亮 */
    core_color = obj->bg_color;
    if (obj->triggered)
        core_color = we_colour_blend(obj->ring_color, core_color, _HB_LIT_BLEND);
    if (obj->flash > 0U)
    {
        uint8_t fa = (uint8_t)(((uint32_t)obj->flash * 255U) >> 8);
        core_color = we_colour_blend(_hb_white, core_color, fa);
    }
    if (obj->pressed && !obj->triggered)
        core_color = we_colour_blend(_hb_white, core_color, _HB_PRESS_LIGHTEN);

    we_draw_round_rect_analytic_fill(lcd, (int16_t)(cx - core_r), (int16_t)(cy - core_r),
                                     (uint16_t)(core_r * 2), (uint16_t)(core_r * 2),
                                     (uint16_t)core_r, core_color, obj->opacity);

    /* 3. 居中标签（PFB 收窄裁剪） */
    _hb_draw_label_clipped(obj, obj->text_color);
}

/**
 * @brief 控件事件回调：按压启动充能、松手转入回退，事件恒消费。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 恒返回 1（交互控件，容器据此锁定并转发后续事件）。
 */
static uint8_t _hb_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_hold_btn_obj_t *obj = (we_hold_btn_obj_t *)ptr;

    (void)data;
    if (obj == NULL)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed = 1U;
        if (!obj->triggered)
        {
            /* 计时交给中央动画节点（STAY 派发频率不可靠，不用于计时） */
            we_anim_start(obj->base.lcd, &obj->anim, _hb_anim_step_cb, obj);
        }
        we_obj_invalidate((we_obj_t *)obj);
        break;

    case WE_EVENT_RELEASED:
        obj->pressed = 0U;
        /* 未满松手：节点已挂链，下一周期自动转入 2 倍速回退 */
        we_obj_invalidate((we_obj_t *)obj);
        break;

    default:
        /* STAY 仅表示仍按住（计时不依赖它）；CLICKED/SWIPE 无额外行为 */
        break;
    }
    return 1U;
}

static const we_class_t _hold_btn_class = {
    .draw_cb = _hb_draw_cb,
    .event_cb = _hb_event_cb,
    .set_pos_cb = NULL, /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化长按确认按钮并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param size 传入：外接正方形边长（像素）。
 * @param label 传入：中心标签文字（UTF-8，调用方持有，可为 NULL）。
 * @return 无。
 */
void we_hold_btn_obj_init(we_hold_btn_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t size, const char *label, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL || font == NULL || size <= 0)
        return;

    obj->font = font; /* 字体必传（上方守卫已拦 NULL） */
    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = size;
    obj->base.h = size;
    obj->base.class_p = &_hold_btn_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->label = label;
    obj->hold_ms = WE_HOLD_BTN_DEF_HOLD_MS;
    obj->charge_ms = 0U;
    obj->progress = 0U;
    obj->flash = 0U;

    obj->bg_color = RGB888TODEV(44, 58, 82);      /* 深蓝底 */
    obj->ring_color = RGB888TODEV(86, 205, 255);  /* 亮青环 */
    obj->text_color = RGB888TODEV(238, 243, 250); /* 近白文字 */

    obj->triggered_cb = NULL;

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;

    obj->opacity = 255U;
    obj->pressed = 0U;
    obj->triggered = 0U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置充满所需按住时长（进行中按新时长重新折算显示进度）。
 * @param obj 传入：控件对象指针。
 * @param hold_ms 传入：充满时长（毫秒，0 按 1 处理）。
 * @return 无。
 */
void we_hold_btn_set_hold_ms(we_hold_btn_obj_t *obj, uint16_t hold_ms)
{
    if (obj == NULL)
        return;
    if (hold_ms == 0U)
        hold_ms = 1U;
    if (obj->hold_ms == hold_ms)
        return;

    obj->hold_ms = hold_ms;
    if (obj->charge_ms > hold_ms)
        obj->charge_ms = hold_ms;
    if (!obj->triggered)
    {
        obj->progress = (uint16_t)(((uint32_t)obj->charge_ms * 256U) / obj->hold_ms);
        we_obj_invalidate((we_obj_t *)obj);
    }
}

/**
 * @brief 设置充满触发回调。
 * @param obj 传入：控件对象指针。
 * @param cb 传入：回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_hold_btn_set_triggered_cb(we_hold_btn_obj_t *obj,
                                  we_hold_btn_triggered_cb_t cb)
{
    if (obj == NULL || obj->triggered_cb == cb)
        return;
    obj->triggered_cb = cb;
}

/**
 * @brief 设置核心圆底色 / 充能环亮色 / 标签文字色。
 * @param obj 传入：控件对象指针。
 * @param bg 传入：核心圆底色。
 * @param ring 传入：充能环亮色。
 * @param text 传入：标签文字色。
 * @return 无。
 */
void we_hold_btn_set_colors(we_hold_btn_obj_t *obj,
                            colour_t bg, colour_t ring, colour_t text)
{
    if (obj == NULL)
        return;
    if (_hb_color_eq(obj->bg_color, bg) &&
        _hb_color_eq(obj->ring_color, ring) &&
        _hb_color_eq(obj->text_color, text))
        return;

    obj->bg_color = bg;
    obj->ring_color = ring;
    obj->text_color = text;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 复位按钮：清除已触发态与充能进度，回到待机可再次充能。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_hold_btn_reset(we_hold_btn_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (!obj->triggered && obj->charge_ms == 0U && obj->flash == 0U)
        return; /* 已是待机态 */

    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->triggered = 0U;
    obj->charge_ms = 0U;
    obj->progress = 0U;
    obj->flash = 0U;

    /* 复位时若仍被按住，重新挂链开始新一轮充能 */
    if (obj->pressed)
        we_anim_start(obj->base.lcd, &obj->anim, _hb_anim_step_cb, obj);

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 查询当前充能进度。
 * @param obj 传入：控件对象指针。
 * @return 进度 Q8（0..256）；obj 为 NULL 返回 0。
 */
uint16_t we_hold_btn_get_progress(const we_hold_btn_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->progress;
}

/**
 * @brief 查询是否处于已触发锁定态。
 * @param obj 传入：控件对象指针。
 * @return 1=已触发（等待 reset），0=未触发。
 */
uint8_t we_hold_btn_is_triggered(const we_hold_btn_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->triggered;
}

/**
 * @brief 设置控件整体透明度并按需重绘。
 * @param obj 传入：控件对象指针。
 * @param opacity 传入：不透明度（0~255）。
 * @return 无。
 */
void we_hold_btn_set_opacity(we_hold_btn_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除按钮控件：先摘除动画节点（we_anim_stop）再摘链。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_hold_btn_obj_delete(we_hold_btn_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    we_anim_stop(obj->base.lcd, &obj->anim); /* 节点归控件所有，删除前必须摘链 */
    we_obj_delete((we_obj_t *)obj);
}
