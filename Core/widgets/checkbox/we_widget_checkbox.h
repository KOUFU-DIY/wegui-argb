#ifndef __WE_WIDGET_CHECKBOX_H
#define __WE_WIDGET_CHECKBOX_H

#include "we_gui_driver.h"

/* 4 种视觉状态 */
typedef enum
{
    WE_CB_STATE_OFF = 0,     /* 未选中 + 松开 */
    WE_CB_STATE_OFF_PRESSED, /* 未选中 + 按下 */
    WE_CB_STATE_ON,          /* 已选中 + 松开 */
    WE_CB_STATE_ON_PRESSED,  /* 已选中 + 按下 */
    WE_CB_STATE_MAX
} we_cb_state_t;

/* 每种状态的样式 */
typedef struct
{
    colour_t box_color;   /* 方框边框/填充色 */
    colour_t check_color; /* 勾选标记色 */
    colour_t text_color;  /* 右侧文本色 */
    uint8_t border_w;     /* 边框厚度 (0=实心填充) */
} we_cb_style_t;

typedef uint8_t (*we_checkbox_event_cb_t)(void *obj, we_event_t event, we_indev_data_t *data);

typedef struct
{
    we_obj_t base;
    const char *text; /* 右侧文本 (可为 NULL) */
    const unsigned char *font;
    we_checkbox_event_cb_t user_event_cb;
    const we_cb_style_t *styles;           /* 指向样式表 (默认指向内置 Flash 表) */
    uint16_t box_size; /* 方框边长 */
    uint16_t radius;   /* 方框圆角半径 */
    uint8_t opacity;
    uint8_t checked : 1; /* 0=未选中, 1=选中 */
    uint8_t pressed : 1; /* 当前是否被按下 */
} we_checkbox_obj_t;

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param box_size 勾选框边长（像素）。
 * @param text UTF-8 文本字符串。
 * @param font 字体资源指针。
 * @param event_cb 回调函数指针。
 * @return 无。。
 */
void we_checkbox_obj_init(we_checkbox_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, uint16_t box_size,
                          const char *text, const unsigned char *font, we_checkbox_event_cb_t event_cb);

/**
 * @brief 设置勾选状态并刷新方框区域。
 * @param obj 目标控件对象指针。
 * @param checked 目标勾选状态（0=未选中，1=选中）。
 * @return 无。。
 */
void we_checkbox_set_checked(we_checkbox_obj_t *obj, uint8_t checked);

/**
 * @brief 切换勾选状态并刷新方框区域。
 * @param obj 目标控件对象指针。
 * @return 无。。
 */
void we_checkbox_toggle(we_checkbox_obj_t *obj);

/**
 * @brief 查询当前是否处于勾选状态。
 * @param obj 目标控件对象指针。
 * @return 1 表示已勾选，0 表示未勾选。
 */
uint8_t we_checkbox_is_checked(const we_checkbox_obj_t *obj);

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param text UTF-8 文本字符串。
 * @return 无。。
 */
void we_checkbox_set_text(we_checkbox_obj_t *obj, const char *text);

/**
 * @brief 设置控件样式参数并刷新显示。
 * @param obj 目标控件对象指针。
 * @param styles 样式表指针（NULL 时恢复内置默认样式）。
 * @return 无。。
 */
void we_checkbox_set_styles(we_checkbox_obj_t *obj, const we_cb_style_t *styles);

/**
 * @brief 删除勾选框对象并从对象链表移除。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_checkbox_obj_delete(we_checkbox_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

#endif
