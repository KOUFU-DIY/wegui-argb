#ifndef PREVIEW_DEMOS_H
#define PREVIEW_DEMOS_H

#include "we_gui_driver.h"

/* demo 层统一字体：与 simple_widget_demos.h 同款定义（资源头只在
 * demo 层包含，Core 零资源感知，控件字体一律经 init 显式传入）。 */
#include "demo_ascii_16.h"
#ifndef we_font_consolas_18
#define we_font_consolas_18 ((const unsigned char *)&demo_ascii_16)
#endif

/* ============================ preview 孵化区 ============================
 * 本文件声明 preview（实验）控件的 demo 入口：
 *   1. 控件实现位于 Core/widgets_preview/<name>/we_widget_<name>.c/.h；
 *   2. demo 位于 Demo/preview/demo_<name>.c；
 *   3. DEMO_ID 使用独立编号段（100 起），与稳定区 1..28 隔离；
 *   4. 三目标均编译（模拟器 CMake glob 自动收编；两个 Keil 工程带
 *      we_widget_preview/demo_preview 文件组，未选中的由链接器剔除）；
 *   5. 未细致优化、随时可能下架；毕业后迁入 Core/widgets 与稳定编号段。
 *
 * 编号一览（2026-07 重排：常用在前、其余靠后；textarea 因弹层键盘
 * 链路提前到 102；今后毕业迁出的编号继续保留空洞不复用）：
 *   101 keyboard(软键盘)      102 textarea(输入框+弹层键盘)
 *   103 menu(多级菜单)        104 spectrum(频谱柱)
 *   105 logview(滚动日志)     106 calendar(日历)
 *   107 table(表格)           108 qrcode(二维码)
 *   109 chart_bar(柱状图)     110 scale(刻度尺)
 *   111 joystick(虚拟摇杆)    112 statusbar(状态栏)
 *   113 ime_pinyin(拼音输入法) 114 spinner(加载指示)
 *   115 btnmatrix(按键矩阵)   116 radio(单选组)
 *   117 sevenseg(数码管)      118 knob(旋钮)
 *   119 colorwheel(色轮)      120 imgbtn(图片按钮)
 *   121 animimg(帧动画)       122 canvas(自绘画布)
 *   123 tabview(页签容器)     124 hold_btn(长按确认)
 *   125 mask_group(蒙版容器)  126 mlabel(多行文本)
 * ======================================================================== */

/* spinner 加载指示（旋转动画，等待状态）。 */
void we_spinner_preview_demo_init(we_lcd_t *lcd);
void we_spinner_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* btnmatrix 按键矩阵（字符串数组渲染整面按键）。 */
void we_btnmatrix_preview_demo_init(we_lcd_t *lcd);
void we_btnmatrix_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* radio 单选组（组内互斥选择）。 */
void we_radio_preview_demo_init(we_lcd_t *lcd);
void we_radio_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* sevenseg 七段数码管（大数字/时钟显示，不吃字库）。 */
void we_sevenseg_preview_demo_init(we_lcd_t *lcd);
void we_sevenseg_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* knob 旋钮/弧形滑块（拖拽改值的交互圆弧）。 */
void we_knob_preview_demo_init(we_lcd_t *lcd);
void we_knob_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* colorwheel HSV 色轮（环形取色 + 选色回调）。 */
void we_colorwheel_preview_demo_init(we_lcd_t *lcd);
void we_colorwheel_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* imgbtn 图片按钮（按状态切图/按压变暗 + 点击回调）。 */
void we_imgbtn_preview_demo_init(we_lcd_t *lcd);
void we_imgbtn_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* animimg 帧动画（图片序列循环播放）。 */
void we_animimg_preview_demo_init(we_lcd_t *lcd);
void we_animimg_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* canvas 自绘壳（draw_cb 转发给用户回调）。 */
void we_canvas_preview_demo_init(we_lcd_t *lcd);
void we_canvas_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* tabview 页签容器（tab 行 + group 页显隐切换）。 */
void we_tabview_preview_demo_init(we_lcd_t *lcd);
void we_tabview_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* keyboard 软键盘（多页布局 + shift 状态 + 键值回调）。 */
void we_keyboard_preview_demo_init(we_lcd_t *lcd);
void we_keyboard_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* textarea 单行输入框（调用方缓冲 + 光标闪烁 + 溢出滚动）。 */
void we_textarea_preview_demo_init(we_lcd_t *lcd);
void we_textarea_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* menu 多级菜单（数据驱动树 + 页面栈返回导航）。 */
void we_menu_preview_demo_init(we_lcd_t *lcd);
void we_menu_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* spectrum 频谱柱（N 柱电平 + 峰值保持缓落）。 */
void we_spectrum_preview_demo_init(we_lcd_t *lcd);
void we_spectrum_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* hold_btn 长按确认按钮（按住充环触发，松手回退）。 */
void we_hold_btn_preview_demo_init(we_lcd_t *lcd);
void we_hold_btn_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* logview 滚动日志窗（行环形缓冲 + 自动滚底 + 拖拽暂停跟随）。 */
void we_logview_preview_demo_init(we_lcd_t *lcd);
void we_logview_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* mask_group 蒙版容器（圆角内容裁剪 + 旋转线性渐变淡出）。 */
void we_mask_group_preview_demo_init(we_lcd_t *lcd);
void we_mask_group_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* calendar 日历（月视图网格 + 选日回调 + 月份切换）。 */
void we_calendar_preview_demo_init(we_lcd_t *lcd);
void we_calendar_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* table 简易表格（表头固定 + 数据行滚动 + 网格线）。 */
void we_table_preview_demo_init(we_lcd_t *lcd);
void we_table_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* qrcode 二维码（内置 byte-mode 编码器 + 模块块渲染）。 */
void we_qrcode_preview_demo_init(we_lcd_t *lcd);
void we_qrcode_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* chart_bar 柱状图（环形缓冲推值 + 定宽柱滚动）。 */
void we_chart_bar_preview_demo_init(we_lcd_t *lcd);
void we_chart_bar_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* scale 刻度尺（横/纵刻度 + 主刻度数字 + 指针）。 */
void we_scale_preview_demo_init(we_lcd_t *lcd);
void we_scale_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* mlabel 多行文本（自动折行 + 断词 + 省略号截断）。 */
void we_mlabel_preview_demo_init(we_lcd_t *lcd);
void we_mlabel_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* joystick 虚拟摇杆（拖拽矢量输出 + 松手弹性回中）。 */
void we_joystick_preview_demo_init(we_lcd_t *lcd);
void we_joystick_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* statusbar 状态栏（时间文本 + 电池/WiFi/信号矢量图标）。 */
void we_statusbar_preview_demo_init(we_lcd_t *lcd);
void we_statusbar_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

/* ime_pinyin 拼音输入法面板（音节索引引擎 + 候选栏 + 键盘/输入框联动）。 */
void we_ime_pinyin_preview_demo_init(we_lcd_t *lcd);
void we_ime_pinyin_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);

#endif
