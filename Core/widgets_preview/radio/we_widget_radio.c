#include "we_widget_radio.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _radio_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 触点命中检测：返回命中的行序号。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触点 X（屏幕绝对坐标）。
 * @param py 传入：触点 Y。
 * @return 命中的行序号（0 起），未命中返回 -1。
 */
static int16_t _radio_hit_row(const we_radio_obj_t *obj, int16_t px, int16_t py)
{
    int16_t ry;
    int16_t row;

    if (obj->count == 0U || obj->row_h == 0U)
        return -1;
    if (px < obj->base.x || px >= (int16_t)(obj->base.x + obj->base.w))
        return -1;

    ry = (int16_t)(py - obj->base.y);
    if (ry < 0)
        return -1;

    row = (int16_t)(ry / (int16_t)obj->row_h);
    if (row >= (int16_t)obj->count)
        return -1;
    return row;
}

/**
 * @brief 按行序号标脏对应整行区域（沿父链裁剪）。
 * @param obj 传入：控件对象指针。
 * @param row 传入：行序号（越界则忽略）。
 * @return 无。
 */
static void _radio_invalidate_row(we_radio_obj_t *obj, int16_t row)
{
    if (row < 0 || row >= (int16_t)obj->count)
        return;
    we_obj_invalidate_area((we_obj_t *)obj,
                           obj->base.x,
                           (int16_t)(obj->base.y + row * (int16_t)obj->row_h),
                           obj->base.w, (int16_t)obj->row_h);
}

/* --------------------------------------------------------------------------
 * 绘图回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note preview 放宽：每次重绘遍历全部行，越出 PFB 的写入由原语裁剪丢弃。
 */
static void _radio_draw_cb(void *ptr)
{
    we_radio_obj_t *obj = (we_radio_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    static const colour_t _c_white = RGB888_CONST(255, 255, 255);
    int16_t i;

    if (obj->opacity == 0U || obj->labels == NULL || obj->count == 0U)
        return;

    for (i = 0; i < (int16_t)obj->count; i++)
    {
        int16_t row_y = (int16_t)(obj->base.y + i * (int16_t)obj->row_h);
        uint8_t is_sel = (i == (int16_t)obj->selected) ? 1U : 0U;
        uint8_t is_press = (obj->pressed && i == obj->press_row) ? 1U : 0U;
        colour_t row_bg = lcd->bg_color; /* 指示器掏空所用的行底色 */
        int16_t ind_x;
        int16_t ind_cy;
        int16_t inner_d;
        const char *label;

        /* 1. 按压行轻微高亮（整行圆角高亮条，行底色随之切换保持视觉连贯） */
        if (is_press)
        {
            row_bg = we_colour_blend(_c_white, lcd->bg_color,
                                     (uint8_t)WE_RADIO_PRESS_LIGHTEN);
            we_draw_round_rect_analytic_fill(lcd, obj->base.x, row_y,
                                             (uint16_t)obj->base.w,
                                             (uint16_t)obj->row_h, 6U,
                                             row_bg, obj->opacity);
        }

        /* 2. 圆形指示器：大圆 -> 小一号背景圆掏环 -> 选中中心实心小圆 */
        ind_x = (int16_t)(obj->base.x + WE_RADIO_ROW_PAD);
        ind_cy = (int16_t)(row_y + (int16_t)obj->row_h / 2);
        we_draw_round_rect_analytic_fill(lcd, ind_x,
                                         (int16_t)(ind_cy - WE_RADIO_IND_D / 2),
                                         (uint16_t)WE_RADIO_IND_D,
                                         (uint16_t)WE_RADIO_IND_D,
                                         (uint16_t)(WE_RADIO_IND_D / 2),
                                         is_sel ? obj->sel_color : obj->ring_color,
                                         obj->opacity);

        inner_d = (int16_t)(WE_RADIO_IND_D - 2 * WE_RADIO_RING_W);
        if (inner_d > 0)
        {
            we_draw_round_rect_analytic_fill(lcd,
                                             (int16_t)(ind_x + WE_RADIO_RING_W),
                                             (int16_t)(ind_cy - inner_d / 2),
                                             (uint16_t)inner_d, (uint16_t)inner_d,
                                             (uint16_t)(inner_d / 2),
                                             row_bg, obj->opacity);
        }

        if (is_sel)
        {
            /* 取偶数直径：与外圆几何中心严格对齐（奇偶混用会偏 0.5px） */
            int16_t dot_d = (int16_t)((WE_RADIO_IND_D / 2) & ~1);
            we_draw_round_rect_analytic_fill(lcd,
                                             (int16_t)(ind_x + (WE_RADIO_IND_D - dot_d) / 2),
                                             (int16_t)(ind_cy - dot_d / 2),
                                             (uint16_t)dot_d, (uint16_t)dot_d,
                                             (uint16_t)(dot_d / 2),
                                             obj->sel_color, obj->opacity);
        }

        /* 3. 右侧文字（垂直按墨迹 bbox 对行居中） */
        label = obj->labels[i];
        if (label != NULL && obj->font != NULL)
        {
            int8_t y_top;
            int8_t y_bot;
            int16_t txt_x = (int16_t)(ind_x + WE_RADIO_IND_D + WE_RADIO_TEXT_GAP);
            int16_t txt_y;

            we_get_text_bbox(obj->font, label, &y_top, &y_bot);
            txt_y = (int16_t)(ind_cy - (y_top + y_bot) / 2);
            we_draw_string(lcd, txt_x, txt_y, obj->font, label,
                           obj->text_color, obj->opacity);
        }
    }
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 选中指定行（互斥），值变时标脏新旧两行并触发回调。
 * @param obj 传入：控件对象指针。
 * @param row 传入：目标行序号。
 * @param fire_cb 传入：1 = 值变时触发 changed_cb（点击路径），0 = 不触发（程序路径）。
 * @return 无。
 */
static void _radio_select(we_radio_obj_t *obj, uint8_t row, uint8_t fire_cb)
{
    uint8_t old;

    if (row >= obj->count || row == obj->selected)
        return;

    old = obj->selected;
    obj->selected = row;
    _radio_invalidate_row(obj, (int16_t)old);
    _radio_invalidate_row(obj, (int16_t)row);

    if (fire_cb && obj->changed_cb != NULL)
        obj->changed_cb(obj, row);
}

/**
 * @brief 控件事件回调，处理按压/拖出/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 = 事件已消费，0 = 穿透。
 */
static uint8_t _radio_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_radio_obj_t *obj = (we_radio_obj_t *)ptr;
    int16_t row;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        row = _radio_hit_row(obj, data->x, data->y);
        if (row >= 0)
        {
            obj->press_row = row;
            obj->pressed = 1U;
            _radio_invalidate_row(obj, row);
            return 1U;
        }
        obj->press_row = -1;
        obj->pressed = 0U;
        return 0U;

    case WE_EVENT_STAY:
        if (obj->press_row >= 0)
        {
            /* 拖出原行：取消按压态，本次触摸不再产生选中切换 */
            if (_radio_hit_row(obj, data->x, data->y) != obj->press_row)
            {
                if (obj->pressed)
                {
                    obj->pressed = 0U;
                    _radio_invalidate_row(obj, obj->press_row);
                }
                obj->press_row = -1;
            }
            return 1U;
        }
        return 0U;

    case WE_EVENT_RELEASED:
        if (obj->pressed)
        {
            obj->pressed = 0U;
            _radio_invalidate_row(obj, obj->press_row);
        }
        /* press_row 留给紧随其后的 CLICKED 判定使用 */
        return (obj->press_row >= 0) ? 1U : 0U;

    case WE_EVENT_CLICKED:
        row = _radio_hit_row(obj, data->x, data->y);
        if (row >= 0 && row == obj->press_row)
        {
            obj->press_row = -1;
            _radio_select(obj, (uint8_t)row, 1U);
            return 1U;
        }
        obj->press_row = -1;
        return 0U;

    default:
        break;
    }
    return 0U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_RADIO_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 进出编辑态，编辑态上下键直接移动选中行（互斥）。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 键控切换走触摸同款 _radio_select（值变才标脏新旧两行并触发
 *       changed_cb）；BACK 不处理，交焦点管理器退出编辑态。
 */
static uint8_t _radio_key_cb(void *ptr, uint8_t key_evt)
{
    we_radio_obj_t *obj = (we_radio_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U && obj->count > 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (we_focus_edit_active(lcd))
            we_focus_edit_exit(lcd);
        else
            we_focus_edit_enter(lcd);
        return 1U;
    case WE_KEY_UP:
    case WE_KEY_DOWN:
        if (!we_focus_edit_active(lcd))
            return 0U; /* 导航态：方向键交焦点管理器移动焦点 */
        if (key_evt == WE_KEY_DOWN)
        {
            if ((uint8_t)(obj->selected + 1U) < obj->count)
                _radio_select(obj, (uint8_t)(obj->selected + 1U), 1U);
        }
        else if (obj->selected > 0U)
        {
            _radio_select(obj, (uint8_t)(obj->selected - 1U), 1U);
        }
        return 1U;
    default:
        return 0U;
    }
}
#endif

static const we_class_t _radio_class = {
    .draw_cb = _radio_draw_cb,
    .event_cb = _radio_event_cb,
    .set_pos_cb = NULL,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_RADIO_USE_KEY == 1)
    .key_cb = _radio_key_cb,
#endif
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化单选组控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param w 控件宽度（像素，整行均可点击）。
 * @param labels 选项名数组（调用方持有，count 个）。
 * @param count 选项行数。
 * @return 无。
 */
void we_radio_obj_init(we_radio_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, int16_t w,
                       const char *const *labels, uint8_t count, const unsigned char *font)
{
    uint16_t line_h;
    uint16_t row_h;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.class_p = &_radio_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->labels = labels;
    obj->count = count;
    obj->selected = 0U;

    obj->ring_color = RGB888TODEV(120, 135, 155);
    obj->sel_color = RGB888TODEV(50, 130, 240);
    obj->text_color = RGB888TODEV(220, 228, 238);
    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;

    obj->changed_cb = NULL;
    obj->press_row = -1;
    obj->pressed = 0U;
    obj->opacity = 255U;

    /* 行高 = max(字体行高, 指示器直径) + 上下内边距，总高自动算出 */
    line_h = we_font_get_line_height(obj->font);
    row_h = (line_h > (uint16_t)WE_RADIO_IND_D) ? line_h : (uint16_t)WE_RADIO_IND_D;
    row_h = (uint16_t)(row_h + 2U * (uint16_t)WE_RADIO_ROW_PAD);
    obj->row_h = row_h;
    obj->base.h = (int16_t)((uint16_t)count * row_h);

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 程序设置选中行（互斥），只标脏受影响的新旧两行（不触发回调）。
 * @param obj 控件对象指针。
 * @param idx 目标行序号（越界或与当前相同则直接返回）。
 * @return 无。
 */
void we_radio_set_selected(we_radio_obj_t *obj, uint8_t idx)
{
    if (obj == NULL)
        return;
    _radio_select(obj, idx, 0U);
}

/**
 * @brief 读取当前选中行序号。
 * @param obj 控件对象指针。
 * @return 选中行序号（0 起）；obj 为 NULL 时返回 0。
 */
uint8_t we_radio_get_selected(const we_radio_obj_t *obj)
{
    if (obj == NULL)
        return 0U;
    return obj->selected;
}

/**
 * @brief 注册选中改变回调（点击切换且值变时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_radio_set_changed_cb(we_radio_obj_t *obj, we_radio_changed_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->changed_cb = cb;
}

/**
 * @brief 设置三项配色：未选中圈色 / 选中色 / 文字色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param ring 未选中外环圈色。
 * @param sel 选中外环与中心实心圆颜色。
 * @param text 选项文字色。
 * @return 无。
 */
void we_radio_set_colors(we_radio_obj_t *obj, colour_t ring,
                         colour_t sel, colour_t text)
{
    if (obj == NULL)
        return;
    if (_radio_colour_eq(obj->ring_color, ring) &&
        _radio_colour_eq(obj->sel_color, sel) &&
        _radio_colour_eq(obj->text_color, text))
        return;

    obj->ring_color = ring;
    obj->sel_color = sel;
    obj->text_color = text;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_radio_set_opacity(we_radio_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除控件并从对象链表移除（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_radio_obj_delete(we_radio_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
