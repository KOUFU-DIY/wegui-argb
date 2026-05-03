#ifndef __WE_WIDGET_LABEL_H
#define __WE_WIDGET_LABEL_H

#include "we_gui_driver.h"

// ---------------------------------------------------------
// 文本控件结构体
// ---------------------------------------------------------
typedef struct
{
    we_obj_t base;             // 必须在首位！包含 lcd, x, y, w, h
    const char *text;          // UTF-8 字符串指针
    const unsigned char *font; // V7.x 字库裸数组指针
    colour_t color;            // 文字前景色
    uint8_t opacity;           // 文字透明度 (0~255)
} we_label_obj_t;

// ---------------------------------------------------------
// 核心 API 声明
// ---------------------------------------------------------

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
                       const unsigned char *font, colour_t color, uint8_t opacity);

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param new_text 新的文本字符串。
 * @return 无。
 */
void we_label_set_text(we_label_obj_t *obj, const char *new_text);

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param new_color 新的颜色值。
 * @return 无。
 */
void we_label_set_color(we_label_obj_t *obj, colour_t new_color);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_label_set_opacity(we_label_obj_t *obj, uint8_t opacity);

// ---------------------------------------------------------
// 继承基类的语法糖 API
// ---------------------------------------------------------

/**
 * @brief 设置文本控件左上角位置。
 * @param obj 文本控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 */
static inline void we_label_obj_set_pos(we_label_obj_t *obj, int16_t x, int16_t y)
{
we_obj_set_pos((we_obj_t *)obj, x, y);
}

/**
 * @brief 执行we_label_obj_delete的核心处理流程。
 * @param obj 控件对象指针
 * @return 无
 */
static inline void we_label_obj_delete(we_label_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

#endif
