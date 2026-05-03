#ifndef __WE_WIDGET_ARC_H
#define __WE_WIDGET_ARC_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 圆弧控件建议脏矩形开关
 * 0: 精细脏矩形, 更高的刷屏效率
 * 1: 简易脏矩形, 更低的代码占用
 * -------------------------------------------------------------------------- */
#ifndef WE_ARC_OPT_MODE
#define WE_ARC_OPT_MODE (0)
#endif

// ---------------------------------------------------------
// 单色圆弧进度条控件结构体 (紧凑包围盒版)
// ---------------------------------------------------------
typedef struct
{
    we_obj_t base;          // 基类
    uint16_t radius;        // 外环半径
    uint16_t thickness;     // 线宽
    int16_t center_off_x;   // 圆心相对于 base.x 的物理偏移量
    int16_t center_off_y;   // 圆心相对于 base.y 的物理偏移量
    int16_t bg_start_angle; // 轨道起点绝对角度，512 步制 (0~511)
    int16_t bg_span;        // 轨道顺时针总跨度，512 步制 (0~512)
    uint8_t value;          // 当前进度 (0~255)
    colour_t fg_color;      // 前景进度条颜色
    colour_t bg_color;      // 轨道底槽颜色
    uint8_t opacity;        // 全局透明度
} we_arc_obj_t;

/**
 * @brief 初始化圆弧控件，并根据角度范围计算初始包围盒。
 * @param obj 圆弧控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param cx 圆心 X 坐标（屏幕坐标）。
 * @param cy 圆心 Y 坐标（屏幕坐标）。
 * @param r 圆弧外半径（像素）。
 * @param th 圆弧线宽（像素）。
 * @param start_angle 圆弧起始角度（512 分度制）。
 * @param end_angle 圆弧结束角度（512 分度制）。
 * @param fg_color 前景进度弧颜色。
 * @param bg_color 背景轨道颜色。
 * @param opacity 控件整体不透明度（0~255）。
 * @return 无。
 */
void we_arc_obj_init(we_arc_obj_t *obj, we_lcd_t *lcd, int16_t cx, int16_t cy, uint16_t r, uint16_t th,
                     int16_t start_angle, int16_t end_angle, colour_t fg_color, colour_t bg_color, uint8_t opacity);

/**
 * @brief 设置圆弧当前进度值，并触发对应区域重绘。
 * @param obj 圆弧控件对象指针。
 * @param value 进度值（0~255，对应 0%~100%）。
 * @return 无。
 */
void we_arc_set_value(we_arc_obj_t *obj, uint8_t value);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_arc_set_opacity(we_arc_obj_t *obj, uint8_t opacity);

/**
 * @brief 设置圆弧控件在对象树中的左上角坐标。
 * @param obj 圆弧控件对象指针。
 * @param x 新的左上角 X 坐标。
 * @param y 新的左上角 Y 坐标。
 * @return 无。
 */
static inline void we_arc_obj_set_pos(we_arc_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_set_pos((we_obj_t *)obj, x, y);
}

/**
 * @brief 执行we_arc_obj_delete的核心处理流程。
 * @param obj 控件对象指针
 * @return 无
 */
static inline void we_arc_obj_delete(we_arc_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

#endif
