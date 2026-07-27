#include "we_widget_table.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * table —— 表头固定 + 数据行滚动的简易表格（preview 孵化区）
 *
 * 结构：表头行（底色块 + 逐列文本，固定顶部）+ 数据行区（PFB 收窄裁剪，
 * 拖拽垂直滚动，硬夹紧无惯性）+ 斑马纹 + 1px 低透明度网格线 +
 * 内容超高时右缘常显滚动条。
 *
 * 单元格文本左对齐，每列各做一次 PFB 收窄（列边界 ∩ 数据区/表头区），
 * 超宽文本在列边界处被硬裁剪，不会溢出到相邻列。
 * -------------------------------------------------------------------------- */

/* PFB 收窄现场保存（save/restore 套路，参考 list/scroll_panel） */
typedef struct
{
    we_area_t area;
    uint16_t y_start;
    uint16_t y_end;
    colour_t *gram;
} _table_pfb_save_t;

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（按当前色深比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _tbl_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 将透明度按控件整体不透明度缩放。
 * @param a 传入：原始透明度（0~255）。
 * @param opacity 传入：控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _tbl_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/**
 * @brief 保存当前 PFB 裁剪现场。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param sv 传出：现场保存结构。
 * @return 无。
 */
static void _table_pfb_save(const we_lcd_t *lcd, _table_pfb_save_t *sv)
{
    sv->area = lcd->pfb_area;
    sv->y_start = lcd->pfb_y_start;
    sv->y_end = lcd->pfb_y_end;
    sv->gram = lcd->pfb_gram;
}

/**
 * @brief 恢复 PFB 裁剪现场。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param sv 传入：之前保存的现场。
 * @return 无。
 */
static void _table_pfb_restore(we_lcd_t *lcd, const _table_pfb_save_t *sv)
{
    lcd->pfb_area = sv->area;
    lcd->pfb_y_start = sv->y_start;
    lcd->pfb_y_end = sv->y_end;
    lcd->pfb_gram = sv->gram;
}

/**
 * @brief 从"原始现场"出发把 PFB 窗口收窄到指定矩形（非嵌套，可反复调用）。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param sv 传入：原始现场（收窄基准，始终相对它计算）。
 * @param x0 传入：目标矩形左缘（屏幕绝对坐标）。
 * @param y0 传入：目标矩形上缘。
 * @param x1 传入：目标矩形右缘（含）。
 * @param y1 传入：目标矩形下缘（含）。
 * @return 1 收窄后窗口非空，0 无交集（本次不必绘制）。
 */
static uint8_t _table_clip_to(we_lcd_t *lcd, const _table_pfb_save_t *sv,
                              int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t cx0 = WE_MAX((int16_t)sv->area.x0, x0);
    int16_t cy0 = WE_MAX((int16_t)sv->y_start, y0);
    int16_t cx1 = WE_MIN((int16_t)sv->area.x1, x1);
    int16_t cy1 = WE_MIN((int16_t)sv->y_end, y1);

    if (cx0 > cx1 || cy0 > cy1)
        return 0U;

    lcd->pfb_area.x0 = (uint16_t)cx0;
    lcd->pfb_area.x1 = (uint16_t)cx1;
    lcd->pfb_y_start = (uint16_t)cy0;
    lcd->pfb_y_end = (uint16_t)cy1;
    lcd->pfb_gram = sv->gram + (int32_t)(cy0 - (int16_t)sv->y_start) * lcd->pfb_width +
                    (cx0 - (int16_t)sv->area.x0);
    return 1U;
}

/**
 * @brief 数据行区高度（控件高减去表头行，最小 0）。
 * @param obj 传入：控件对象指针。
 * @return 数据区高度（像素）。
 */
static int32_t _table_data_h(const we_table_obj_t *obj)
{
    int32_t d = (int32_t)obj->base.h - (int32_t)obj->row_h;
    return (d > 0) ? d : 0;
}

/**
 * @brief 数据行内容总高度（数据行数 × 行高，不含表头）。
 * @param obj 传入：控件对象指针。
 * @return 内容总高（像素）。
 */
static int32_t _table_content_h(const we_table_obj_t *obj)
{
    if (obj->row_cnt <= 1U)
        return 0;
    return (int32_t)(obj->row_cnt - 1U) * (int32_t)obj->row_h;
}

/**
 * @brief 最大可滚动像素（内容不溢出时为 0）。
 * @param obj 传入：控件对象指针。
 * @return 最大 scroll_px（>= 0）。
 */
static int32_t _table_max_scroll(const we_table_obj_t *obj)
{
    int32_t m = _table_content_h(obj) - _table_data_h(obj);
    return (m > 0) ? m : 0;
}

/**
 * @brief 将 scroll_px 硬夹紧到 [0, max] 后应用并按需标脏。
 * @param obj 传入：控件对象指针。
 * @param new_scroll 传入：目标滚动像素。
 * @return 无。
 */
static void _table_apply_scroll(we_table_obj_t *obj, int32_t new_scroll)
{
    int32_t max_scroll = _table_max_scroll(obj);

    if (new_scroll < 0)
        new_scroll = 0;
    if (new_scroll > max_scroll)
        new_scroll = max_scroll;

    if (new_scroll == obj->scroll_px)
        return;

    obj->scroll_px = new_scroll;
    we_obj_invalidate((we_obj_t *)obj); /* preview：整控件包围盒标脏 */
}

/**
 * @brief 按当前列权重重算列边界缓存 col_edge[0..col_cnt]。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note col_edge[i] = w × 权重前缀和 / 权重总和（整数比例分配，
 *       末列自动吸收除法余数像素）。
 */
static void _table_recalc_edges(we_table_obj_t *obj)
{
    int32_t total = 0;
    int32_t acc = 0;
    uint8_t i;

    for (i = 0U; i < obj->col_cnt; i++)
        total += (int32_t)obj->col_weight[i];
    if (total <= 0)
        total = 1;

    obj->col_edge[0] = 0;
    for (i = 0U; i < obj->col_cnt; i++)
    {
        acc += (int32_t)obj->col_weight[i];
        obj->col_edge[i + 1U] = (int16_t)(((int32_t)obj->base.w * acc) / total);
    }
}

/**
 * @brief 读取单元格文本（带越界/空数组防护）。
 * @param obj 传入：控件对象指针。
 * @param row 传入：行号（0 = 表头行）。
 * @param col 传入：列号。
 * @return 单元格文本指针，越界或未绑定返回 NULL。
 */
static const char *_table_cell_text(const we_table_obj_t *obj, uint16_t row, uint8_t col)
{
    if (obj->cells == NULL || row >= obj->row_cnt || col >= obj->col_cnt)
        return NULL;
    return obj->cells[(uint32_t)row * (uint32_t)obj->col_cnt + (uint32_t)col];
}

/**
 * @brief 在单元格内左对齐 + 垂直居中绘制文本（PFB 窗口已收窄到该列）。
 * @param obj 传入：控件对象指针。
 * @param text 传入：单元格文本（NULL 直接返回）。
 * @param cell_x 传入：单元格左缘 X（屏幕绝对坐标）。
 * @param row_y 传入：行顶部 Y。
 * @param color 传入：文字颜色。
 * @return 无。
 */
static void _table_draw_cell_text(we_table_obj_t *obj, const char *text,
                                  int16_t cell_x, int16_t row_y, colour_t color)
{
    int8_t y_top;
    int8_t y_bot;
    int16_t ty;

    if (text == NULL)
        return;

    we_get_text_bbox(obj->font, text, &y_top, &y_bot);
    ty = (int16_t)(row_y + (int16_t)obj->row_h / 2 - (y_top + y_bot) / 2);
    we_draw_string(obj->base.lcd, (int16_t)(cell_x + WE_TABLE_CELL_PAD_X), ty,
                   obj->font, text, color, obj->opacity);
}

/**
 * @brief 绘制右缘常显滚动条（仅内容超高时显示，覆盖数据行区）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 滑块高按"数据区高/内容高"比例（下限 8px），位置按滚动进度插值。
 */
static void _table_draw_scrollbar(we_table_obj_t *obj)
{
    int32_t content_h = _table_content_h(obj);
    int32_t max_scroll = _table_max_scroll(obj);
    int32_t data_h = _table_data_h(obj);
    int16_t track_x;
    int16_t track_y0;
    int16_t thumb_h;
    int16_t thumb_y;

    if (max_scroll <= 0 || content_h <= 0 || data_h <= 0)
        return; /* 内容未溢出，无需滚动条 */

    track_x = (int16_t)(obj->base.x + obj->base.w - WE_TABLE_SB_MARGIN - WE_TABLE_SB_WIDTH);
    track_y0 = (int16_t)(obj->base.y + (int16_t)obj->row_h);

    thumb_h = (int16_t)((data_h * data_h) / content_h);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > (int16_t)data_h)
        thumb_h = (int16_t)data_h;

    thumb_y = (int16_t)(track_y0 + (data_h - thumb_h) * obj->scroll_px / max_scroll);

    we_draw_round_rect_analytic_fill(obj->base.lcd, track_x, thumb_y,
                                     (uint16_t)WE_TABLE_SB_WIDTH, (uint16_t)thumb_h,
                                     (uint16_t)(WE_TABLE_SB_WIDTH / 2), obj->sb_color,
                                     _tbl_scale_opa(WE_TABLE_SB_OPA, obj->opacity));
}

/* --------------------------------------------------------------------------
 * 绘制回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：表头 + 数据行区（斑马纹/横线/逐列文本）+ 竖线 + 滚动条。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 * @note 表头文本与数据单元格文本均按"列边界 ∩ 所在行区"做 PFB 收窄，
 *       超宽文本在列边界处硬截断；收窄始终从进入时保存的原始现场推导，
 *       不存在嵌套叠加。
 */
static void _table_draw_cb(void *ptr)
{
    we_table_obj_t *obj = (we_table_obj_t *)ptr;
    we_lcd_t *lcd;
    _table_pfb_save_t sv;
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
    int16_t rh;
    int16_t hdr_h;
    int16_t data_y0;
    int32_t data_rows;
    int32_t first;
    int32_t top_ofs;
    uint8_t c;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->font == NULL || obj->row_h == 0U ||
        obj->base.w <= 0 || obj->base.h <= 0)
        return;

    rh = (int16_t)obj->row_h;
    x0 = obj->base.x;
    y0 = obj->base.y;
    x1 = (int16_t)(x0 + obj->base.w - 1);
    y1 = (int16_t)(y0 + obj->base.h - 1);
    hdr_h = (rh < obj->base.h) ? rh : obj->base.h;
    data_y0 = (int16_t)(y0 + hdr_h);
    data_rows = (obj->row_cnt > 1U) ? (int32_t)(obj->row_cnt - 1U) : 0;

    /* 1. 表头行底色（固定顶部，不随滚动） */
    we_fill_rect(lcd, x0, y0, (uint16_t)obj->base.w, (uint16_t)hdr_h,
                 obj->head_bg_color, obj->opacity);

    _table_pfb_save(lcd, &sv);

    /* 2. 表头单元格文本（第 0 行，逐列收窄裁剪） */
    if (obj->cells != NULL && obj->row_cnt > 0U)
    {
        for (c = 0U; c < obj->col_cnt; c++)
        {
            int16_t cell_x = (int16_t)(x0 + obj->col_edge[c]);
            int16_t cell_x1 = (int16_t)(x0 + obj->col_edge[c + 1U] - 2);

            if (!_table_clip_to(lcd, &sv, cell_x, y0, cell_x1, (int16_t)(y0 + hdr_h - 1)))
                continue;
            _table_draw_cell_text(obj, _table_cell_text(obj, 0U, c),
                                  cell_x, y0, obj->head_text_color);
        }
        _table_pfb_restore(lcd, &sv);
    }

    /* 3. 数据行区（表头以下，滚动内容） */
    if (data_rows > 0 && data_y0 <= y1)
    {
        first = obj->scroll_px / rh;   /* 首个（可能半露）数据行 */
        top_ofs = obj->scroll_px % rh; /* 首行被裁掉的顶部像素 */

        /* 3a. 斑马纹 + 行底 1px 分隔横线（数据区整宽收窄） */
        if (_table_clip_to(lcd, &sv, x0, data_y0, x1, y1))
        {
            int16_t iy = (int16_t)(data_y0 - (int16_t)top_ofs);
            int32_t ri;

            for (ri = first; ri < data_rows && iy <= y1; ri++, iy = (int16_t)(iy + rh))
            {
                if (obj->zebra != 0U && (ri & 1) != 0)
                {
                    we_fill_rect(lcd, x0, iy, (uint16_t)obj->base.w, (uint16_t)rh,
                                 obj->zebra_color, obj->opacity);
                }
                if (ri < data_rows - 1)
                {
                    we_fill_rect(lcd, x0, (int16_t)(iy + rh - 1),
                                 (uint16_t)obj->base.w, 1U, obj->grid_color,
                                 _tbl_scale_opa(WE_TABLE_GRID_OPA, obj->opacity));
                }
            }
        }

        /* 3b. 数据单元格文本：每列收窄一次，列内逐可见行绘制 */
        for (c = 0U; c < obj->col_cnt; c++)
        {
            int16_t cell_x = (int16_t)(x0 + obj->col_edge[c]);
            int16_t cell_x1 = (int16_t)(x0 + obj->col_edge[c + 1U] - 2);
            int16_t iy;
            int32_t ri;

            if (!_table_clip_to(lcd, &sv, cell_x, data_y0, cell_x1, y1))
                continue;

            iy = (int16_t)(data_y0 - (int16_t)top_ofs);
            for (ri = first; ri < data_rows && iy <= y1; ri++, iy = (int16_t)(iy + rh))
            {
                _table_draw_cell_text(obj, _table_cell_text(obj, (uint16_t)(ri + 1), c),
                                      cell_x, iy, obj->cell_text_color);
            }
        }
        _table_pfb_restore(lcd, &sv);
    }

    /* 4. 网格竖线（整控件高）+ 表头下沿线（稍强分隔） */
    for (c = 1U; c < obj->col_cnt; c++)
    {
        we_fill_rect(lcd, (int16_t)(x0 + obj->col_edge[c]), y0, 1U, (uint16_t)obj->base.h,
                     obj->grid_color, _tbl_scale_opa(WE_TABLE_GRID_OPA, obj->opacity));
    }
    we_fill_rect(lcd, x0, (int16_t)(y0 + hdr_h - 1), (uint16_t)obj->base.w, 1U,
                 obj->grid_color, _tbl_scale_opa(120U, obj->opacity));

    /* 5. 内容超高时叠加右缘常显滚动条 */
    _table_draw_scrollbar(obj);
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调：拖拽垂直滚动（硬夹紧，无惯性）。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 1 表示消费事件，0 表示穿透。
 */
static uint8_t _table_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_table_obj_t *obj = (we_table_obj_t *)ptr;

    if (obj == NULL || data == NULL)
        return 0U;

    if (event == WE_EVENT_PRESSED)
    {
        obj->tracking = 1U;
        obj->dragging = 0U;
        obj->press_y = data->y;
        obj->press_scroll = obj->scroll_px;
        return 1U;
    }

    if (!obj->tracking)
        return 1U; /* 无有效按压序列，仅消费防穿透 */

    if (event == WE_EVENT_STAY)
    {
        int16_t dy_total = (int16_t)(data->y - obj->press_y);
        int16_t ady = (dy_total >= 0) ? dy_total : (int16_t)(-dy_total);

        if (!obj->dragging && ady >= WE_TABLE_DRAG_THRESHOLD)
            obj->dragging = 1U;

        if (obj->dragging)
        {
            /* 内容跟手：手指下移 → 内容下移 → scroll_px 减小 */
            _table_apply_scroll(obj, obj->press_scroll - (int32_t)dy_total);
        }
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        obj->tracking = 0U;
        obj->dragging = 0U;
        return 1U;
    }

    /* CLICKED / SWIPE：无单元格点击语义，仅消费防穿透 */
    return 1U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_TABLE_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 进出编辑态，编辑态上下键按行高步进滚动数据区。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 内容不溢出（无可滚动量）时拒绝聚焦，避免成为无操作的死停靠点；
 *       BACK 不处理，交焦点管理器退出编辑态。
 */
static uint8_t _table_key_cb(void *ptr, uint8_t key_evt)
{
    we_table_obj_t *obj = (we_table_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U && _table_max_scroll(obj) > 0) ? 1U : 0U;
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
        if (obj->row_h != 0U)
        {
            int32_t step = (key_evt == WE_KEY_DOWN) ? (int32_t)obj->row_h
                                                    : -(int32_t)obj->row_h;

            _table_apply_scroll(obj, obj->scroll_px + step);
        }
        return 1U;
    default:
        return 0U;
    }
}
#endif

static const we_class_t _table_class = {
    .draw_cb = _table_draw_cb,
    .event_cb = _table_event_cb,
    .set_pos_cb = NULL, /* 通用移动逻辑（旧区标脏 + 新区标脏）已足够 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_TABLE_USE_KEY == 1)
    .key_cb = _table_key_cb,
#endif
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化表格控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param h 传入：控件高度（像素）。
 * @param col_cnt 传入：列数，钳制到 [1, WE_TABLE_COL_MAX]。
 * @return 无。
 */
void we_table_obj_init(we_table_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, int16_t w, int16_t h, uint8_t col_cnt, const unsigned char *font)
{
    uint16_t line_h;
    uint8_t i;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_table_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    if (col_cnt < 1U)
        col_cnt = 1U;
    if (col_cnt > (uint8_t)WE_TABLE_COL_MAX)
        col_cnt = (uint8_t)WE_TABLE_COL_MAX;
    obj->col_cnt = col_cnt;

    obj->cells = NULL;
    obj->row_cnt = 0U;
    obj->zebra = 1U;

    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;
    line_h = we_font_get_line_height(obj->font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->row_h = (uint16_t)(line_h + 2U * WE_TABLE_ROW_PAD);

    for (i = 0U; i < (uint8_t)WE_TABLE_COL_MAX; i++)
        obj->col_weight[i] = 1U; /* 默认全列等分 */
    _table_recalc_edges(obj);

    obj->head_bg_color = RGB888TODEV(48, 62, 86);
    obj->head_text_color = RGB888TODEV(236, 241, 248);
    obj->cell_text_color = RGB888TODEV(206, 214, 228);
    obj->grid_color = RGB888TODEV(220, 228, 242);
    obj->zebra_color = RGB888TODEV(30, 38, 52);
    obj->sb_color = RGB888TODEV(200, 210, 226);
    obj->opacity = 255U;

    obj->scroll_px = 0;
    obj->tracking = 0U;
    obj->dragging = 0U;
    obj->press_y = 0;
    obj->press_scroll = 0;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 绑定单元格文本数组（控件只保存指针，不复制内容）。
 * @param obj 传入：控件对象指针。
 * @param cells 传入：行优先一维字符串指针数组（含表头行，调用方持有）。
 * @param row_cnt 传入：总行数（含表头行）。
 * @return 无。
 */
void we_table_set_cells(we_table_obj_t *obj, const char *const *cells, uint16_t row_cnt)
{
    if (obj == NULL)
        return;
    if (obj->cells == cells && obj->row_cnt == row_cnt)
        return;

    obj->cells = cells;
    obj->row_cnt = (cells != NULL) ? row_cnt : 0U;
    obj->scroll_px = 0;
    obj->tracking = 0U;
    obj->dragging = 0U;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置列宽权重（拷入控件定长数组，按占比分配列宽）。
 * @param obj 传入：控件对象指针。
 * @param weights 传入：权重数组（长度 >= 列数，元素 0 按 1 处理），
 *                NULL = 全列等分。
 * @return 无。
 */
void we_table_set_col_weights(we_table_obj_t *obj, const uint8_t *weights)
{
    uint8_t sanitized[WE_TABLE_COL_MAX];
    uint8_t changed = 0U;
    uint8_t i;

    if (obj == NULL)
        return;

    for (i = 0U; i < obj->col_cnt; i++)
    {
        uint8_t v = (weights != NULL) ? weights[i] : 1U;

        sanitized[i] = (v != 0U) ? v : 1U;
        if (sanitized[i] != obj->col_weight[i])
            changed = 1U;
    }
    if (!changed)
        return;

    for (i = 0U; i < obj->col_cnt; i++)
        obj->col_weight[i] = sanitized[i];
    _table_recalc_edges(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置行高（像素，表头与数据行同高），滚动偏移重新夹紧。
 * @param obj 传入：控件对象指针。
 * @param row_h 传入：新行高（0 时忽略）。
 * @return 无。
 */
void we_table_set_row_h(we_table_obj_t *obj, uint16_t row_h)
{
    int32_t max_scroll;

    if (obj == NULL || row_h == 0U || obj->row_h == row_h)
        return;

    obj->row_h = row_h;

    max_scroll = _table_max_scroll(obj);
    if (obj->scroll_px > max_scroll)
        obj->scroll_px = max_scroll;

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置五项配色（全部未变时直接返回）。
 * @param obj 传入：控件对象指针。
 * @param head_bg 传入：表头行底色。
 * @param head_text 传入：表头文字色。
 * @param cell_text 传入：数据单元格文字色。
 * @param grid 传入：网格线颜色。
 * @param zebra 传入：斑马纹行底色。
 * @return 无。
 */
void we_table_set_colors(we_table_obj_t *obj, colour_t head_bg, colour_t head_text,
                         colour_t cell_text, colour_t grid, colour_t zebra)
{
    if (obj == NULL)
        return;
    if (_tbl_colour_eq(obj->head_bg_color, head_bg) &&
        _tbl_colour_eq(obj->head_text_color, head_text) &&
        _tbl_colour_eq(obj->cell_text_color, cell_text) &&
        _tbl_colour_eq(obj->grid_color, grid) &&
        _tbl_colour_eq(obj->zebra_color, zebra))
        return;

    obj->head_bg_color = head_bg;
    obj->head_text_color = head_text;
    obj->cell_text_color = cell_text;
    obj->grid_color = grid;
    obj->zebra_color = zebra;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 斑马纹隔行着色开关（值未变时直接返回）。
 * @param obj 传入：控件对象指针。
 * @param en 传入：1 开启，0 关闭。
 * @return 无。
 */
void we_table_set_zebra(we_table_obj_t *obj, uint8_t en)
{
    en = (en != 0U) ? 1U : 0U;
    if (obj == NULL || obj->zebra == en)
        return;
    obj->zebra = en;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 单元格内容原地更新后的手动重绘接口（整控件包围盒标脏）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_table_refresh(we_table_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 传入：控件对象指针。
 * @param opacity 传入：不透明度（0~255）。
 * @return 无。
 */
void we_table_set_opacity(we_table_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除表格控件并从对象链表移除（无动画节点，无需摘链）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_table_obj_delete(we_table_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
