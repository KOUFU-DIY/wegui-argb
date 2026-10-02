#include "we_widget_checkbox.h"
#include "wegui.h"

/* 方框与文本之间的间距 (像素) */
#define CB_TEXT_GAP 6

/* ---- 默认样式表 (暗色主题, 存放 Flash) ---- */
static const we_cb_style_t _default_styles[WE_CB_STATE_MAX] = {
    /* OFF:         灰色空心框 */
    [WE_CB_STATE_OFF]         = { .box_color = RGB888_CONST(120, 135, 155),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(220, 228, 238),
                                  .border_w = 2 },
    /* OFF_PRESSED: 亮灰空心框 */
    [WE_CB_STATE_OFF_PRESSED] = { .box_color   = RGB888_CONST(170, 185, 200),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(245, 248, 252),
                                  .border_w = 2 },
    /* ON:          蓝色实心框 */
    [WE_CB_STATE_ON]          = { .box_color   = RGB888_CONST( 50, 130, 240),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(220, 228, 238),
                                  .border_w = 0 },
    /* ON_PRESSED:  深蓝实心框 */
    [WE_CB_STATE_ON_PRESSED]  = { .box_color   = RGB888_CONST( 35, 100, 190),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(245, 248, 252),
                                  .border_w = 0 },
};

/* ---- 获取当前视觉状态对应的样式 ---- */
static const we_cb_style_t *_cb_get_style(const we_checkbox_obj_t *obj)
{
    we_cb_state_t s;

    if (obj->checked)
        s = obj->pressed ? WE_CB_STATE_ON_PRESSED : WE_CB_STATE_ON;
    else
        s = obj->pressed ? WE_CB_STATE_OFF_PRESSED : WE_CB_STATE_OFF;

    return &obj->styles[s];
}

/**
 * @brief 勾选状态翻转后标脏：通常只标方框；ON/OFF 两态文字颜色不同时整控件标脏。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _cb_invalidate_check_change(we_checkbox_obj_t *obj)
{
    uint8_t off = obj->pressed ? (uint8_t)WE_CB_STATE_OFF_PRESSED : (uint8_t)WE_CB_STATE_OFF;
    uint8_t on = obj->pressed ? (uint8_t)WE_CB_STATE_ON_PRESSED : (uint8_t)WE_CB_STATE_ON;

    if (!we_colour_eq(obj->styles[off].text_color, obj->styles[on].text_color))
    {
        we_obj_invalidate((we_obj_t *)obj);
        return;
    }
    we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y,
                           (int16_t)obj->box_size, (int16_t)obj->box_size);
}

/**
 * @brief 在方框内绘制对勾。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param bx 方框左上角 X 坐标。
 * @param by 方框左上角 Y 坐标。
 * @param size 方框边长（像素），对勾按其百分比定位。
 * @param color 目标颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 * @note 对勾两笔：(20%,50%)→(40%,72%)→(80%,28%)。size*k/100 用 ×5243>>19
 *       （边长不超过 2442 时与整除逐位相同），每个切片省 6 次库除法。
 */
static void _cb_draw_checkmark(we_lcd_t *lcd, int16_t bx, int16_t by,
                               uint16_t size, colour_t color, uint8_t opacity)
{
#define _CB_PCT(k) ((int16_t)(((uint32_t)size * (k) * 5243U) >> 19))
    int16_t ax1 = (int16_t)(bx + _CB_PCT(20U));
    int16_t ay1 = (int16_t)(by + _CB_PCT(50U));
    int16_t ax2 = (int16_t)(bx + _CB_PCT(40U));
    int16_t ay2 = (int16_t)(by + _CB_PCT(72U));
    int16_t ax3 = (int16_t)(bx + _CB_PCT(80U));
    int16_t ay3 = (int16_t)(by + _CB_PCT(28U));
#undef _CB_PCT
    uint8_t lw = (uint8_t)(size / 8);

    if (lw < 2)
        lw = 2;
    we_draw_line(lcd, ax1, ay1, ax2, ay2, lw, color, opacity);
    we_draw_line(lcd, ax2, ay2, ax3, ay3, lw, color, opacity);
}

/* ---- 绘图回调 ---- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _checkbox_draw_cb(void *ptr)
{
    we_checkbox_obj_t *obj = (we_checkbox_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    const we_cb_style_t *st = _cb_get_style(obj);
    int16_t bx = obj->base.x;
    int16_t by = obj->base.y;
    uint16_t bs = obj->box_size;

    if (obj->opacity == 0)
        return;

    /* 1. 方框：border_w 为 0 时实心，否则只画边框环带（内部露出下面的内容） */
    if (st->border_w == 0)
        we_draw_round_rect_analytic_fill(lcd, bx, by, bs, bs, obj->radius,
                                         st->box_color, obj->opacity);
    else
        we_draw_round_rect_ring(lcd, bx, by, bs, bs, obj->radius, st->border_w,
                                st->box_color, obj->opacity);

    /* 2. 勾选标记 */
    if (obj->checked)
        _cb_draw_checkmark(lcd, bx, by, bs, st->check_color, obj->opacity);

    /* 3. 右侧文本（竖直中线已随文本缓存） */
    if (obj->text && obj->font)
        we_draw_string(lcd, (int16_t)(bx + bs + CB_TEXT_GAP), (int16_t)(by + bs / 2 - obj->txt_ymid),
                       obj->font, obj->text, st->text_color, obj->opacity);
}

/* ---- 事件回调 ---- */

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CHECKBOX_USE_KEY == 1)
static uint8_t _checkbox_key_cb(void *ptr, uint8_t key_evt);
#endif

/**
 * @brief 控件事件回调，处理按压/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 已消费；全透明时按压返回 0（交给外层）。
 */
static uint8_t _checkbox_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CHECKBOX_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _checkbox_key_cb(ptr, (uint8_t)event);
#endif

    we_checkbox_obj_t *cb = (we_checkbox_obj_t *)ptr;

    /* 叠加语义：默认按压视觉始终执行；业务（CLICKED 是否勾选）
     * 在有用户回调时交给用户回调，避免默认勾选与用户逻辑双重生效。 */
    switch (event)
    {
    case WE_EVENT_PRESSED:
        if (cb->opacity == 0U)
            return 0U; /* 看不见的复选框不接触摸 */
        cb->pressed = 1;
        /* 按压样式同时改变方框与文字颜色，须整控件标脏，
         * 只标方框会让右侧文字停留在旧色。 */
        we_obj_invalidate((we_obj_t *)cb);
        break;
    case WE_EVENT_RELEASED:
        cb->pressed = 0;
        we_obj_invalidate((we_obj_t *)cb);
        break;
    default:
        break;
    }

    if (cb->user_event_cb != NULL)
    {
        (void)cb->user_event_cb(ptr, event, data);
        return 1;
    }

    (void)data;

    if (event == WE_EVENT_CLICKED)
        we_checkbox_toggle(cb);
    return 1;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CHECKBOX_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK = 翻转勾选（镜像触摸 CLICKED 语义）。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（WE_KEY_xx / WE_KEY_EVT_xx）。
 * @return 非 0 表示已消费。
 * @note 与触摸路径一致：有用户回调时业务交用户回调（data 为 NULL），
 *       否则执行默认勾选翻转并触发 changed_cb。
 */
static uint8_t _checkbox_key_cb(void *ptr, uint8_t key_evt)
{
    we_checkbox_obj_t *cb = (we_checkbox_obj_t *)ptr;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (cb->opacity != 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (cb->user_event_cb != NULL)
            (void)cb->user_event_cb(ptr, WE_EVENT_CLICKED, NULL);
        else
            we_checkbox_toggle(cb);
        return 1U;
    default:
        return 0U;
    }
}
#endif

/* ---- 公开 API ---- */

/**
 * @brief 依据方框尺寸与文本宽度/行高算出控件包围盒，并缓存文字竖直中线。
 * @param obj 目标控件对象指针。
 * @param out_w 传出：包围盒宽度。
 * @param out_h 传出：包围盒高度。
 * @return 无。
 */
static void _cb_measure(we_checkbox_obj_t *obj, int16_t *out_w, int16_t *out_h)
{
    int16_t w = (int16_t)obj->box_size;
    int16_t h = (int16_t)obj->box_size;

    obj->txt_ymid = 0;
    if (obj->text && obj->font)
    {
        int8_t y_top, y_bot;

        w = (int16_t)(obj->box_size + CB_TEXT_GAP + we_get_text_ink_width(obj->font, obj->text));
        if (we_font_get_line_height(obj->font) > (uint16_t)h)
            h = (int16_t)we_font_get_line_height(obj->font);
        we_get_text_bbox(obj->font, obj->text, &y_top, &y_bot);
        obj->txt_ymid = (int8_t)((y_top + y_bot) / 2);
    }

    *out_w = w;
    *out_h = h;
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param box_size 复选框方框边长（像素）。
 * @param text UTF-8 文本字符串。
 * @param font 字体资源指针。
 * @param event_cb 回调函数指针。
 * @return 无。
 */
void we_checkbox_obj_init(we_checkbox_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y,
                          uint16_t box_size, const char *text, const unsigned char *font,
                          we_checkbox_event_cb_t event_cb)
{
    if (obj == NULL || lcd == NULL)
        return;

    static const we_class_t _cb_class = {
        .draw_cb = _checkbox_draw_cb,
        .event_cb = _checkbox_event_cb,
        .set_pos_cb = NULL,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CHECKBOX_USE_KEY == 1)
        .class_flags = WE_CLASS_FLAG_FOCUSABLE, /* 键/焦点走统一 event_cb 通道 */
#endif
    };

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.class_p = &_cb_class;
    obj->base.parent = NULL;

    obj->text = text;
    obj->font = font;
    obj->user_event_cb = event_cb;
    obj->box_size = box_size;
    obj->radius = (uint16_t)(box_size / 5);
    obj->opacity = 255;
    obj->checked = 0;
    obj->pressed = 0;
    obj->changed_cb = NULL;

    /* 默认指向 Flash 内置样式表 */
    obj->styles = _default_styles;

    _cb_measure(obj, &obj->base.w, &obj->base.h);

    /* 加入显示链表 */
    obj->base.next = NULL;
    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置勾选状态并刷新方框区域（值未变时直接返回）。
 * @param obj 目标控件对象指针。
 * @param checked 目标勾选状态（0 未选中，非 0 选中）。
 * @return 无。
 */
void we_checkbox_set_checked(we_checkbox_obj_t *obj, uint8_t checked)
{
    if (obj == NULL)
        return;

    uint8_t val = checked ? 1U : 0U;
    if (obj->checked == val)
        return;

    obj->checked = val;
    _cb_invalidate_check_change(obj);
}

/**
 * @brief 执行 we_checkbox_toggle。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_checkbox_toggle(we_checkbox_obj_t *obj)
{
    if (obj == NULL)
        return;
    obj->checked = obj->checked ? 0U : 1U;
    _cb_invalidate_check_change(obj);

    if (obj->changed_cb != NULL)
        obj->changed_cb(obj, obj->checked);
}

/**
 * @brief 注册勾选状态改变回调（替代轮询 is_checked）。
 * @param obj 目标控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_checkbox_set_changed_cb(we_checkbox_obj_t *obj, we_checkbox_changed_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->changed_cb = cb;
}

/**
 * @brief 查询当前是否勾选。
 * @param obj 目标控件对象指针。
 * @return 1=已勾选，0=未勾选或 obj 为 NULL。
 */
uint8_t we_checkbox_is_checked(const we_checkbox_obj_t *obj)
{
    if (obj == NULL)
        return 0;
    return obj->checked;
}

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param text UTF-8 文本字符串。
 * @return 无。
 * @note 包围盒尺寸变了走 we_obj_set_size：新旧包围盒与焦点光标环一起标脏，
 *       持有焦点时改文字也不会留下光标残影。
 */
void we_checkbox_set_text(we_checkbox_obj_t *obj, const char *text)
{
    int16_t w;
    int16_t h;

    if (obj == NULL)
        return;
    obj->text = text;
    _cb_measure(obj, &w, &h);
    if (w == obj->base.w && h == obj->base.h)
        we_obj_invalidate((we_obj_t *)obj);
    else
        we_obj_set_size((we_obj_t *)obj, w, h);
}

/**
 * @brief 设置控件样式参数并刷新显示。
 * @param obj 目标控件对象指针。
 * @param styles 四种状态对应的样式表指针。
 * @return 无。
 */
void we_checkbox_set_styles(we_checkbox_obj_t *obj, const we_cb_style_t *styles)
{
    if (obj == NULL)
        return;
    obj->styles = (styles != NULL) ? styles : _default_styles;
    we_obj_invalidate((we_obj_t *)obj);
}
