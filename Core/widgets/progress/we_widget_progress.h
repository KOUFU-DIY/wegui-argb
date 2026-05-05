#ifndef WE_WIDGET_PROGRESS_H
#define WE_WIDGET_PROGRESS_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 数值过渡动画总时长（毫秒） */
#ifndef WE_PROGRESS_ANIM_DURATION_MS
#define WE_PROGRESS_ANIM_DURATION_MS 220U
#endif

/* 默认圆角半径；0 表示直角 */
#ifndef WE_PROGRESS_RADIUS
#define WE_PROGRESS_RADIUS 8
#endif

typedef struct
{
    we_obj_t base;
    int8_t task_id;

    uint8_t value;
    uint8_t display_value;
    uint8_t anim_from_value;
    uint8_t anim_to_value;
    uint32_t anim_elapsed_ms;
    uint16_t anim_duration_ms;
    uint8_t animating : 1;

    colour_t track_color;
    colour_t fill_color;
    uint8_t opacity;
    uint8_t radius;
} we_progress_obj_t;

/**
 * @brief 初始化进度条对象并注册动画任务。
 * @param obj 进度条对象指针。
 * @param lcd 所属 LCD 上下文。
 * @param x 控件左上角 X 坐标。
 * @param y 控件左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param h 控件高度（像素）。
 * @param init_value 初始进度值（0~255）。
 * @param track_color 背景轨道颜色。
 * @param fill_color 前景进度颜色。
 * @param opacity 整体透明度（0~255）。
 */
void we_progress_obj_init(we_progress_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t w, uint16_t h,
                          uint8_t init_value,
                          colour_t track_color, colour_t fill_color, uint8_t opacity);

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_progress_obj_delete(we_progress_obj_t *obj);

/**
 * @brief 设置目标进度值，并按差值决定是否启用缓动动画。
 * @param obj 进度条对象指针。
 * @param value 新进度值（0~255）。
 */
void we_progress_set_value(we_progress_obj_t *obj, uint8_t value);

/**
 * @brief 在当前目标进度上增加指定增量（饱和到 255）。
 * @param obj 进度条对象指针。
 * @param delta 增量值。
 */
void we_progress_add_value(we_progress_obj_t *obj, uint8_t delta);

/**
 * @brief 在当前目标进度上减少指定增量（不低于 0）。
 * @param obj 进度条对象指针。
 * @param delta 减量值。
 */
void we_progress_sub_value(we_progress_obj_t *obj, uint8_t delta);

/**
 * @brief 获取目标进度值。
 * @param obj 进度条对象指针。
 * @return 目标进度值（0~255）；obj 为 NULL 时返回 0。
 */
uint8_t we_progress_get_value(const we_progress_obj_t *obj);

/**
 * @brief 获取当前显示进度值（动画中的中间值）。
 * @param obj 进度条对象指针。
 * @return 显示进度值（0~255）；obj 为 NULL 时返回 0。
 */
uint8_t we_progress_get_display_value(const we_progress_obj_t *obj);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_progress_set_opacity(we_progress_obj_t *obj, uint8_t opacity);

/**
 * @brief 设置轨道圆角半径。
 * @param obj 进度条对象指针。
 * @param radius 圆角半径（像素，实际绘制会受控件高度限制）。
 */
void we_progress_set_radius(we_progress_obj_t *obj, uint8_t radius);

/**
 * @brief 设置轨道与进度填充颜色。
 * @param obj 进度条对象指针。
 * @param track_color 背景轨道颜色。
 * @param fill_color 前景进度颜色。
 */
void we_progress_set_colors(we_progress_obj_t *obj, colour_t track_color,
                            colour_t fill_color);

/**
 * @brief 设置进度条左上角位置。
 * @param obj 进度条对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 */
static inline void we_progress_obj_set_pos(we_progress_obj_t *obj, int16_t x, int16_t y)
{
we_obj_set_pos((we_obj_t *)obj, x, y);
}

#ifdef __cplusplus
}
#endif

#endif
