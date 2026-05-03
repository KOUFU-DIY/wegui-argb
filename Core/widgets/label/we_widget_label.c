#include "we_widget_label.h"
#include "we_font_text.h"

// =========================================================
// 内部核心对接桩
// =========================================================

/**
 * @brief 计算文本的包围盒尺寸（按换行分段统计）。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @param out_w 输出文本最大行宽（像素）。
 * @param out_h 输出文本总高度（像素）。
 */
static void _label_get_text_bbox(const unsigned char *font, const char *text, int16_t *out_w, int16_t *out_h)
{
    if (font == NULL || text == NULL)
    {
        *out_w = 0;
        *out_h = 0;
        return;
    }

uint16_t line_height = we_font_get_line_height(font);
    int16_t max_w = 0;
    int16_t lines = 1;
    const char *p = text;

    // 逐行测宽，寻找最长的一行
    while (*p)
    {
        // 调用你的函数测量当前行宽
uint16_t current_line_w = we_get_text_width(font, p);
        if (current_line_w > max_w)
        {
            max_w = current_line_w;
        }

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
    *out_h = lines * line_height;
}

/**
 * @brief 使控件当前包围盒区域失效，触发重绘。
 * @param obj 文本控件对象指针。
 */
static void _label_invalidate_obj_bbox(we_label_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == 0)
        return;

we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y, obj->base.w, obj->base.h);
}

/**
 * @brief 按指定文本尺寸使对应区域失效。
 * @param obj 文本控件对象指针。
 * @param text 用于计算失效区域尺寸的文本。
 */
static void _label_invalidate_text_bbox(we_label_obj_t *obj, const char *text)
{
    int16_t w = 0;
    int16_t h = 0;

    if (obj == NULL || obj->base.lcd == NULL || obj->font == NULL || text == NULL || obj->opacity == 0)
        return;

_label_get_text_bbox(obj->font, text, &w, &h);
we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y, w, h);
}

/**
 * @brief 按指定文本尺寸使对应区域失效。
 * @param obj 文本控件对象指针。
 * @param text 用于计算失效区域尺寸的文本。
 */
static void _label_invalidate_text(we_label_obj_t *obj, const char *text)
{
_label_invalidate_text_bbox(obj, text);
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
// ?? 控件生命周期 API
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
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;

    obj->text = text;
    obj->font = font;
    obj->color = color;
    obj->opacity = opacity;

    static const we_class_t _label_class = {.draw_cb = _label_draw_cb, .event_cb = NULL};
    obj->base.class_p = &_label_class;
    obj->base.next = NULL;

    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = lcd->obj_list_head;
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }

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

    if (obj->opacity > 0)
    {
_label_invalidate_obj_bbox(obj);
    }

    obj->text = new_text;
    obj->base.w = new_w;
    obj->base.h = new_h;

    if (obj->opacity > 0)
    {
_label_invalidate_text_bbox(obj, obj->text);
    }
}

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param new_color 新的颜色值。
 * @return 无。
 */
void we_label_set_color(we_label_obj_t *obj, colour_t new_color)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
#if (LCD_DEEP == DEEP_RGB565)
    if (obj->color.dat16 == new_color.dat16)
        return;
#elif (LCD_DEEP == DEEP_RGB888)
    if (obj->color.rgb.r == new_color.rgb.r && obj->color.rgb.g == new_color.rgb.g &&
        obj->color.rgb.b == new_color.rgb.b)
        return;
#endif

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

    if (obj->opacity > 0)
_label_invalidate_text(obj, obj->text);

    obj->opacity = opacity;

    if (obj->opacity > 0)
_label_invalidate_text(obj, obj->text);
}
