#include "we_widget_btn.h"
#include "wegui.h"

#if !WE_BTN_USE_CUSTOM_STYLE
/* 默认配色以 RGB565 手写表为准；RGB888/其它色深表由 565 值按位复制
 * 展开（888→565 截断可精确还原），三张表跨色深呈现同一配色。 */
static const we_btn_style_t _built_in_styles[WE_BTN_STATE_MAX] = {
#if (LCD_DEEP == DEEP_RGB565)
    [WE_BTN_STATE_NORMAL] = {.bg_color = {.dat16 = 0x39AA},
                             .text_color = {.dat16 = 0xEF7D}},
    [WE_BTN_STATE_SELECTED] = {.bg_color = {.dat16 = 0x5D9F},
                               .text_color = {.dat16 = 0xFFBF}},
    [WE_BTN_STATE_PRESSED] = {.bg_color = {.dat16 = 0x44DB},
                              .text_color = {.dat16 = 0xF77E}},
    [WE_BTN_STATE_DISABLED] = {.bg_color = {.dat16 = 0x4A69},
                               .text_color = {.dat16 = 0x9CD3}},
#elif (LCD_DEEP == DEEP_RGB888)
    [WE_BTN_STATE_NORMAL] = {.bg_color = {.rgb = {59, 52, 82}},
                             .text_color = {.rgb = {239, 239, 239}}},
    [WE_BTN_STATE_SELECTED] = {.bg_color = {.rgb = {90, 178, 255}},
                               .text_color = {.rgb = {255, 247, 255}}},
    [WE_BTN_STATE_PRESSED] = {.bg_color = {.rgb = {66, 154, 222}},
                              .text_color = {.rgb = {247, 239, 247}}},
    [WE_BTN_STATE_DISABLED] = {.bg_color = {.rgb = {74, 77, 74}},
                               .text_color = {.rgb = {156, 154, 156}}},
#else /* 其它色深（GRAY8 等）：用编译期常量宏按 RGB888 等值转换 */
    [WE_BTN_STATE_NORMAL] = {.bg_color = RGB888_CONST(59, 52, 82),
                             .text_color = RGB888_CONST(239, 239, 239)},
    [WE_BTN_STATE_SELECTED] = {.bg_color = RGB888_CONST(90, 178, 255),
                               .text_color = RGB888_CONST(255, 247, 255)},
    [WE_BTN_STATE_PRESSED] = {.bg_color = RGB888_CONST(66, 154, 222),
                              .text_color = RGB888_CONST(247, 239, 247)},
    [WE_BTN_STATE_DISABLED] = {.bg_color = RGB888_CONST(74, 77, 74),
                               .text_color = RGB888_CONST(156, 154, 156)},
#endif
};
#endif

/**
 * @brief 绘制按钮皮肤：解析式抗锯齿圆角矩形填充背景（不绘制边框）。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param radius 圆角半径（像素，超过半宽/半高时由绘图原语钳制）。
 * @param style 样式配置结构体指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_btn_draw_skin(we_lcd_t *lcd, int16_t x, int16_t y, uint16_t w, uint16_t h,
                      uint16_t radius, const we_btn_style_t *style, uint8_t opacity)
{
    if (lcd == NULL || style == NULL)
        return;
    we_draw_round_rect_analytic_fill(lcd, x, y, w, h, radius, style->bg_color, opacity);
}

/**
 * @brief 量出文字宽度与墨迹竖直中线并缓存（init / set_text 时调用）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _btn_measure_text(we_btn_obj_t *obj)
{
    obj->txt_w = 0U;
    obj->txt_ymid = 0;
    if (obj->text != NULL && obj->font != NULL)
    {
        int8_t y_top, y_bot;

        obj->txt_w = we_get_text_width(obj->font, obj->text);
        we_get_text_bbox(obj->font, obj->text, &y_top, &y_bot);
        obj->txt_ymid = (int8_t)((y_top + y_bot) / 2);
    }
}

/**
 * @brief 在按钮中央绘制文字（超出按钮的部分由内核按包围盒裁掉）。
 * @param obj 目标控件对象指针。
 * @param text_color 文字颜色。
 * @return 无。
 */
static void _btn_draw_text(we_btn_obj_t *obj, colour_t text_color)
{
    if (obj->text == NULL || obj->font == NULL)
        return;
    we_draw_string(obj->base.lcd,
                   (int16_t)(obj->base.x + (int16_t)(obj->base.w / 2) - (int16_t)(obj->txt_w / 2)),
                   (int16_t)(obj->base.y + (int16_t)(obj->base.h / 2) - obj->txt_ymid),
                   obj->font, obj->text, text_color, obj->opacity);
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _btn_draw_cb(void *ptr)
{
    we_btn_obj_t *obj = (we_btn_obj_t *)ptr;
    const we_btn_style_t *style;

    if (obj->opacity == 0)
        return;

    // 1. 获取当前状态下的样式
#if WE_BTN_USE_CUSTOM_STYLE
    style = &obj->styles[obj->state];
#else
    style = &_built_in_styles[obj->state];
#endif

#if (WE_CFG_MONO_STYLE == 1)
    /* 单色样式：常态白描边黑底 + 白字，按压整块反白 + 挖空（黑）字 */
    {
        static const colour_t mono_fg = RGB888_CONST(255, 255, 255);
        static const colour_t mono_bg = RGB888_CONST(0, 0, 0);

        (void)style;
        if (obj->state == WE_BTN_STATE_PRESSED)
        {
            we_draw_round_rect_analytic_fill(obj->base.lcd, obj->base.x, obj->base.y,
                                             (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                             obj->radius, mono_fg, obj->opacity);
            _btn_draw_text(obj, mono_bg);
        }
        else
        {
            we_draw_round_rect_outline_fill(obj->base.lcd, obj->base.x, obj->base.y,
                                            (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                            obj->radius, mono_fg, mono_bg, obj->opacity);
            _btn_draw_text(obj, mono_fg);
        }
    }
#else
    // 2. 抗锯齿圆角矩形一次画完背景与圆角（当前样式不绘制边框），再画居中文字
    we_btn_draw_skin(obj->base.lcd, obj->base.x, obj->base.y, obj->base.w, obj->base.h,
                     obj->radius, style, obj->opacity);
    _btn_draw_text(obj, style->text_color);
#endif
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_BTN_USE_KEY == 1)
static uint8_t _btn_key_cb(void *ptr, uint8_t key_evt);
#endif

/**
 * @brief 控件事件回调，处理按压/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 已消费；禁用或全透明时按压返回 0（交给外层，也不改状态）。
 */
static uint8_t _btn_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_BTN_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _btn_key_cb(ptr, (uint8_t)event);
#endif

    we_btn_obj_t *btn = (we_btn_obj_t *)ptr;

    /* 与按键路径同口径：禁用或看不见的按钮不接按压 */
    if (event == WE_EVENT_PRESSED && (btn->state == WE_BTN_STATE_DISABLED || btn->opacity == 0U))
        return 0U;

    /* 叠加语义：默认按压视觉始终执行，用户回调只补业务逻辑，
     * 不需要自己维护按压态、也不必关心返回值。 */
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
        (void)btn->user_event_cb(ptr, event, data);

    return 1; /* 交互控件恒返回 1：成为按压对象，后续事件由内核直接派发给它 */
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_BTN_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 按下沿进入按压态，松开沿回弹并派发 CLICKED。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（WE_KEY_xx / WE_KEY_EVT_xx）。
 * @return 非 0 表示已消费。
 * @note 按住 OK 期间保持 PRESSED（与触摸按住一致）。按键合成的 CLICKED
 *       事件 data 为 NULL，用户回调不得解引用坐标。
 */
static uint8_t _btn_key_cb(void *ptr, uint8_t key_evt)
{
    we_btn_obj_t *btn = (we_btn_obj_t *)ptr;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        /* 禁用或全透明时拒绝聚焦，遍历自动跳过本实例 */
        return (btn->state != WE_BTN_STATE_DISABLED && btn->opacity != 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK: /* 按下沿：进入按压态，等待松开沿 */
        we_btn_set_state(btn, WE_BTN_STATE_PRESSED);
        return 1U;
    case WE_KEY_EVT_OK_RELEASE: /* 松开沿：回弹 + 触发点击 */
        we_btn_set_state(btn, WE_BTN_STATE_NORMAL);
        if (btn->user_event_cb != NULL)
            (void)btn->user_event_cb(ptr, WE_EVENT_CLICKED, NULL);
        return 1U;
    case WE_KEY_EVT_FLASH_END: /* 取消（焦点切走等）：仅回弹不点击 */
        we_btn_set_state(btn, WE_BTN_STATE_NORMAL);
        return 1U;
    default:
        return 0U; /* 方向/前后键交焦点管理器默认导航 */
    }
}
#endif

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

    static const we_class_t _btn_class = {
        .draw_cb = _btn_draw_cb,
        .event_cb = _btn_event_cb,
        .set_pos_cb = NULL,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_BTN_USE_KEY == 1)
        .class_flags = WE_CLASS_FLAG_FOCUSABLE, /* 键/焦点走统一 event_cb 通道 */
#endif
    };
    obj->base.class_p = &_btn_class;

    // 2. 初始化属性
    obj->text = text;
    obj->font = font;
    _btn_measure_text(obj);
    obj->user_event_cb = event_cb;
    obj->state = WE_BTN_STATE_NORMAL;
    obj->radius = (uint16_t)(h / 4);
    if (obj->radius < 6U)
        obj->radius = 6U;
    obj->opacity = 255;

#if WE_BTN_USE_CUSTOM_STYLE
    // 3. 设置默认配色方案 (如果使用独立样式；与内置样式表同源，见表头注释)
    we_btn_set_style(obj, WE_BTN_STATE_NORMAL, RGB888TODEV(59, 52, 82), RGB888TODEV(239, 239, 239));
    we_btn_set_style(obj, WE_BTN_STATE_SELECTED, RGB888TODEV(90, 178, 255), RGB888TODEV(255, 247, 255));
    we_btn_set_style(obj, WE_BTN_STATE_PRESSED, RGB888TODEV(66, 154, 222), RGB888TODEV(247, 239, 247));
    we_btn_set_style(obj, WE_BTN_STATE_DISABLED, RGB888TODEV(74, 77, 74), RGB888TODEV(156, 154, 156));
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
 * @brief 设置文本内容并重绘。
 * @param obj 目标控件对象指针。
 * @param text UTF-8 文本字符串（按指针引用，调用方保持其存活）。
 * @return 无。
 * @note 传入同一个缓冲区也会重新测量并重绘，原地改写缓冲区内容后
 *       再调用一次即可刷新；不要每帧重复调用。
 */
void we_btn_set_text(we_btn_obj_t *obj, const char *text)
{
    if (obj == NULL)
        return;
    obj->text = text;
    _btn_measure_text(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置控件样式参数并刷新显示。
 * @param obj 目标控件对象指针。
 * @param state 目标状态枚举值。
 * @param bg 按钮背景色。
 * @param text 按钮文字颜色。
 * @return 无。
 */
void we_btn_set_style(we_btn_obj_t *obj, we_btn_state_t state, colour_t bg, colour_t text)
{
#if WE_BTN_USE_CUSTOM_STYLE
    if (obj == NULL || state >= WE_BTN_STATE_MAX)
        return;
    obj->styles[state].bg_color = bg;
    obj->styles[state].text_color = text;
    we_obj_invalidate((we_obj_t *)obj);
#else
    // 内置样式模式下样式表只读，本函数静默忽略所有参数（无运行时副作用）
    (void)obj;
    (void)state;
    (void)bg;
    (void)text;
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

