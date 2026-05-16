#ifndef SIMPLE_WIDGET_DEMOS_H
#define SIMPLE_WIDGET_DEMOS_H

#include "we_gui_driver.h"

/* 所有 simple demo 统一按固定 280x240 样式组织：
 * 1. 先调用对应 init(...) 创建控件并完成固定布局；
 * 2. 再用 we_gui_timer_create(lcd, 对应 tick, 16U, 1U) 注册周期定时器；
 * 3. 主循环里继续调用 we_gui_tick_inc(...) 和 we_gui_task_handler(...)。 */

/**
 * @brief 初始化 label simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_label_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 label simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_label_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 btn simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_btn_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 btn simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_btn_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 img simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_img_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 img simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_img_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 img_ex simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_img_ex_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 img_ex simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_img_ex_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 arc simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_arc_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 arc simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_arc_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 group simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_group_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 group simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_group_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 slideshow simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_slideshow_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 slideshow simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_slideshow_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 concentric arc simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_concentric_arc_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 concentric arc simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_concentric_arc_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 checkbox simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_checkbox_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 checkbox simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_checkbox_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 label_ex simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_label_ex_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 label_ex simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_label_ex_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 chart simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_chart_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 chart simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_chart_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 toggle simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_toggle_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 toggle simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_toggle_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 progress simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_progress_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 progress simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_progress_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 msgbox simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_msgbox_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 msgbox simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_msgbox_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 flash img simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_flash_img_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 flash img simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_flash_img_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 flash font simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_flash_font_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 flash font simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_flash_font_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 slider simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_slider_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 slider simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_slider_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/**
 * @brief 初始化 scroll_panel simple demo。
 * @param lcd LCD 运行实例。
 * @return 无。
 */
void we_scroll_panel_simple_demo_init(we_lcd_t *lcd);

/**
 * @brief 执行 scroll_panel simple demo 周期逻辑。
 * @param lcd LCD 运行实例。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_scroll_panel_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

#endif
