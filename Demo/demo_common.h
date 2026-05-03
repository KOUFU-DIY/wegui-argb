#ifndef DEMO_COMMON_H
#define DEMO_COMMON_H

#include "we_gui_driver.h"
#include "widgets/label/we_widget_label.h"

/**
 * @brief Demo 公共辅助声明。
 * @details
 * 所有 active demo 统一按固定 280x240 布局直接书写。
 * 下面这些缩放 helper 仅为历史兼容暂时保留，新的 active demo 不再使用。
 */
#define WE_DEMO_BASE_W 280
#define WE_DEMO_BASE_H 240

/**
 * @brief 按当前屏幕宽度缩放 X 坐标。
 * @param lcd LCD 运行实例。
 * @param base_x 基准坐标（以 280x240 为基准）。
 * @return 缩放后的 X 坐标。
 */
int16_t we_demo_scale_x(const we_lcd_t *lcd, int16_t base_x);

/**
 * @brief 按当前屏幕高度缩放 Y 坐标。
 * @param lcd LCD 运行实例。
 * @param base_y 基准坐标（以 280x240 为基准）。
 * @return 缩放后的 Y 坐标。
 */
int16_t we_demo_scale_y(const we_lcd_t *lcd, int16_t base_y);

/**
 * @brief 按当前屏幕宽度缩放宽度值。
 * @param lcd LCD 运行实例。
 * @param base_w 基准宽度（以 280x240 为基准）。
 * @return 缩放后的宽度。
 */
int16_t we_demo_scale_w(const we_lcd_t *lcd, int16_t base_w);

/**
 * @brief 按当前屏幕高度缩放高度值。
 * @param lcd LCD 运行实例。
 * @param base_h 基准高度（以 280x240 为基准）。
 * @return 缩放后的高度。
 */
int16_t we_demo_scale_h(const we_lcd_t *lcd, int16_t base_h);

/**
 * @brief 计算对象右对齐时的 X 坐标。
 * @param lcd LCD 运行实例。
 * @param right_margin 距右边距（基准分辨率下）。
 * @param obj_w 对象宽度（当前分辨率下）。
 * @return 右对齐后对象左上角 X 坐标。
 */
int16_t we_demo_right_x(const we_lcd_t *lcd, int16_t right_margin, int16_t obj_w);

/**
 * @brief 计算对象底对齐时的 Y 坐标。
 * @param lcd LCD 运行实例。
 * @param bottom_margin 距底边距（基准分辨率下）。
 * @param obj_h 对象高度（当前分辨率下）。
 * @return 底对齐后对象左上角 Y 坐标。
 */
int16_t we_demo_bottom_y(const we_lcd_t *lcd, int16_t bottom_margin, int16_t obj_h);

/**
 * @brief 更新并显示 FPS 文本。
 * @param lcd LCD 运行实例。
 * @param fps_label 用于显示 FPS 的标签对象。
 * @param fps_timer FPS 累计计时器（毫秒）。
 * @param last_frames 上次统计时的总渲染帧计数。
 * @param fps_buf FPS 文本缓冲区。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_demo_update_fps(we_lcd_t *lcd,
                        we_label_obj_t *fps_label,
                        uint32_t *fps_timer,
                        uint32_t *last_frames,
                        char *fps_buf,
                        uint16_t ms_tick);

#endif
