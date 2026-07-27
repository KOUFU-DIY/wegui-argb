#ifndef __WE_WIDGET_CANVAS_H
#define __WE_WIDGET_CANVAS_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 用户自绘壳控件（canvas）—— preview 孵化区实验控件
 *
 * 定位：给业务层"直接用绘图原语自绘"的逃生舱。框架负责：
 *   1. draw_cb 把 PFB 窗口收窄到自身矩形（group 同款套路），
 *      用户回调里的任何原语越界都会被窗口自动裁掉；
 *   2. 把控件 opacity 乘入 lcd->opa_scale 级联乘子——用户回调内
 *      所有原语按各自 opacity 正常传参，整体透明度自动叠乘；
 *   3. 事件转发：设置了 user_event_cb 则全部输入交给它，
 *      否则返回 0 穿透给背后控件。
 *
 * 业务层数据变化后调用 we_canvas_invalidate() 请求重绘——
 * 控件本身不感知内容，永远整框标脏。
 *
 * 用户绘制回调内可用的常见原语（均自动被收窄后的窗口裁剪）：
 *   we_fill_rect / we_draw_pixel / we_draw_line / we_draw_line_round /
 *   we_draw_string / we_draw_round_rect_analytic_fill（需 we_render.h）等。
 *
 * 零 malloc、无动画节点。preview 限制见 widget.md。
 * -------------------------------------------------------------------------- */

/**
 * @brief 用户自绘回调类型。
 * @param lcd GUI 屏幕上下文指针（PFB 窗口已收窄到控件矩形）。
 * @param canvas 控件实例指针（可 cast 为 we_canvas_obj_t* 读取 base.x/y/w/h）。
 * @param user_data init 时登记的业务上下文指针，原样传回。
 * @return 无。
 * @note 回调内请使用屏幕绝对坐标（以 base.x/base.y 为原点自行偏移）。
 */
typedef void (*we_canvas_draw_cb_t)(we_lcd_t *lcd, void *canvas, void *user_data);

/**
 * @brief 用户事件回调类型。
 * @param canvas 控件实例指针。
 * @param e 输入事件类型。
 * @param d 输入设备数据指针。
 * @return 1 表示消费该事件，0 表示穿透给背后控件。
 */
typedef uint8_t (*we_canvas_event_cb_t)(void *canvas, we_event_t e, we_indev_data_t *d);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_canvas_obj_t
{
    we_obj_t base;                     /* 必须在首位：base.x/y/w/h 为自绘区矩形 */
    we_canvas_draw_cb_t user_draw_cb;  /* 用户自绘回调（NULL 则什么都不画） */
    void *user_data;                   /* 业务上下文，原样透传给 user_draw_cb */
    we_canvas_event_cb_t user_event_cb;/* 非 NULL 时接管全部输入事件 */
    uint8_t opacity;                   /* 整体不透明度（0~255，经 opa_scale 级联） */
} we_canvas_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化自绘壳控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 自绘区左上角 X（屏幕绝对坐标）。
 * @param y 自绘区左上角 Y。
 * @param w 自绘区宽度（像素）。
 * @param h 自绘区高度（像素）。
 * @param user_draw_cb 用户自绘回调（可为 NULL，之后不重绘任何内容）。
 * @param user_data 业务上下文指针（原样透传，可为 NULL）。
 * @return 无。
 * @note 默认不透明、无事件回调（输入穿透）。
 */
void we_canvas_obj_init(we_canvas_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        we_canvas_draw_cb_t user_draw_cb, void *user_data);

/**
 * @brief 设置用户事件回调（非 NULL 时接管全部输入事件，NULL 恢复穿透）。
 * @param obj 控件对象指针。
 * @param cb 用户事件回调。
 * @return 无。
 */
void we_canvas_set_event_cb(we_canvas_obj_t *obj, we_canvas_event_cb_t cb);

/**
 * @brief 请求重绘（透传 we_obj_invalidate，整框标脏）。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 业务层数据变了就调它，下个调度周期用户回调会被重新执行。
 */
void we_canvas_invalidate(we_canvas_obj_t *obj);

/**
 * @brief 设置整体不透明度并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255；0 时跳过用户回调）。
 * @return 无。
 */
void we_canvas_set_opacity(we_canvas_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件并从对象链表移除（无动画节点，直接转调 we_obj_delete）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_canvas_obj_delete(we_canvas_obj_t *obj);

#endif /* __WE_WIDGET_CANVAS_H */
