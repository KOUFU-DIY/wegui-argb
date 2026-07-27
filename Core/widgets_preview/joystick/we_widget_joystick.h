#ifndef __WE_WIDGET_JOYSTICK_H
#define __WE_WIDGET_JOYSTICK_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 虚拟摇杆控件（joystick）—— preview 孵化区实验控件
 *
 * 内切于 size×size 正方形的圆形虚拟摇杆，组成：
 *   - 底盘大圆：暗色圆形托盘（半透明观感靠暗色实现，全不透明绘制）；
 *   - 十字准线：过中心的水平/垂直两条 1px 细线；
 *   - 摇杆头：直径 ≈ size/2.5 的实心亮色圆，跟随触点偏移，按压时增亮。
 *
 * 输出为二维矢量 (dx, dy)，每轴 -127..127，中心为 0，屏幕坐标系
 * （右为 +X，下为 +Y）。偏移限幅在 travel = (size - 头径)/2 半径圆内：
 * 越界判定用平方比较（无开方），越界时用整数 sqrt 求模长后按比例缩回
 * 圆周。死区（默认 8%，按 travel 百分比）内矢量输出 0。
 *
 * 交互：PRESSED 触点落在底盘圆内才接管（圆外返回 0，语义上穿透）；
 * STAY 拖动摇杆头实时跟随（触点可拖出控件包围盒，方向仍正确）；
 * RELEASED 摇杆头经中央动画节点弹性回中（we_ease_out_back 轻微过冲，
 * 默认 220ms），回中过程持续输出衰减矢量并触发 changed_cb。
 *
 * 渲染全部走既有原语（圆 = we_draw_round_rect_analytic_fill 退化用法，
 * 线 = we_draw_line_round），控件自身零浮点、零 malloc。
 *
 * preview 限制（毕业前需优化项见 widget.md）：
 *   - 任何变化按整控件包围盒标脏；
 *   - 死区边界处矢量有小台阶（死区外未重映射平滑起坡）；
 *   - 圆外 PRESSED 返回 0 仅为语义声明，driver 当前不消费 PRESSED
 *     返回值，触点仍会被本控件锁定，不会真正穿透到下层控件。
 * -------------------------------------------------------------------------- */

/* 松手弹性回中动画时长（毫秒，包含本头文件前可用宏覆盖） */
#ifndef WE_JOYSTICK_RETURN_MS
#define WE_JOYSTICK_RETURN_MS 220U
#endif

/* 默认死区百分比（相对最大行程 travel） */
#ifndef WE_JOYSTICK_DEF_DEADZONE
#define WE_JOYSTICK_DEF_DEADZONE 8U
#endif

/**
 * @brief 矢量变化回调。
 * @param js 触发回调的摇杆对象指针（we_joystick_obj_t *，以 void * 透传）。
 * @param dx 当前 X 轴矢量（-127..127，右为正）。
 * @param dy 当前 Y 轴矢量（-127..127，下为正）。
 * @return 无。
 * @note 用户拖动与松手回中过程中矢量每次变化都会触发；
 *       程序侧 setter（如 set_deadzone）不触发。
 */
typedef void (*we_joystick_changed_cb_t)(void *js, int8_t dx, int8_t dy);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_joystick_obj_t
{
    we_obj_t base;          /* 基类，必须在首位：base.w = base.h = size */

    uint16_t travel;        /* 摇杆头最大行程半径 = (size - 头径)/2（像素） */
    uint16_t knob_r;        /* 摇杆头半径（像素，≈ size/5） */
    uint8_t  deadzone_pct;  /* 死区百分比（相对 travel，0~90，默认 8） */

    int16_t  ofs_x;         /* 摇杆头当前偏移 X（像素，相对底盘中心） */
    int16_t  ofs_y;         /* 摇杆头当前偏移 Y（像素） */
    int8_t   vec_x;         /* 当前输出矢量 X（-127..127，死区内为 0） */
    int8_t   vec_y;         /* 当前输出矢量 Y（-127..127） */

    colour_t base_color;    /* 底盘大圆颜色（暗色） */
    colour_t knob_color;    /* 摇杆头颜色（亮色，按压时向白增亮） */
    colour_t cross_color;   /* 十字准线颜色 */

    we_anim_t anim;         /* 中央动画节点（松手弹性回中专用） */
    int16_t  ret_from_x;    /* 回中动画起点偏移 X（内部） */
    int16_t  ret_from_y;    /* 回中动画起点偏移 Y（内部） */
    uint16_t ret_acc;       /* 回中动画累计时间（毫秒，内部） */

    we_joystick_changed_cb_t changed_cb; /* 矢量变化回调（可为 NULL） */
    uint8_t  opacity;       /* 整体不透明度（0~255，默认 255） */
    uint8_t  pressed;       /* 按压状态（摇杆头增亮反馈，内部） */
} we_joystick_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化虚拟摇杆并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 外接正方形左上角 X（屏幕绝对坐标）。
 * @param y 外接正方形左上角 Y。
 * @param size 底盘圆直径（像素，包围盒 = size × size，最小 40）。
 * @return 无。
 * @note 摇杆头直径 ≈ size/2.5，行程半径 = (size - 头径)/2；
 *       默认死区 8%、暗蓝灰底盘 / 亮青蓝摇杆头 / 灰蓝准线、不透明。
 */
void we_joystick_obj_init(we_joystick_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t size);

/**
 * @brief 读取当前输出矢量。
 * @param obj 控件对象指针。
 * @param dx 传出：X 轴矢量（-127..127），可为 NULL。
 * @param dy 传出：Y 轴矢量（-127..127），可为 NULL。
 * @return 无。
 * @note 中心/死区内为 (0,0)；obj 为 NULL 时输出 (0,0)。
 */
void we_joystick_get_vector(const we_joystick_obj_t *obj, int8_t *dx, int8_t *dy);

/**
 * @brief 注册矢量变化回调（拖动与松手回中过程均触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_joystick_set_changed_cb(we_joystick_obj_t *obj, we_joystick_changed_cb_t cb);

/**
 * @brief 设置底盘 / 摇杆头 / 十字准线颜色。
 * @param obj 控件对象指针。
 * @param base_color 底盘大圆颜色。
 * @param knob_color 摇杆头颜色。
 * @param cross_color 十字准线颜色。
 * @return 无。
 * @note 三色均未变化时直接返回，不触发重绘。
 */
void we_joystick_set_colors(we_joystick_obj_t *obj, colour_t base_color,
                            colour_t knob_color, colour_t cross_color);

/**
 * @brief 设置死区百分比（相对最大行程 travel）。
 * @param obj 控件对象指针。
 * @param pct 死区百分比（0~90，超出自动钳制；默认 8）。
 * @return 无。
 * @note 死区内矢量输出 0；程序侧调整，不触发 changed_cb 也不重绘
 *       （摇杆头位置不变，仅输出矢量口径变化）。
 */
void we_joystick_set_deadzone(we_joystick_obj_t *obj, uint8_t pct);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_joystick_set_opacity(we_joystick_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件：先摘除回中动画节点（we_anim_stop）再摘链。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_joystick_obj_delete(we_joystick_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_JOYSTICK_H */
