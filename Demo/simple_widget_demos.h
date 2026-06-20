#ifndef SIMPLE_WIDGET_DEMOS_H
#define SIMPLE_WIDGET_DEMOS_H

#include "we_gui_driver.h"

/* 所有 simple demo 统一按固定 280x240 样式组织：
 * 1. 先调用对应 init(...) 创建控件并完成固定布局；
 * 2. 再用 we_gui_timer_create(lcd, 对应 tick, 16U, 1U) 注册周期定时器；
 * 3. 主循环里继续调用 we_gui_tick_inc(...) 和 we_gui_task_handler(...)。 */

/* label demo：init 创建并布局控件，tick 每帧推进（ms_tick 为毫秒增量）。 */
void we_label_simple_demo_init(we_lcd_t *lcd);
void we_label_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* btn demo。 */
void we_btn_simple_demo_init(we_lcd_t *lcd);
void we_btn_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* img demo。 */
void we_img_simple_demo_init(we_lcd_t *lcd);
void we_img_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* img_ex demo（旋转/缩放变换图像）。 */
void we_img_ex_simple_demo_init(we_lcd_t *lcd);
void we_img_ex_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* arc demo。 */
void we_arc_simple_demo_init(we_lcd_t *lcd);
void we_arc_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* group demo（子控件容器 + 透明度级联）。 */
void we_group_simple_demo_init(we_lcd_t *lcd);
void we_group_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* slideshow demo（分页滑动 + 翻页吸附）。 */
void we_slideshow_simple_demo_init(we_lcd_t *lcd);
void we_slideshow_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* concentric arc demo（同心圆弧）。 */
void we_concentric_arc_simple_demo_init(we_lcd_t *lcd);
void we_concentric_arc_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* checkbox demo。 */
void we_checkbox_simple_demo_init(we_lcd_t *lcd);
void we_checkbox_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* label_ex demo（旋转/缩放文本）。 */
void we_label_ex_simple_demo_init(we_lcd_t *lcd);
void we_label_ex_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* chart demo（环形缓冲折线图）。 */
void we_chart_simple_demo_init(we_lcd_t *lcd);
void we_chart_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* toggle demo（开关动画）。 */
void we_toggle_simple_demo_init(we_lcd_t *lcd);
void we_toggle_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* progress demo（0..255 目标值 + 平滑动画）。 */
void we_progress_simple_demo_init(we_lcd_t *lcd);
void we_progress_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* msgbox demo（模态弹窗）。 */
void we_msgbox_simple_demo_init(we_lcd_t *lcd);
void we_msgbox_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* flash img demo（外部 Flash 取图）。 */
void we_flash_img_simple_demo_init(we_lcd_t *lcd);
void we_flash_img_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* flash font demo（外部 Flash 取字库）。 */
void we_flash_font_simple_demo_init(we_lcd_t *lcd);
void we_flash_font_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* slider demo（拖动滑块）。 */
void we_slider_simple_demo_init(we_lcd_t *lcd);
void we_slider_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* scroll_panel demo（可滚动容器）。 */
void we_scroll_panel_simple_demo_init(we_lcd_t *lcd);
void we_scroll_panel_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* dropdown demo（数据驱动下拉列表 + overlay 弹层）。 */
void we_dropdown_simple_demo_init(we_lcd_t *lcd);
void we_dropdown_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* stepper demo（定点步进 + 长按连续步进）。 */
void we_stepper_simple_demo_init(we_lcd_t *lcd);
void we_stepper_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* indicator demo（状态指示灯 + 颜色/辉光过渡）。 */
void we_indicator_simple_demo_init(we_lcd_t *lcd);
void we_indicator_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* 汇总 demo（仅模拟器：4 页 slideshow 串联主要控件与子系统）。 */
void we_showcase_simple_demo_init(we_lcd_t *lcd);
void we_showcase_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

#endif
