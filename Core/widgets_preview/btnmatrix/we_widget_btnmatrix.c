#include "we_widget_btnmatrix.h"
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
static uint8_t _bm_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 判断某格是否为空位（labels 为 NULL、元素为 NULL 或空串）。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：行优先格序号。
 * @return 1 空位，0 有效按键。
 */
static uint8_t _bm_cell_is_empty(const we_btnmatrix_obj_t *obj, int16_t idx)
{
    const char *label;

    if (obj->labels == NULL || idx < 0 ||
        (int32_t)idx >= (int32_t)obj->rows * (int32_t)obj->cols)
        return 1U;

    label = obj->labels[idx];
    return (label == NULL || label[0] == '\0') ? 1U : 0U;
}

/**
 * @brief 计算单格外接矩形（屏幕绝对坐标）。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：行优先格序号。
 * @param out_x 传出：格左上角 X。
 * @param out_y 传出：格左上角 Y。
 * @param out_w 传出：格宽度。
 * @param out_h 传出：格高度。
 * @return 1 成功，0 参数非法或网格尺寸退化。
 */
static uint8_t _bm_cell_rect(const we_btnmatrix_obj_t *obj, int16_t idx,
                             int16_t *out_x, int16_t *out_y,
                             int16_t *out_w, int16_t *out_h)
{
    int16_t cell_w;
    int16_t cell_h;
    int16_t row;
    int16_t col;

    if (obj->rows == 0U || obj->cols == 0U || idx < 0 ||
        (int32_t)idx >= (int32_t)obj->rows * (int32_t)obj->cols)
        return 0U;

    cell_w = (int16_t)((obj->base.w - (int16_t)(obj->cols - 1U) * WE_BTNMATRIX_GAP) / (int16_t)obj->cols);
    cell_h = (int16_t)((obj->base.h - (int16_t)(obj->rows - 1U) * WE_BTNMATRIX_GAP) / (int16_t)obj->rows);
    if (cell_w <= 0 || cell_h <= 0)
        return 0U;

    row = (int16_t)(idx / (int16_t)obj->cols);
    col = (int16_t)(idx % (int16_t)obj->cols);

    *out_x = (int16_t)(obj->base.x + col * (cell_w + WE_BTNMATRIX_GAP));
    *out_y = (int16_t)(obj->base.y + row * (cell_h + WE_BTNMATRIX_GAP));
    *out_w = cell_w;
    *out_h = cell_h;
    return 1U;
}

/**
 * @brief 触点命中检测：返回命中的格序号（落在格间距上视为未命中）。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触点 X（屏幕绝对坐标）。
 * @param py 传入：触点 Y。
 * @return 命中的行优先格序号，未命中返回 -1。
 */
static int16_t _bm_hit_cell(const we_btnmatrix_obj_t *obj, int16_t px, int16_t py)
{
    int16_t cell_w;
    int16_t cell_h;
    int16_t rx;
    int16_t ry;
    int16_t row;
    int16_t col;

    if (obj->rows == 0U || obj->cols == 0U)
        return -1;

    cell_w = (int16_t)((obj->base.w - (int16_t)(obj->cols - 1U) * WE_BTNMATRIX_GAP) / (int16_t)obj->cols);
    cell_h = (int16_t)((obj->base.h - (int16_t)(obj->rows - 1U) * WE_BTNMATRIX_GAP) / (int16_t)obj->rows);
    if (cell_w <= 0 || cell_h <= 0)
        return -1;

    rx = (int16_t)(px - obj->base.x);
    ry = (int16_t)(py - obj->base.y);
    if (rx < 0 || ry < 0)
        return -1;

    col = (int16_t)(rx / (cell_w + WE_BTNMATRIX_GAP));
    row = (int16_t)(ry / (cell_h + WE_BTNMATRIX_GAP));
    if (col >= (int16_t)obj->cols || row >= (int16_t)obj->rows)
        return -1;

    /* 落在格右侧/下侧间距带上不算命中 */
    if ((int16_t)(rx - col * (cell_w + WE_BTNMATRIX_GAP)) >= cell_w)
        return -1;
    if ((int16_t)(ry - row * (cell_h + WE_BTNMATRIX_GAP)) >= cell_h)
        return -1;

    return (int16_t)(row * (int16_t)obj->cols + col);
}

/**
 * @brief 按格序号标脏对应格子区域（沿父链裁剪）。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：行优先格序号。
 * @return 无。
 */
static void _bm_invalidate_cell(we_btnmatrix_obj_t *obj, int16_t idx)
{
    int16_t cx;
    int16_t cy;
    int16_t cw;
    int16_t ch;

    if (_bm_cell_rect(obj, idx, &cx, &cy, &cw, &ch))
        we_obj_invalidate_area((we_obj_t *)obj, cx, cy, cw, ch);
}

/* --------------------------------------------------------------------------
 * 绘图回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note preview 放宽：每次重绘遍历全部格子，越出 PFB 的写入由原语裁剪丢弃。
 */
static void _btnmatrix_draw_cb(void *ptr)
{
    we_btnmatrix_obj_t *obj = (we_btnmatrix_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    int32_t total = (int32_t)obj->rows * (int32_t)obj->cols;
    int32_t idx;

    if (obj->opacity == 0U || obj->labels == NULL)
        return;

    for (idx = 0; idx < total; idx++)
    {
        int16_t cx;
        int16_t cy;
        int16_t cw;
        int16_t ch;
        uint16_t draw_r;
        colour_t bg;
        const char *label;
        uint16_t txt_w;
        int8_t y_top;
        int8_t y_bot;
        int16_t txt_x;
        int16_t txt_y;

        if (_bm_cell_is_empty(obj, (int16_t)idx))
            continue;
        if (!_bm_cell_rect(obj, (int16_t)idx, &cx, &cy, &cw, &ch))
            continue;

        /* 1. 圆角底：按压格换按压底色 */
        bg = (obj->pressed && idx == (int32_t)obj->press_idx) ? obj->bg_press_color : obj->bg_color;
        draw_r = WE_BTNMATRIX_RADIUS;
        if (draw_r > (uint16_t)(cw / 2))
            draw_r = (uint16_t)(cw / 2);
        if (draw_r > (uint16_t)(ch / 2))
            draw_r = (uint16_t)(ch / 2);
        we_draw_round_rect_analytic_fill(lcd, cx, cy, (uint16_t)cw, (uint16_t)ch,
                                         draw_r, bg, obj->opacity);

        /* 2. 键名文字（水平按测宽、垂直按有效像素区 bbox 居中） */
        label = obj->labels[idx];
        if (obj->font != NULL)
        {
            txt_w = we_get_text_width(obj->font, label);
            we_get_text_bbox(obj->font, label, &y_top, &y_bot);
            txt_x = (int16_t)(cx + cw / 2 - (int16_t)(txt_w / 2U));
            txt_y = (int16_t)(cy + ch / 2 - (y_top + y_bot) / 2);
            we_draw_string(lcd, txt_x, txt_y, obj->font, label,
                           obj->text_color, obj->opacity);
        }
    }
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调，处理按压/拖出/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 = 事件已消费，0 = 穿透（空位格/间距带不响应）。
 */
static uint8_t _btnmatrix_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_btnmatrix_obj_t *obj = (we_btnmatrix_obj_t *)ptr;
    int16_t idx;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        idx = _bm_hit_cell(obj, data->x, data->y);
        if (idx >= 0 && !_bm_cell_is_empty(obj, idx))
        {
            obj->press_idx = idx;
            obj->pressed = 1U;
            _bm_invalidate_cell(obj, idx);
            return 1U;
        }
        obj->press_idx = -1;
        obj->pressed = 0U;
        return 0U; /* 空位/间距带：不消费，事件穿透 */

    case WE_EVENT_STAY:
        if (obj->press_idx >= 0)
        {
            /* 拖出原格：取消按压态，本次触摸序列不再产生点击 */
            if (_bm_hit_cell(obj, data->x, data->y) != obj->press_idx)
            {
                if (obj->pressed)
                {
                    obj->pressed = 0U;
                    _bm_invalidate_cell(obj, obj->press_idx);
                }
                obj->press_idx = -1;
            }
            return 1U;
        }
        return 0U;

    case WE_EVENT_RELEASED:
        if (obj->pressed)
        {
            obj->pressed = 0U;
            _bm_invalidate_cell(obj, obj->press_idx);
        }
        /* press_idx 留给紧随其后的 CLICKED 判定使用 */
        return (obj->press_idx >= 0) ? 1U : 0U;

    case WE_EVENT_CLICKED:
        idx = _bm_hit_cell(obj, data->x, data->y);
        if (idx >= 0 && idx == obj->press_idx && !_bm_cell_is_empty(obj, idx))
        {
            obj->press_idx = -1;
            if (obj->clicked_cb != NULL)
                obj->clicked_cb(obj, (uint8_t)idx, obj->labels[idx]);
            return 1U;
        }
        obj->press_idx = -1;
        return 0U;

    default:
        break;
    }
    return 0U;
}

static const we_class_t _btnmatrix_class = {
    .draw_cb = _btnmatrix_draw_cb,
    .event_cb = _btnmatrix_event_cb,
    .set_pos_cb = NULL
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化按键矩阵控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 外接矩形左上角 X（屏幕绝对坐标）。
 * @param y 外接矩形左上角 Y。
 * @param w 外接矩形宽度（像素）。
 * @param h 外接矩形高度（像素）。
 * @param labels 键名数组（调用方持有，行优先 rows*cols 个）。
 * @param rows 行数。
 * @param cols 列数。
 * @return 无。
 */
void we_btnmatrix_obj_init(we_btnmatrix_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           const char *const *labels, uint8_t rows, uint8_t cols, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_btnmatrix_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->labels = labels;
    obj->rows = rows;
    obj->cols = cols;

    obj->bg_color = RGB888TODEV(58, 66, 82);
    obj->bg_press_color = RGB888TODEV(64, 152, 231);
    obj->text_color = RGB888TODEV(239, 243, 250);
    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;

    obj->clicked_cb = NULL;
    obj->press_idx = -1;
    obj->pressed = 0U;
    obj->opacity = 255U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 注册按键点击回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_btnmatrix_set_clicked_cb(we_btnmatrix_obj_t *obj, we_btnmatrix_clicked_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->clicked_cb = cb;
}

/**
 * @brief 设置三项配色：普通底色 / 按压底色 / 文字色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param bg 普通按键底色。
 * @param bg_press 按压按键底色。
 * @param text 键名文字色。
 * @return 无。
 */
void we_btnmatrix_set_colors(we_btnmatrix_obj_t *obj, colour_t bg,
                             colour_t bg_press, colour_t text)
{
    if (obj == NULL)
        return;
    if (_bm_colour_eq(obj->bg_color, bg) &&
        _bm_colour_eq(obj->bg_press_color, bg_press) &&
        _bm_colour_eq(obj->text_color, text))
        return;

    obj->bg_color = bg;
    obj->bg_press_color = bg_press;
    obj->text_color = text;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_btnmatrix_set_opacity(we_btnmatrix_obj_t *obj, uint8_t opacity)
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
void we_btnmatrix_obj_delete(we_btnmatrix_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
