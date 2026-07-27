#ifndef SIMPLE_WIDGET_DEMOS_H
#define SIMPLE_WIDGET_DEMOS_H

#include "we_gui_driver.h"

/* demo 层统一字体：资源头只在 demo 层包含（Core 无任何资源感知，
 * 控件字体一律经 init 显式传入）；历史别名 we_font_consolas_18
 * 供全部 demo 书写，指向 demo_ascii_16 字库。 */
#include "demo_ascii_16.h"
#ifndef we_font_consolas_18
#define we_font_consolas_18 ((const unsigned char *)&demo_ascii_16)
#endif

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

/* line demo（线段：端点/平移/颜色动画 + 圆头 cap）。 */
void we_line_simple_demo_init(we_lcd_t *lcd);
void we_line_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* box demo（矩形面板：四角独立圆角/切角 + 边框 + 颜色/透明度动画）。 */
void we_box_simple_demo_init(we_lcd_t *lcd);
void we_box_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* gauge demo（仪表盘：指针差分标脏 + Q16 量程斜率 + 平滑扫动动画）。 */
void we_gauge_simple_demo_init(we_lcd_t *lcd);
void we_gauge_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* list demo（数据驱动列表：快扫惯性 + 过冲回弹 + 滚动条空闲渐隐）。 */
void we_list_simple_demo_init(we_lcd_t *lcd);
void we_list_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* roller demo（滚轮选值器：惯性甩动滑过多行减速吸附 + 点击直达）。 */
void we_roller_simple_demo_init(we_lcd_t *lcd);
void we_roller_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* marquee demo（跑马灯标签：长文本无缝循环滚动 + 不可见字形快进跳过）。 */
void we_marquee_simple_demo_init(we_lcd_t *lcd);
void we_marquee_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* toast demo（轻提示横幅：非模态滑入自动消失 + union 标脏 + 超宽省略号）。 */
void we_toast_simple_demo_init(we_lcd_t *lcd);
void we_toast_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* focus demo（全局聚焦 + 按键导航：方向/Tab 移焦，OK 进入容器/触发控件，BACK 退出）。
 * WE_CFG_ENABLE_KEY_INPUT == 0 时降级为提示桩。 */
void we_focus_simple_demo_init(we_lcd_t *lcd);
void we_focus_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* focus2 demo（聚焦编辑态：OK 进编辑光标变橙，方向键调 slider/stepper/roller/list）。
 * WE_CFG_ENABLE_KEY_INPUT == 0 时降级为提示桩。 */
void we_focus2_simple_demo_init(we_lcd_t *lcd);
void we_focus2_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* 汇总 demo（仅模拟器：4 页 slideshow 串联主要控件与子系统）。 */
void we_showcase_simple_demo_init(we_lcd_t *lcd);
void we_showcase_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

#endif
