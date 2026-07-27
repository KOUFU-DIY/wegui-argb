/**
 * @file  we_widget_box.c
 * @brief 矩形面板控件（box）实现
 *
 * 渲染分解为“快速整块 + 角落合成”两级：
 *   1. 中央与四条直边（含边框直段）全部走 we_fill_rect 整块填充；
 *   2. 仅四个 K×K 角落方块（K = max(各角半径, 边框厚, 边框厚+内半径)）做合成：
 *      - 圆角：带边框时用 we_mask_quarter_ring_alpha 单次 4x4 子采样同时求
 *        外/内覆盖（外减内即边框环），无边框退回 we_mask_quarter_circle_alpha；
 *        方块内的平直区（角半径之外）不做任何 mask 计算，直接按内矩形界定。
 *      - 切角：45° 覆盖率精确整数解析（alpha 仅 0/128/255），每行只算两个
 *        断点后按段整块写入，无逐像素函数调用。
 *
 * 所有 set 接口在目标值与当前值相同时直接返回，不触发重绘。
 * 可选动画（WE_BOX_USE_ANIM，默认关闭）：开启后填充颜色 / 透明度各占一个独立
 * 中央动画节点，可同时进行，两通道共用一个缓动；关闭时 we_box_anim_* 退化为
 * 立即生效的兼容桩。
 */

#include "we_widget_box.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _box_draw_cb(void *ptr);
static uint8_t _box_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void    _box_set_pos_cb(void *ptr, int16_t x, int16_t y);

static const we_class_t _box_class = {
    .draw_cb    = _box_draw_cb,
    .event_cb   = _box_event_cb,
    .set_pos_cb = _box_set_pos_cb
};

/* 切角边框的内轮廓收缩系数：
 * 圆角内轮廓半径 = r - bw 即可得到处处等厚的边框；45° 切角若同样减 bw，
 * 对角段的垂直厚度只有 bw/√2（视觉偏细）。改为收缩 (2-√2)·bw ≈ 0.586·bw
 * 可让对角段与直边段等厚。Q8 定点：0.586*256 ≈ 150。 */
#define _BOX_CHAMFER_INSET(bw) ((uint16_t)(((uint32_t)(bw) * 150U) >> 8))

/* 从打包字节里取一个角的样式（每角 2bit，位移 = 角索引*2） */
#define _BOX_STYLE(o, i) ((uint8_t)(((o)->corner_styles >> ((uint8_t)(i) * 2U)) & 0x3U))

/**
 * @brief 颜色相等比较（RGB565/RGB888），供 setter 的“值未变则跳过”守卫使用。
 */
static __inline uint8_t _box_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#elif (LCD_DEEP == DEEP_RGB888)
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/* 一次角落绘制所需的几何快照（外轮廓 + 内轮廓 + 有效透明度） */
typedef struct
{
    int16_t  x, y, w, h;     /* 外轮廓矩形 */
    int16_t  xi, yi, wi, hi; /* 内轮廓矩形（内缩 bw；wi/hi<=0 表示无内轮廓） */
    uint16_t r_out[4];       /* 各角外半径（已钳制） */
    uint16_t r_in[4];        /* 各角内半径（已钳制） */
    uint8_t  bw;             /* 边框厚度 */
    uint8_t  eff_op;         /* 级联后的有效透明度 */
} _box_geo_t;

/**
 * @brief 圆角角落：对一个 K×K 方块执行“填充 + 边框环”合成。
 * @param bx/by 角落方块左上角（屏幕绝对坐标）。
 * @param k 方块边长。
 * @param q 角落象限（WE_MASK_QUADRANT_xx）。
 * @note 方块内平直区零 mask 开销；圆角 AA 区带边框时内外覆盖共享一遍子采样。
 */
static void _box_draw_corner_round(const we_box_obj_t *o, we_lcd_t *lcd,
                                   const _box_geo_t *g, int16_t bx, int16_t by,
                                   uint16_t k, uint8_t q)
{
    int16_t cx0 = bx;
    int16_t cy0 = by;
    int16_t cx1 = (int16_t)(bx + (int16_t)k - 1);
    int16_t cy1 = (int16_t)(by + (int16_t)k - 1);
    int16_t x1  = (int16_t)(g->x + g->w - 1);
    int16_t y1  = (int16_t)(g->y + g->h - 1);
    uint16_t r  = g->r_out[q];
    uint16_t ri = g->r_in[q];
    int16_t sx;
    int16_t sy;
    uint8_t has_in;
    int16_t px;
    int16_t py;
    uint16_t stride;
    colour_t *row;

    /* 外圆角 r×r 外接方块左上角 */
    sx = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB)
           ? g->x : (int16_t)(x1 - (int16_t)r + 1);
    sy = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT)
           ? g->y : (int16_t)(y1 - (int16_t)r + 1);
    has_in = (uint8_t)((g->bw > 0U) && (g->wi > 0) && (g->hi > 0));

    /* 裁剪到当前 PFB 切片，避免越界写入 */
    if (cx0 < (int16_t)lcd->pfb_area.x0) cx0 = (int16_t)lcd->pfb_area.x0;
    if (cy0 < (int16_t)lcd->pfb_y_start) cy0 = (int16_t)lcd->pfb_y_start;
    if (cx1 > (int16_t)lcd->pfb_area.x1) cx1 = (int16_t)lcd->pfb_area.x1;
    if (cy1 > (int16_t)lcd->pfb_y_end) cy1 = (int16_t)lcd->pfb_y_end;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    stride = lcd->pfb_width;
    row = lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)lcd->pfb_y_start) * stride
        + (uint32_t)(cx0 - (int16_t)lcd->pfb_area.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        int16_t v = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT)
                      ? (int16_t)(py - g->y) : (int16_t)(y1 - py);
        colour_t *p = row;

        for (px = cx0; px <= cx1; px++, p++)
        {
            int16_t u = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB)
                          ? (int16_t)(px - g->x) : (int16_t)(x1 - px);
            uint8_t a_out;
            uint8_t a_in;

            if (u >= (int16_t)r || v >= (int16_t)r)
            {
                /* 圆角方块外的平直区：按内矩形边界直接分边框/填充，零 mask 开销。
                 * （内圆角方块必在外圆角方块之内，此处不会落入内 AA 区。） */
                if (g->bw == 0U ||
                    (has_in && u >= (int16_t)g->bw && v >= (int16_t)g->bw))
                {
                    if (g->eff_op >= 250U)
                        we_store_color(p, o->bg_color);
                    else
                        we_store_blended_color(p, o->bg_color, g->eff_op);
                }
                else
                {
                    if (g->eff_op >= 250U)
                        we_store_color(p, o->border_color);
                    else
                        we_store_blended_color(p, o->border_color, g->eff_op);
                }
                continue;
            }

            /* 圆角 AA 区：带边框时内外覆盖共享一遍 4x4 子采样 */
            if (g->bw == 0U)
            {
                a_out = we_mask_quarter_circle_alpha(sx, sy, r, q, px, py);
                a_in  = a_out; /* 无边框：全部记作填充 */
            }
            else
            {
                a_out = we_mask_quarter_ring_alpha(sx, sy, r, ri, q, px, py, &a_in);
            }

            if (a_out == 0U)
                continue;
            if (a_in != 0U)
            {
                if (a_in >= 250U && g->eff_op >= 250U)
                    we_store_color(p, o->bg_color);
                else
                    we_store_blended_color(p, o->bg_color,
                                           we_div255((uint32_t)g->eff_op * a_in));
            }
            if (a_out > a_in)
            {
                uint8_t a_bd = (uint8_t)(a_out - a_in);
                if (a_bd >= 250U && g->eff_op >= 250U)
                    we_store_color(p, o->border_color);
                else
                    we_store_blended_color(p, o->border_color,
                                           we_div255((uint32_t)g->eff_op * a_bd));
            }
        }
    }
}

/**
 * @brief 切角行扫描的分段写入：把 u 坐标区间 [ua, ub] 映射为像素段并整块写入。
 * @param row_cx0 本行 PFB 指针（指向 cx0 列）。
 * @param cx0/cx1 本行已裁剪的列范围（屏幕绝对坐标）。
 * @param bx 角落方块左上角 X；k 方块边长。
 * @param left 非 0 表示左侧角落（u 随 px 递增），否则右侧（u 随 px 递减）。
 * @param mask 段覆盖 alpha（0~255），与 eff_op 相乘后写入。
 */
static void _box_chamfer_span(colour_t *row_cx0, int16_t cx0, int16_t cx1,
                              int16_t bx, uint16_t k, uint8_t left,
                              int16_t ua, int16_t ub,
                              colour_t color, uint8_t mask, uint8_t eff_op)
{
    int16_t pxa;
    int16_t pxb;
    int16_t n;
    uint8_t alpha;
    colour_t *p;

    if (ua < 0)
        ua = 0;
    if (ub > (int16_t)(k - 1U))
        ub = (int16_t)(k - 1U);
    if (ua > ub)
        return;

    if (left)
    {
        pxa = (int16_t)(bx + ua);
        pxb = (int16_t)(bx + ub);
    }
    else
    {
        pxa = (int16_t)(bx + ((int16_t)(k - 1U) - ub));
        pxb = (int16_t)(bx + ((int16_t)(k - 1U) - ua));
    }
    if (pxa < cx0) pxa = cx0;
    if (pxb > cx1) pxb = cx1;
    if (pxa > pxb)
        return;

    alpha = we_div255((uint32_t)eff_op * mask);
    if (alpha <= 5U)
        return;

    p = row_cx0 + (pxa - cx0);
    n = (int16_t)(pxb - pxa + 1);
    if (alpha >= 250U)
    {
        while (n--)
        {
            we_store_color(p, color);
            p++;
        }
    }
    else
    {
        while (n--)
        {
            we_store_blended_color(p, color, alpha);
            p++;
        }
    }
}

/**
 * @brief 切角角落：按行分段写入一个 K×K 方块（无逐像素函数调用）。
 * @note 以角顶点为原点的 (u,v) 坐标里，外切线为 u+v = r-1（该像素 alpha=128），
 *       内切线为 u'+v' = r_in-1（u'=u-bw, v'=v-bw）。每行只需两个断点：
 *       外 AA 像素 u0 = r-1-v，内 AA 像素 u = bw + (r_in-1-(v-bw))，
 *       其余像素按 空/边框/填充 三段整块写。内外切线间距 ≥ 2 像素
 *       （2bw - 0.586bw > 1，bw>=1），两个 AA 像素不会重叠。
 */
static void _box_draw_corner_chamfer(const we_box_obj_t *o, we_lcd_t *lcd,
                                     const _box_geo_t *g, int16_t bx, int16_t by,
                                     uint16_t k, uint8_t q)
{
    int16_t cx0 = bx;
    int16_t cy0 = by;
    int16_t cx1 = (int16_t)(bx + (int16_t)k - 1);
    int16_t cy1 = (int16_t)(by + (int16_t)k - 1);
    int16_t y1  = (int16_t)(g->y + g->h - 1);
    uint8_t left = (uint8_t)(q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB);
    uint8_t top  = (uint8_t)(q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT);
    uint8_t has_in = (uint8_t)((g->bw > 0U) && (g->wi > 0) && (g->hi > 0));
    int16_t py;
    uint16_t stride;
    colour_t *row;

    /* 裁剪到当前 PFB 切片 */
    if (cx0 < (int16_t)lcd->pfb_area.x0) cx0 = (int16_t)lcd->pfb_area.x0;
    if (cy0 < (int16_t)lcd->pfb_y_start) cy0 = (int16_t)lcd->pfb_y_start;
    if (cx1 > (int16_t)lcd->pfb_area.x1) cx1 = (int16_t)lcd->pfb_area.x1;
    if (cy1 > (int16_t)lcd->pfb_y_end) cy1 = (int16_t)lcd->pfb_y_end;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    stride = lcd->pfb_width;
    row = lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)lcd->pfb_y_start) * stride
        + (uint32_t)(cx0 - (int16_t)lcd->pfb_area.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        int16_t v  = top ? (int16_t)(py - g->y) : (int16_t)(y1 - py);
        int16_t u0 = (int16_t)((int16_t)g->r_out[q] - 1 - v); /* 外切线 AA 像素 */
        int16_t in_aa = -1;                                   /* 内切线 AA 像素，<0 无 */
        int16_t fill_from;                                    /* 填充实心段起始 u */

        if (g->bw == 0U)
        {
            /* 无边框：外 AA 像素为半透填充，其后整段填充 */
            _box_chamfer_span(row, cx0, cx1, bx, k, left, u0, u0,
                              o->bg_color, 128U, g->eff_op);
            _box_chamfer_span(row, cx0, cx1, bx, k, left,
                              (int16_t)(u0 + 1), (int16_t)(k - 1U),
                              o->bg_color, 255U, g->eff_op);
            continue;
        }

        if (!has_in || v < (int16_t)g->bw)
        {
            fill_from = (int16_t)k; /* 本行无填充（全边框） */
        }
        else
        {
            int16_t u1 = (int16_t)((int16_t)g->r_in[q] - 1 - (v - (int16_t)g->bw));
            if (u1 >= 0)
            {
                in_aa     = (int16_t)((int16_t)g->bw + u1);
                fill_from = (int16_t)(in_aa + 1);
            }
            else
            {
                fill_from = (int16_t)g->bw; /* 内切线已过，填充从内矩形左沿开始 */
            }
        }

        /* 外切线 AA 像素：纯边框半透（内轮廓在此必为 0） */
        _box_chamfer_span(row, cx0, cx1, bx, k, left, u0, u0,
                          o->border_color, 128U, g->eff_op);
        /* 边框实心段 */
        _box_chamfer_span(row, cx0, cx1, bx, k, left, (int16_t)(u0 + 1),
                          (int16_t)((in_aa >= 0 ? in_aa : fill_from) - 1),
                          o->border_color, 255U, g->eff_op);
        /* 内切线 AA 像素：半透填充 + 半透边框叠加 */
        if (in_aa >= 0)
        {
            _box_chamfer_span(row, cx0, cx1, bx, k, left, in_aa, in_aa,
                              o->bg_color, 128U, g->eff_op);
            _box_chamfer_span(row, cx0, cx1, bx, k, left, in_aa, in_aa,
                              o->border_color, 127U, g->eff_op);
        }
        /* 填充实心段 */
        _box_chamfer_span(row, cx0, cx1, bx, k, left, fill_from, (int16_t)(k - 1U),
                          o->bg_color, 255U, g->eff_op);
    }
}

/**
 * @brief 控件绘制回调：快速整块填充 + 四角合成。
 */
static void _box_draw_cb(void *ptr)
{
    we_box_obj_t *o = (we_box_obj_t *)ptr;
    we_lcd_t *lcd;
    _box_geo_t g;
    int16_t x, y, x1, y1;
    int16_t w, h;
    uint16_t k;
    uint16_t half;
    uint8_t i;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL)
        return;

    x = o->base.x;
    y = o->base.y;
    w = o->base.w;
    h = o->base.h;
    if (w <= 0 || h <= 0)
        return;
    x1 = (int16_t)(x + w - 1);
    y1 = (int16_t)(y + h - 1);

    g.x = x;
    g.y = y;
    g.w = w;
    g.h = h;
    g.eff_op = we_opa_apply(lcd, o->opacity);
    if (g.eff_op == 0U)
        return;

    /* 各角半径钳制到宽高各半；边框厚度同样钳制 */
    half = (uint16_t)((w < h ? w : h) / 2);
    g.bw = o->border_w;
    if ((uint16_t)g.bw > half)
        g.bw = (uint8_t)half;

    k = g.bw;
    for (i = 0U; i < 4U; i++)
    {
        uint16_t r = o->corner_r[i];
        if (r > (uint16_t)(w / 2))
            r = (uint16_t)(w / 2);
        if (r > (uint16_t)(h / 2))
            r = (uint16_t)(h / 2);
        g.r_out[i] = r;
        if (r > k)
            k = r;
    }

    /* 内轮廓：内缩 bw 的同形轮廓（圆角减 bw；切角减 0.586·bw 保持等厚） */
    g.xi = (int16_t)(x + (int16_t)g.bw);
    g.yi = (int16_t)(y + (int16_t)g.bw);
    g.wi = (int16_t)(w - 2 * (int16_t)g.bw);
    g.hi = (int16_t)(h - 2 * (int16_t)g.bw);
    for (i = 0U; i < 4U; i++)
    {
        uint16_t r  = g.r_out[i];
        uint16_t in = (_BOX_STYLE(o, i) == (uint8_t)WE_BOX_CORNER_ROUND)
                        ? (uint16_t)g.bw : _BOX_CHAMFER_INSET(g.bw);
        uint16_t ri = (r > in) ? (uint16_t)(r - in) : 0U;
        if (g.wi <= 0 || g.hi <= 0)
            ri = 0U; /* 边框吃满整盒：无内轮廓，内半径必须清零 */
        else
        {
            if (ri > (uint16_t)(g.wi / 2))
                ri = (uint16_t)(g.wi / 2);
            if (ri > (uint16_t)(g.hi / 2))
                ri = (uint16_t)(g.hi / 2);
        }
        g.r_in[i] = ri;

        /* 切角内轮廓收缩量 < bw，内角复杂区可伸到 bw+ri > max(r,bw)，
         * K 必须同时罩住内外两个角落方块，快速填充带才全是平直区域 */
        if ((uint16_t)(g.bw + ri) > k)
            k = (uint16_t)(g.bw + ri);
    }

    /* K×K 角落方块不会越过中线（ri 已钳制到内矩形半宽，bw+ri <= half），
     * 四角分块互不重叠；此处再钳一次仅作防御 */
    if (k > half)
        k = half;

    /* --- 1. 上下两条 K 高横带的中段（去掉左右角落方块）：边框条 + 填充条 --- */
    if ((int16_t)(w - 2 * (int16_t)k) > 0)
    {
        int16_t xm = (int16_t)(x + (int16_t)k);
        uint16_t wm = (uint16_t)(w - 2 * (int16_t)k);
        uint16_t fh = (uint16_t)(k - g.bw); /* 横带内的填充条高度 */

        if (g.bw > 0U)
        {
            we_fill_rect(lcd, xm, y, wm, g.bw, o->border_color, o->opacity);
            we_fill_rect(lcd, xm, (int16_t)(y1 - (int16_t)g.bw + 1), wm, g.bw,
                         o->border_color, o->opacity);
        }
        if (fh > 0U)
        {
            we_fill_rect(lcd, xm, (int16_t)(y + (int16_t)g.bw), wm, fh,
                         o->bg_color, o->opacity);
            we_fill_rect(lcd, xm, (int16_t)(y1 - (int16_t)k + 1), wm, fh,
                         o->bg_color, o->opacity);
        }
    }

    /* --- 2. 中段整行区（两条横带之间）：左右边框柱 + 中央整块填充 --- */
    if ((int16_t)(h - 2 * (int16_t)k) > 0)
    {
        int16_t ym = (int16_t)(y + (int16_t)k);
        uint16_t hm = (uint16_t)(h - 2 * (int16_t)k);

        if (g.bw > 0U)
        {
            we_fill_rect(lcd, x, ym, g.bw, hm, o->border_color, o->opacity);
            we_fill_rect(lcd, (int16_t)(x1 - (int16_t)g.bw + 1), ym, g.bw, hm,
                         o->border_color, o->opacity);
        }
        we_fill_rect(lcd, (int16_t)(x + (int16_t)g.bw), ym,
                     (uint16_t)(w - 2 * (int16_t)g.bw), hm, o->bg_color, o->opacity);
    }

    /* --- 3. 四个 K×K 角落方块合成（圆角单遍子采样 / 切角行扫描） --- */
    if (k > 0U)
    {
        int16_t bxs[4];
        int16_t bys[4];

        bxs[0] = x;
        bys[0] = y;
        bxs[1] = (int16_t)(x1 - (int16_t)k + 1);
        bys[1] = y;
        bxs[2] = x;
        bys[2] = (int16_t)(y1 - (int16_t)k + 1);
        bxs[3] = (int16_t)(x1 - (int16_t)k + 1);
        bys[3] = (int16_t)(y1 - (int16_t)k + 1);

        for (i = 0U; i < 4U; i++)
        {
            if (_BOX_STYLE(o, i) == (uint8_t)WE_BOX_CORNER_CHAMFER)
                _box_draw_corner_chamfer(o, lcd, &g, bxs[i], bys[i], k, i);
            else
                _box_draw_corner_round(o, lcd, &g, bxs[i], bys[i], k, i);
        }
    }
}

/**
 * @brief 控件事件回调：有自定义回调则接管，否则装饰性穿透。
 */
static uint8_t _box_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_box_obj_t *o = (we_box_obj_t *)ptr;

    if (o == NULL)
        return 0U;
    if (o->user_event_cb != NULL)
        return o->user_event_cb(ptr, event, data);

    (void)event;
    (void)data;
    return 0U; /* 默认装饰性，不消费事件，让其穿透给背后控件 */
}

/**
 * @brief 容器/框架重定位回调。
 */
static void _box_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_box_set_pos((we_box_obj_t *)ptr, x, y);
}

/* --------------------------------------------------------------------------
 * 动画推进（编译期可关）
 * -------------------------------------------------------------------------- */
#if WE_BOX_USE_ANIM

/**
 * @brief 推进一个 Q8 进度并经缓动输出（两通道共用此样板）。
 * @return 1=需要更新（*eased 有效）；0=已就位（已 we_anim_stop 摘链）。
 */
static uint8_t _box_advance(we_box_obj_t *o, we_anim_t *node, uint16_t *t,
                            uint16_t ms, uint16_t elapsed_ms, uint16_t *eased)
{
    uint32_t delta;
    uint16_t e;

    if (*t >= 256U)
    {
        we_anim_stop(o->base.lcd, node); /* 已就位：摘链停表 */
        return 0U;
    }

    if (ms == 0U)
    {
        *t = 256U;
    }
    else
    {
        delta = (uint32_t)elapsed_ms * 256U / (uint32_t)ms;
        if (delta == 0U)
            delta = 1U;
        *t = ((uint32_t)*t + delta >= 256U) ? 256U : (uint16_t)(*t + delta);
    }

    e = o->ease ? o->ease(*t) : *t;
    if (e > 256U)
        e = 256U;
    *eased = e;
    return 1U;
}

/**
 * @brief 推进一步填充颜色动画（bg_color from→to）。
 */
static void _box_anim_col_step(we_box_obj_t *o, uint16_t elapsed_ms)
{
    uint16_t e;
    uint8_t  a;

    if (o == NULL || elapsed_ms == 0U)
        return;
    if (!_box_advance(o, &o->anim_col, &o->col_t, o->col_ms, elapsed_ms, &e))
        return;

    a = (uint8_t)((uint32_t)e * 255U / 256U); /* 0=起点色，255=终点色 */
    o->bg_color = we_colour_blend(o->c_to, o->c_from, a);
    we_obj_invalidate((we_obj_t *)o);

    if (o->col_t >= 256U)
        we_anim_stop(o->base.lcd, &o->anim_col);
}

static void _box_anim_col_step_cb(void *owner, uint16_t elapsed_ms)
{
    _box_anim_col_step((we_box_obj_t *)owner, elapsed_ms);
}

/**
 * @brief 推进一步透明度动画（opacity from→to）。
 */
static void _box_anim_opa_step(we_box_obj_t *o, uint16_t elapsed_ms)
{
    uint16_t e;

    if (o == NULL || elapsed_ms == 0U)
        return;
    if (!_box_advance(o, &o->anim_opa, &o->opa_t, o->opa_ms, elapsed_ms, &e))
        return;

    o->opacity = (uint8_t)we_lerp(o->opa_from, o->opa_to, e);
    we_obj_invalidate((we_obj_t *)o);

    if (o->opa_t >= 256U)
        we_anim_stop(o->base.lcd, &o->anim_opa);
}

static void _box_anim_opa_step_cb(void *owner, uint16_t elapsed_ms)
{
    _box_anim_opa_step((we_box_obj_t *)owner, elapsed_ms);
}

#endif /* WE_BOX_USE_ANIM */

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_box_obj_init(we_box_obj_t *obj, we_lcd_t *lcd,
                     int16_t x, int16_t y, int16_t w, int16_t h)
{
    uint8_t i;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd     = lcd;
    obj->base.class_p = &_box_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = w;
    obj->base.h       = h;

    obj->corner_styles = 0U; /* 四角全部圆角 */
    for (i = 0U; i < 4U; i++)
    {
        obj->corner_r[i] = (uint8_t)WE_BOX_DEF_RADIUS;
    }
    {
        colour_t c = RGB888_CONST(WE_BOX_DEF_R, WE_BOX_DEF_G, WE_BOX_DEF_B);
        obj->bg_color     = c;
        obj->border_color = c;
    }
    obj->border_w      = 0U;
    obj->opacity       = 255U;
    obj->user_event_cb = NULL;

#if WE_BOX_USE_ANIM
    obj->anim_col.next    = NULL;
    obj->anim_col.step_cb = NULL;
    obj->anim_col.owner   = NULL;
    obj->anim_opa.next    = NULL;
    obj->anim_opa.step_cb = NULL;
    obj->anim_opa.owner   = NULL;
    obj->ease   = we_ease_in_out_sine; /* 两通道共用缓动 */
    obj->col_ms = WE_BOX_ANIM_MS;
    obj->opa_ms = WE_BOX_ANIM_MS;
    obj->col_t  = 256U; /* 空闲（无动画在跑） */
    obj->opa_t  = 256U;
    obj->c_from   = obj->bg_color;
    obj->c_to     = obj->bg_color;
    obj->opa_from = obj->opacity;
    obj->opa_to   = obj->opacity;
#endif

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_box_set_corner(we_box_obj_t *obj, we_box_corner_idx_t idx,
                       we_box_corner_style_t style, uint16_t r)
{
    uint8_t r8;
    uint8_t styles;

    if (obj == NULL || (uint8_t)idx >= 4U)
        return;
    r8 = (r > 255U) ? 255U : (uint8_t)r;
    styles = (uint8_t)((obj->corner_styles & (uint8_t)~(0x3U << ((uint8_t)idx * 2U)))
                       | (((uint8_t)style & 0x3U) << ((uint8_t)idx * 2U)));
    if (styles == obj->corner_styles && obj->corner_r[idx] == r8)
        return;
    obj->corner_styles = styles;
    obj->corner_r[idx] = r8;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_box_set_radius(we_box_obj_t *obj, uint16_t r)
{
    uint8_t r8;
    uint8_t i;

    if (obj == NULL)
        return;
    r8 = (r > 255U) ? 255U : (uint8_t)r;
    if (obj->corner_styles == 0U &&
        obj->corner_r[0] == r8 && obj->corner_r[1] == r8 &&
        obj->corner_r[2] == r8 && obj->corner_r[3] == r8)
        return;
    obj->corner_styles = 0U; /* 四角全部圆角 */
    for (i = 0U; i < 4U; i++)
    {
        obj->corner_r[i] = r8;
    }
    we_obj_invalidate((we_obj_t *)obj);
}

void we_box_set_color(we_box_obj_t *obj, colour_t color)
{
    if (obj == NULL)
        return;
    if (_box_colour_eq(obj->bg_color, color))
        return;
    obj->bg_color = color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_box_set_border(we_box_obj_t *obj, colour_t color, uint8_t width)
{
    if (obj == NULL)
        return;
    if (_box_colour_eq(obj->border_color, color) && obj->border_w == width)
        return;
    obj->border_color = color;
    obj->border_w     = width;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_box_set_opacity(we_box_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_box_set_pos(we_box_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || (obj->base.x == x && obj->base.y == y))
        return;
    we_obj_invalidate((we_obj_t *)obj); /* 旧位置 */
    obj->base.x = x;
    obj->base.y = y;
    we_obj_invalidate((we_obj_t *)obj); /* 新位置 */
}

void we_box_set_size(we_box_obj_t *obj, int16_t w, int16_t h)
{
    if (obj == NULL || w <= 0 || h <= 0)
        return;
    if (obj->base.w == w && obj->base.h == h)
        return;
    we_obj_invalidate((we_obj_t *)obj); /* 旧尺寸 */
    obj->base.w = w;
    obj->base.h = h;
    we_obj_invalidate((we_obj_t *)obj); /* 新尺寸 */
}

void we_box_set_event_cb(we_box_obj_t *obj, we_box_event_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->user_event_cb = cb;
}

#if WE_BOX_USE_ANIM

void we_box_anim_color(we_box_obj_t *obj, colour_t target, uint16_t dur_ms,
                       we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    obj->c_from = obj->bg_color;
    obj->c_to   = target;
    obj->col_ms = dur_ms;
    obj->ease   = ease ? ease : we_ease_in_out_sine;
    obj->col_t  = 0U;
    we_anim_start(obj->base.lcd, &obj->anim_col, _box_anim_col_step_cb, obj);
}

void we_box_anim_opacity(we_box_obj_t *obj, uint8_t target, uint16_t dur_ms,
                         we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    obj->opa_from = obj->opacity;
    obj->opa_to   = target;
    obj->opa_ms   = dur_ms;
    obj->ease     = ease ? ease : we_ease_in_out_sine;
    obj->opa_t    = 0U;
    we_anim_start(obj->base.lcd, &obj->anim_opa, _box_anim_opa_step_cb, obj);
}

#else /* WE_BOX_USE_ANIM == 0：兼容桩，立即生效，调用方代码无需改动 */

void we_box_anim_color(we_box_obj_t *obj, colour_t target, uint16_t dur_ms,
                       we_ease_fn_t ease)
{
    (void)dur_ms;
    (void)ease;
    we_box_set_color(obj, target);
}

void we_box_anim_opacity(we_box_obj_t *obj, uint8_t target, uint16_t dur_ms,
                         we_ease_fn_t ease)
{
    (void)dur_ms;
    (void)ease;
    we_box_set_opacity(obj, target);
}

#endif /* WE_BOX_USE_ANIM */

void we_box_obj_delete(we_box_obj_t *obj)
{
    if (obj == NULL)
        return;
#if WE_BOX_USE_ANIM
    we_anim_stop(obj->base.lcd, &obj->anim_col);
    we_anim_stop(obj->base.lcd, &obj->anim_opa);
#endif
    we_obj_delete((we_obj_t *)obj);
}
