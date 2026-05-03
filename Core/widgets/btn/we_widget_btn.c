#include "we_widget_btn.h"
#include "we_font_text.h"
#include "we_render.h"

#if !WE_BTN_USE_CUSTOM_STYLE
#if 0
static const we_btn_style_t _built_in_styles[WE_BTN_STATE_MAX] = {
    // Normal: 浅灰
    [WE_BTN_STATE_NORMAL] = {.bg_color = RGB888TODEV(240, 240, 240),
                             .border_color = RGB888TODEV(200, 200, 200),
                             .text_color = RGB888TODEV(50, 50, 50),
                             .border_w = 1}, // RGB(240,240,240), RGB(200,200,200), RGB(50,50,50)
    // Selected: 浅蓝
    [WE_BTN_STATE_SELECTED] = {.bg_color = RGB888TODEV(230, 245, 255),
                               .border_color = RGB888TODEV(0, 120, 215),
                               .text_color = RGB888TODEV(0, 100, 200),
                               .border_w = 1}, // RGB(230,245,255), RGB(0,120,215), RGB(0,100,200)
    // Pressed: 蓝
    [WE_BTN_STATE_PRESSED] = {.bg_color = {.dat16 = 0xD73F},
                              .border_color = RGB888TODEV(0, 90, 180),
                              .text_color = RGB888TODEV(0, 80, 160),
                              .border_w = 1}, // RGB(210,230,255), RGB(0,90,180), RGB(0,80,160)
    // Disabled: 深灰
    [WE_BTN_STATE_DISABLED] = {.bg_color = RGB888TODEV(220, 220, 220),
                               .border_color = RGB888TODEV(180, 180, 180),
                               .text_color = RGB888TODEV(150, 150, 150),
                               .border_w = 1}, // RGB(220,220,220), RGB(180,180,180), RGB(150,150,150)
};
#endif

static const we_btn_style_t _built_in_styles[WE_BTN_STATE_MAX] = {
#if (LCD_DEEP == DEEP_RGB565)
    [WE_BTN_STATE_NORMAL] = {.bg_color = {.dat16 = 0x39AA},
                             .border_color = {.dat16 = 0x39AA},
                             .text_color = {.dat16 = 0xEF7D},
                             .border_w = 0},
    [WE_BTN_STATE_SELECTED] = {.bg_color = {.dat16 = 0x5D9F},
                               .border_color = {.dat16 = 0x5D9F},
                               .text_color = {.dat16 = 0xFFBF},
                               .border_w = 0},
    [WE_BTN_STATE_PRESSED] = {.bg_color = {.dat16 = 0x44DB},
                              .border_color = {.dat16 = 0x44DB},
                              .text_color = {.dat16 = 0xF77E},
                              .border_w = 0},
    [WE_BTN_STATE_DISABLED] = {.bg_color = {.dat16 = 0x4A69},
                               .border_color = {.dat16 = 0x4A69},
                               .text_color = {.dat16 = 0x9CD3},
                               .border_w = 0},
#elif (LCD_DEEP == DEEP_RGB888)
    [WE_BTN_STATE_NORMAL] = {.bg_color = {.rgb = {58, 66, 82}},
                             .border_color = {.rgb = {58, 66, 82}},
                             .text_color = {.rgb = {239, 243, 250}},
                             .border_w = 0},
    [WE_BTN_STATE_SELECTED] = {.bg_color = {.rgb = {92, 181, 255}},
                               .border_color = {.rgb = {92, 181, 255}},
                               .text_color = {.rgb = {255, 255, 255}},
                               .border_w = 0},
    [WE_BTN_STATE_PRESSED] = {.bg_color = {.rgb = {64, 152, 231}},
                              .border_color = {.rgb = {64, 152, 231}},
                              .text_color = {.rgb = {247, 250, 255}},
                              .border_w = 0},
    [WE_BTN_STATE_DISABLED] = {.bg_color = {.rgb = {74, 79, 92}},
                               .border_color = {.rgb = {74, 79, 92}},
                               .text_color = {.rgb = {156, 164, 179}},
                               .border_w = 0},
#endif
};
#endif

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
                      uint16_t radius, const we_btn_style_t *style, uint8_t opacity)
{
    uint16_t draw_r;

    if (lcd == NULL || style == NULL)
        return;

    draw_r = radius;
    if (draw_r > w / 2U)
        draw_r = (uint16_t)(w / 2U);
    if (draw_r > h / 2U)
        draw_r = (uint16_t)(h / 2U);

    /* 当前统一取消按钮边框厚度，直接走公用解析式圆角填充。 */
we_draw_round_rect_analytic_fill(lcd, x, y, w, h, draw_r, style->bg_color, opacity);
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _btn_draw_cb(void *ptr)
{
    we_btn_obj_t *obj = (we_btn_obj_t *)ptr;
    if (obj->opacity == 0)
        return;

    // 1. 获取当前状态下的样式
    const we_btn_style_t *style;
#if WE_BTN_USE_CUSTOM_STYLE
    style = &obj->styles[obj->state];
#else
    style = &_built_in_styles[obj->state];
#endif

    we_lcd_t *lcd = obj->base.lcd;

    // 2. [合并绘制] 使用新版抗锯齿圆角矩形引擎一次性画完背景、边框和抗锯齿
    we_btn_draw_skin(lcd, obj->base.x, obj->base.y, obj->base.w, obj->base.h,
                     obj->radius, style, obj->opacity);

    // 3. 居中绘制文字
    if (obj->text && obj->font)
    {
uint16_t txt_w = we_get_text_width(obj->font, obj->text);
        int8_t y_top, y_bot;
we_get_text_bbox(obj->font, obj->text, &y_top, &y_bot);

        int16_t btn_cx = obj->base.x + (int16_t)(obj->base.w / 2);
        int16_t btn_cy = obj->base.y + (int16_t)(obj->base.h / 2);
        int16_t txt_x  = btn_cx - (int16_t)(txt_w / 2);
        int16_t txt_y  = btn_cy - (y_top + y_bot) / 2;

we_draw_string(lcd, txt_x, txt_y, obj->font, obj->text, style->text_color, obj->opacity);
    }
}

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _btn_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_btn_obj_t *btn = (we_btn_obj_t *)ptr;
    if (btn->user_event_cb != NULL)
    {
        return btn->user_event_cb(ptr, event, data);
    }
    (void)data; // 不使用具体坐标
    switch (event)
    {
    case WE_EVENT_PRESSED:
we_btn_set_state(btn, WE_BTN_STATE_PRESSED);
        break;
    case WE_EVENT_RELEASED:
we_btn_set_state(btn, WE_BTN_STATE_NORMAL);
        break;
    case WE_EVENT_CLICKED:
        // TODO: 调用用户自定义的点击回调
        break;
    default:
        break;
    }
    return 1; // 按钮始终拦截并消费事件
}

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
                     const unsigned char *font, we_btn_event_cb_t event_cb)
{
    if (obj == NULL || lcd == NULL)
        return;

    // 1. 初始化基类
    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;

    static const we_class_t _btn_class = {.draw_cb = _btn_draw_cb, .event_cb = _btn_event_cb};
    obj->base.class_p = &_btn_class;

    // 2. 初始化属性
    obj->text = text;
    obj->font = font;
    obj->user_event_cb = event_cb;
    obj->state = WE_BTN_STATE_NORMAL;
    obj->radius = (uint16_t)(h / 4);
    if (obj->radius < 6U)
        obj->radius = 6U;
    obj->opacity = 255;

#if WE_BTN_USE_CUSTOM_STYLE
    // 3. 设置默认配色方案 (如果使用独立样式)
    we_btn_set_style(obj, WE_BTN_STATE_NORMAL, RGB888TODEV(58, 66, 82), RGB888TODEV(58, 66, 82),
RGB888TODEV(239, 243, 250), 1);
    we_btn_set_style(obj, WE_BTN_STATE_SELECTED, RGB888TODEV(92, 181, 255), RGB888TODEV(92, 181, 255),
RGB888TODEV(255, 255, 255), 1);
    we_btn_set_style(obj, WE_BTN_STATE_PRESSED, RGB888TODEV(64, 152, 231), RGB888TODEV(64, 152, 231),
RGB888TODEV(247, 250, 255), 1);
    we_btn_set_style(obj, WE_BTN_STATE_DISABLED, RGB888TODEV(74, 79, 92), RGB888TODEV(74, 79, 92),
RGB888TODEV(156, 164, 179), 1);
#endif

    // 4. 加入显示链表
    obj->base.next = NULL;
    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = lcd->obj_list_head;
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }

    // 5. 标脏显示（经父节点链裁剪，支持挂入容器后正确初始化）
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 更新按钮属性并触发重绘。
 * @param obj 目标控件对象指针。
 * @param state 目标状态枚举值。
 * @return 无。
 */
void we_btn_set_state(we_btn_obj_t *obj, we_btn_state_t state)
{
    if (obj == NULL || obj->state == state)
        return;
    obj->state = state;
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param text UTF-8 文本字符串。
 * @return 无。
 */
void we_btn_set_text(we_btn_obj_t *obj, const char *text)
{
    if (obj == NULL || obj->text == text)
        return;
    obj->text = text;
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置控件样式参数并刷新显示。
 * @param obj 目标控件对象指针。
 * @param state 目标状态枚举值。
 * @param bg 按钮背景色。
 * @param border 按钮边框色。
 * @param text UTF-8 文本字符串。
 * @param border_w 边框宽度（像素）。
 * @return 无。
 */
void we_btn_set_style(we_btn_obj_t *obj, we_btn_state_t state, colour_t bg, colour_t border, colour_t text,
                      uint8_t border_w)
{
#if WE_BTN_USE_CUSTOM_STYLE
    if (obj == NULL || state >= WE_BTN_STATE_MAX)
        return;
    obj->styles[state].bg_color = bg;
    obj->styles[state].border_color = border;
    obj->styles[state].text_color = text;
    obj->styles[state].border_w = border_w;
we_obj_invalidate((we_obj_t *)obj);
#else
    // 内置模式下，该函数为空或报错
    (void)obj;
    (void)state;
    (void)bg;
    (void)border;
    (void)text;
    (void)border_w;
#endif
}

/**
 * @brief 更新按钮属性并触发重绘。
 * @param obj 目标控件对象指针。
 * @param radius 圆角半径（像素）。
 * @return 无。
 */
void we_btn_set_radius(we_btn_obj_t *obj, uint16_t radius)
{
    if (obj == NULL || obj->radius == radius)
        return;
    obj->radius = radius;
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 更新按钮属性并触发重绘。
 * @param obj 目标控件对象指针。
 * @param border_w 边框宽度（像素）。
 * @return 无。
 */
void we_btn_set_border_width(we_btn_obj_t *obj, uint8_t border_w)
{
#if WE_BTN_USE_CUSTOM_STYLE
    uint8_t i;
    uint8_t changed = 0;

    if (obj == NULL)
        return;

    for (i = 0; i < WE_BTN_STATE_MAX; i++)
    {
        if (obj->styles[i].border_w != border_w)
        {
            obj->styles[i].border_w = border_w;
            changed = 1;
        }
    }

    if (changed)
    {
we_obj_invalidate((we_obj_t *)obj);
    }
#else
    (void)obj;
    (void)border_w;
#endif
}
