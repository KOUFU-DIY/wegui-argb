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
 * 带边框的角落先画边框、再按内轮廓覆盖画填充：外沿以内的像素不会透出背后内容。
 *
 * 所有 set 接口在目标值与当前值相同时直接返回，不触发重绘。
 * 可选动画（WE_BOX_USE_ANIM，默认关闭）：开启后填充颜色 / 透明度各占一个独立
 * 中央动画节点，可同时进行，两通道共用一个缓动；关闭时 we_box_anim_* 退化为
 * 立即生效的兼容桩。
 */

#include "we_widget_box.h"
#include "wegui.h"

/* 单色样式：绘制路径的取色统一改道（白边黑底、边宽至少 1px，四角样式保留）；
 * 非单色档保持原字段直读。 */
#if (WE_CFG_MONO_STYLE == 1)
static const colour_t _box_mono_fg = RGB888_CONST(255, 255, 255);
static const colour_t _box_mono_bg = RGB888_CONST(0, 0, 0);
#define _BOX_BG_COLOR(o) _box_mono_bg
#define _BOX_BORDER_COLOR(o) _box_mono_fg
#define _BOX_BORDER_W(o) (((o)->border_w > 0U) ? (o)->border_w : 1U)
#else
#define _BOX_BG_COLOR(o) ((o)->bg_color)
#define _BOX_BORDER_COLOR(o) ((o)->border_color)
#define _BOX_BORDER_W(o) ((o)->border_w)
#endif

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _box_draw_cb(void *ptr);
static uint8_t _box_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

/* 没有用户回调时用 event_cb 为 NULL 的类：命中测试跳过它，触摸落到背后的控件；
 * 几何全部由 base 推导，移动走内核默认逻辑 */
static const we_class_t _box_class = {
    .draw_cb    = _box_draw_cb,
    .event_cb   = NULL,
    .set_pos_cb = NULL
};

/* 设了用户回调后换成这个类，全部事件交给用户回调 */
static const we_class_t _box_class_cb = {
    .draw_cb    = _box_draw_cb,
    .event_cb   = _box_event_cb,
    .set_pos_cb = NULL
};

/* 切角边框的内轮廓收缩系数：
 * 圆角内轮廓半径 = r - bw 即可得到处处等厚的边框；45° 切角若同样减 bw，
 * 对角段的垂直厚度只有 bw/√2（视觉偏细）。改为收缩 (2-√2)·bw ≈ 0.586·bw
 * 可让对角段与直边段等厚。Q8 定点：0.586*256 ≈ 150。 */
#define _BOX_CHAMFER_INSET(bw) ((uint16_t)(((uint32_t)(bw) * 150U) >> 8))

/* 从打包字节里取一个角的样式（每角 2bit，位移 = 角索引*2） */
#define _BOX_STYLE(o, i) ((uint8_t)(((o)->corner_styles >> ((uint8_t)(i) * 2U)) & 0x3U))

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
 * @brief 把 K×K 角落方块裁到当前窗口，并给出首行首列的显存指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param bx/by 角落方块左上角（屏幕绝对坐标）。
 * @param k 方块边长。
 * @param c 传出：裁剪后的列行范围 {x0, y0, x1, y1}。
 * @return 首行首列像素指针；方块与窗口不相交时返回 NULL。
 */
static colour_t *_box_corner_clip(we_lcd_t *lcd, int16_t bx, int16_t by, uint16_t k, int16_t c[4])
{
    c[0] = (bx < (int16_t)lcd->clip.x0) ? (int16_t)lcd->clip.x0 : bx;
    c[1] = (by < (int16_t)lcd->clip.y0) ? (int16_t)lcd->clip.y0 : by;
    c[2] = (int16_t)(bx + (int16_t)k - 1);
    c[3] = (int16_t)(by + (int16_t)k - 1);
    if (c[2] > (int16_t)lcd->clip.x1) c[2] = (int16_t)lcd->clip.x1;
    if (c[3] > (int16_t)lcd->clip.y1) c[3] = (int16_t)lcd->clip.y1;
    if (c[0] > c[2] || c[1] > c[3])
        return NULL;
    return lcd->pfb_gram
         + (uint32_t)(c[1] - (int16_t)lcd->clip.y0) * lcd->pfb_width
         + (uint32_t)(c[0] - (int16_t)lcd->clip.x0);
}

/**
 * @brief 圆角角落：对一个 K×K 方块执行“边框环 + 填充”合成。
 * @param bx/by 角落方块左上角（屏幕绝对坐标）。
 * @param k 方块边长。
 * @param q 角落象限（WE_MASK_QUADRANT_xx）。
 * @note 方块内平直区零 mask 开销；圆角 AA 区带边框时内外覆盖共享一遍子采样。
 *       合成顺序：先以外轮廓覆盖画边框，再以内轮廓覆盖画填充——外沿满覆盖的
 *       像素结果为 填充·内覆盖 + 边框·(1-内覆盖)，背后的内容不会从内沿透出。
 */
static void _box_draw_corner_round(const we_box_obj_t *o, we_lcd_t *lcd,
                                   const _box_geo_t *g, int16_t bx, int16_t by,
                                   uint16_t k, uint8_t q)
{
    int16_t c[4];
    int16_t x1  = (int16_t)(g->x + g->w - 1);
    int16_t y1  = (int16_t)(g->y + g->h - 1);
    uint16_t r  = g->r_out[q];
    uint16_t ri = g->r_in[q];
    int16_t sx;
    int16_t sy;
    uint8_t has_in;
    int16_t px;
    int16_t py;
    colour_t *row = _box_corner_clip(lcd, bx, by, k, c);

    if (row == NULL)
        return;

    /* 外圆角 r×r 外接方块左上角 */
    sx = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB)
           ? g->x : (int16_t)(x1 - (int16_t)r + 1);
    sy = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT)
           ? g->y : (int16_t)(y1 - (int16_t)r + 1);
    has_in = (uint8_t)((g->bw > 0U) && (g->wi > 0) && (g->hi > 0));

    for (py = c[1]; py <= c[3]; py++, row += lcd->pfb_width)
    {
        int16_t v = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT)
                      ? (int16_t)(py - g->y) : (int16_t)(y1 - py);
        colour_t *p = row;

        for (px = c[0]; px <= c[2]; px++, p++)
        {
            int16_t u = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB)
                          ? (int16_t)(px - g->x) : (int16_t)(x1 - px);
            uint8_t a_out;
            uint8_t a_in;

            if (u >= (int16_t)r || v >= (int16_t)r)
            {
                /* 圆角方块外的平直区：按内矩形边界直接分边框/填充，零 mask 开销。
                 * （内圆角方块必在外圆角方块之内，此处不会落入内 AA 区。） */
                uint8_t is_fill = (uint8_t)(g->bw == 0U ||
                                            (has_in && u >= (int16_t)g->bw && v >= (int16_t)g->bw));

                we_store_blended_color(p, is_fill ? _BOX_BG_COLOR(o) : _BOX_BORDER_COLOR(o), g->eff_op);
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
            if (a_out > a_in)
            {
                /* 边框按外轮廓覆盖先画 */
                if (a_out >= 250U && g->eff_op >= 250U)
                    we_store_color(p, _BOX_BORDER_COLOR(o));
                else
                    we_store_blended_color(p, _BOX_BORDER_COLOR(o),
                                           we_div255((uint32_t)g->eff_op * a_out));
            }
            if (a_in != 0U)
            {
                /* 填充按内轮廓覆盖压在边框之上 */
                if (a_in >= 250U && g->eff_op >= 250U)
                    we_store_color(p, _BOX_BG_COLOR(o));
                else
                    we_store_blended_color(p, _BOX_BG_COLOR(o),
                                           we_div255((uint32_t)g->eff_op * a_in));
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
 *       内 AA 像素先按边框实心写入、再叠半透填充，背后的内容不会透出。
 */
static void _box_draw_corner_chamfer(const we_box_obj_t *o, we_lcd_t *lcd,
                                     const _box_geo_t *g, int16_t bx, int16_t by,
                                     uint16_t k, uint8_t q)
{
    int16_t c[4];
    int16_t y1  = (int16_t)(g->y + g->h - 1);
    uint8_t left = (uint8_t)(q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB);
    uint8_t top  = (uint8_t)(q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT);
    uint8_t has_in = (uint8_t)((g->bw > 0U) && (g->wi > 0) && (g->hi > 0));
    int16_t py;
    colour_t *row = _box_corner_clip(lcd, bx, by, k, c);
    int16_t cx0 = c[0];
    int16_t cx1 = c[2];

    if (row == NULL)
        return;

    for (py = c[1]; py <= c[3]; py++, row += lcd->pfb_width)
    {
        int16_t v  = top ? (int16_t)(py - g->y) : (int16_t)(y1 - py);
        int16_t u0 = (int16_t)((int16_t)g->r_out[q] - 1 - v); /* 外切线 AA 像素 */
        int16_t in_aa = -1;                                   /* 内切线 AA 像素，<0 无 */
        int16_t fill_from;                                    /* 填充实心段起始 u */

        if (g->bw == 0U)
        {
            /* 无边框：外 AA 像素为半透填充，其后整段填充 */
            _box_chamfer_span(row, cx0, cx1, bx, k, left, u0, u0,
                              _BOX_BG_COLOR(o), 128U, g->eff_op);
            _box_chamfer_span(row, cx0, cx1, bx, k, left,
                              (int16_t)(u0 + 1), (int16_t)(k - 1U),
                              _BOX_BG_COLOR(o), 255U, g->eff_op);
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
                          _BOX_BORDER_COLOR(o), 128U, g->eff_op);
        /* 边框实心段（含内切线 AA 像素） */
        _box_chamfer_span(row, cx0, cx1, bx, k, left, (int16_t)(u0 + 1),
                          (int16_t)((in_aa >= 0) ? in_aa : (int16_t)(fill_from - 1)),
                          _BOX_BORDER_COLOR(o), 255U, g->eff_op);
        /* 内切线 AA 像素：在实心边框上叠半透填充 */
        if (in_aa >= 0)
            _box_chamfer_span(row, cx0, cx1, bx, k, left, in_aa, in_aa,
                              _BOX_BG_COLOR(o), 128U, g->eff_op);
        /* 填充实心段 */
        _box_chamfer_span(row, cx0, cx1, bx, k, left, fill_from, (int16_t)(k - 1U),
                          _BOX_BG_COLOR(o), 255U, g->eff_op);
    }
}

/**
 * @brief 控件绘制回调：快速整块填充 + 四角合成。
 * @note 当前窗口整个落在上下两条角落横带之间时，只画中段（左右边框柱 + 中央填充）。
 */
static void _box_draw_cb(void *ptr)
{
    we_box_obj_t *o = (we_box_obj_t *)ptr;
    we_lcd_t *lcd = o->base.lcd;
    _box_geo_t g;
    int16_t x, y, x1, y1;
    int16_t w, h;
    uint16_t k;
    uint16_t half;
    uint8_t i;

    if (o->opacity == 0U)
        return;

    x = o->base.x;
    y = o->base.y;
    w = o->base.w;
    h = o->base.h;
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
    g.bw = (uint8_t)_BOX_BORDER_W(o);
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

    /* 窗口整个落在两条角落横带之间：只有中段可见 */
    if (lcd->clip.y0 >= (int16_t)(y + (int16_t)k) && lcd->clip.y1 <= (int16_t)(y1 - (int16_t)k))
    {
        if (g.bw > 0U)
        {
            we_fill_rect(lcd, x, lcd->clip.y0, g.bw, (uint16_t)(lcd->clip.y1 - lcd->clip.y0 + 1),
                         _BOX_BORDER_COLOR(o), o->opacity);
            we_fill_rect(lcd, (int16_t)(x1 - (int16_t)g.bw + 1), lcd->clip.y0, g.bw,
                         (uint16_t)(lcd->clip.y1 - lcd->clip.y0 + 1), _BOX_BORDER_COLOR(o), o->opacity);
        }
        we_fill_rect(lcd, (int16_t)(x + (int16_t)g.bw), lcd->clip.y0, (uint16_t)(w - 2 * (int16_t)g.bw),
                     (uint16_t)(lcd->clip.y1 - lcd->clip.y0 + 1), _BOX_BG_COLOR(o), o->opacity);
        return;
    }

    /* --- 1. 上下两条 K 高横带的中段（去掉左右角落方块）：边框条 + 填充条 --- */
    if ((int16_t)(w - 2 * (int16_t)k) > 0)
    {
        int16_t xm = (int16_t)(x + (int16_t)k);
        uint16_t wm = (uint16_t)(w - 2 * (int16_t)k);
        uint16_t fh = (uint16_t)(k - g.bw); /* 横带内的填充条高度 */

        if (g.bw > 0U)
        {
            we_fill_rect(lcd, xm, y, wm, g.bw, _BOX_BORDER_COLOR(o), o->opacity);
            we_fill_rect(lcd, xm, (int16_t)(y1 - (int16_t)g.bw + 1), wm, g.bw,
                         _BOX_BORDER_COLOR(o), o->opacity);
        }
        if (fh > 0U)
        {
            we_fill_rect(lcd, xm, (int16_t)(y + (int16_t)g.bw), wm, fh,
                         _BOX_BG_COLOR(o), o->opacity);
            we_fill_rect(lcd, xm, (int16_t)(y1 - (int16_t)k + 1), wm, fh,
                         _BOX_BG_COLOR(o), o->opacity);
        }
    }

    /* --- 2. 中段整行区（两条横带之间）：左右边框柱 + 中央整块填充 --- */
    if ((int16_t)(h - 2 * (int16_t)k) > 0)
    {
        int16_t ym = (int16_t)(y + (int16_t)k);
        uint16_t hm = (uint16_t)(h - 2 * (int16_t)k);

        if (g.bw > 0U)
        {
            we_fill_rect(lcd, x, ym, g.bw, hm, _BOX_BORDER_COLOR(o), o->opacity);
            we_fill_rect(lcd, (int16_t)(x1 - (int16_t)g.bw + 1), ym, g.bw, hm,
                         _BOX_BORDER_COLOR(o), o->opacity);
        }
        we_fill_rect(lcd, (int16_t)(x + (int16_t)g.bw), ym,
                     (uint16_t)(w - 2 * (int16_t)g.bw), hm, _BOX_BG_COLOR(o), o->opacity);
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
 * @brief 控件事件回调（仅 _box_class_cb 使用）：全部事件交给用户回调。
 */
static uint8_t _box_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_box_obj_t *o = (we_box_obj_t *)ptr;

    return (o->user_event_cb != NULL) ? o->user_event_cb(ptr, event, data) : 0U;
}

/* --------------------------------------------------------------------------
 * 动画推进（编译期可关）
 * -------------------------------------------------------------------------- */
#if WE_BOX_USE_ANIM

/**
 * @brief 推进一个 Q8 进度并经缓动输出（两通道共用）。
 * @return 缓动后的进度（0..256）。
 */
static uint16_t _box_advance(const we_box_obj_t *o, uint16_t *t, uint16_t ms, uint16_t elapsed_ms)
{
    uint16_t e;

    *t = we_motion_advance(*t, ms, elapsed_ms);
    e = o->ease ? o->ease(*t) : *t;
    return (e > 256U) ? 256U : e;
}

/**
 * @brief 填充颜色动画步进回调（bg_color from→to，到位摘链）。
 */
static void _box_anim_col_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_box_obj_t *o = (we_box_obj_t *)owner;
    colour_t c;

    if (elapsed_ms == 0U)
        return;
    c = we_colour_blend(o->c_to, o->c_from,
                        (uint8_t)((uint32_t)_box_advance(o, &o->col_t, o->col_ms, elapsed_ms) * 255U / 256U));
    if (!we_colour_eq(c, o->bg_color))
    {
        o->bg_color = c;
        we_obj_invalidate((we_obj_t *)o);
    }
    if (o->col_t >= 256U)
        we_anim_stop(o->base.lcd, &o->anim_col);
}

/**
 * @brief 透明度动画步进回调（opacity from→to，到位摘链）。
 */
static void _box_anim_opa_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_box_obj_t *o = (we_box_obj_t *)owner;
    uint8_t a;

    if (elapsed_ms == 0U)
        return;
    a = (uint8_t)we_lerp(o->opa_from, o->opa_to, _box_advance(o, &o->opa_t, o->opa_ms, elapsed_ms));
    if (a != o->opacity)
    {
        o->opacity = a;
        we_obj_invalidate((we_obj_t *)o);
    }
    if (o->opa_t >= 256U)
        we_anim_stop(o->base.lcd, &o->anim_opa);
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
    if (we_colour_eq(obj->bg_color, color))
        return;
    obj->bg_color = color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_box_set_border(we_box_obj_t *obj, colour_t color, uint8_t width)
{
    if (obj == NULL)
        return;
    if (we_colour_eq(obj->border_color, color) && obj->border_w == width)
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

void we_box_set_size(we_box_obj_t *obj, int16_t w, int16_t h)
{
    if (obj == NULL || w <= 0 || h <= 0)
        return;
    we_obj_set_size((we_obj_t *)obj, w, h);
}

void we_box_set_event_cb(we_box_obj_t *obj, we_box_event_cb_t cb)
{
    if (obj == NULL || obj->base.class_p == NULL)
        return;
    obj->user_event_cb = cb;
    obj->base.class_p = (cb != NULL) ? &_box_class_cb : &_box_class;
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
