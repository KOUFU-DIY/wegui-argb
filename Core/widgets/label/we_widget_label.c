#include "we_widget_label.h"
#include "wegui.h"

// =========================================================
// 内部核心对接桩
// =========================================================

/**
 * @brief 计算文本的包围盒尺寸（按换行分段统计）。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @param out_w 输出文本最大行宽（像素，计入字形向右伸出的墨迹）。
 * @param out_h 输出文本总高度（像素）。
 */
static void _label_get_text_bbox(const unsigned char *font, const char *text, int16_t *out_w, int16_t *out_h)
{
    uint16_t line_height;
    int16_t max_w = 0;
    int16_t lines = 1;
    const char *p = text;

    if (font == NULL || text == NULL)
    {
        *out_w = 0;
        *out_h = 0;
        return;
    }

    line_height = we_font_get_line_height(font);

    // 逐行测宽，寻找最长的一行（墨迹伸出步进的字形按墨迹算，包围盒不裁掉它）
    while (*p)
    {
        uint16_t current_line_w = we_get_text_ink_width(font, p);
        if ((int16_t)current_line_w > max_w)
            max_w = (int16_t)current_line_w;

        // 推进指针到下一行
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
        {
            lines++;
            p++; // 跳过换行符
        }
    }

    *out_w = max_w;
    *out_h = (int16_t)(lines * line_height);
}

/**
 * @brief 绘制文本内容到当前 PFB。
 * @param ptr 文本控件对象指针。
 */
static void _label_draw_cb(void *ptr)
{
    we_label_obj_t *obj = (we_label_obj_t *)ptr;
    if (obj->text == NULL || obj->font == NULL || obj->opacity == 0)
        return;

    we_draw_string(obj->base.lcd, obj->base.x, obj->base.y, obj->font, obj->text, obj->color, obj->opacity);
}

// =========================================================
// 控件生命周期 API
// =========================================================

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param text UTF-8 文本字符串。
 * @param font 字体资源指针。
 * @param color 目标颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_label_obj_init(we_label_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, const char *text,
                       const unsigned char *font, colour_t color, uint8_t opacity)
{
    if (obj == NULL || lcd == NULL)
        return;

    int16_t w = 0, h = 0;
    _label_get_text_bbox(font, text, &w, &h);

    obj->base.lcd = lcd;
    obj->base.parent = NULL;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;

    obj->text = text;
    obj->font = font;
    obj->color = color;
    obj->opacity = opacity;

    static const we_class_t _label_class = {.draw_cb = _label_draw_cb, .event_cb = NULL, .set_pos_cb = NULL};
    obj->base.class_p = &_label_class;
    obj->base.next = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0 && w > 0 && h > 0)
    {
        we_obj_invalidate((we_obj_t *)obj);
    }
}

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param new_text 新的文本字符串。
 * @return 无。
 */
void we_label_set_text(we_label_obj_t *obj, const char *new_text)
{
    int16_t new_w;
    int16_t new_h;

    if (obj == NULL || obj->base.lcd == NULL || new_text == NULL)
        return;

    _label_get_text_bbox(obj->font, new_text, &new_w, &new_h);

    /* 包围盒就是文字框：先标旧框，换文字与尺寸后再标新框 */
    if (obj->opacity > 0)
        we_obj_invalidate((we_obj_t *)obj);

    obj->text = new_text;
    obj->base.w = new_w;
    obj->base.h = new_h;

    if (obj->opacity > 0)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param new_color 新的颜色值。
 * @return 无。
 */
void we_label_set_color(we_label_obj_t *obj, colour_t new_color)
{
    if (obj == NULL || obj->base.lcd == NULL || we_colour_eq(obj->color, new_color))
        return;

    obj->color = new_color;
    if (obj->opacity > 0)
    {
        we_obj_invalidate((we_obj_t *)obj);
    }
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_label_set_opacity(we_label_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->opacity == opacity)
        return;

    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj); /* 包围盒不变，标一次即可 */
}
