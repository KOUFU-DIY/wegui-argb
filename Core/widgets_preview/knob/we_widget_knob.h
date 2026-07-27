#ifndef __WE_WIDGET_KNOB_H
#define __WE_WIDGET_KNOB_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 弧形旋钮滑块控件（knob）—— preview 孵化区实验控件
 *
 * 内切于 size×size 正方形的可拖拽圆弧旋钮，组成：
 *   - track 底弧：暗色全跨度弧带（默认 135° 起顺时针扫 270°，开口朝下）；
 *   - value 弧：亮色弧带，从起点扫到当前值对应角度；
 *   - 拖拽端点：value 弧末端的小圆手柄（按压时增亮）。
 *
 * 渲染：弧带走距离平方场逐像素扫描（we_widget_arc.c 的简化版——
 * 无端帽圆角，弧带端面为平头），端点小圆用
 * we_draw_round_rect_analytic_fill 退化实心抗锯齿圆。
 * 全程整数运算（Q15 三角 / 平方距离场），零 malloc、内环零浮点。
 *
 * 交互：event_cb 消费 PRESSED/STAY/CLICKED —— 触点相对中心的 (dx,dy)
 * 经内置八分区近似整数 atan2（输出 512 步制）转为角度，钳制在弧跨度内
 * 后线性映射到 [v_min, v_max]；值变化时触发 changed_cb 并整体标脏。
 * 中心小半径死区内的触点忽略，防止过圆心时角度跳变。
 *
 * 角度统一 512 步制（0..511 = 一圈，90° = 128，用 WE_DEG() 换算）。
 * 所有 setter 值未变时直接返回不重绘。
 * 本控件无动画节点（值由用户拖动直接驱动），删除无需摘链。
 *
 * preview 限制（毕业前需优化项见 widget.md）：
 *   - 值变化按整控件包围盒标脏；
 *   - 近似 atan2 最大误差约 0.3°（毕业前可换查表反正切细化）；
 *   - 量程跨度 |v_max - v_min| 需小于 2^22（int32 映射防溢出）。
 * -------------------------------------------------------------------------- */

/* 默认起始角 / 扫角（512 步制，包含本头文件前可用宏覆盖） */
#ifndef WE_KNOB_DEF_START
#define WE_KNOB_DEF_START WE_DEG(135)
#endif
#ifndef WE_KNOB_DEF_SWEEP
#define WE_KNOB_DEF_SWEEP WE_DEG(270)
#endif

/* 默认颜色：track 暗灰蓝 / value 亮青蓝 / 端点近白 */
#ifndef WE_KNOB_TRACK_R
#define WE_KNOB_TRACK_R 52
#endif
#ifndef WE_KNOB_TRACK_G
#define WE_KNOB_TRACK_G 60
#endif
#ifndef WE_KNOB_TRACK_B
#define WE_KNOB_TRACK_B 76
#endif
#ifndef WE_KNOB_VALUE_R
#define WE_KNOB_VALUE_R 86
#endif
#ifndef WE_KNOB_VALUE_G
#define WE_KNOB_VALUE_G 170
#endif
#ifndef WE_KNOB_VALUE_B
#define WE_KNOB_VALUE_B 255
#endif
#ifndef WE_KNOB_DOT_R8
#define WE_KNOB_DOT_R8 240
#endif
#ifndef WE_KNOB_DOT_G8
#define WE_KNOB_DOT_G8 244
#endif
#ifndef WE_KNOB_DOT_B8
#define WE_KNOB_DOT_B8 250
#endif

/* 数值变化回调（仅用户拖动/点击改变值时触发；obj 为 we_knob_obj_t*） */
typedef void (*we_knob_changed_cb_t)(void *obj, int32_t value);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_knob_obj_t
{
    we_obj_t base;          /* 基类，必须在首位：base.w = base.h = size */

    int16_t  start_angle;   /* 起始角（512 步制，对应量程下限） */
    int16_t  sweep;         /* 扫过角（512 步制，正值顺时针，对应量程上限） */
    uint16_t radius;        /* 弧带外半径（像素） */
    uint8_t  thickness;     /* 弧带厚度（像素） */
    uint8_t  dot_r;         /* 拖拽端点小圆半径（像素） */

    int32_t  v_min;         /* 量程下限 */
    int32_t  v_max;         /* 量程上限（> v_min） */
    int32_t  value;         /* 当前值（钳制在量程内） */

    colour_t track_color;   /* track 底弧颜色（暗色） */
    colour_t value_color;   /* value 弧颜色（亮色） */
    colour_t dot_color;     /* 拖拽端点颜色 */
    uint8_t  opacity;       /* 整体不透明度（0~255） */
    uint8_t  pressed;       /* 按压状态（端点增亮反馈） */

    we_knob_changed_cb_t changed_cb; /* 可为 NULL；为空时调用方自行轮询 get_value */
} we_knob_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化弧形旋钮并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 外接正方形左上角 X（屏幕绝对坐标）。
 * @param y 外接正方形左上角 Y。
 * @param size 控件边长（像素，包围盒 = size × size，建议 >= 60）。
 * @return 无。
 * @note 默认：量程 0..100、初值 0、start = WE_DEG(135)、sweep = WE_DEG(270)、
 *       弧厚 ≈ size/8、暗灰蓝 track / 亮青蓝 value / 近白端点、不透明。
 */
void we_knob_obj_init(we_knob_obj_t *obj, we_lcd_t *lcd,
                      int16_t x, int16_t y, uint16_t size);

/**
 * @brief 设置量程 [v_min, v_max]。
 * @param obj 控件对象指针。
 * @param v_min 量程下限。
 * @param v_max 量程上限（必须大于 v_min，否则忽略本次调用）。
 * @return 无。
 * @note 当前值会重新钳制到新量程（程序侧调整，不触发 changed_cb）；
 *       跨度 |v_max - v_min| 应小于 2^22。
 */
void we_knob_set_range(we_knob_obj_t *obj, int32_t v_min, int32_t v_max);

/**
 * @brief 程序设置当前值并按需重绘（不触发 changed_cb）。
 * @param obj 控件对象指针。
 * @param value 新值，自动钳制到量程内。
 * @return 无。
 */
void we_knob_set_value(we_knob_obj_t *obj, int32_t value);

/**
 * @brief 读取当前值。
 * @param obj 控件对象指针。
 * @return 当前值；obj 为 NULL 时返回 0。
 */
int32_t we_knob_get_value(const we_knob_obj_t *obj);

/**
 * @brief 注册数值变化回调（仅用户拖动/点击改变值时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_knob_set_changed_cb(we_knob_obj_t *obj, we_knob_changed_cb_t cb);

/**
 * @brief 设置 track / value 弧与端点小圆的颜色。
 * @param obj 控件对象指针。
 * @param track_color track 底弧颜色。
 * @param value_color value 弧颜色。
 * @param dot_color 拖拽端点颜色。
 * @return 无。
 */
void we_knob_set_colors(we_knob_obj_t *obj, colour_t track_color,
                        colour_t value_color, colour_t dot_color);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_knob_set_opacity(we_knob_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件并从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 * @note knob 无动画节点（值由用户拖动直接驱动），无需 we_anim_stop。
 */
void we_knob_obj_delete(we_knob_obj_t *obj);

#endif /* __WE_WIDGET_KNOB_H */
