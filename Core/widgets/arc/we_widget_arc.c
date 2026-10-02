#include "we_widget_arc.h"
#include "wegui.h"

/**
 * @brief 将任意角度归一化到 512 分度制范围 [0, 511]（位与运算，与 we_sin 一致）。
 * @param a 输入角度（允许负值或超过 511）。
 * @return 归一化后的角度值。
 */
static uint16_t _norm_512(int16_t a)
{
    return (uint16_t)(a & 0x1FF);
}

/**
 * @brief 计算两阈值区间的反比系数，用于后续 alpha 线性插值（提取公共除法倒数）。
 * @param a 外层半径平方（或上界平方）。
 * @param b 内层半径平方（或下界平方）。
 * @return 归一化后的倒数系数；当 a<=b 时返回 0。
 */
static uint32_t _get_inv(int32_t a, int32_t b)
{
    return (a > b) ? (8355840 / (a - b)) : 0; // 8355840 = 255 << 15
}

/**
 * @brief 根据半径和方向余弦/正弦计算圆周点的 Q4 定点坐标。
 * @param r 半径值（Q1 语义，参与三角变换）。
 * @param c 方向余弦（Q15，we_cos 的返回值）。
 * @param s 方向正弦（Q15，we_sin 的返回值）。
 * @param x 输出 X 坐标指针（Q4 定点）。
 * @param y 输出 Y 坐标指针（Q4 定点）。
 * @return 无。
 */
static void _get_xy_q4(int32_t r, int32_t c, int32_t s, int32_t *x, int32_t *y)
{
    int32_t vx = r * c;
    int32_t vy = r * s;
    *x = vx >= 0 ? ((vx + 2048) >> 12) : -((-vx + 2048) >> 12);
    *y = vy >= 0 ? ((vy + 2048) >> 12) : -((-vy + 2048) >> 12);
}

/**
 * @brief 计算圆角端帽在当前像素的覆盖 alpha（独立成函数，内层循环只剩一次调用）。
 * @param dx_q4 当前像素 X（相对圆心）Q4 坐标。
 * @param cx_q4 端帽圆心 X 的 Q4 坐标。
 * @param dy_sq_q8 当前像素与端帽圆心在 Y 方向差值平方（Q8）。
 * @param rmax2_q8 端帽外扩半径平方（Q8）。
 * @param r2_q8 端帽实心半径平方（Q8）。
 * @param inv_cap 端帽过渡区倒数系数。
 * @return 端帽覆盖 alpha（0~255）。
 */
static uint32_t _cap_alpha(int32_t dx_q4, int32_t cx_q4, int32_t dy_sq_q8, int32_t rmax2_q8, int32_t r2_q8,
                           uint32_t inv_cap)
{
    int32_t dx = dx_q4 - cx_q4;
    int32_t d2 = dx * dx + dy_sq_q8;
    if (d2 >= rmax2_q8)
        return 0;
    if (d2 <= r2_q8)
        return 255;
    return ((rmax2_q8 - d2) * inv_cap) >> 15;
}

#if WE_ARC_OPT_MODE == 0
/* =========================================================================
 * 极简几何扫描器 (公用包围盒引擎) - 仅在性能模式下编译占用 ROM
 * ========================================================================= */

/**
 * @brief 计算圆弧在当前几何参数下的紧致包围盒。
 * @param cx 圆心 X 坐标。
 * @param cy 圆心 Y 坐标。
 * @param r 外半径（像素）。
 * @param th 线宽（像素）。
 * @param start 起始角度（512 分度制）。
 * @param span 顺时针跨度（512 分度制）。
 * @param out_x 输出包围盒左上角 X。
 * @param out_y 输出包围盒左上角 Y。
 * @param out_w 输出包围盒宽度。
 * @param out_h 输出包围盒高度。
 * @return 无。
 */
static void _calc_arc_tight_bbox(int16_t cx, int16_t cy, int16_t r, int16_t th, uint16_t start, uint16_t span,
                                 int16_t *out_x, int16_t *out_y, int16_t *out_w, int16_t *out_h)
{
    uint16_t pad = 2;
    if (span >= 512)
    {
        *out_x = cx - r - pad;
        *out_y = cy - r - pad;
        *out_w = (r + pad) * 2 + 1;
        *out_h = (r + pad) * 2 + 1;
        return;
    }

    uint16_t end = _norm_512(start + span);
    uint16_t mid_r = r - (th >> 1);
    uint16_t cap_r = (th >> 1) + pad;
    uint16_t outer_r = r + pad;

    int16_t s_cx = cx + (((int32_t)mid_r * we_cos(start)) >> 15);
    int16_t s_cy = cy + (((int32_t)mid_r * we_sin(start)) >> 15);
    int16_t e_cx = cx + (((int32_t)mid_r * we_cos(end)) >> 15);
    int16_t e_cy = cy + (((int32_t)mid_r * we_sin(end)) >> 15);

    int16_t min_x = WE_MIN(s_cx, e_cx) - cap_r;
    int16_t max_x = WE_MAX(s_cx, e_cx) + cap_r;
    int16_t min_y = WE_MIN(s_cy, e_cy) - cap_r;
    int16_t max_y = WE_MAX(s_cy, e_cy) + cap_r;

    /* 弧线扫过的象限边界（512 步制，每象限 128 步）：跨过哪条轴，包围盒就扩到那一侧的外圆 */
    uint16_t start_q = (uint16_t)((start >> 7) + 1U);
    uint16_t end_q = (uint16_t)((start + span) >> 7);

    for (uint16_t q = start_q; q <= end_q; q++)
    {
        switch (q & 3)
        {
        case 0:
            max_x = WE_MAX(max_x, cx + outer_r);
            break;
        case 1:
            max_y = WE_MAX(max_y, cy + outer_r);
            break;
        case 2:
            min_x = WE_MIN(min_x, cx - outer_r);
            break;
        case 3:
            min_y = WE_MIN(min_y, cy - outer_r);
            break;
        }
    }
    *out_x = min_x;
    *out_y = min_y;
    *out_w = max_x - min_x + 1;
    *out_h = max_y - min_y + 1;
}
#endif

/**
 * @brief 标记圆弧的一块包围盒区域，但排除内圆内接安全空洞。
 * @param obj 圆弧控件对象指针。
 * @param bx 区域左上角 X 坐标。
 * @param by 区域左上角 Y 坐标。
 * @param bw 区域宽度。
 * @param bh 区域高度。
 * @return 无。
 * @note 圆心取控件当前位置（base + center_off），挪动前后各调一次即覆盖新旧两处。
 */
static void _arc_invalidate_exclude_hole(we_arc_obj_t *obj, int16_t bx, int16_t by, int16_t bw, int16_t bh)
{
    int16_t inner_r;
    int16_t hole_half;
    int16_t hole_size;
    int16_t cx = (int16_t)(obj->base.x + obj->center_off_x);
    int16_t cy = (int16_t)(obj->base.y + obj->center_off_y);

    if (bw <= 0 || bh <= 0)
        return;

    inner_r = (int16_t)obj->radius - (int16_t)obj->thickness;
    if (inner_r <= 4)
    {
        we_obj_invalidate_area((we_obj_t *)obj, bx, by, bw, bh);
        return;
    }

    /* 使用内圆的内接正方形作为安全空洞：181/256 ≈ 1/sqrt(2)，再留 2px 抗锯齿余量。 */
    hole_half = (int16_t)(((uint32_t)inner_r * 181U) >> 8);
    if (hole_half > 2)
        hole_half = (int16_t)(hole_half - 2);
    else
    {
        we_obj_invalidate_area((we_obj_t *)obj, bx, by, bw, bh);
        return;
    }

    hole_size = (int16_t)(hole_half * 2 + 1);
    we_obj_invalidate_area_exclude((we_obj_t *)obj, bx, by, bw, bh,
                                   (int16_t)(cx - hole_half), (int16_t)(cy - hole_half),
                                   hole_size, hole_size);
}

/**
 * @brief 标记整个控件包围盒（排除内圆空洞）。
 * @param obj 圆弧控件对象指针。
 * @return 无。
 */
static void _arc_invalidate_all(we_arc_obj_t *obj)
{
    _arc_invalidate_exclude_hole(obj, obj->base.x, obj->base.y, obj->base.w, obj->base.h);
}

/* =========================================================================
 * 渲染引擎 (极致精简指令流版)
 * ========================================================================= */

/**
 * @brief 圆弧绘制回调：基于距离平方场渲染轨道/进度弧及圆角端帽，写入 PFB。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 内核调用前已把 lcd->clip 收窄到本控件包围盒内，窗口左上角即 pfb_gram 起点。
 */
static void _arc_draw_cb(void *ptr)
{
    we_arc_obj_t *obj = (we_arc_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t opa;

    if (obj->radius <= obj->thickness || obj->bg_span == 0)
        return;
    opa = we_opa_apply(lcd, obj->opacity); /* 容器透明度级联 */
    if (opa == 0U)
        return;

    int16_t draw_x_start = lcd->clip.x0;
    int16_t draw_y_start = lcd->clip.y0;
    int16_t draw_x_end = lcd->clip.x1;
    int16_t draw_y_end = lcd->clip.y1;

    int16_t cx = obj->base.x + obj->center_off_x;
    int16_t cy = obj->base.y + obj->center_off_y;
    int32_t r = obj->radius;
    int32_t ir = r - obj->thickness;

    // 1. 半径平方常量全部无符号化 (强制类型转换防止 16位 乘法溢出)
    uint32_t r2 = (uint32_t)r * r;
    uint32_t rmax2 = (uint32_t)(r + 1) * (r + 1);
    uint32_t ir2 = (uint32_t)ir * ir;
    uint32_t irmin2 = (ir > 0) ? ((uint32_t)(ir - 1) * (ir - 1)) : 0;

    uint32_t inv_r_out = _get_inv(rmax2, r2);
    uint32_t inv_r_in = _get_inv(ir2, irmin2);

    int16_t bg_end_ang = _norm_512(obj->bg_start_angle + obj->bg_span);
    int32_t v_s_x = we_cos(obj->bg_start_angle), v_s_y = we_sin(obj->bg_start_angle);
    int32_t v_ebg_x = we_cos(bg_end_ang), v_ebg_y = we_sin(bg_end_ang);

    // 将 0~255 映射到 0~256，直接使用单周期 >> 8 代替 *257 和 >>16
    uint16_t fast_v = obj->value + (obj->value >> 7);
    uint32_t fg_span = (obj->bg_span * fast_v) >> 8;

    int16_t curr_angle = _norm_512(obj->bg_start_angle + fg_span);
    int32_t v_efg_x = we_cos(curr_angle), v_efg_y = we_sin(curr_angle);

    uint8_t bg_large = obj->bg_span > 256;
    uint8_t fg_large = fg_span > 256;
    uint8_t is_full_ring = (obj->bg_span >= 512);
    uint8_t is_full_fg = (is_full_ring && obj->value == 255);

    uint8_t has_ebg_cap = !is_full_ring;
    uint8_t has_efg_cap = (obj->value > 0 && !is_full_fg);

    int32_t mid_r_q1 = r + ir;
    int32_t cxs_q4, cys_q4, cxebg_q4 = 0, cyebg_q4 = 0, cxefg_q4 = 0, cyefg_q4 = 0;

    _get_xy_q4(mid_r_q1, v_s_x, v_s_y, &cxs_q4, &cys_q4);
    if (has_ebg_cap)
        _get_xy_q4(mid_r_q1, v_ebg_x, v_ebg_y, &cxebg_q4, &cyebg_q4);
    if (has_efg_cap)
        _get_xy_q4(mid_r_q1, v_efg_x, v_efg_y, &cxefg_q4, &cyefg_q4);

    int32_t cap_r_q4 = (r - ir) << 3;
    int32_t cap_r2_q8 = cap_r_q4 * cap_r_q4;
    int32_t cap_blend = (cap_r_q4 + 16) * (cap_r_q4 + 16) - cap_r2_q8;
    int32_t cap_rmax2_q8 = cap_r2_q8 + cap_blend;
    uint32_t inv_cap = _get_inv(cap_rmax2_q8, cap_r2_q8);

    colour_t *dst_line = lcd->pfb_gram;

    for (int16_t y = draw_y_start; y <= draw_y_end; y++)
    {
        int32_t dy = y - cy;              // dy 必须是有符号的，因为在圆心上方为负
        uint32_t dy2 = (uint32_t)dy * dy; // 但平方之后绝对是正数，果断 uint32_t
        int32_t dy_q4 = dy << 4;

        int32_t dys_q8 = (dy_q4 - cys_q4) * (dy_q4 - cys_q4);
        int32_t dyebg_q8 = has_ebg_cap ? (dy_q4 - cyebg_q4) * (dy_q4 - cyebg_q4) : 0;
        int32_t dyefg_q8 = has_efg_cap ? (dy_q4 - cyefg_q4) * (dy_q4 - cyefg_q4) : 0;

        /* 端帽圆在本行的纵向距离已超出外扩半径时，整行覆盖为 0，不必逐像素调用 */
        uint8_t s_on = (uint8_t)(dys_q8 < cap_rmax2_q8);
        uint8_t ebg_on = (uint8_t)(has_ebg_cap && dyebg_q8 < cap_rmax2_q8);
        uint8_t efg_on = (uint8_t)(has_efg_cap && dyefg_q8 < cap_rmax2_q8);

        int32_t dx_start = draw_x_start - cx;
        int32_t cp_s_base = dx_start * v_s_y - dy * v_s_x;
        int32_t cp_ebg_base = dx_start * v_ebg_y - dy * v_ebg_x;
        int32_t cp_efg_base = dx_start * v_efg_y - dy * v_efg_x;

        colour_t *p_dst = dst_line;
        for (int16_t x = draw_x_start; x <= draw_x_end; x++)
        {
            int32_t dx = x - cx;                   // dx 必须是有符号的
            uint32_t d2 = (uint32_t)dx * dx + dy2; // 核心：当前像素距离平方也是无符号的！

            uint32_t ring_a = 255;
            uint8_t in_bg = 0;
            uint8_t in_fg = 0;
            uint32_t bg_body_a = 0;
            uint32_t fg_body_a = 0;
            int32_t dx_q4 = 0;
            uint32_t cap_s_a = 0;
            uint32_t cap_ebg_a = 0;
            uint32_t cap_efg_a = 0;
            uint32_t final_bg_a = 0;
            uint32_t final_fg_a = 0;

            if (d2 >= rmax2 || d2 <= irmin2)
                goto PIXEL_NEXT;

            // 这里的 d2 和 r2 都是 uint32_t，比较极速！
            if (d2 > r2)
                ring_a = (((rmax2 - d2) * inv_r_out) + 16384) >> 15;
            else if (d2 < ir2)
                ring_a = (((d2 - irmin2) * inv_r_in) + 16384) >> 15;

            in_bg = is_full_ring
                        ? 1
                        : (bg_large ? ((cp_s_base < 0) || (cp_ebg_base > 0)) : ((cp_s_base < 0) && (cp_ebg_base > 0)));
            in_fg = (obj->value > 0) ? (is_full_fg ? 1
                                                   : (fg_large ? ((cp_s_base < 0) || (cp_efg_base > 0))
                                                               : ((cp_s_base < 0) && (cp_efg_base > 0))))
                                     : 0;

            bg_body_a = in_bg ? ring_a : 0;
            fg_body_a = in_fg ? ring_a : 0;

            dx_q4 = dx << 4;
            if (s_on)
                cap_s_a = _cap_alpha(dx_q4, cxs_q4, dys_q8, cap_rmax2_q8, cap_r2_q8, inv_cap);
            if (ebg_on)
                cap_ebg_a = _cap_alpha(dx_q4, cxebg_q4, dyebg_q8, cap_rmax2_q8, cap_r2_q8, inv_cap);
            if (efg_on)
                cap_efg_a = _cap_alpha(dx_q4, cxefg_q4, dyefg_q8, cap_rmax2_q8, cap_r2_q8, inv_cap);

            final_bg_a = WE_MAX(bg_body_a, WE_MAX(cap_s_a, cap_ebg_a));
            final_fg_a = WE_MAX(fg_body_a, WE_MAX(cap_s_a, cap_efg_a));

            if (final_bg_a > 0 || final_fg_a > 0)
            {
                colour_t pixel_color = *p_dst;
                uint32_t fg_a = (final_fg_a * opa) >> 8;

                if (fg_a >= 250)
                {
                    pixel_color = obj->fg_color;
                }
                else
                {
                    if (final_bg_a > 0)
                    {
                        uint32_t bg_a = (final_bg_a * opa) >> 8;
                        pixel_color = we_colour_blend(obj->bg_color, pixel_color, (uint8_t)bg_a);
                    }
                    if (fg_a > 0)
                    {
                        pixel_color = we_colour_blend(obj->fg_color, pixel_color, (uint8_t)fg_a);
                    }
                }
                we_store_color(p_dst, pixel_color);
            }

        PIXEL_NEXT:
            p_dst++;
            cp_s_base += v_s_y;
            cp_ebg_base += v_ebg_y;
            cp_efg_base += v_efg_y;
        }
        dst_line += lcd->pfb_width;
    }
}

static void _arc_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_arc_obj_set_pos((we_arc_obj_t *)ptr, x, y);
}

/* =========================================================================
 * API 接口
 * ========================================================================= */

/**
 * @brief 初始化圆弧控件，并根据角度范围计算初始包围盒。
 * @param obj 圆弧控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param cx 圆心 X 坐标（屏幕坐标）。
 * @param cy 圆心 Y 坐标（屏幕坐标）。
 * @param r 圆弧外半径（像素）。
 * @param th 圆弧线宽（像素）。
 * @param start_angle 圆弧起始角度（512 分度制）。
 * @param end_angle 圆弧结束角度（512 分度制）。
 * @param fg_color 前景进度弧颜色。
 * @param bg_color 背景轨道颜色。
 * @param opacity 控件整体不透明度（0~255）。
 * @return 无。
 */
void we_arc_obj_init(we_arc_obj_t *obj, we_lcd_t *lcd, int16_t cx, int16_t cy, uint16_t r, uint16_t th,
                     int16_t start_angle, int16_t end_angle, colour_t fg_color, colour_t bg_color, uint8_t opacity)
{
    if (obj == NULL || lcd == NULL)
        return;

    uint16_t s = _norm_512(start_angle);
    uint16_t e = _norm_512(end_angle);
    uint16_t span = (e >= s) ? (e - s) : (512 - s + e);

    /* 起止角重合（含相差整圈）视为整圆 */
    if (span == 0)
        span = 512;

    int16_t bx, by, bw, bh;

#if WE_ARC_OPT_MODE == 1
    // 空间优先：拒绝复杂的极值推演运算，直接包围最大安全正方形
    uint16_t pad = 2;
    bx = cx - r - pad;
    by = cy - r - pad;
    bw = (r + pad) * 2 + 1;
    bh = (r + pad) * 2 + 1;
#else
    // 性能优先：计算绝对紧凑的几何包围盒
    _calc_arc_tight_bbox(cx, cy, r, th, s, span, &bx, &by, &bw, &bh);
#endif

    obj->base.lcd = lcd;
    obj->base.parent = NULL;
    obj->base.x = bx;
    obj->base.y = by;
    obj->base.w = bw;
    obj->base.h = bh;

    obj->center_off_x = cx - bx;
    obj->center_off_y = cy - by;

    obj->radius = r;
    obj->thickness = th;
    obj->bg_start_angle = s;
    obj->bg_span = span;
    obj->value = 0;

    obj->fg_color = fg_color;
    obj->bg_color = bg_color;
    obj->opacity = opacity;

    static const we_class_t _arc_class = {.draw_cb = _arc_draw_cb, .event_cb = NULL, .set_pos_cb = _arc_set_pos_cb};
    obj->base.class_p = &_arc_class;
    obj->base.next = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0)
        _arc_invalidate_all(obj);
}

/**
 * @brief 设置圆弧当前进度值，并触发对应区域重绘。
 * @param obj 圆弧控件对象指针。
 * @param value 进度值（0-255，对应 0%-100%）。
 * @return 无。
 */
void we_arc_set_value(we_arc_obj_t *obj, uint8_t value)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->value == value)
        return;

#if WE_ARC_OPT_MODE != 1
    uint8_t old_val = obj->value;
#endif
    obj->value = value;

    if (obj->opacity > 0)
    {
#if WE_ARC_OPT_MODE == 1
        _arc_invalidate_all(obj);
#else
        /* 只标新旧进度端点之间扫过的那段弧 */
        uint16_t fast_v1 = old_val + (old_val >> 7);
        uint16_t fast_v2 = value + (value >> 7);
        uint32_t fg_span1 = (obj->bg_span * fast_v1) >> 8;
        uint32_t fg_span2 = (obj->bg_span * fast_v2) >> 8;

        uint16_t a_start = _norm_512(obj->bg_start_angle + WE_MIN(fg_span1, fg_span2));
        uint16_t span = WE_MAX(fg_span1, fg_span2) - WE_MIN(fg_span1, fg_span2);

        int16_t bx, by, bw, bh;
        _calc_arc_tight_bbox(obj->base.x + obj->center_off_x, obj->base.y + obj->center_off_y, obj->radius,
                             obj->thickness, a_start, span, &bx, &by, &bw, &bh);

        int16_t ex = WE_MIN(bx + bw - 1, obj->base.x + obj->base.w - 1);
        int16_t ey = WE_MIN(by + bh - 1, obj->base.y + obj->base.h - 1);
        bx = WE_MAX(bx, obj->base.x);
        by = WE_MAX(by, obj->base.y);

        if (ex >= bx && ey >= by)
            _arc_invalidate_exclude_hole(obj, bx, by, ex - bx + 1, ey - by + 1);
#endif
    }
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度(0~255)。
 * @return 无。
 */
void we_arc_set_opacity(we_arc_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    _arc_invalidate_all(obj);
}

/**
 * @brief 设置圆弧控件在对象树中的左上角坐标，并避开中心空洞标脏。
 * @param obj 圆弧控件对象指针。
 * @param x 新的左上角 X 坐标。
 * @param y 新的左上角 Y 坐标。
 * @return 无。
 */
void we_arc_obj_set_pos(we_arc_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->base.x == x && obj->base.y == y)
        return;

    if (obj->opacity > 0U)
        _arc_invalidate_all(obj); /* 旧位置 */

    obj->base.x = x;
    obj->base.y = y;

    if (obj->opacity > 0U)
        _arc_invalidate_all(obj); /* 新位置 */
}
