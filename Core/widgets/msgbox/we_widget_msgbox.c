#include "we_widget_msgbox.h"
#include "we_font_text.h"

#include "we_render.h"
#include <string.h>

enum
{
    WE_POPUP_BTN_NONE = 0,
    WE_POPUP_BTN_CONFIRM = 1,
    WE_POPUP_BTN_CANCEL = 2
};

/* 无回弹缓出：
 * 起步更快，越接近目标越慢，符合“开始快，结束慢”的消息框滑入感觉。
 * 公式：easeOutCubic = 1 - (1 - t)^3，t 为 Q8 定点 [0,256]。
 */

/**
 * @brief 计算弹窗滑入阶段的三次缓出插值值（Q8：0~256）。
 * @param t 动画归一化进度，范围 0~256。
 * @return 对应缓动后的进度值（0~256）。
 */
static uint16_t _msgbox_ease_slide(uint16_t t)
{
    uint32_t inv;
    uint32_t inv2;

    if (t >= 256U)
        return 256U;

    inv = 256U - t;
    inv2 = (inv * inv) >> 8;
    return (uint16_t)(256U - ((inv2 * inv) >> 8));
}

/* 快速回退：一开始慢一点点，随后迅速拉走，适合消息框收起。 */

/**
 * @brief 计算弹窗收起阶段的二次缓入插值值（Q8：0~256）。
 * @param t 动画归一化进度，范围 0~256。
 * @return 对应缓动后的进度值（0~256）。
 */
static uint16_t _msgbox_ease_hide(uint16_t t)
{
    uint32_t t2;

    if (t >= 256U)
        return 256U;

    t2 = ((uint32_t)t * (uint32_t)t) >> 8;
    return (uint16_t)t2;
}

/**
 * @brief 解析 UTF-8 字符并输出码点与字节长度。
 * @param str 待解析 UTF-8 字符串指针。
 * @param out_code 输出 Unicode 码点。
 * @param out_len 输出当前字符字节数。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _popup_utf8_decode(const char *str, uint16_t *out_code, uint8_t *out_len)
{
    uint8_t c;

    if (str == NULL || *str == '\0' || out_code == NULL || out_len == NULL)
        return 0U;

    c = (uint8_t)str[0];
    if (c < 0x80U)
    {
        *out_code = c;
        *out_len = 1U;
        return 1U;
    }
    if ((c & 0xE0U) == 0xC0U && str[1] != '\0')
    {
        *out_code = (uint16_t)(((c & 0x1FU) << 6) | ((uint8_t)str[1] & 0x3FU));
        *out_len = 2U;
        return 1U;
    }
    if ((c & 0xF0U) == 0xE0U && str[1] != '\0' && str[2] != '\0')
    {
        *out_code = (uint16_t)(((c & 0x0FU) << 12) |
                               (((uint8_t)str[1] & 0x3FU) << 6) |
                               ((uint8_t)str[2] & 0x3FU));
        *out_len = 3U;
        return 1U;
    }

    *out_code = c;
    *out_len = 1U;
    return 1U;
}

/**
 * @brief 判断 Unicode 码点是否为空白分隔字符（空格或制表符）。
 * @param code 待判断的 Unicode 码点。
 * @return 1 表示为空白分隔符，0 表示非空白分隔符。
 */
static uint8_t _popup_is_space(uint16_t code)
{
    return (uint8_t)(code == ' ' || code == '\t');
}

/**
 * @brief 查询单个码点的排版步进宽度。
 * @param font 字体资源指针。
 * @param code 待查询的 Unicode 码点。
 * @return 该码点的步进宽度（像素）。
 */
static uint16_t _popup_glyph_width(const unsigned char *font, uint16_t code)
{
    we_glyph_info_t info;

    if (font == NULL)
        return 0U;
    if (we_font_get_glyph_info(font, code, &info))
        return info.adv_w;
    return (uint16_t)(we_font_get_line_height(font) / 2U);
}

/**
 * @brief 统计多行文本中最宽一行的像素宽度。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @return 最宽行的宽度（像素）。
 */
static uint16_t _popup_text_longest_line_width(const unsigned char *font, const char *text)
{
    const char *p;
    uint16_t code;
    uint8_t len;
    uint16_t line_width;
    uint16_t max_width;

    if (font == NULL || text == NULL || text[0] == '\0')
        return 0U;

    p = text;
    line_width = 0U;
    max_width = 0U;

    while (*p != '\0')
    {
        if (!_popup_utf8_decode(p, &code, &len))
            break;

        if (code == '\n')
        {
            if (line_width > max_width)
                max_width = line_width;
            line_width = 0U;
            p += len;
            continue;
        }

        line_width = (uint16_t)(line_width + _popup_glyph_width(font, code));
        p += len;
    }

    if (line_width > max_width)
        max_width = line_width;

    return max_width;
}

static const char *_popup_find_wrap_point(const unsigned char *font, const char *str, uint16_t max_width)
{
    const char *line_start;
    const char *p;
    const char *last_space;
    const char *last_fit;
    uint16_t line_width;
    uint16_t code;
    uint8_t len;
    uint16_t glyph_w;

    if (font == NULL || str == NULL || *str == '\0')
        return str;

    line_start = str;
    p = str;
    last_space = NULL;
    last_fit = str;
    line_width = 0U;

    while (*p != '\0')
    {
        if (!_popup_utf8_decode(p, &code, &len))
            break;

        if (code == '\n')
            return p;

        glyph_w = _popup_glyph_width(font, code);
        if (line_width > 0U && line_width + glyph_w > max_width)
        {
            if (last_space != NULL && last_space > line_start)
                return last_space;
            if (last_fit > line_start)
                return last_fit;
            return p + len;
        }

        if (_popup_is_space(code))
            last_space = p;

        line_width = (uint16_t)(line_width + glyph_w);
        p += len;
        last_fit = p;
    }

    return p;
}

static const char *_popup_skip_break_chars(const char *str)
{
    uint16_t code;
    uint8_t len;

    if (str == NULL)
        return NULL;

    while (*str != '\0')
    {
        if (!_popup_utf8_decode(str, &code, &len))
            break;
        if (code == '\n' || _popup_is_space(code))
        {
            str += len;
            continue;
        }
        break;
    }
    return str;
}

/**
 * @brief 计算文本像素高度。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @return 返回对应结果值。
 */
static uint16_t _popup_text_height(const unsigned char *font, const char *text)
{
    int8_t y_top;
    int8_t y_bottom;

    if (font == NULL || text == NULL || text[0] == '\0')
        return 0U;

    we_get_text_bbox(font, text, &y_top, &y_bottom);
    if (y_bottom <= y_top)
        return 0U;
    return (uint16_t)(y_bottom - y_top);
}

/**
 * @brief 按最大宽度计算文本换行边界。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @param max_width 宽度值（像素）。
 * @return 返回对应结果值。
 */
static uint16_t _popup_text_block_height_wrapped(const unsigned char *font, const char *text, uint16_t max_width)
{
    uint16_t single_h;
    uint16_t line_count;
    const char *line;
    const char *next;

    uint16_t line_height;

    if (font == NULL || text == NULL || text[0] == '\0')
        return 0U;

    line_height = we_font_get_line_height(font);
    single_h = _popup_text_height(font, text);
    if (single_h == 0U)
        single_h = line_height;

    line_count = 0U;
    line = text;
    while (line != NULL && *line != '\0')
    {
        next = _popup_find_wrap_point(font, line, max_width);
        if (next == line)
            break;
        line_count++;
        if (*next == '\0')
            break;
        line = _popup_skip_break_chars(next);
    }

    if (line_count <= 1U)
        return single_h;
    return (uint16_t)(single_h + (uint16_t)(line_count - 1U) * line_height);
}

/**
 * @brief 在当前 PFB 裁剪区内执行局部绘制。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param max_width 宽度值（像素）。
 * @param color 目标颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
static void _popup_draw_text_wrapped(we_lcd_t *lcd, const unsigned char *font, const char *text,
                                     int16_t x, int16_t y, uint16_t max_width,
                                     colour_t color, uint8_t opacity)
{
    const char *line;
    const char *next;
    int16_t cursor_y;
    char line_buf[96];
    size_t copy_len;

    uint16_t line_height;

    if (lcd == NULL || font == NULL || text == NULL || text[0] == '\0')
        return;

    line_height = we_font_get_line_height(font);
    cursor_y = y;
    line = text;
    while (line != NULL && *line != '\0')
    {
        next = _popup_find_wrap_point(font, line, max_width);
        if (next == line)
            break;

        copy_len = (size_t)(next - line);
        if (copy_len >= sizeof(line_buf))
            copy_len = sizeof(line_buf) - 1U;
        memcpy(line_buf, line, copy_len);
        line_buf[copy_len] = '\0';
        we_draw_string(lcd, x, cursor_y, font, line_buf, color, opacity);

        if (*next == '\0')
            break;
        line = _popup_skip_break_chars(next);
        cursor_y = (int16_t)(cursor_y + line_height);
    }
}

/**
 * @brief 根据文本与按钮内容重新计算弹窗布局。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_refresh_layout(we_popup_obj_t *obj)
{
    uint16_t max_panel_w;
    uint16_t title_w;
    uint16_t msg_w;
    uint16_t confirm_w;
    uint16_t cancel_w;
    uint16_t btn_need_w;
    uint16_t content_need_w;
    uint16_t desired_w;
    uint16_t min_h;
    uint16_t title_h;
    uint16_t msg_h;
    uint16_t desired_h;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    max_panel_w = (uint16_t)(obj->base.lcd->width - WE_POPUP_EDGE_MARGIN * 2);
    title_w = _popup_text_longest_line_width(obj->title_font, obj->title);
    msg_w = _popup_text_longest_line_width(obj->message_font, obj->message);
    confirm_w = _popup_text_longest_line_width(obj->button_font, obj->confirm_text);
    cancel_w = _popup_text_longest_line_width(obj->button_font, obj->cancel_text);

    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
    {
        uint16_t action_left;
        uint16_t action_right;

        action_left = (uint16_t)((cancel_w > 0U) ? (cancel_w + 20U) : 48U);
        action_right = (uint16_t)((confirm_w > 0U) ? (confirm_w + 20U) : 48U);
        btn_need_w = (uint16_t)(action_left + 24U + action_right + 24U);
    }
    else
    {
        uint16_t action_single;

        action_single = (uint16_t)((confirm_w > 0U) ? (confirm_w + 20U) : 48U);
        btn_need_w = (uint16_t)(action_single + 24U);
    }

    content_need_w = title_w;
    if (msg_w > content_need_w)
        content_need_w = msg_w;
    content_need_w = (uint16_t)(content_need_w + 36U);
    desired_w = content_need_w;
    if (btn_need_w > desired_w)
        desired_w = btn_need_w;
    if (desired_w < obj->panel_min_w)
        desired_w = obj->panel_min_w;
    if (desired_w > max_panel_w)
        desired_w = max_panel_w;
    obj->panel_w = desired_w;

    title_h = _popup_text_block_height_wrapped(obj->title_font, obj->title,
                                               (obj->panel_w > 36U) ? (uint16_t)(obj->panel_w - 36U) : obj->panel_w);
    msg_h = _popup_text_block_height_wrapped(obj->message_font, obj->message,
                                             (obj->panel_w > 36U) ? (uint16_t)(obj->panel_w - 36U) : obj->panel_w);

    /* 扁平版垂直预算：
     * 顶部留白 18
     * 标题区
     * 标题到正文留白 8（正文存在时）
     * 正文区
     * 正文到底部分隔线/动作区留白 28
     * 动作区高度 28
     * 底部留白 14
     */
    min_h = (uint16_t)(18U + 28U + 28U + 14U);
    desired_h = (uint16_t)(min_h + title_h + msg_h);
    if (title_h > 0U && msg_h > 0U)
        desired_h = (uint16_t)(desired_h + 8U);

    if (desired_h > obj->panel_h)
        obj->panel_h = desired_h;

    obj->hidden_y = (int16_t)(-(int16_t)obj->panel_h - 8);
    if (obj->target_y > (int16_t)(obj->base.lcd->height - obj->panel_h))
        obj->target_y = (int16_t)(obj->base.lcd->height - obj->panel_h);
    if (obj->target_y < 0)
        obj->target_y = 0;
}

/**
 * @brief 内部辅助：popup_panel_x。
 * @param obj 目标控件对象指针。
 * @return 返回对应结果值。
 */
static int16_t _popup_panel_x(const we_popup_obj_t *obj)
{
    return (int16_t)((obj->base.lcd->width - obj->panel_w) / 2);
}

/**
 * @brief 计算脏区并发起局部重绘请求。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_invalidate_panel(const we_popup_obj_t *obj)
{
    int16_t x;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    x = _popup_panel_x(obj);
    we_obj_invalidate_area((we_obj_t *)obj, x, obj->panel_y, obj->panel_w, obj->panel_h);
}

/**
 * @brief 内部辅助：popup_stop_anim。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_stop_anim(we_popup_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->task_id >= 0)
    {
        _we_gui_task_unregister(obj->base.lcd, obj->task_id);
        obj->task_id = -1;
    }
    obj->animating = 0U;
    obj->anim_hiding = 0U;
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param new_y 新的 y 值。
 * @return 无。
 */
static void _popup_set_panel_y(we_popup_obj_t *obj, int16_t new_y)
{
    if (obj == NULL)
        return;
    if (obj->panel_y == new_y)
        return;

    _popup_invalidate_panel(obj);
    obj->panel_y = new_y;
    _popup_invalidate_panel(obj);
}

/**
 * @brief 内部辅助：popup_bring_to_front。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_bring_to_front(we_popup_obj_t *obj)
{
    we_obj_t *curr;
    we_obj_t *prev;
    we_obj_t *tail;
    we_lcd_t *lcd;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    lcd = obj->base.lcd;
    curr = lcd->obj_list_head;
    prev = NULL;
    while (curr != NULL && curr != (we_obj_t *)obj)
    {
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL || curr->next == NULL)
        return;

    if (prev == NULL)
        lcd->obj_list_head = curr->next;
    else
        prev->next = curr->next;

    tail = lcd->obj_list_head;
    while (tail->next != NULL)
        tail = tail->next;
    tail->next = curr;
    curr->next = NULL;
}

/**
 * @brief 读取当前属性值或计算结果。
 * @param obj 目标控件对象指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @return 无。
 */
static void _popup_get_confirm_rect(const we_popup_obj_t *obj,
                                    int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    int16_t panel_x;
    int16_t action_h;
    int16_t action_y;
    int16_t confirm_w;
    int16_t cancel_w;
    int16_t group_w;
    int16_t group_x;

    panel_x = _popup_panel_x(obj);
    action_h = 28;
    action_y = (int16_t)(obj->panel_y + obj->panel_h - action_h - 14);
    confirm_w = (int16_t)((_popup_text_longest_line_width(obj->button_font, obj->confirm_text) > 0U) ?
                          (_popup_text_longest_line_width(obj->button_font, obj->confirm_text) + 20U) : 48U);

    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
    {
        cancel_w = (int16_t)((_popup_text_longest_line_width(obj->button_font, obj->cancel_text) > 0U) ?
                             (_popup_text_longest_line_width(obj->button_font, obj->cancel_text) + 20U) : 48U);
        group_w = (int16_t)(cancel_w + 24 + confirm_w);
        group_x = (int16_t)(panel_x + (obj->panel_w - group_w) / 2);
        *x = (int16_t)(group_x + cancel_w + 24);
        *w = confirm_w;
    }
    else
    {
        *x = (int16_t)(panel_x + (obj->panel_w - confirm_w) / 2);
        *w = confirm_w;
    }

    *y = action_y;
    *h = action_h;
}

/**
 * @brief 读取当前属性值或计算结果。
 * @param obj 目标控件对象指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @return 无。
 */
static void _popup_get_cancel_rect(const we_popup_obj_t *obj,
                                   int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    int16_t panel_x;
    int16_t action_h;
    int16_t action_y;
    int16_t confirm_w;
    int16_t cancel_w;
    int16_t group_w;
    int16_t group_x;

    panel_x = _popup_panel_x(obj);
    action_h = 28;
    action_y = (int16_t)(obj->panel_y + obj->panel_h - action_h - 14);
    confirm_w = (int16_t)((_popup_text_longest_line_width(obj->button_font, obj->confirm_text) > 0U) ?
                          (_popup_text_longest_line_width(obj->button_font, obj->confirm_text) + 20U) : 48U);
    cancel_w = (int16_t)((_popup_text_longest_line_width(obj->button_font, obj->cancel_text) > 0U) ?
                         (_popup_text_longest_line_width(obj->button_font, obj->cancel_text) + 20U) : 48U);
    group_w = (int16_t)(cancel_w + 24 + confirm_w);
    group_x = (int16_t)(panel_x + (obj->panel_w - group_w) / 2);
    *w = cancel_w;
    *x = group_x;
    *y = action_y;
    *h = action_h;
}

/**
 * @brief 命中测试：判断点是否落在目标区域。
 * @param px 待检测点 X 坐标。
 * @param py 待检测点 Y 坐标。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @return 1 表示命中，0 表示未命中。
 */
static uint8_t _popup_hit_rect(int16_t px, int16_t py, int16_t x, int16_t y, int16_t w, int16_t h)
{
    return (uint8_t)(px >= x && px < (x + w) && py >= y && py < (y + h));
}

/**
 * @brief 命中测试：判断点是否落在目标区域。
 * @param obj 目标控件对象指针。
 * @param px 待检测点 X 坐标。
 * @param py 待检测点 Y 坐标。
 * @return 1 表示命中，0 表示未命中。
 */
static uint8_t _popup_hit_button(const we_popup_obj_t *obj, int16_t px, int16_t py)
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;

    _popup_get_confirm_rect(obj, &x, &y, &w, &h);
    if (_popup_hit_rect(px, py, x, y, w, h))
        return WE_POPUP_BTN_CONFIRM;

    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
    {
        _popup_get_cancel_rect(obj, &x, &y, &w, &h);
        if (_popup_hit_rect(px, py, x, y, w, h))
            return WE_POPUP_BTN_CANCEL;
    }

    return WE_POPUP_BTN_NONE;
}

/**
 * @brief 计算脏区并发起局部重绘请求。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_invalidate_buttons(we_popup_obj_t *obj)
{
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;

    _popup_get_confirm_rect(obj, &x, &y, &w, &h);
    we_obj_invalidate_area((we_obj_t *)obj, x, y, w, h);

    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
    {
        _popup_get_cancel_rect(obj, &x, &y, &w, &h);
        we_obj_invalidate_area((we_obj_t *)obj, x, y, w, h);
    }
}

/**
 * @brief 在当前 PFB 裁剪区内执行局部绘制。
 * @param obj 目标控件对象指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param text UTF-8 文本字符串。
 * @param pressed 当前按钮是否处于按下态。
 * @param secondary 是否按次级按钮配色绘制。
 * @return 无。
 */
static void _popup_draw_button(we_popup_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h,
                               const char *text, uint8_t pressed, uint8_t secondary)
{
    int16_t txt_x;
    int16_t txt_y;
    int16_t underline_y;
    int16_t underline_x0;
    int16_t underline_x1;
    uint16_t txt_w;
    int8_t y_top;
    int8_t y_bottom;
    colour_t text_color;
    colour_t line_color;
    uint8_t show_underline;

    if (pressed)
    {
        text_color = RGB888TODEV(245, 248, 252);
        line_color = RGB888TODEV(245, 248, 252);
        show_underline = 1U;
    }
    else
    {
        if (secondary)
        {
            text_color = RGB888TODEV(176, 183, 194);
            line_color = text_color;
            show_underline = 0U;
        }
        else
        {
            text_color = RGB888TODEV(236, 241, 248);
            line_color = RGB888TODEV(236, 241, 248);
            show_underline = 1U;
        }
    }

    if (text == NULL || obj->button_font == NULL)
        return;

    txt_w = we_get_text_width(obj->button_font, text);
    we_get_text_bbox(obj->button_font, text, &y_top, &y_bottom);
    txt_x = (int16_t)(x + (w - (int16_t)txt_w) / 2);
    txt_y = (int16_t)(y + h / 2 - (y_top + y_bottom) / 2 - 2);
    we_draw_string(obj->base.lcd, txt_x, txt_y, obj->button_font, text, text_color, 255);

    if (show_underline)
    {
        underline_x0 = (int16_t)(txt_x - 2);
        underline_x1 = (int16_t)(txt_x + (int16_t)txt_w + 1);
        underline_y = (int16_t)(y + h - 4);
        we_draw_line(obj->base.lcd, underline_x0, underline_y, underline_x1, underline_y, 1, line_color, 255);
    }
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _popup_draw_cb(void *ptr)
{
    we_popup_obj_t *obj = (we_popup_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t panel_x;
    int16_t title_x;
    int16_t title_y;
    int16_t msg_x;
    int16_t msg_y;
    int16_t inner_w;
    int16_t content_x;
    int16_t content_y;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    uint16_t title_h;
    uint16_t msg_h;
    int8_t title_top;
    int8_t msg_top;

    if (obj == NULL || obj->visible == 0U)
        return;

    lcd = obj->base.lcd;
    panel_x = _popup_panel_x(obj);
    inner_w = (int16_t)obj->panel_w - 36;
    content_x = (int16_t)(panel_x + 18);

    /* 面板改成纯直角矩形，直接走矩形快路径。 */
    we_fill_rect(lcd, panel_x, obj->panel_y, obj->panel_w, obj->panel_h,
                 RGB888TODEV(46, 52, 64), 255);

    if (obj->title != NULL && obj->title_font != NULL)
    {
        title_h = _popup_text_block_height_wrapped(obj->title_font, obj->title, (uint16_t)inner_w);
        we_get_text_bbox(obj->title_font, obj->title, &title_top, &msg_top);
        title_x = content_x;
        title_y = (int16_t)(obj->panel_y + 18 - title_top);
        _popup_draw_text_wrapped(lcd, obj->title_font, obj->title, title_x, title_y,
                                 (uint16_t)inner_w, RGB888TODEV(244, 247, 252), 255);
        content_y = (int16_t)(obj->panel_y + 18 + title_h);
    }
    else
    {
        content_y = (int16_t)(obj->panel_y + 18);
    }

    _popup_get_confirm_rect(obj, &x, &y, &w, &h);

    if (obj->message != NULL && obj->message_font != NULL)
    {
        if (obj->title != NULL && obj->title[0] != '\0')
            content_y = (int16_t)(content_y + 8);
        msg_h = _popup_text_block_height_wrapped(obj->message_font, obj->message, (uint16_t)inner_w);
        (void)msg_h;
        we_get_text_bbox(obj->message_font, obj->message, &msg_top, &title_top);
        msg_x = content_x;
        msg_y = (int16_t)(content_y - msg_top);
        _popup_draw_text_wrapped(lcd, obj->message_font, obj->message, msg_x, msg_y,
                                 (uint16_t)inner_w, RGB888TODEV(204, 214, 228), 255);
    }

    {
        int16_t sep_y = (int16_t)(y - 10);
        we_draw_line(lcd, content_x, sep_y, (int16_t)(panel_x + obj->panel_w - 18), sep_y,
1, RGB888TODEV(78, 178, 255), 255);
    }

    _popup_draw_button(obj, x, y, w, h, obj->confirm_text,
                       (uint8_t)(obj->pressed_btn == WE_POPUP_BTN_CONFIRM), 0U);

    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
    {
    _popup_get_cancel_rect(obj, &x, &y, &w, &h);
        _popup_draw_button(obj, x, y, w, h, obj->cancel_text,
                           (uint8_t)(obj->pressed_btn == WE_POPUP_BTN_CANCEL), 1U);
    }
}

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _popup_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_popup_obj_t *obj = (we_popup_obj_t *)ptr;
    uint8_t hit_btn;
    uint8_t old_pressed;

    if (obj == NULL || obj->visible == 0U || data == NULL)
        return 0;

    old_pressed = obj->pressed_btn;
    hit_btn = _popup_hit_button(obj, data->x, data->y);

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed_btn = hit_btn;
        obj->armed_btn = hit_btn;
        break;
    case WE_EVENT_RELEASED:
        obj->pressed_btn = WE_POPUP_BTN_NONE;
        break;
    case WE_EVENT_CLICKED:
        if (obj->armed_btn == hit_btn)
        {
            obj->pressed_btn = WE_POPUP_BTN_NONE;
            if (hit_btn == WE_POPUP_BTN_CONFIRM && obj->confirm_cb != NULL)
                obj->confirm_cb(obj);
            else if (hit_btn == WE_POPUP_BTN_CANCEL && obj->cancel_cb != NULL)
                obj->cancel_cb(obj);
        }
        else
        {
            obj->pressed_btn = WE_POPUP_BTN_NONE;
        }
        obj->armed_btn = WE_POPUP_BTN_NONE;
        break;
    case WE_EVENT_STAY:
    case WE_EVENT_SWIPE_LEFT:
    case WE_EVENT_SWIPE_RIGHT:
    case WE_EVENT_SWIPE_UP:
    case WE_EVENT_SWIPE_DOWN:
    default:
        break;
    }

    if (old_pressed != obj->pressed_btn)
    _popup_invalidate_buttons(obj);

    return 1;
}

/**
 * @brief 周期回调中按时间片推进动画状态。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _popup_anim_task(we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms)
{
    we_popup_obj_t *obj = (we_popup_obj_t *)user_data;
    uint16_t t;
    uint16_t eased;
    int16_t next_y;

    (void)lcd;

    if (obj == NULL || obj->visible == 0U)
    {
    _popup_stop_anim(obj);
        return;
    }

    if (obj->anim_duration_ms == 0U)
        obj->anim_duration_ms = WE_POPUP_ANIM_DURATION_MS;

    if (obj->anim_elapsed_ms + elapsed_ms >= obj->anim_duration_ms)
        obj->anim_elapsed_ms = obj->anim_duration_ms;
    else
        obj->anim_elapsed_ms = (uint16_t)(obj->anim_elapsed_ms + elapsed_ms);

    t = (uint16_t)(((uint32_t)obj->anim_elapsed_ms * 256U) / obj->anim_duration_ms);
    if (t > 256U)
        t = 256U;
#if WE_MSGBOX_USE_ANIM
eased = obj->anim_hiding ? _msgbox_ease_hide(t) : _msgbox_ease_slide(t);
    next_y = (int16_t)we_lerp(obj->anim_from_y, obj->anim_to_y, eased);
    if (next_y == obj->panel_y && obj->panel_y != obj->anim_to_y)
    {
        next_y = (obj->anim_to_y > obj->panel_y) ? (int16_t)(obj->panel_y + 1) : (int16_t)(obj->panel_y - 1);
    }
#else
    (void)t;
    (void)eased;
    next_y = obj->anim_to_y;
    obj->anim_elapsed_ms = obj->anim_duration_ms;
#endif
    _popup_set_panel_y(obj, next_y);

    if (obj->anim_elapsed_ms >= obj->anim_duration_ms)
    {
    _popup_set_panel_y(obj, obj->anim_to_y);
        if (obj->anim_hiding)
            obj->visible = 0U;
    _popup_stop_anim(obj);
    }
}

static const we_class_t _popup_class = {
    .draw_cb  = _popup_draw_cb,
    .event_cb = _popup_event_cb,
};

/**
 * @brief 初始化对象状态并绑定所需回调。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param target_y 弹窗目标 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param confirm_text 确认按钮文本。
 * @param cancel_text 取消按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param confirm_cb 确认按钮回调函数。
 * @param cancel_cb 取消按钮回调函数。
 * @param layout 弹窗布局类型（仅确认/确认+取消）。
 * @return 无。
 */
static void _popup_init_common(we_popup_obj_t *obj, we_lcd_t *lcd,
                               uint16_t w, uint16_t h, int16_t target_y,
                               const char *title, const char *message,
                               const char *confirm_text, const char *cancel_text,
                               const unsigned char *title_font,
                               const unsigned char *message_font,
                               const unsigned char *button_font,
                               we_popup_action_cb_t confirm_cb,
                               we_popup_action_cb_t cancel_cb,
                               we_popup_layout_t layout)
{
    we_obj_t *tail;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = 0;
    obj->base.y = 0;
    obj->base.w = lcd->width;
    obj->base.h = lcd->height;
    obj->base.parent = NULL;
    obj->base.class_p = &_popup_class;
    obj->base.next = NULL;

    obj->panel_min_w = (uint16_t)WE_MIN((int32_t)w, (int32_t)(lcd->width - WE_POPUP_EDGE_MARGIN * 2));
    obj->panel_w = obj->panel_min_w;
    obj->panel_h = h;
    obj->target_y = target_y;
    obj->anim_elapsed_ms = 0U;
    obj->anim_duration_ms = WE_POPUP_ANIM_DURATION_MS;
    obj->task_id = -1;
    obj->layout = (uint8_t)layout;
    obj->pressed_btn = WE_POPUP_BTN_NONE;
    obj->armed_btn = WE_POPUP_BTN_NONE;
    obj->visible = 0U;
    obj->animating = 0U;
    obj->anim_hiding = 0U;

    obj->title = title;
    obj->message = message;
    obj->confirm_text = confirm_text;
    obj->cancel_text = cancel_text;
    obj->title_font = title_font;
    obj->message_font = message_font;
    obj->button_font = button_font;
    obj->confirm_cb = confirm_cb;
    obj->cancel_cb = cancel_cb;

    _popup_refresh_layout(obj);
    obj->panel_y = obj->hidden_y;
    obj->anim_from_y = obj->hidden_y;
    obj->anim_to_y = obj->target_y;

    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        tail = lcd->obj_list_head;
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param target_y 弹窗目标 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param confirm_text 确认按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param confirm_cb 确认按钮回调函数。
 * @return 无。
 */
static void _popup_confirm_obj_init(we_popup_obj_t *obj, we_lcd_t *lcd,
                                    uint16_t w, uint16_t h, int16_t target_y,
                                    const char *title, const char *message,
                                    const char *confirm_text,
                                    const unsigned char *title_font,
                                    const unsigned char *message_font,
                                    const unsigned char *button_font,
                                    we_popup_action_cb_t confirm_cb)
{
    _popup_init_common(obj, lcd, w, h, target_y, title, message,
                       confirm_text, NULL, title_font, message_font, button_font,
                       confirm_cb, NULL, WE_POPUP_LAYOUT_CONFIRM);
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param target_y 弹窗目标 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param confirm_text 确认按钮文本。
 * @param cancel_text 取消按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param confirm_cb 确认按钮回调函数。
 * @param cancel_cb 取消按钮回调函数。
 * @return 无。
 */
static void _popup_confirm_cancel_obj_init(we_popup_obj_t *obj, we_lcd_t *lcd,
                                           uint16_t w, uint16_t h, int16_t target_y,
                                           const char *title, const char *message,
                                           const char *confirm_text, const char *cancel_text,
                                           const unsigned char *title_font,
                                           const unsigned char *message_font,
                                           const unsigned char *button_font,
                                           we_popup_action_cb_t confirm_cb,
                                           we_popup_action_cb_t cancel_cb)
{
    _popup_init_common(obj, lcd, w, h, target_y, title, message,
                       confirm_text, cancel_text, title_font, message_font, button_font,
                       confirm_cb, cancel_cb, WE_POPUP_LAYOUT_CONFIRM_CANCEL);
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param target_y 弹窗目标 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @return 无。
 */
void we_msgbox_ok_obj_init(we_msgbox_obj_t *obj, we_lcd_t *lcd,
                           uint16_t w, uint16_t h, int16_t target_y,
                           const char *title, const char *message,
                           const char *ok_text,
                           const unsigned char *title_font,
                           const unsigned char *message_font,
                           const unsigned char *button_font,
                           we_msgbox_action_cb_t ok_cb)
{
    _popup_confirm_obj_init(obj, lcd, w, h, target_y, title, message,
                            ok_text, title_font, message_font, button_font, ok_cb);
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param target_y 弹窗目标 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param cancel_text 取消按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @param cancel_cb 取消按钮回调函数。
 * @return 无。
 */
void we_msgbox_ok_cancel_obj_init(we_msgbox_obj_t *obj, we_lcd_t *lcd,
                                  uint16_t w, uint16_t h, int16_t target_y,
                                  const char *title, const char *message,
                                  const char *ok_text, const char *cancel_text,
                                  const unsigned char *title_font,
                                  const unsigned char *message_font,
                                  const unsigned char *button_font,
                                  we_msgbox_action_cb_t ok_cb,
                                  we_msgbox_action_cb_t cancel_cb)
{
    _popup_confirm_cancel_obj_init(obj, lcd, w, h, target_y, title, message,
                                   ok_text, cancel_text, title_font, message_font,
                                   button_font, ok_cb, cancel_cb);
}

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @return 无。
 */
void we_popup_set_text(we_popup_obj_t *obj, const char *title, const char *message)
{
    if (obj == NULL)
        return;
    if (obj->visible)
    _popup_invalidate_panel(obj);
    obj->title = title;
    obj->message = message;
    _popup_refresh_layout(obj);
    if (obj->visible == 0U)
        obj->panel_y = obj->hidden_y;
    if (obj->visible)
    _popup_invalidate_panel(obj);
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param confirm_text 确认按钮文本。
 * @param cancel_text 取消按钮文本。
 * @return 无。
 */
void we_popup_set_buttons(we_popup_obj_t *obj, const char *confirm_text, const char *cancel_text)
{
    if (obj == NULL)
        return;
    if (obj->visible)
    _popup_invalidate_buttons(obj);
    obj->confirm_text = confirm_text;
    obj->cancel_text = cancel_text;
    if (obj->visible)
    _popup_invalidate_buttons(obj);
}

/**
 * @brief 读取当前属性值或计算结果。
 * @param obj 目标控件对象指针。
 * @param target_y 弹窗目标 Y 坐标。
 * @return 无。
 */
void we_popup_set_target_y(we_popup_obj_t *obj, int16_t target_y)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (target_y < 0)
        target_y = 0;
    if (target_y > (int16_t)(obj->base.lcd->height - obj->panel_h))
        target_y = (int16_t)(obj->base.lcd->height - obj->panel_h);
    obj->target_y = target_y;
    if (obj->visible != 0U && obj->animating == 0U)
    _popup_set_panel_y(obj, obj->target_y);
}

/**
 * @brief 执行 we_popup_show。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_popup_show(we_popup_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;

    _popup_bring_to_front(obj);
    obj->visible = 1U;
    obj->pressed_btn = WE_POPUP_BTN_NONE;
    obj->armed_btn = WE_POPUP_BTN_NONE;
    obj->anim_elapsed_ms = 0U;
    obj->anim_hiding = 0U;
    obj->anim_duration_ms = WE_MSGBOX_ANIM_DURATION_MS;
    obj->anim_from_y = obj->hidden_y;
    obj->anim_to_y = obj->target_y;
    _popup_refresh_layout(obj);
#if WE_MSGBOX_USE_ANIM
    _popup_set_panel_y(obj, obj->hidden_y);
#else
    _popup_set_panel_y(obj, obj->target_y);
#endif

#if WE_MSGBOX_USE_ANIM
    if (obj->task_id < 0)
obj->task_id = _we_gui_task_register_with_data(obj->base.lcd, _popup_anim_task, obj);
    if (obj->task_id >= 0)
        obj->animating = 1U;
    else
    _popup_set_panel_y(obj, obj->target_y);
#else
    obj->animating = 0U;
#endif

    _popup_invalidate_panel(obj);
}

/**
 * @brief 执行 we_popup_hide。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_popup_hide(we_popup_obj_t *obj)
{
    if (obj == NULL)
        return;
    if (obj->visible == 0U && obj->animating == 0U)
        return;

    obj->pressed_btn = WE_POPUP_BTN_NONE;
    obj->armed_btn = WE_POPUP_BTN_NONE;
    obj->anim_elapsed_ms = 0U;
    obj->anim_hiding = 1U;
    obj->visible = 1U;
    obj->anim_duration_ms = (uint16_t)(WE_MSGBOX_ANIM_DURATION_MS / 2U);
    if (obj->anim_duration_ms < 90U)
        obj->anim_duration_ms = 90U;
    obj->anim_from_y = obj->panel_y;
    obj->anim_to_y = obj->hidden_y;

#if WE_MSGBOX_USE_ANIM
    if (obj->task_id < 0)
obj->task_id = _we_gui_task_register_with_data(obj->base.lcd, _popup_anim_task, obj);
    if (obj->task_id >= 0)
        obj->animating = 1U;
    else
    {
    _popup_set_panel_y(obj, obj->hidden_y);
        obj->visible = 0U;
    _popup_stop_anim(obj);
    }
#else
    _popup_set_panel_y(obj, obj->hidden_y);
    obj->visible = 0U;
    obj->animating = 0U;
    obj->anim_hiding = 0U;
#endif
}
