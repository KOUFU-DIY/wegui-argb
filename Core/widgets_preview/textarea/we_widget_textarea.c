/**
 * @file  we_widget_textarea.c
 * @brief 单行输入框控件（preview）：调用方缓冲 + 光标闪烁 + 溢出左移裁剪
 *
 * 渲染：解析抗锯齿圆角底框 + 左对齐单行文本 + 末尾 2px 光标竖条。
 * 文本宽超过可视宽（内容区宽 - 光标预留）时整体左移显示尾部，
 * 头部经 group 同款 PFB 窗口收窄裁掉；光标始终贴在文本末尾。
 *
 * 光标闪烁：单个中央动画节点（we_anim_t）累计毫秒，每
 * WE_TEXTAREA_BLINK_MS 翻转亮灭并整控件标脏（preview 放宽粒度）。
 * 任何内容变化都把光标拉回亮相位并复位计时（输入期间常亮手感）。
 */

#include "we_widget_textarea.h"
#include "../keyboard/we_widget_keyboard.h" /* 绑定弹层软键盘呼出 */
#include "we_render.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _ta_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 把光标拉回亮相位并复位闪烁计时（内容变化后调用）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _ta_reset_cursor(we_textarea_obj_t *obj)
{
    obj->cursor_on = 1U;
    obj->blink_acc = 0U;
}

/**
 * @brief 只标脏光标竖条区域（与绘制端同一套几何推导）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 溢出左移时光标恒在 inner_x + avail_w 处；未溢出时跟在文本末尾。
 *       每次闪烁一次 we_get_text_width（≤ 缓冲长度次查表），代价可忽略。
 */
static void _ta_invalidate_cursor(we_textarea_obj_t *obj)
{
    int16_t inner_x = (int16_t)(obj->base.x + WE_TEXTAREA_PAD_X);
    int16_t inner_w = (int16_t)(obj->base.w - 2 * WE_TEXTAREA_PAD_X);
    int16_t avail_w = (int16_t)(inner_w - WE_TEXTAREA_CURSOR_W);
    int16_t cursor_x = inner_x;

    if (avail_w <= 0)
        return;
    if (obj->len > 0U && obj->font != NULL)
    {
        uint16_t text_w = we_get_text_width(obj->font, obj->buf);

        if ((int32_t)text_w > (int32_t)avail_w)
            cursor_x = (int16_t)(inner_x + avail_w);
        else
            cursor_x = (int16_t)(inner_x + (int16_t)text_w);
    }
    we_obj_invalidate_area((we_obj_t *)obj, cursor_x,
                           (int16_t)(obj->base.y + WE_TEXTAREA_PAD_Y + 1),
                           WE_TEXTAREA_CURSOR_W,
                           (int16_t)(obj->base.h - 2 * WE_TEXTAREA_PAD_Y - 2));
}

/**
 * @brief 中央动画引擎回调：累计毫秒并按半周期翻转光标亮灭。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本调度周期经过的毫秒数。
 * @return 无。
 * @note 翻转只标脏光标竖条（精细脏矩形），文本与底框零重绘。
 */
static void _ta_blink_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_textarea_obj_t *obj = (we_textarea_obj_t *)owner;
    uint32_t acc;
    uint8_t toggled = 0U;

    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (!obj->editing)
    {
        /* 防御：编辑态已退出则摘链停表（正常路径由 set_editing 摘） */
        we_anim_stop(obj->base.lcd, &obj->blink_anim);
        return;
    }

    acc = (uint32_t)obj->blink_acc + elapsed_ms;
    while (acc >= WE_TEXTAREA_BLINK_MS)
    {
        acc -= WE_TEXTAREA_BLINK_MS;
        obj->cursor_on ^= 1U;
        toggled = 1U;
    }
    obj->blink_acc = (uint16_t)acc;

    if (toggled && obj->opacity > 0U)
        _ta_invalidate_cursor(obj);
}

/* --------------------------------------------------------------------------
 * 绘图回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 文本与光标在收窄后的 PFB 窗口（内容区）内绘制：
 *       溢出左移时文本头部落在窗口外被自动裁掉，不会污染圆角与边距。
 */
static void _textarea_draw_cb(void *ptr)
{
    we_textarea_obj_t *obj = (we_textarea_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    colour_t bg;
    uint16_t draw_r;
    int16_t inner_x;
    int16_t inner_w;
    int16_t avail_w;

    if (obj->opacity == 0U)
        return;

    /* 1. 圆角底框：按压反馈时底色向白微调（整数混色，无浮点） */
    bg = obj->bg_color;
    if (obj->pressed)
        bg = we_colour_blend(we_rgb888_to_dev(255U, 255U, 255U), bg, 30U);

    draw_r = WE_TEXTAREA_RADIUS;
    if (draw_r > (uint16_t)(obj->base.w / 2))
        draw_r = (uint16_t)(obj->base.w / 2);
    if (draw_r > (uint16_t)(obj->base.h / 2))
        draw_r = (uint16_t)(obj->base.h / 2);
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     draw_r, bg, obj->opacity);

    /* 2. 内容区几何：光标常驻末尾，可视文本宽须给光标预留位置 */
    inner_x = (int16_t)(obj->base.x + WE_TEXTAREA_PAD_X);
    inner_w = (int16_t)(obj->base.w - 2 * WE_TEXTAREA_PAD_X);
    avail_w = (int16_t)(inner_w - WE_TEXTAREA_CURSOR_W);
    if (avail_w <= 0)
        return;

    {
        /* group 同款 PFB 窗口收窄：窗口外的字形/光标像素被自动裁掉 */
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;

        int16_t new_x0 = WE_MAX(old_pfb_area.x0, inner_x);
        int16_t new_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
        int16_t new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(inner_x + inner_w - 1));
        int16_t new_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

        if (new_x0 <= new_x1 && new_y0 <= new_y1)
        {
            int16_t text_y = (int16_t)(obj->base.y + WE_TEXTAREA_PAD_Y);
            int16_t cursor_x = inner_x;

            lcd->pfb_area.x0 = (uint16_t)new_x0;
            lcd->pfb_area.x1 = (uint16_t)new_x1;
            lcd->pfb_y_start = (uint16_t)new_y0;
            lcd->pfb_y_end = (uint16_t)new_y1;
            lcd->pfb_gram = old_gram + (new_y0 - (int16_t)old_y_start) * lcd->pfb_width +
                            (new_x0 - (int16_t)old_pfb_area.x0);

            if (obj->len > 0U && obj->font != NULL)
            {
                /* 3. 正文：溢出时左移显示尾部（头部被窗口裁掉） */
                uint16_t text_w = we_get_text_width(obj->font, obj->buf);
                int16_t tx = inner_x;

                if ((int32_t)text_w > (int32_t)avail_w)
                    tx = (int16_t)(inner_x + avail_w - (int16_t)text_w);
                we_draw_string(lcd, tx, text_y, obj->font, obj->buf,
                               obj->text_color, obj->opacity);
                cursor_x = (int16_t)(tx + (int16_t)text_w);
            }
            else if (obj->placeholder != NULL && obj->font != NULL)
            {
                /* 3'. 占位提示：空内容时灰色显示，光标停在行首 */
                we_draw_string(lcd, inner_x, text_y, obj->font, obj->placeholder,
                               obj->placeholder_color, obj->opacity);
            }

            /* 4. 光标：2px 竖条贴在文本末尾，仅编辑中且亮相位才绘制 */
            if (obj->editing && obj->cursor_on)
                we_fill_rect(lcd, cursor_x, (int16_t)(obj->base.y + WE_TEXTAREA_PAD_Y + 1),
                             (uint16_t)WE_TEXTAREA_CURSOR_W,
                             (uint16_t)(obj->base.h - 2 * WE_TEXTAREA_PAD_Y - 2),
                             obj->cursor_color, obj->opacity);
        }

        lcd->pfb_area = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram = old_gram;
    }
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调：点击仅作按压视觉反馈（无焦点系统，视为常聚焦）。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 = 事件已消费，0 = 穿透（仅完全透明时）。
 */
static uint8_t _textarea_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_textarea_obj_t *obj = (we_textarea_obj_t *)ptr;

    (void)data;
    if (obj->opacity == 0U)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        if (!obj->pressed)
        {
            obj->pressed = 1U;
            we_obj_invalidate((we_obj_t *)obj);
        }
        break;

    case WE_EVENT_RELEASED:
        if (obj->pressed)
        {
            obj->pressed = 0U;
            we_obj_invalidate((we_obj_t *)obj);
        }
        break;

    case WE_EVENT_CLICKED:
        if (obj->summon_cb != NULL) /* 点击呼出绑定的弹层编辑器（本框为注入目标） */
            obj->summon_cb(obj->editor, obj);
        break;

    default:
        break; /* STAY/SWIPE：常聚焦无需处理，但仍消费 */
    }
    return 1U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_TEXTAREA_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 按下沿底框按压、松开沿回弹并呼出绑定键盘。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 键盘弹出后按键全部走弹层键通道，不再经过本回调；
 *       收回后焦点仍在本输入框上，可再次 OK 呼出。
 */
static uint8_t _textarea_key_cb(void *ptr, uint8_t key_evt)
{
    we_textarea_obj_t *obj = (we_textarea_obj_t *)ptr;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U && obj->buf != NULL) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK: /* 按下沿：底框按压视觉 */
        if (!obj->pressed)
        {
            obj->pressed = 1U;
            we_obj_invalidate((we_obj_t *)obj);
        }
        return 1U;
    case WE_KEY_EVT_OK_RELEASE: /* 松开沿：回弹并呼出编辑器 */
        if (obj->pressed)
        {
            obj->pressed = 0U;
            we_obj_invalidate((we_obj_t *)obj);
        }
        if (obj->summon_cb != NULL)
            obj->summon_cb(obj->editor, obj);
        return 1U;
    case WE_KEY_EVT_FLASH_END: /* 取消：仅回弹不呼出 */
        if (obj->pressed)
        {
            obj->pressed = 0U;
            we_obj_invalidate((we_obj_t *)obj);
        }
        return 1U;
    default:
        return 0U;
    }
}
#endif /* WE_CFG_ENABLE_KEY_INPUT && WE_TEXTAREA_USE_KEY */

static const we_class_t _textarea_class = {
    .draw_cb = _textarea_draw_cb,
    .event_cb = _textarea_event_cb,
    .set_pos_cb = NULL,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_TEXTAREA_USE_KEY == 1)
    .key_cb = _textarea_key_cb,
#endif
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化单行输入框并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 底框左上角 X（屏幕绝对坐标）。
 * @param y 底框左上角 Y。
 * @param w 底框宽度（像素）。
 * @param buf 文本缓冲（调用方提供并持有，须含结尾 0）。
 * @param buf_size 缓冲总容量（含结尾 0 的字节数）。
 * @return 无。
 */
void we_textarea_obj_init(we_textarea_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w,
                          char *buf, uint16_t buf_size, const unsigned char *font)
{
    uint16_t line_h;
    size_t cur;

    if (obj == NULL || lcd == NULL || buf == NULL || buf_size == 0U)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.class_p = &_textarea_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;
    line_h = we_font_get_line_height(obj->font);
    obj->base.h = (int16_t)(line_h + 2 * WE_TEXTAREA_PAD_Y); /* 高度 = 行高 + 上下边距 */

    obj->buf = buf;
    obj->buf_size = buf_size;
    /* 保留缓冲内既有内容；越界防御一次（API 边界） */
    cur = strlen(buf);
    if (cur > (size_t)(buf_size - 1U))
    {
        cur = (size_t)(buf_size - 1U);
        buf[cur] = '\0';
    }
    obj->len = (uint16_t)cur;

    obj->placeholder = NULL;
    obj->bg_color = RGB888TODEV(36, 44, 58);
    obj->text_color = RGB888TODEV(236, 241, 248);
    obj->cursor_color = RGB888TODEV(92, 181, 255);
    obj->placeholder_color = RGB888TODEV(122, 131, 146);

    obj->blink_anim.next = NULL;
    obj->blink_anim.step_cb = NULL;
    obj->blink_anim.owner = NULL;
    obj->blink_acc = 0U;
    obj->editing = 0U; /* 初始空闲：不显示/不闪烁光标，进入编辑态才启动 */
    obj->cursor_on = 1U;
    obj->pressed = 0U;
    obj->opacity = 255U;
    obj->editor = NULL;
    obj->summon_cb = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置编辑中状态：进入时光标常亮并开始闪烁，退出时熄灭停表。
 * @param obj 控件对象指针。
 * @param on 1 = 编辑中，0 = 空闲（值未变时直接返回）。
 * @return 无。
 */
void we_textarea_set_editing(we_textarea_obj_t *obj, uint8_t on)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    on = (on != 0U) ? 1U : 0U;
    if (obj->editing == on)
        return;

    obj->editing = on;
    if (on)
    {
        _ta_reset_cursor(obj); /* 进入即常亮，随后按半周期闪烁 */
        we_anim_start(obj->base.lcd, &obj->blink_anim, _ta_blink_step_cb, obj);
    }
    else
    {
        we_anim_stop(obj->base.lcd, &obj->blink_anim);
    }
    _ta_invalidate_cursor(obj); /* 只刷光标竖条：进入画出 / 退出擦除 */
}

/**
 * @brief 注入一个键值：追加键面字符串，"\b" 为退格（按 UTF-8 字符回退）。
 * @param obj 控件对象指针。
 * @param key 键面字符串（UTF-8）。
 * @return 无。
 */
void we_textarea_input(we_textarea_obj_t *obj, const char *key)
{
    if (obj == NULL || obj->buf == NULL || key == NULL || key[0] == '\0')
        return;

    if (key[0] == '\b' && key[1] == '\0')
    {
        uint16_t i;

        if (obj->len == 0U)
            return;
        /* 回退到上一个 UTF-8 字符首字节：跳过 10xxxxxx 续字节 */
        i = obj->len;
        do
        {
            i--;
        } while (i > 0U && ((uint8_t)obj->buf[i] & 0xC0U) == 0x80U);
        obj->buf[i] = '\0';
        obj->len = i;
    }
    else
    {
        size_t add = strlen(key);

        if ((uint32_t)obj->len + add >= (uint32_t)obj->buf_size)
            return; /* 余量不足（含结尾 0），本次追加整体忽略 */
        memcpy(&obj->buf[obj->len], key, add + 1U);
        obj->len = (uint16_t)(obj->len + add);
    }

    _ta_reset_cursor(obj); /* 输入期间光标保持常亮 */
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 清空文本内容（内容本就为空时直接返回）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_textarea_clear(we_textarea_obj_t *obj)
{
    if (obj == NULL || obj->buf == NULL || obj->len == 0U)
        return;

    obj->len = 0U;
    obj->buf[0] = '\0';
    _ta_reset_cursor(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 获取当前文本内容指针。
 * @param obj 控件对象指针。
 * @return 文本字符串指针；obj 为 NULL 时返回 NULL。
 */
const char *we_textarea_get_text(const we_textarea_obj_t *obj)
{
    return (obj != NULL) ? obj->buf : NULL;
}

/**
 * @brief 设置占位提示文本（指针未变时直接返回）。
 * @param obj 控件对象指针。
 * @param placeholder 占位字符串（调用方持有，可为 NULL）。
 * @return 无。
 */
void we_textarea_set_placeholder(we_textarea_obj_t *obj, const char *placeholder)
{
    if (obj == NULL || obj->placeholder == placeholder)
        return;

    obj->placeholder = placeholder;
    if (obj->len == 0U) /* 仅空内容时占位可见，才需要重绘 */
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 软键盘呼出 thunk：bind_keyboard 的内部 summon 回调。
 * @param editor 传入：绑定的键盘对象指针。
 * @param ta 传入：触发呼出的输入框对象指针。
 * @return 无。
 */
static void _ta_summon_keyboard(void *editor, void *ta)
{
    we_keyboard_popup_show((we_keyboard_obj_t *)editor, ta);
}

/**
 * @brief 通用弹层编辑器绑定。
 * @param obj 控件对象指针。
 * @param editor 编辑器对象指针（随 summon 回调透传）。
 * @param summon_cb 呼出回调（editor, ta）；NULL 解绑。
 * @return 无。
 */
void we_textarea_bind_editor(we_textarea_obj_t *obj, void *editor,
                             void (*summon_cb)(void *editor, void *ta))
{
    if (obj == NULL)
        return;
    obj->editor = editor;
    obj->summon_cb = summon_cb;
}

/**
 * @brief 绑定弹层软键盘：点击输入框（或聚焦后按 OK）呼出并注入本框。
 * @param obj 控件对象指针。
 * @param kb 弹层键盘对象指针（we_keyboard_obj_t*，NULL 解绑）。
 * @return 无。
 */
void we_textarea_bind_keyboard(we_textarea_obj_t *obj, void *kb)
{
    we_textarea_bind_editor(obj, kb, (kb != NULL) ? _ta_summon_keyboard : NULL);
}

/**
 * @brief 设置三项配色：底色 / 文字色 / 光标色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param bg 底框填充色。
 * @param text 文本前景色。
 * @param cursor 光标颜色。
 * @return 无。
 */
void we_textarea_set_colors(we_textarea_obj_t *obj, colour_t bg,
                            colour_t text, colour_t cursor)
{
    if (obj == NULL)
        return;
    if (_ta_colour_eq(obj->bg_color, bg) &&
        _ta_colour_eq(obj->text_color, text) &&
        _ta_colour_eq(obj->cursor_color, cursor))
        return;

    obj->bg_color = bg;
    obj->text_color = text;
    obj->cursor_color = cursor;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_textarea_set_opacity(we_textarea_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除控件：先摘除光标闪烁动画节点，再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_textarea_obj_delete(we_textarea_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;

    we_anim_stop(obj->base.lcd, &obj->blink_anim); /* 动画节点归控件所有，删除前必须摘链 */
    we_obj_delete((we_obj_t *)obj);
}
