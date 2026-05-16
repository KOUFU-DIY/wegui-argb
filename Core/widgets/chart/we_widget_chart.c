#include "we_widget_chart.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 波形绘制算法说明
 *
 * 当前波形主体覆盖区 + 柔边 alpha 递减的整体思路参考自 Arm-2D，
 * 但这里按 WeGui 的环形缓冲、脏矩形、PFB 分块裁剪和整数坐标体系重写。
 * -------------------------------------------------------------------------- */

/**
 * @brief 将屏幕 Y 坐标限制在控件绘制区域内。
 * @param sy 待裁剪的屏幕 Y 坐标。
 * @param by 绘制区域起始 Y 坐标。
 * @param bh 绘制区域高度（像素）。
 * @return 返回对应计算结果。
 */
static __inline int32_t _chart_clamp_sy(int32_t sy, int16_t by, int16_t bh)
{
    if (sy < (int32_t)by)
        sy = (int32_t)by;
    if (sy > (int32_t)(by + bh - 1))
        sy = (int32_t)(by + bh - 1);
    return sy;
}

/**
 * @brief 将采样值映射为屏幕 Y 坐标并做边界裁剪。
 * @param value 输入采样值。
 * @param by 绘制区域起始 Y 坐标。
 * @param bh 绘制区域高度（像素）。
 * @return 返回对应计算结果。
 */
static __inline int32_t _chart_value_to_sy(int16_t value, int16_t by, int16_t bh)
{
    return _chart_clamp_sy((int32_t)(by + bh / 2) - (int32_t)value, by, bh);
}

/**
 * @brief 计算当前可见列数。
 * @param obj 目标控件对象指针。
 * @param bw 绘制区域宽度（像素）。
 * @return 返回对应计算结果。
 */
static __inline uint16_t _chart_visible_count(const we_chart_obj_t *obj, int16_t bw)
{
    uint16_t visible_cap = (bw > 0) ? (uint16_t)bw : 0U;
    return (obj->data_count < visible_cap) ? obj->data_count : visible_cap;
}

/**
 * @brief 计算可见窗口在环形缓冲中的起始索引。
 * @param obj 目标控件对象指针。
 * @param visible_count 数量值。
 * @param bw 绘制区域宽度（像素）。
 * @return 返回对应计算结果。
 */
static __inline uint16_t _chart_visible_start_idx(const we_chart_obj_t *obj, uint16_t visible_count, int16_t bw)
{
    uint16_t base = (uint16_t)((obj->data_head + (uint32_t)obj->data_cap - obj->data_count) % obj->data_cap);
    uint16_t skip = (obj->data_count > (uint16_t)bw) ? (uint16_t)(obj->data_count - (uint16_t)bw) : 0U;
    if (visible_count == 0U)
        return base;
    return (uint16_t)((base + skip) % obj->data_cap);
}

/**
 * @brief 将可见列偏移转换为环形缓冲索引。
 * @param obj 目标控件对象指针。
 * @param idx0 索引值。
 * @param col_idx 索引值。
 * @return 返回对应计算结果。
 */
static __inline uint16_t _chart_value_at_visible(const we_chart_obj_t *obj, uint16_t idx0, uint16_t col_idx)
{
    return (uint16_t)((idx0 + col_idx) % obj->data_cap);
}

/**
 * @brief 读取辅助计算结果。
 * @return 返回对应计算结果。
 */
static __inline uint8_t _chart_get_aa_height(void)
{
#if (WE_CHART_AA_MAX > 0)
    return (uint8_t)WE_CHART_AA_MAX;
#else
    return 0U;
#endif
}

/* --------------------------------------------------------------------------
 * 编译期柔边 base_alpha 查表
 *
 * 把 _chart_get_aa_alpha 里依赖 aa_h 的除法（线性 /aa_h，二次 /(aa_h*aa_h)）
 * 全部搬到编译期，运行期只剩一次表读 + 一次乘移。
 * 没有硬件除法的 Cortex-M0（如 STM32F030）受益最大。
 *
 * 表项含义：当 opacity=255 时，edge=i 的 base_alpha。
 * 当前最多支持 WE_CHART_AA_MAX = 8；需要更厚柔边时扩展下面的 #if 即可。
 * -------------------------------------------------------------------------- */
#if (WE_CHART_AA_MAX > 0)

#if (WE_CHART_AA_MAX > 8)
#error "WE_CHART_AA_MAX 超出编译期表上限 (max 8)，请扩展 k_chart_aa_table 或减小该值"
#endif

#if (WE_CFG_CHART_AA_CURVE == 0)
/* 线性：base_alpha = 255 * (aa_h - i) / aa_h */
#define WE_CHART_AA_VAL(i) \
    ((uint8_t)(255U * (WE_CHART_AA_MAX - (i)) / WE_CHART_AA_MAX))
#else
/* 二次：base_alpha = 255 * (aa_h - i)^2 / aa_h^2 */
#define WE_CHART_AA_VAL(i)                                                \
    ((uint8_t)(255UL * (WE_CHART_AA_MAX - (i)) * (WE_CHART_AA_MAX - (i))  \
               / ((unsigned long)WE_CHART_AA_MAX * WE_CHART_AA_MAX)))
#endif

static const uint8_t k_chart_aa_table[WE_CHART_AA_MAX] = {
    WE_CHART_AA_VAL(0),
#if (WE_CHART_AA_MAX > 1)
    WE_CHART_AA_VAL(1),
#endif
#if (WE_CHART_AA_MAX > 2)
    WE_CHART_AA_VAL(2),
#endif
#if (WE_CHART_AA_MAX > 3)
    WE_CHART_AA_VAL(3),
#endif
#if (WE_CHART_AA_MAX > 4)
    WE_CHART_AA_VAL(4),
#endif
#if (WE_CHART_AA_MAX > 5)
    WE_CHART_AA_VAL(5),
#endif
#if (WE_CHART_AA_MAX > 6)
    WE_CHART_AA_VAL(6),
#endif
#if (WE_CHART_AA_MAX > 7)
    WE_CHART_AA_VAL(7),
#endif
};

#endif /* WE_CHART_AA_MAX > 0 */

/**
 * @brief 根据柔边层级与整体透明度计算当前像素 alpha。
 * @param aa_h 柔边总层数。
 * @param edge_idx 当前像素所在的柔边层级索引。
 * @param opacity 整体不透明度（0~255）。
 * @return 当前像素应使用的 alpha 值。
 */
static __inline uint8_t _chart_get_aa_alpha(uint8_t aa_h, uint8_t edge_idx, uint8_t opacity)
{
    if (aa_h == 0U || edge_idx >= aa_h || opacity == 0U)
        return 0U;

#if (WE_CHART_AA_MAX > 0)
    return (uint8_t)(((uint16_t)k_chart_aa_table[edge_idx] * opacity) >> 8);
#else
    (void)edge_idx;
    return 0U;
#endif
}

/**
 * @brief 读取辅助计算结果。
 * @param col_idx 索引值。
 * @param idx0 索引值。
 * @param obj 目标控件对象指针。
 * @param by 绘制区域起始 Y 坐标。
 * @param bh 绘制区域高度（像素）。
 * @return 返回对应计算结果。
 */
static __inline int32_t _get_sy(int32_t col_idx, uint16_t idx0,
                                 const we_chart_obj_t *obj,
                                 int16_t by, int16_t bh)
{
    uint16_t ri = _chart_value_at_visible(obj, idx0, (uint16_t)col_idx);
    return _chart_value_to_sy(obj->data[ri], by, bh);
}

/**
 * @brief 计算脏区并发起局部重绘请求。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _chart_invalidate_full(we_chart_obj_t *obj)
{
    if (!obj)
        return;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 累积两点连线在当前列的垂直覆盖区间。
 * @param y0 起始 Y 坐标。
 * @param y1 结束 Y 坐标。
 * @param value_a 起点采样值。
 * @param value_b 终点采样值。
 * @param by 绘制区域起始 Y 坐标。
 * @param bh 绘制区域高度（像素）。
 * @param stroke 线宽（像素）。
 * @return 无。
 */
static void _chart_accum_span(int32_t *y0, int32_t *y1,
                              int32_t value_a, int32_t value_b,
                              int16_t by, int16_t bh, int32_t stroke)
{
    uint8_t aa_h = _chart_get_aa_height();
    int32_t half_stroke = stroke >> 1;
    int32_t y_cur = _chart_value_to_sy((int16_t)value_a, by, bh);
    int32_t y_nxt = _chart_value_to_sy((int16_t)value_b, by, bh);
    int32_t iCurTop = y_cur - half_stroke;
    int32_t iNxtTop = y_nxt - half_stroke;
    int32_t span_top = (iCurTop < iNxtTop) ? iCurTop : iNxtTop;
    int32_t span_bot = ((iCurTop > iNxtTop) ? iCurTop : iNxtTop) + stroke - 1;

    span_top -= (int32_t)aa_h;
    span_bot += (int32_t)aa_h;

    if (span_top < (int32_t)by)
        span_top = (int32_t)by;
    if (span_bot > (int32_t)(by + bh - 1))
        span_bot = (int32_t)(by + bh - 1);

    if (span_top < *y0)
        *y0 = span_top;
    if (span_bot > *y1)
        *y1 = span_bot;
}

/**
 * @brief 向控件数据队列推入一个新采样点。
 * @param obj 目标控件对象指针。
 * @param new_value 待写入的新值。
 * @return 无。
 */
static void _chart_invalidate_push(we_chart_obj_t *obj, int16_t new_value)
{
    int16_t bx;
    int16_t by;
    int16_t bw;
    int16_t bh;
    uint16_t old_count;
    uint16_t new_count;
    uint16_t old_idx0;
    uint16_t new_idx0;
    int32_t old_sc_start;
    int32_t new_sc_start;
    int32_t stroke;
    int32_t block_w;
    int32_t col_start;
    int32_t count_delta;

    if (!obj || !obj->data || obj->opacity == 0U)
        return;

#if (WE_CFG_CHART_DIRTY_MODE == 0)
    (void)new_value;
    _chart_invalidate_full(obj);
    return;
#else
    bx = obj->base.x;
    by = obj->base.y;
    bw = obj->base.w;
    bh = obj->base.h;
    if (bw <= 0 || bh <= 0)
        return;

    old_count = _chart_visible_count(obj, bw);
    new_count = (obj->data_count < obj->data_cap) ? (uint16_t)(old_count + 1U) : old_count;
    if (new_count > (uint16_t)bw)
        new_count = (uint16_t)bw;

    old_idx0 = _chart_visible_start_idx(obj, old_count, bw);
    new_idx0 = (new_count > 0U)
                 ? (uint16_t)(((obj->data_head + 1U) + (uint32_t)obj->data_cap - new_count) % obj->data_cap)
                 : obj->data_head;
    old_sc_start = (int32_t)bw - (int32_t)old_count;
    new_sc_start = (int32_t)bw - (int32_t)new_count;
    count_delta = (int32_t)new_count - (int32_t)old_count;
    stroke = (obj->stroke > 0U) ? (int32_t)obj->stroke : 2;
    block_w = (WE_CHART_DIRTY_BLOCK_W > 0) ? WE_CHART_DIRTY_BLOCK_W : 1;

    for (col_start = 0; col_start < (int32_t)new_count; col_start += block_w)
    {
        int32_t col_end = col_start + block_w - 1;
        int32_t y0 = (int32_t)(by + bh - 1);
        int32_t y1 = (int32_t)by;
        uint8_t has_span = 0U;

        if (col_end >= (int32_t)new_count)
            col_end = (int32_t)new_count - 1;

        for (int32_t col = col_start; col <= col_end; col++)
        {
            int32_t old_col = col - count_delta;

            if (old_col >= 0 && old_col < (int32_t)old_count)
            {
                uint16_t ri_old_a = _chart_value_at_visible(obj, old_idx0, (uint16_t)old_col);
                uint16_t ri_old_b = (old_col + 1 < (int32_t)old_count)
                                      ? _chart_value_at_visible(obj, old_idx0, (uint16_t)(old_col + 1))
                                      : ri_old_a;
                _chart_accum_span(&y0, &y1, obj->data[ri_old_a], obj->data[ri_old_b], by, bh, stroke);
                has_span = 1U;
            }

            if (col < (int32_t)new_count)
            {
                uint16_t ri_new_a = _chart_value_at_visible(obj, new_idx0, (uint16_t)col);
                uint16_t ri_new_b = (col + 1 < (int32_t)new_count)
                                      ? _chart_value_at_visible(obj, new_idx0, (uint16_t)(col + 1))
                                      : ri_new_a;
                int32_t new_a = (ri_new_a == obj->data_head) ? new_value : obj->data[ri_new_a];
                int32_t new_b = (ri_new_b == obj->data_head) ? new_value : obj->data[ri_new_b];
                _chart_accum_span(&y0, &y1, new_a, new_b, by, bh, stroke);
                has_span = 1U;
            }
        }

        if (has_span && y0 <= y1)
        {
            int16_t rx = (int16_t)(bx + new_sc_start + col_start);
            int16_t rw = (int16_t)(col_end - col_start + 1);
            we_obj_invalidate_area((we_obj_t *)obj, rx, (int16_t)y0, rw, (int16_t)(y1 - y0 + 1));
        }
    }

    if (old_count != new_count && old_count > 0U)
    {
        int16_t rx = (int16_t)(bx + old_sc_start);
        int16_t rw = (int16_t)(new_sc_start - old_sc_start);
        if (rw > 0)
            we_obj_invalidate_area((we_obj_t *)obj, rx, by, rw, bh);
    }
#endif
}

/**
 * @brief 在当前 PFB 裁剪区内绘制局部内容。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y0 起始 Y 坐标。
 * @param y1 结束 Y 坐标。
 * @param color 目标颜色值。
 * @return 无。
 */
static void _chart_draw_grid_vline(we_lcd_t *lcd, int16_t x, int16_t y0, int16_t y1, colour_t color)
{
    int16_t draw_y0;
    int16_t draw_y1;
    colour_t *dst;

    if (!lcd)
        return;
    if (x < (int16_t)lcd->pfb_area.x0 || x > (int16_t)lcd->pfb_area.x1)
        return;
    if (y1 < (int16_t)lcd->pfb_y_start || y0 > (int16_t)lcd->pfb_y_end)
        return;

    draw_y0 = (y0 < (int16_t)lcd->pfb_y_start) ? (int16_t)lcd->pfb_y_start : y0;
    draw_y1 = (y1 > (int16_t)lcd->pfb_y_end) ? (int16_t)lcd->pfb_y_end : y1;
    dst = lcd->pfb_gram + (uint16_t)(draw_y0 - (int16_t)lcd->pfb_y_start) * lcd->pfb_width +
          (uint16_t)(x - (int16_t)lcd->pfb_area.x0);

    for (int16_t y = draw_y0; y <= draw_y1; y++)
    {
        *dst = color;
        dst += lcd->pfb_width;
    }
}

/**
 * @brief 在当前 PFB 裁剪区内绘制局部内容。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x0 起始 X 坐标。
 * @param x1 结束 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param color 目标颜色值。
 * @return 无。
 */
static void _chart_draw_grid_hline(we_lcd_t *lcd, int16_t x0, int16_t x1, int16_t y, colour_t color)
{
    int16_t draw_x0;
    int16_t draw_x1;
    colour_t *dst;

    if (!lcd)
        return;
    if (y < (int16_t)lcd->pfb_y_start || y > (int16_t)lcd->pfb_y_end)
        return;
    if (x1 < (int16_t)lcd->pfb_area.x0 || x0 > (int16_t)lcd->pfb_area.x1)
        return;

    draw_x0 = (x0 < (int16_t)lcd->pfb_area.x0) ? (int16_t)lcd->pfb_area.x0 : x0;
    draw_x1 = (x1 > (int16_t)lcd->pfb_area.x1) ? (int16_t)lcd->pfb_area.x1 : x1;
    dst = lcd->pfb_gram + (uint16_t)(y - (int16_t)lcd->pfb_y_start) * lcd->pfb_width +
          (uint16_t)(draw_x0 - (int16_t)lcd->pfb_area.x0);

    for (int16_t x = draw_x0; x <= draw_x1; x++)
    {
        *dst++ = color;
    }
}

/**
 * @brief 在当前 PFB 裁剪区内绘制局部内容。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param bx 绘制区域起始 X 坐标。
 * @param by 绘制区域起始 Y 坐标。
 * @param bw 绘制区域宽度（像素）。
 * @param bh 绘制区域高度（像素）。
 * @return 无。
 */
static void _chart_draw_grid(we_lcd_t *lcd, int16_t bx, int16_t by, int16_t bw, int16_t bh)
{
#if (WE_CHART_GRID_COLS > 0) || (WE_CHART_GRID_ROWS > 0)
    colour_t gc = RGB888TODEV(WE_CHART_GRID_R, WE_CHART_GRID_G, WE_CHART_GRID_B);
#endif

#if WE_CHART_GRID_COLS > 0
    for (uint8_t c = 1U; c < (uint8_t)WE_CHART_GRID_COLS; c++)
    {
        int16_t gx = (int16_t)(bx + (int32_t)bw * c / WE_CHART_GRID_COLS);
        _chart_draw_grid_vline(lcd, gx, by, (int16_t)(by + bh - 1), gc);
    }
#endif

#if WE_CHART_GRID_ROWS > 0
    for (uint8_t r = 1U; r < (uint8_t)WE_CHART_GRID_ROWS; r++)
    {
        int16_t gy = (int16_t)(by + (int32_t)bh * r / WE_CHART_GRID_ROWS);
        _chart_draw_grid_hline(lcd, bx, (int16_t)(bx + bw - 1), gy, gc);
    }
#endif
}

/**
 * @brief 在当前 PFB 裁剪区内绘制局部内容。
 * @param obj 目标控件对象指针。
 * @param cb 回调函数指针。
 * @param pfb_w PFB 行跨度（像素）。
 * @param pfb_y0 PFB 起始 Y 坐标。
 * @param clip_y0 本次绘制裁剪起始 Y。
 * @param clip_y1 本次绘制裁剪结束 Y。
 * @param span_top 波形主体顶部 Y。
 * @param span_bot 波形主体底部 Y。
 * @param color 目标颜色值。
 * @param a_solid 主体像素覆盖 alpha 值。
 * @return 无。
 */
static void _chart_draw_wave_body(we_chart_obj_t *obj,
                                  colour_t *cb,
                                  uint16_t pfb_w,
                                  int16_t pfb_y0,
                                  int32_t clip_y0,
                                  int32_t clip_y1,
                                  int32_t span_top,
                                  int32_t span_bot,
                                  colour_t color,
                                  uint8_t a_solid)
{
    if (a_solid <= 5U)
        return;

    {
        int32_t   y0  = (span_top < clip_y0) ? clip_y0 : span_top;
        int32_t   y1  = (span_bot > clip_y1) ? clip_y1 : span_bot;
        /* 起点乘法只算一次，循环里改成 += pfb_w 步进，省掉每行的一次乘法。 */
        colour_t *dst = cb + (uint16_t)(y0 - pfb_y0) * pfb_w;
        for (int32_t y = y0; y <= y1; y++)
        {
            *dst = (a_solid >= 250U) ? color : we_colour_blend(color, *dst, a_solid);
            dst += pfb_w;
        }
    }
}

/**
 * @brief 在当前 PFB 裁剪区内绘制局部内容。
 * @param obj 目标控件对象指针。
 * @param cb 回调函数指针。
 * @param pfb_w PFB 行跨度（像素）。
 * @param pfb_y0 PFB 起始 Y 坐标。
 * @param clip_y0 本次绘制裁剪起始 Y。
 * @param clip_y1 本次绘制裁剪结束 Y。
 * @param span_top 波形主体顶部 Y。
 * @param span_bot 波形主体底部 Y。
 * @param color 目标颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
static void _chart_draw_wave_feather(we_chart_obj_t *obj,
                                     colour_t *cb,
                                     uint16_t pfb_w,
                                     int16_t pfb_y0,
                                     int32_t clip_y0,
                                     int32_t clip_y1,
                                     int32_t span_top,
                                     int32_t span_bot,
                                     colour_t color,
                                     uint8_t opacity)
{
    uint8_t aa_h = _chart_get_aa_height();

    (void)obj;
    if (aa_h == 0U)
        return;

    for (uint8_t edge = 0U; edge < aa_h; edge++)
    {
        uint8_t a_edge = _chart_get_aa_alpha(aa_h, edge, opacity);
        int32_t y_top = span_top - 1 - (int32_t)edge;
        int32_t y_bot = span_bot + 1 + (int32_t)edge;

        if (a_edge > 0U && y_top >= clip_y0 && y_top <= clip_y1)
        {
            colour_t *dst = cb + (uint16_t)(y_top - pfb_y0) * pfb_w;
            *dst = we_colour_blend(color, *dst, a_edge);
        }
        if (a_edge > 0U && y_bot >= clip_y0 && y_bot <= clip_y1)
        {
            colour_t *dst = cb + (uint16_t)(y_bot - pfb_y0) * pfb_w;
            *dst = we_colour_blend(color, *dst, a_edge);
        }
    }
}

/**
 * @brief 在当前 PFB 裁剪区内绘制局部内容。
 * @param obj 目标控件对象指针。
 * @param cb 回调函数指针。
 * @param pfb_w PFB 行跨度（像素）。
 * @param pfb_y0 PFB 起始 Y 坐标。
 * @param clip_y0 本次绘制裁剪起始 Y。
 * @param clip_y1 本次绘制裁剪结束 Y。
 * @param span_top 波形主体顶部 Y。
 * @param span_bot 波形主体底部 Y。
 * @param color 目标颜色值。
 * @param a_solid 主体像素覆盖 alpha 值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
static void _chart_draw_wave_col(we_chart_obj_t *obj,
                                 colour_t *cb,
                                 uint16_t pfb_w,
                                 int16_t pfb_y0,
                                 int32_t clip_y0,
                                 int32_t clip_y1,
                                 int32_t span_top,
                                 int32_t span_bot,
                                 colour_t color,
                                 uint8_t a_solid,
                                 uint8_t opacity)
{
    _chart_draw_wave_body(obj, cb, pfb_w, pfb_y0, clip_y0, clip_y1, span_top, span_bot, color, a_solid);
    _chart_draw_wave_feather(obj, cb, pfb_w, pfb_y0, clip_y0, clip_y1, span_top, span_bot, color, opacity);
}

/* --------------------------------------------------------------------------
 * 波形图 v4：前瞻式 vspan + 可配置柔边
 *
 * 核心思路：
 *   每列用「当前列 y_cur」与「下一列 y_nxt」决定本列的垂直填充范围。
 *
 *     top = min(iCurTop, iNxtTop)     ← 覆盖到下一列主体点的顶部
 *     bot = max(iCurTop, iNxtTop) + stroke - 1
 *
 *   这样相邻两列的填充区间首尾相接，左右两侧都不出现间隙，彻底消除
 *   "左侧锯齿"（ARM-2D 算法每列硬顶边缘的问题）。
 *
 *   边缘 AA：固定 1px，alpha = 128（50%），不依赖斜率，无突变。
 *
 * 历史注记（可删）：
 *   v1 叉积 SDF  → 左光滑右阶梯
 *   v2 vspan + 动态 aa_w → 峰值尖刺 / 窗口钳制后突变
 *   v3 ARM-2D 跨列 AA    → 左侧硬顶阶梯（前瞻 vs 后顾之别）
 *   v4 前瞻 vspan + 固定 1px AA（本文件）
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * 绘制回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _chart_draw_cb(void *ptr)
{
    we_chart_obj_t *obj   = (we_chart_obj_t *)ptr;
    we_lcd_t       *p_lcd = obj->base.lcd;
    int16_t  bx = obj->base.x;
    int16_t  by = obj->base.y;
    int16_t  bw = obj->base.w;
    int16_t  bh = obj->base.h;

    if (!p_lcd || bw <= 0 || bh <= 0 || !obj->data)
        return;

    colour_t *pfb_gram = p_lcd->pfb_gram;
    uint16_t  pfb_w    = p_lcd->pfb_width;
    int16_t   pfb_x0   = p_lcd->pfb_area.x0;
    int16_t   pfb_x1   = p_lcd->pfb_area.x1;
    int16_t   pfb_y0   = p_lcd->pfb_y_start;
    int16_t   pfb_y1   = p_lcd->pfb_y_end;
    colour_t  color    = obj->line_color;
    uint8_t   opacity  = obj->opacity;

    _chart_draw_grid(p_lcd, bx, by, bw, bh);

    uint16_t draw_cnt = (obj->data_count < (uint16_t)bw)
                        ? obj->data_count : (uint16_t)bw;
    if (draw_cnt < 2U)
        return;

    /* 环形缓冲起始索引 */
    uint16_t idx0;
    {
        uint16_t base = (uint16_t)((obj->data_head + (uint32_t)obj->data_cap
                                    - obj->data_count) % obj->data_cap);
        uint16_t skip = (obj->data_count > (uint16_t)bw)
                        ? (uint16_t)(obj->data_count - (uint16_t)bw) : 0U;
        idx0 = (uint16_t)((base + skip) % obj->data_cap);
    }

    int32_t stroke      = (int32_t)obj->stroke;
    int32_t half_stroke = stroke >> 1;

    /* Y 裁剪区间（PFB 带 ∩ 控件） */
    int32_t clip_y0 = (pfb_y0 > (int32_t)by)            ? pfb_y0 : (int32_t)by;
    int32_t clip_y1 = (pfb_y1 < (int32_t)(by + bh - 1)) ? pfb_y1 : (int32_t)(by + bh - 1);

    /* 数据不满时右对齐 */
    int32_t sc_start = (int32_t)bw - (int32_t)draw_cnt;

    /* 全实心 alpha（考虑 opacity） */
    uint8_t a_solid = (uint8_t)(((uint16_t)255U * opacity) >> 8);

    /* D: 把 sc 循环的合法区间预先收敛到 PFB X 范围内，
     *    省掉每列两次 sx 比较与 continue。 */
    int32_t sc_lo = sc_start;
    int32_t sc_hi = (int32_t)bw - 1;
    {
        int32_t sc_x_lo = (int32_t)pfb_x0 - (int32_t)bx;
        int32_t sc_x_hi = (int32_t)pfb_x1 - (int32_t)bx;
        if (sc_x_lo > sc_lo) sc_lo = sc_x_lo;
        if (sc_x_hi < sc_hi) sc_hi = sc_x_hi;
    }
    if (sc_lo > sc_hi) return;

    /* A: 主循环里 y_cur 沿用上一轮的 y_nxt，每列只调一次 _get_sy。
     *    先把起点 y_cur prime 出来。 */
    int32_t col_idx = sc_lo - sc_start;
    int32_t y_cur   = _get_sy(col_idx, idx0, obj, by, bh);

    for (int32_t sc = sc_lo; sc <= sc_hi; sc++)
    {
        int16_t sx       = (int16_t)(bx + sc);
        int32_t col_next = col_idx + 1;
        int32_t y_nxt    = (col_next < (int32_t)draw_cnt)
                           ? _get_sy(col_next, idx0, obj, by, bh)
                           : y_cur;

        /* 顶锚刷子：top = 最小屏幕 Y（最高处） */
        int32_t iCurTop = y_cur - half_stroke;
        int32_t iNxtTop = y_nxt - half_stroke;

        /* 前瞻 vspan：覆盖当前列和下一列之间的完整区间 */
        int32_t span_top = (iCurTop < iNxtTop) ? iCurTop : iNxtTop;
        int32_t span_bot = ((iCurTop > iNxtTop) ? iCurTop : iNxtTop) + stroke - 1;

        colour_t *cb = pfb_gram + (uint16_t)(sx - pfb_x0);

        _chart_draw_wave_col(obj, cb, pfb_w, pfb_y0, clip_y0, clip_y1,
                             span_top, span_bot, color, a_solid, opacity);

        /* 滑动一格：本轮的 y_nxt 即下一轮的 y_cur。 */
        y_cur   = y_nxt;
        col_idx = col_next;
    }
}

/* =========================================================
 * 公开 API 实现
 * ========================================================= */

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param data_buf 采样环形缓冲区指针。
 * @param data_cap 缓冲区容量（采样点个数）。
 * @param line_color 线条颜色值。
 * @param stroke 线宽（像素）。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_chart_obj_init(we_chart_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, uint16_t w, uint16_t h,
                       int16_t *data_buf, uint16_t data_cap,
                       colour_t line_color, uint8_t stroke, uint8_t opacity)
{
    static const we_class_t _chart_class = { .draw_cb = _chart_draw_cb, .event_cb = NULL };
    we_obj_t *tail;

    if (!obj || !lcd || !data_buf || data_cap == 0U)
        return;

    obj->data       = data_buf;
    obj->data_cap   = data_cap;
    obj->data_head  = 0U;
    obj->data_count = 0U;
    obj->line_color = line_color;
    obj->stroke     = (stroke > 0U) ? stroke : 2U;
    obj->opacity    = opacity;

    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)w;
    obj->base.h       = (int16_t)h;
    obj->base.class_p = &_chart_class;
    obj->base.lcd     = lcd;
    obj->base.next    = NULL;
    obj->base.parent  = NULL;

    if (lcd->obj_list_head == NULL)
        lcd->obj_list_head = (we_obj_t *)obj;
    else
    {
        tail = lcd->obj_list_head;
        while (tail->next) tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }

    if (opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 向控件数据队列推入一个新采样点。
 * @param obj 目标控件对象指针。
 * @param value 输入采样值。
 * @return 无。
 */
void we_chart_push(we_chart_obj_t *obj, int16_t value)
{
    if (!obj || !obj->data) return;
    _chart_invalidate_push(obj, value);
    obj->data[obj->data_head] = value;
    obj->data_head = (uint16_t)((obj->data_head + 1U) % obj->data_cap);
    if (obj->data_count < obj->data_cap) obj->data_count++;
}

/**
 * @brief 清空控件内部数据并重置游标。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_chart_clear(we_chart_obj_t *obj)
{
    if (!obj) return;
    if (obj->opacity > 0U) _chart_invalidate_full(obj);
    obj->data_head  = 0U;
    obj->data_count = 0U;
}

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param color 目标颜色值。
 * @return 无。
 */
void we_chart_set_color(we_chart_obj_t *obj, colour_t color)
{
    if (!obj) return;
    obj->line_color = color;
    if (obj->opacity > 0U) _chart_invalidate_full((we_chart_obj_t *)obj);
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_chart_set_opacity(we_chart_obj_t *obj, uint8_t opacity)
{
    if (!obj || obj->opacity == opacity) return;
    if (obj->opacity > 0U) _chart_invalidate_full(obj);
    obj->opacity = opacity;
    if (obj->opacity > 0U) _chart_invalidate_full(obj);
}
