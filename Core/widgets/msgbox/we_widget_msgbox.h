#ifndef __WE_WIDGET_MSGBOX_H
#define __WE_WIDGET_MSGBOX_H

#include "wegui.h"

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

/* 本控件按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 弹窗显示期间是模态，按键经模态键通道直送：方向键在按钮间移动键光标
 * （首键只落位到确认按钮），OK 按下沿按压、松开沿触发，BACK 走取消
 * （单按钮布局走确认）。置 0 单独裁剪按键操作，模态照常吞键。 */
#ifndef WE_MSGBOX_USE_KEY
#define WE_MSGBOX_USE_KEY 1
#endif

/* 小屏（高度 <=64）默认收紧边距与圆角，可在用户配置覆盖 */
#ifndef WE_MSGBOX_EDGE_MARGIN
#if (SCREEN_HEIGHT <= 64)
#define WE_MSGBOX_EDGE_MARGIN 4
#else
#define WE_MSGBOX_EDGE_MARGIN 12
#endif
#endif

#ifndef WE_MSGBOX_RADIUS
#if (SCREEN_HEIGHT <= 64)
#define WE_MSGBOX_RADIUS 6
#else
#define WE_MSGBOX_RADIUS 14
#endif
#endif

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
    int16_t panel_y; /* 面板 Y（彩色淡入淡出时即停靠位；单色平移动画时为当前位置） */
#if (WE_CFG_MONO_STYLE == 1)
    int16_t slide_target_y; /* 单色：停靠 Y（平移动画的终点，退场结束回到这里） */
#endif
    int16_t anim_from; /* 动画起点：彩色为起始不透明度，单色为起始 Y */
    uint16_t panel_min_w;
    uint16_t panel_w;
    uint16_t panel_h;
    uint16_t anim_elapsed_ms;
    uint16_t anim_duration_ms;
    uint16_t btn_w[2];     /* 按钮宽（[0]=确认 [1]=取消；文字宽 + 留白，空文字取最小宽） */
    uint16_t btn_txt_w[2]; /* 按钮文字宽（居中用） */
    uint16_t title_h;      /* 标题折行后的块高 */
    int8_t btn_ymid[2];    /* 按钮文字墨迹竖直中线（相对行顶） */
    int8_t title_top;      /* 标题墨迹上沿（相对行顶） */
    int8_t msg_top;        /* 正文墨迹上沿（相对行顶） */
    uint8_t fade_opa; /* 淡入淡出当前不透明度 0~255 */
    we_anim_t anim; /* 中央动画引擎节点（淡入/淡出动画） */
    uint8_t layout;
    uint8_t pressed_btn; /* 0=none, 1=confirm, 2=cancel */
    uint8_t armed_btn;   /* 记录本次按下命中的按钮，供 CLICKED 阶段确认 */
    uint8_t visible : 1;
    uint8_t animating : 1;
    uint8_t anim_hiding : 1;
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_MSGBOX_USE_KEY == 1)
    uint8_t key_btn : 2;   /* 键光标所在按钮（取值同 pressed_btn，0=未落位） */
    uint8_t key_armed : 1; /* OK 按下沿已按住键光标按钮，等松开沿触发 */
#endif
} we_popup_obj_t;

typedef we_popup_obj_t we_msgbox_obj_t;
typedef we_popup_action_cb_t we_msgbox_action_cb_t;

/**
 * @brief 初始化单确定按钮消息框并挂载到 LCD 对象链表（初始隐藏）。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 面板最小宽度（像素，面板宽随内容增长）。
 * @param h 面板初始高度（像素，布局随内容重算）。
 * @param target_y 弹窗停靠 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @return 无。
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
 * @brief 初始化确定+取消双按钮消息框并挂载到 LCD 对象链表（初始隐藏）。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 面板最小宽度（像素，面板宽随内容增长）。
 * @param h 面板初始高度（像素，布局随内容重算）。
 * @param target_y 弹窗停靠 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param cancel_text 取消按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @param cancel_cb 取消按钮回调函数。
 * @return 无。
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
 * @brief 设置标题与正文并重排（可见时重绘旧/新面板区）。
 * @param obj 目标控件对象指针。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @return 无。
 */
void we_popup_set_text(we_popup_obj_t *obj, const char *title, const char *message);

/**
 * @brief 设置弹窗按钮文案并重排（按钮宽影响面板宽；单按钮布局时仅使用 confirm_text）。
 * @param obj 弹窗对象指针。
 * @param confirm_text 确认按钮文本。
 * @param cancel_text 取消按钮文本。
 */
void we_popup_set_buttons(we_popup_obj_t *obj, const char *confirm_text, const char *cancel_text);

/**
 * @brief 设置弹窗停靠的目标 Y 坐标（夹紧到屏内；可见时立即移动并重绘旧/新区）。
 * @param obj 弹窗对象指针。
 * @param target_y 目标显示 Y 坐标。
 */
void we_popup_set_target_y(we_popup_obj_t *obj, int16_t target_y);

/**
 * @brief 显示弹窗（触发淡入动画或直接显示）。
 * @param obj 弹窗对象指针。
 * @note 挂 LCD 顶层链并声明模态：显示期间背后控件收不到触摸与按键。
 *       已有其他模态弹层（dropdown 展开列表、软键盘等）时会先把它收起。
 */
void we_popup_show(we_popup_obj_t *obj);

/**
 * @brief 隐藏弹窗（触发淡出动画或直接隐藏）。
 * @param obj 弹窗对象指针。
 * @note 淡出期间仍是模态并吞掉输入（不再响应按钮），淡出完成后解除模态。
 */
void we_popup_hide(we_popup_obj_t *obj);

/**
 * @brief 删除弹窗（显示中删除同样安全：内核回收模态槽与动画节点并擦除面板区）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_popup_obj_delete(we_popup_obj_t *obj)
{
    if (obj != NULL)
        we_obj_delete((we_obj_t *)obj);
}

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_MSGBOX_H */
