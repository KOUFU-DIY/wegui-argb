#include "we_widget_btn.h"
#include "we_font_text.h"
#include "we_render.h"

#if !WE_BTN_USE_CUSTOM_STYLE
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
 * @brief 绘制按钮皮肤：将圆角半径钳制到不超过半宽/半高后，用解析式抗锯齿圆角矩形填充背景（当前不绘制边框）。
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

static void _btn_draw_text_clipped(we_btn_obj_t *obj, const we_btn_style_t *style)
{
    we_lcd_t *lcd;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    uint16_t txt_w;
    int8_t y_top;
    int8_t y_bot;
    int16_t btn_cx;
    int16_t btn_cy;
    int16_t txt_x;
    int16_t txt_y;
    int16_t clip_x0;
    int16_t clip_y0;
    int16_t clip_x1;
    int16_t clip_y1;

    if (obj == NULL || style == NULL || obj->text == NULL || obj->font == NULL)
        return;

    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    txt_w = we_get_text_width(obj->font, obj->text);
    we_get_text_bbox(obj->font, obj->text, &y_top, &y_bot);

    btn_cx = obj->base.x + (int16_t)(obj->base.w / 2);
    btn_cy = obj->base.y + (int16_t)(obj->base.h / 2);
    txt_x = btn_cx - (int16_t)(txt_w / 2);
    txt_y = btn_cy - (y_top + y_bot) / 2;

    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    clip_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
    clip_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
    clip_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
    clip_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

    if (clip_x0 <= clip_x1 && clip_y0 <= clip_y1)
    {
        lcd->pfb_area.x0 = clip_x0;
        lcd->pfb_area.x1 = clip_x1;
        lcd->pfb_y_start = (uint16_t)clip_y0;
        lcd->pfb_y_end = (uint16_t)clip_y1;
        lcd->pfb_gram = old_gram + (clip_y0 - (int16_t)old_y_start) * lcd->pfb_width
                                + (clip_x0 - old_pfb_area.x0);

        we_draw_string(lcd, txt_x, txt_y, obj->font, obj->text, style->text_color, obj->opacity);
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;
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

    // 2. [合并绘制] 使用抗锯齿圆角矩形引擎一次画完背景与圆角抗锯齿（当前样式不绘制边框）
    we_btn_draw_skin(lcd, obj->base.x, obj->base.y, obj->base.w, obj->base.h,
                     obj->radius, style, obj->opacity);

    _btn_draw_text_clipped(obj, style);
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

    /* 叠加语义：默认按压视觉始终执行，用户回调只补业务逻辑，
     * 不再需要自己维护按压态、也不必关心返回值
     * （旧"接管"语义连官方 demo 都连续踩坑：忘补样式 + 返回 0
     *   导致容器不锁定、CLICKED 不被转发）。 */
    switch (event)
    {
    case WE_EVENT_PRESSED:
we_btn_set_state(btn, WE_BTN_STATE_PRESSED);
        break;
    case WE_EVENT_RELEASED:
we_btn_set_state(btn, WE_BTN_STATE_NORMAL);
        break;
    default:
        break;
    }

    if (btn->user_event_cb != NULL)
    {
        (void)btn->user_event_cb(ptr, event, data);
    }

    (void)data; // 不使用具体坐标
    return 1; // 交互控件恒返回 1，容器（slideshow 等）据此锁定并转发后续事件
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

    static const we_class_t _btn_class = {.draw_cb = _btn_draw_cb, .event_cb = _btn_event_cb, .set_pos_cb = NULL};
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
    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

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
    // 内置样式模式下样式表只读，本函数静默忽略所有参数（无运行时副作用）
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
