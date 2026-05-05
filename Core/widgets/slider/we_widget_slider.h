#ifndef WE_WIDGET_SLIDER_H
#define WE_WIDGET_SLIDER_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef WE_SLIDER_TRACK_THICKNESS
#define WE_SLIDER_TRACK_THICKNESS 8U
#endif

#ifndef WE_SLIDER_THUMB_SIZE
#define WE_SLIDER_THUMB_SIZE 22U
#endif

#ifndef WE_SLIDER_ENABLE_VERTICAL
#define WE_SLIDER_ENABLE_VERTICAL 1
#endif

typedef enum
{
    WE_SLIDER_ORIENT_HOR = 0,
    WE_SLIDER_ORIENT_VER = 1
} we_slider_orient_t;

typedef struct
{
    we_obj_t base;
    uint8_t min_value;
    uint8_t max_value;
    uint8_t value;
    uint8_t opacity;
    uint8_t track_thickness;
    uint8_t thumb_size;
    uint8_t pressed : 1;
    we_slider_orient_t orient;
    colour_t track_color;
    colour_t fill_color;
    colour_t thumb_color;
} we_slider_obj_t;

/**
 * @brief 初始化滑条对象并挂载到 LCD 对象链表。
 * @param obj 滑条对象指针。
 * @param lcd 所属 LCD 上下文。
 * @param x 控件左上角 X 坐标。
 * @param y 控件左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param h 控件高度（像素）。
 * @param orient 滑条方向；默认支持横向和竖向，关闭 `WE_SLIDER_ENABLE_VERTICAL` 后竖向会回退为横向。
 * @param min_value 最小值。
 * @param max_value 最大值。
 * @param init_value 初始值，会被限制到 min/max 范围内。
 * @param track_color 背景轨道颜色。
 * @param fill_color 已选择区间颜色。
 * @param thumb_color 滑块颜色。
 * @param opacity 整体透明度（0~255）。
 */
void we_slider_obj_init(we_slider_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, uint16_t w, uint16_t h,
                        we_slider_orient_t orient,
                        uint8_t min_value, uint8_t max_value, uint8_t init_value,
                        colour_t track_color, colour_t fill_color,
                        colour_t thumb_color, uint8_t opacity);

/**
 * @brief 删除滑条控件对象并从对象链表移除。
 * @param obj 滑条对象指针。
 */
void we_slider_obj_delete(we_slider_obj_t *obj);

/**
 * @brief 设置滑条当前值。
 * @param obj 滑条对象指针。
 * @param value 新值，会被限制到初始化 min/max 范围内。
 */
void we_slider_set_value(we_slider_obj_t *obj, uint8_t value);

/**
 * @brief 在当前值基础上增加指定增量（饱和到 max_value）。
 * @param obj 滑条对象指针。
 * @param delta 增量值。
 */
void we_slider_add_value(we_slider_obj_t *obj, uint8_t delta);

/**
 * @brief 在当前值基础上减少指定增量（不低于 min_value）。
 * @param obj 滑条对象指针。
 * @param delta 减量值。
 */
void we_slider_sub_value(we_slider_obj_t *obj, uint8_t delta);

/**
 * @brief 获取滑条当前值。
 * @param obj 滑条对象指针。
 * @return 当前值；obj 为 NULL 时返回 0。
 */
uint8_t we_slider_get_value(const we_slider_obj_t *obj);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 滑条对象指针。
 * @param opacity 不透明度（0~255）。
 */
void we_slider_set_opacity(we_slider_obj_t *obj, uint8_t opacity);

/**
 * @brief 设置轨道、填充区和滑块颜色。
 * @param obj 滑条对象指针。
 * @param track_color 背景轨道颜色。
 * @param fill_color 已选择区间颜色。
 * @param thumb_color 滑块颜色。
 */
void we_slider_set_colors(we_slider_obj_t *obj, colour_t track_color,
                          colour_t fill_color, colour_t thumb_color);

/**
 * @brief 设置滑块尺寸。
 * @param obj 滑条对象指针。
 * @param thumb_size 滑块直径/边长（像素）。
 */
void we_slider_set_thumb_size(we_slider_obj_t *obj, uint8_t thumb_size);

/**
 * @brief 设置轨道厚度。
 * @param obj 滑条对象指针。
 * @param track_thickness 轨道厚度（像素）。
 */
void we_slider_set_track_thickness(we_slider_obj_t *obj, uint8_t track_thickness);

/**
 * @brief 设置滑条左上角位置。
 * @param obj 滑条对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 */
static inline void we_slider_obj_set_pos(we_slider_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_set_pos((we_obj_t *)obj, x, y);
}

#ifdef __cplusplus
}
#endif

#endif
