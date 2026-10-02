#include "we_widget_chart.h"
#include "wegui.h"

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
 * @return 可见列数 = min(有效样本数, 绘制宽度)。
 */
static __inline uint16_t _chart_visible_count(const we_chart_obj_t *obj, int16_t bw)
{
    uint16_t visible_cap = (bw > 0) ? (uint16_t)bw : 0U;
    return (obj->data_count < visible_cap) ? obj->data_count : visible_cap;
}

/**
 * @brief 环形缓冲索引加偏移（比较回绕，不做取模）。
 * @param obj 目标控件对象指针。
 * @param idx 起始索引（< data_cap）。
 * @param ofs 偏移（<= data_cap）。
 * @return (idx + ofs) 回绕到 [0, data_cap) 的索引。
 * @note 两数之和小于 2*data_cap，减一次即可；M0 没有除法指令，取模要调库函数。
 */
static __inline uint16_t _chart_ring_add(const we_chart_obj_t *obj, uint16_t idx, uint16_t ofs)
{
    uint32_t r = (uint32_t)idx + ofs;

    if (r >= obj->data_cap)
        r -= obj->data_cap;
    return (uint16_t)r;
}

/**
 * @brief 计算可见窗口在环形缓冲中的起始索引。
 * @param obj 目标控件对象指针。
 * @param visible_count 可见列数。
 * @param bw 绘制区域宽度（像素）。
 * @return 可见窗口第一列对应的环形缓冲索引。
 */
static __inline uint16_t _chart_visible_start_idx(const we_chart_obj_t *obj, uint16_t visible_count, int16_t bw)
{
    uint16_t base = _chart_ring_add(obj, obj->data_head, (uint16_t)(obj->data_cap - obj->data_count));
    uint16_t skip = (obj->data_count > (uint16_t)bw) ? (uint16_t)(obj->data_count - (uint16_t)bw) : 0U;
    if (visible_count == 0U)
        return base;
    return _chart_ring_add(obj, base, skip);
}

/**
 * @brief 返回柔边层数（WE_CHART_AA_MAX；关闭柔边时为 0）。
 * @return 柔边层数。
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
 * @brief 取环形缓冲第 ri 个采样并映射为屏幕 Y 坐标。
 * @param obj 目标控件对象指针。
 * @param ri 环形缓冲索引。
 * @param by 绘制区域起始 Y 坐标。
 * @param bh 绘制区域高度（像素）。
 * @return 屏幕 Y 坐标。
 */
static __inline int32_t _get_sy_at(const we_chart_obj_t *obj, uint16_t ri,
                                   int16_t by, int16_t bh)
{
    return _chart_value_to_sy(obj->data[ri], by, bh);
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
 * @brief push 前标脏：新旧两帧波形在每个列块上的联合包络。
 * @param obj 目标控件对象指针。
 * @param new_value 即将写入 data_head 的新值（此刻该槽还是被覆盖的旧值）。
 * @return 无。
 * @note 新旧两帧各用一个环形游标逐列 +1、比较回绕，整趟不做取模。
 */
static void _chart_invalidate_push(we_chart_obj_t *obj, int16_t new_value)
{
    int16_t bx;
    int16_t by;
    int16_t bw;
    int16_t bh;
    uint16_t old_count;
    uint16_t new_count;
    uint16_t ri_old;
    uint16_t ri_new;
    int32_t old_sc_start;
    int32_t new_sc_start;
    int32_t stroke;
    int32_t block_w;
    int32_t col_start;
    int32_t count_delta;

    if (obj->opacity == 0U)
        return;

#if (WE_CFG_CHART_DIRTY_MODE == 0)
    (void)new_value;
    we_obj_invalidate((we_obj_t *)obj);
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

    ri_old = _chart_visible_start_idx(obj, old_count, bw);
    ri_new = (new_count > 0U)
               ? _chart_ring_add(obj, _chart_ring_add(obj, obj->data_head, 1U), (uint16_t)(obj->data_cap - new_count))
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

        if (col_end >= (int32_t)new_count)
            col_end = (int32_t)new_count - 1;

        for (int32_t col = col_start; col <= col_end; col++)
        {
            int32_t old_col = col - count_delta;
            uint16_t ri_new_next = _chart_ring_add(obj, ri_new, 1U);

            /* 旧帧：本列（及下一列）在旧可见窗口里的样本 */
            if (old_col >= 0 && old_col < (int32_t)old_count)
            {
                uint16_t ri_old_b = (old_col + 1 < (int32_t)old_count) ? _chart_ring_add(obj, ri_old, 1U) : ri_old;
                _chart_accum_span(&y0, &y1, obj->data[ri_old], obj->data[ri_old_b], by, bh, stroke);
                ri_old = _chart_ring_add(obj, ri_old, 1U);
            }

            /* 新帧：data_head 槽读作即将写入的新值 */
            {
                uint16_t ri_new_b = (col + 1 < (int32_t)new_count) ? ri_new_next : ri_new;
                int32_t new_a = (ri_new == obj->data_head) ? new_value : obj->data[ri_new];
                int32_t new_b = (ri_new_b == obj->data_head) ? new_value : obj->data[ri_new_b];
                _chart_accum_span(&y0, &y1, new_a, new_b, by, bh, stroke);
            }
            ri_new = ri_new_next;
        }

        if (y0 <= y1)
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
 * @brief 按预计算偏移绘制全部行/列网格线（1px 宽的矩形填充）。
 * @param obj 目标控件对象指针（提供预计算的网格分割点偏移）。
 * @return 无。
 * @note 传控件自身透明度，容器级联由 we_fill_rect 入口统一乘一次。
 */
static void _chart_draw_grid(const we_chart_obj_t *obj)
{
#if (WE_CHART_GRID_COLS > 1) || (WE_CHART_GRID_ROWS > 1)
    we_lcd_t *lcd = obj->base.lcd;
    colour_t gc = RGB888TODEV(WE_CHART_GRID_R, WE_CHART_GRID_G, WE_CHART_GRID_B);
#else
    (void)obj;
#endif

    /* 分割点在 init 时已预计算（见 we_chart_obj_init），这里只做加法 */
#if (WE_CHART_GRID_COLS > 1)
    for (uint8_t c = 1U; c < (uint8_t)WE_CHART_GRID_COLS; c++)
        we_fill_rect(lcd, (int16_t)(obj->base.x + obj->grid_dx[c - 1U]), obj->base.y,
                     1U, (uint16_t)obj->base.h, gc, obj->opacity);
#endif

#if (WE_CHART_GRID_ROWS > 1)
    for (uint8_t r = 1U; r < (uint8_t)WE_CHART_GRID_ROWS; r++)
        we_fill_rect(lcd, obj->base.x, (int16_t)(obj->base.y + obj->grid_dy[r - 1U]),
                     (uint16_t)obj->base.w, 1U, gc, obj->opacity);
#endif
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
 *   边缘 AA：主体上下各 WE_CHART_AA_MAX 层，alpha 按离主体的层数递减
 *   （线性或二次，编译期查表），不依赖斜率，无突变。
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * 绘制回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 内核调用前已把窗口收窄到本控件包围盒内。每个切片只处理波形
 *       （含柔边）与窗口行范围相交的列，其余列只推进游标。
 */
static void _chart_draw_cb(void *ptr)
{
    we_chart_obj_t *obj   = (we_chart_obj_t *)ptr;
    we_lcd_t       *p_lcd = obj->base.lcd;
    int16_t  bx = obj->base.x;
    int16_t  by = obj->base.y;
    int16_t  bw = obj->base.w;
    int16_t  bh = obj->base.h;
    uint8_t  opacity = we_opa_apply(p_lcd, obj->opacity); /* 容器透明度级联 */

    if (opacity == 0U)
        return;

    _chart_draw_grid(obj);

    uint16_t draw_cnt = _chart_visible_count(obj, bw);
    if (draw_cnt < 2U)
        return;

    colour_t *pfb_gram = p_lcd->pfb_gram;
    uint16_t  pfb_w    = p_lcd->pfb_width;
    int16_t   pfb_x0   = p_lcd->clip.x0;
    int32_t   clip_y0  = p_lcd->clip.y0;
    int32_t   clip_y1  = p_lcd->clip.y1;
    colour_t  color    = obj->line_color;
    int32_t   stroke      = (int32_t)obj->stroke;
    int32_t   half_stroke = stroke >> 1;
    uint8_t   aa_h        = _chart_get_aa_height();

    /* 数据不满时右对齐 */
    int32_t sc_start = (int32_t)bw - (int32_t)draw_cnt;

    /* 全实心 alpha（考虑 opacity）；<=5 时主体不可见 */
    uint8_t a_solid = (uint8_t)(((uint16_t)255U * opacity) >> 8);

    /* 各层柔边 alpha 每次绘制只查一次表 */
#if (WE_CHART_AA_MAX > 0)
    uint8_t a_edge[WE_CHART_AA_MAX];
    for (uint8_t e = 0U; e < aa_h; e++)
        a_edge[e] = _chart_get_aa_alpha(aa_h, e, opacity);
#endif

    /* 列循环只跑窗口内的列（窗口右缘不会超出控件） */
    int32_t sc_lo = sc_start;
    int32_t sc_hi = (int32_t)p_lcd->clip.x1 - (int32_t)bx;
    if ((int32_t)pfb_x0 - (int32_t)bx > sc_lo)
        sc_lo = (int32_t)pfb_x0 - (int32_t)bx;
    if (sc_lo > sc_hi)
        return;

    /* y_cur 沿用上一轮的 y_nxt，每列只取一次采样；环形索引用游标 +1 与比较回绕维护 */
    int32_t  col_idx = sc_lo - sc_start;
    uint16_t ri_cur  = _chart_ring_add(obj, _chart_visible_start_idx(obj, draw_cnt, bw), (uint16_t)col_idx);
    int32_t  y_cur   = _get_sy_at(obj, ri_cur, by, bh);

    for (int32_t sc = sc_lo; sc <= sc_hi; sc++)
    {
        int32_t col_next = col_idx + 1;
        uint16_t ri_next = _chart_ring_add(obj, ri_cur, 1U);
        int32_t y_nxt    = (col_next < (int32_t)draw_cnt)
                           ? _get_sy_at(obj, ri_next, by, bh)
                           : y_cur;

        /* 顶锚刷子：top = 最小屏幕 Y（最高处） */
        int32_t iCurTop = y_cur - half_stroke;
        int32_t iNxtTop = y_nxt - half_stroke;

        /* 前瞻 vspan：覆盖当前列和下一列之间的完整区间 */
        int32_t span_top = (iCurTop < iNxtTop) ? iCurTop : iNxtTop;
        int32_t span_bot = ((iCurTop > iNxtTop) ? iCurTop : iNxtTop) + stroke - 1;

        /* 主体 [top, bot] 连同上下柔边 [top-aa, bot+aa] 与窗口行范围相交才画 */
        if (span_bot + (int32_t)aa_h >= clip_y0 && span_top - (int32_t)aa_h <= clip_y1)
        {
            colour_t *cb = pfb_gram + (uint16_t)((int16_t)(bx + sc) - pfb_x0);

            if (a_solid > 5U)
            {
                int32_t   y0  = (span_top < clip_y0) ? clip_y0 : span_top;
                int32_t   y1  = (span_bot > clip_y1) ? clip_y1 : span_bot;
                colour_t *dst = cb + (uint16_t)(y0 - clip_y0) * pfb_w;

                for (int32_t y = y0; y <= y1; y++)
                {
                    *dst = (a_solid >= 250U) ? color : we_colour_blend(color, *dst, a_solid);
                    dst += pfb_w;
                }
            }

#if (WE_CHART_AA_MAX > 0)
            for (uint8_t e = 0U; e < aa_h; e++)
            {
                int32_t y_top = span_top - 1 - (int32_t)e;
                int32_t y_bot = span_bot + 1 + (int32_t)e;

                if (a_edge[e] == 0U)
                    continue;
                if (y_top >= clip_y0 && y_top <= clip_y1)
                {
                    colour_t *dst = cb + (uint16_t)(y_top - clip_y0) * pfb_w;
                    *dst = we_colour_blend(color, *dst, a_edge[e]);
                }
                if (y_bot >= clip_y0 && y_bot <= clip_y1)
                {
                    colour_t *dst = cb + (uint16_t)(y_bot - clip_y0) * pfb_w;
                    *dst = we_colour_blend(color, *dst, a_edge[e]);
                }
            }
#endif
        }

        /* 滑动一格：本轮的 y_nxt / ri_next 即下一轮的 y_cur / ri_cur。 */
        y_cur   = y_nxt;
        col_idx = col_next;
        ri_cur  = ri_next;
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
    static const we_class_t _chart_class = { .draw_cb = _chart_draw_cb, .event_cb = NULL, .set_pos_cb = NULL};

    if (!obj || !lcd || !data_buf || data_cap == 0U)
        return;

    obj->data       = data_buf;
    obj->data_cap   = data_cap;
    obj->data_head  = 0U;
    obj->data_count = 0U;
    obj->line_color = line_color;
    obj->stroke     = (stroke > 0U) ? stroke : 2U;
    obj->opacity    = opacity;

    /* 预计算网格分割点偏移：绘制热路径（每 PFB 带重绘）只做加法。 */
#if (WE_CHART_GRID_COLS > 1)
    for (uint8_t c = 1U; c < (uint8_t)WE_CHART_GRID_COLS; c++)
        obj->grid_dx[c - 1U] = (int16_t)((int32_t)w * c / WE_CHART_GRID_COLS);
#endif
#if (WE_CHART_GRID_ROWS > 1)
    for (uint8_t r = 1U; r < (uint8_t)WE_CHART_GRID_ROWS; r++)
        obj->grid_dy[r - 1U] = (int16_t)((int32_t)h * r / WE_CHART_GRID_ROWS);
#endif

    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)w;
    obj->base.h       = (int16_t)h;
    obj->base.class_p = &_chart_class;
    obj->base.lcd     = lcd;
    obj->base.next    = NULL;
    obj->base.parent  = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

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
    /* 先按新旧两帧标脏，再写入 */
    _chart_invalidate_push(obj, value);
    obj->data[obj->data_head] = value;
    obj->data_head++;
    if (obj->data_head >= obj->data_cap) /* 比较回绕替代 %，省 M0 软件除法 */
        obj->data_head = 0U;
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
    if (obj->opacity > 0U) we_obj_invalidate((we_obj_t *)obj);
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
    if (!obj || we_colour_eq(obj->line_color, color)) return;
    obj->line_color = color;
    if (obj->opacity > 0U) we_obj_invalidate((we_obj_t *)obj);
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
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj); /* 包围盒不变，标一次即可 */
}
