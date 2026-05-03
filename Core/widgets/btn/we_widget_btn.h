#ifndef __WE_WIDGET_BTN_H
#define __WE_WIDGET_BTN_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 是否启用按钮自定义样式
 *
 * 0：
 * 每个按钮对象不单独保存样式表，统一使用内置样式。
 * 这样更省 RAM，更适合低成本 MCU。
 *
 * 1：
 * 每个按钮对象都保存一套自己的状态样式表。
 * 这样更灵活，但会增加每个按钮对象的 RAM 占用。
 * -------------------------------------------------------------------------- */
#ifndef WE_BTN_USE_CUSTOM_STYLE
#define WE_BTN_USE_CUSTOM_STYLE 0
#endif

/* --------------------------------------------------------------------------
 * 按钮圆角边框绘制模式
 *
 * 0：
 * 精细模式，使用按钮私有的圆角边框绘制逻辑。
 * 观感更好，圆角和边框衔接更自然，但代码体积更大。
 *
 * 1：
 * 紧凑模式，尽量复用通用圆角矩形绘制能力。
 * 代码更小，但圆角边框衔接会更生硬。
 * -------------------------------------------------------------------------- */
#ifndef WE_BTN_DRAW_MODE
#define WE_BTN_DRAW_MODE 0
#endif

typedef enum
{
    WE_BTN_STATE_NORMAL = 0,
    WE_BTN_STATE_SELECTED,
    WE_BTN_STATE_PRESSED,
    WE_BTN_STATE_DISABLED,
    WE_BTN_STATE_MAX
} we_btn_state_t;

typedef uint8_t (*we_btn_event_cb_t)(void *obj, we_event_t event, we_indev_data_t *data);

typedef struct
{
    colour_t bg_color;
    colour_t border_color;
    colour_t text_color;
    uint8_t border_w;
} we_btn_style_t;

typedef struct
{
    we_obj_t base;
    const char *text;
    const unsigned char *font;
    we_btn_event_cb_t user_event_cb;
    uint8_t state; /* 当前按钮状态，按 we_btn_state_t 枚举值存储 */
#if WE_BTN_USE_CUSTOM_STYLE
    we_btn_style_t styles[WE_BTN_STATE_MAX];
#endif
    uint16_t radius;
    uint8_t opacity;
} we_btn_obj_t;

/**
 * @brief 执行局部绘制流程并写入 PFB。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param radius 圆角半径（像素）。
 * @param style 样式配置结构体指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_btn_draw_skin(we_lcd_t *lcd, int16_t x, int16_t y, uint16_t w, uint16_t h,
                      uint16_t radius, const we_btn_style_t *style, uint8_t opacity);

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param text UTF-8 文本字符串。
 * @param font 字体资源指针。
 * @param event_cb 回调函数指针。
 * @return 无。
 */
void we_btn_obj_init(we_btn_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, int16_t w, int16_t h, const char *text,
                     const unsigned char *font, we_btn_event_cb_t event_cb);

/**
 * @brief 切换按钮状态并刷新显示。
 * @param obj 目标控件对象指针。
 * @param state 目标状态枚举值。
 * @return 无。
 */
void we_btn_set_state(we_btn_obj_t *obj, we_btn_state_t state);

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param text UTF-8 文本字符串。
 * @return 无。
 */
void we_btn_set_text(we_btn_obj_t *obj, const char *text);

/**
 * @brief 设置控件样式参数并刷新显示。
 * @param obj 目标控件对象指针。
 * @param state 目标状态枚举值。
 * @param bg 按钮背景色。
 * @param border 按钮边框色。
 * @param text 按钮文字颜色。
 * @param border_w 边框宽度（像素）。
 * @return 无。
 */
void we_btn_set_style(we_btn_obj_t *obj, we_btn_state_t state, colour_t bg, colour_t border, colour_t text,
                      uint8_t border_w);

/**
 * @brief 设置按钮圆角半径并刷新显示。
 * @param obj 目标控件对象指针。
 * @param radius 圆角半径（像素）。
 * @return 无。
 */
void we_btn_set_radius(we_btn_obj_t *obj, uint16_t radius);

/**
 * @brief 统一设置各状态边框宽度并按需刷新。
 * @param obj 目标控件对象指针。
 * @param border_w 新的边框宽度（像素）。
 * @return 无。
 */
void we_btn_set_border_width(we_btn_obj_t *obj, uint8_t border_w);

#endif
