#ifndef __WE_WIDGET_MSGBOX_H
#define __WE_WIDGET_MSGBOX_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef WE_MSGBOX_ANIM_DURATION_MS
#define WE_MSGBOX_ANIM_DURATION_MS 220U
#endif

#ifndef WE_MSGBOX_USE_ANIM
#define WE_MSGBOX_USE_ANIM 1
#endif

#ifndef WE_MSGBOX_EDGE_MARGIN
#define WE_MSGBOX_EDGE_MARGIN 12
#endif

#ifndef WE_MSGBOX_RADIUS
#define WE_MSGBOX_RADIUS 14
#endif

#ifndef WE_MSGBOX_BTN_RADIUS
#define WE_MSGBOX_BTN_RADIUS 10
#endif

#define WE_POPUP_ANIM_DURATION_MS WE_MSGBOX_ANIM_DURATION_MS
#define WE_POPUP_EDGE_MARGIN WE_MSGBOX_EDGE_MARGIN
#define WE_POPUP_RADIUS WE_MSGBOX_RADIUS
#define WE_POPUP_BTN_RADIUS WE_MSGBOX_BTN_RADIUS

typedef enum
{
    WE_POPUP_LAYOUT_CONFIRM = 1,
    WE_POPUP_LAYOUT_CONFIRM_CANCEL = 2
} we_popup_layout_t;

struct we_popup_obj_t;
typedef void (*we_popup_action_cb_t)(struct we_popup_obj_t *obj);

typedef struct we_popup_obj_t
{
    we_obj_t base;
    const char *title;
    const char *message;
    const char *confirm_text;
    const char *cancel_text;
    const unsigned char *title_font;
    const unsigned char *message_font;
    const unsigned char *button_font;
    we_popup_action_cb_t confirm_cb;
    we_popup_action_cb_t cancel_cb;
    int16_t panel_y;
    int16_t anim_from_y;
    int16_t anim_to_y;
    int16_t target_y;
    int16_t hidden_y;
    uint16_t panel_min_w;
    uint16_t panel_w;
    uint16_t panel_h;
    uint16_t anim_elapsed_ms;
    uint16_t anim_duration_ms;
    int8_t task_id;
    uint8_t layout;
    uint8_t pressed_btn; /* 0=none, 1=confirm, 2=cancel */
    uint8_t armed_btn;   /* 记录本次按下命中的按钮，供 CLICKED 阶段确认 */
    uint8_t visible : 1;
    uint8_t animating : 1;
    uint8_t anim_hiding : 1;
} we_popup_obj_t;

typedef we_popup_obj_t we_msgbox_obj_t;
typedef we_popup_action_cb_t we_msgbox_action_cb_t;

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param target_y 弹窗目标 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @return 无。。
 */
void we_msgbox_ok_obj_init(we_msgbox_obj_t *obj, we_lcd_t *lcd,
                           uint16_t w, uint16_t h, int16_t target_y,
                           const char *title, const char *message,
                           const char *ok_text,
                           const unsigned char *title_font,
                           const unsigned char *message_font,
                           const unsigned char *button_font,
                           we_msgbox_action_cb_t ok_cb);

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param target_y 弹窗目标 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param cancel_text 取消按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @param cancel_cb 取消按钮回调函数。
 * @return 无。。
 */
void we_msgbox_ok_cancel_obj_init(we_msgbox_obj_t *obj, we_lcd_t *lcd,
                                   uint16_t w, uint16_t h, int16_t target_y,
                                   const char *title, const char *message,
                                   const char *ok_text, const char *cancel_text,
                                   const unsigned char *title_font,
                                   const unsigned char *message_font,
                                   const unsigned char *button_font,
                                   we_msgbox_action_cb_t ok_cb,
                                   we_msgbox_action_cb_t cancel_cb);

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @return 无。。
 */
void we_popup_set_text(we_popup_obj_t *obj, const char *title, const char *message);

/**
 * @brief 设置弹窗按钮文案（单按钮布局时仅使用 confirm_text）。
 * @param obj 弹窗对象指针。
 * @param confirm_text 确认按钮文本。
 * @param cancel_text 取消按钮文本。
 */
void we_popup_set_buttons(we_popup_obj_t *obj, const char *confirm_text, const char *cancel_text);

/**
 * @brief 设置弹窗停靠的目标 Y 坐标。
 * @param obj 弹窗对象指针。
 * @param target_y 目标显示 Y 坐标。
 */
void we_popup_set_target_y(we_popup_obj_t *obj, int16_t target_y);

/**
 * @brief 显示弹窗（触发滑入动画或直接显示）。
 * @param obj 弹窗对象指针。
 */
void we_popup_show(we_popup_obj_t *obj);

/**
 * @brief 隐藏弹窗（触发收起动画或直接隐藏）。
 * @param obj 弹窗对象指针。
 */
void we_popup_hide(we_popup_obj_t *obj);

/**
 * @brief 显示消息框（兼容旧接口，等价于 we_popup_show）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_msgbox_show(we_msgbox_obj_t *obj) { we_popup_show(obj); }
/**
 * @brief 隐藏消息框（兼容旧接口，等价于 we_popup_hide）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_msgbox_hide(we_msgbox_obj_t *obj) {     we_popup_hide(obj); }

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_popup_obj_delete(we_popup_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_popup_hide(obj);
    we_obj_delete((we_obj_t *)obj);
}

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_msgbox_obj_delete(we_msgbox_obj_t *obj)
{
    we_popup_obj_delete(obj);
}

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_MSGBOX_H */
