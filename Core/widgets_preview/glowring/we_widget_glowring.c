/**
 * @file  we_widget_glowring.c
 * @brief 透明渐变色环控件（glowring）实现 —— preview 孵化区
 *
 * 渲染：draw_cb 扫描内核已收窄到"包围盒 ∩ PFB 切片"的 lcd->clip，
 * 逐行用 d² 增量递推判环带（无乘法），踏入中心空洞直接跳到对称点；
 * 环带内像素经 八分区折叠 → 倒数表 → atan 表 求 512 步制角度（无除法），
 * 与 angle 求差后经剖面（默认余弦 = 线性渐变投影，可选三角）得 alpha，
 * 再乘内外边缘 1px 渐隐与整体不透明度，自带舍入混色直写 pfb_gram；
 * 可选双色（透明端尾色沿剖面向主色插值）与实心模式。
 *
 * 脏区：旋转 / 换色 / 改透明度只提交宽度贴圆周的水平条带（4 / 6 / 8 块，
 * 边界由 _gr_plan_build 在 init 时穷举最优并存入对象），包围盒四角与中心
 * 空洞永不刷新。
 *
 * 全程整数；每个像素的结果只取决于它自己的屏幕坐标，与 PFB 切片高度、
 * 脏矩形切法无关。
 */

#include "we_widget_glowring.h"
#include "wegui.h"

/* --------------------------------------------------------------------------
 * 查表（ROM 常量，Python 生成；见 widget.md）
 * -------------------------------------------------------------------------- */

/* 倒数表：_gr_recip[k] = round(65536 / k)，k = 1..256（[0] 不用；[1] 截到 65535）。
 * 八分区内 t = (短边 * _gr_recip[长边]) >> 8 得 Q8 正切比值 0..255，
 * 用一次乘法 + 一次查表替代逐像素除法。 */
static const uint16_t _gr_recip[257] = {
        0, 65535, 32768, 21845, 16384, 13107, 10923,  9362,
     8192,  7282,  6554,  5958,  5461,  5041,  4681,  4369,
     4096,  3855,  3641,  3449,  3277,  3121,  2979,  2849,
     2731,  2621,  2521,  2427,  2341,  2260,  2185,  2114,
     2048,  1986,  1928,  1872,  1820,  1771,  1725,  1680,
     1638,  1598,  1560,  1524,  1489,  1456,  1425,  1394,
     1365,  1337,  1311,  1285,  1260,  1237,  1214,  1192,
     1170,  1150,  1130,  1111,  1092,  1074,  1057,  1040,
     1024,  1008,   993,   978,   964,   950,   936,   923,
      910,   898,   886,   874,   862,   851,   840,   830,
      819,   809,   799,   790,   780,   771,   762,   753,
      745,   736,   728,   720,   712,   705,   697,   690,
      683,   676,   669,   662,   655,   649,   643,   636,
      630,   624,   618,   612,   607,   601,   596,   590,
      585,   580,   575,   570,   565,   560,   555,   551,
      546,   542,   537,   533,   529,   524,   520,   516,
      512,   508,   504,   500,   496,   493,   489,   485,
      482,   478,   475,   471,   468,   465,   462,   458,
      455,   452,   449,   446,   443,   440,   437,   434,
      431,   428,   426,   423,   420,   417,   415,   412,
      410,   407,   405,   402,   400,   397,   395,   392,
      390,   388,   386,   383,   381,   379,   377,   374,
      372,   370,   368,   366,   364,   362,   360,   358,
      356,   354,   352,   350,   349,   347,   345,   343,
      341,   340,   338,   336,   334,   333,   331,   329,
      328,   326,   324,   323,   321,   320,   318,   317,
      315,   314,   312,   311,   309,   308,   306,   305,
      303,   302,   301,   299,   298,   297,   295,   294,
      293,   291,   290,   289,   287,   286,   285,   284,
      282,   281,   280,   279,   278,   277,   275,   274,
      273,   272,   271,   270,   269,   267,   266,   265,
      264,   263,   262,   261,   260,   259,   258,   257,
      256,
};

/* 八分区 atan 表：_gr_atan[t] = round(atan(t/256) * 512 / 2π)，t = 0..256 → 0..64。
 * 与倒数表组合后整圈最大角度误差 < 1 步（1/512 圈）。 */
static const uint8_t _gr_atan[257] = {
     0,  0,  1,  1,  1,  2,  2,  2,  3,  3,  3,  3,  4,  4,  4,  5,
     5,  5,  6,  6,  6,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10, 10,
    10, 10, 11, 11, 11, 12, 12, 12, 13, 13, 13, 14, 14, 14, 14, 15,
    15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 18, 19, 19, 19, 20,
    20, 20, 21, 21, 21, 21, 22, 22, 22, 23, 23, 23, 24, 24, 24, 24,
    25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 28, 28, 28, 28, 29, 29,
    29, 30, 30, 30, 30, 31, 31, 31, 31, 32, 32, 32, 33, 33, 33, 33,
    34, 34, 34, 34, 35, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 38,
    38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 41, 42,
    42, 42, 42, 42, 43, 43, 43, 43, 44, 44, 44, 44, 45, 45, 45, 45,
    46, 46, 46, 46, 46, 47, 47, 47, 47, 48, 48, 48, 48, 48, 49, 49,
    49, 49, 50, 50, 50, 50, 50, 51, 51, 51, 51, 51, 52, 52, 52, 52,
    52, 53, 53, 53, 53, 53, 54, 54, 54, 54, 54, 55, 55, 55, 55, 55,
    56, 56, 56, 56, 56, 57, 57, 57, 57, 57, 57, 58, 58, 58, 58, 58,
    59, 59, 59, 59, 59, 59, 60, 60, 60, 60, 60, 61, 61, 61, 61, 61,
    61, 62, 62, 62, 62, 62, 62, 63, 63, 63, 63, 63, 63, 64, 64, 64,
    64,
};

/* 4x4 Bayer 有序抖动阈值（舍入项，0..255）：bayer[i] * 16 + 8，均值 128 与
 * 普通四舍五入一致；按屏幕绝对坐标索引，结果与 PFB 切片、脏矩形无关 */
#if (LCD_DEEP == DEEP_RGB565) && (WE_GLOWRING_DITHER == 1)
static const uint8_t _gr_bayer[16] = {
      8, 136,  40, 168,
    200,  72, 232, 104,
     56, 184,  24, 152,
    248, 120, 216,  88,
};
#endif

/* 外半径上限 = 倒数表覆盖范围 */
#define _GR_R_MAX (256U)

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void _gr_draw_cb(void *ptr);
static void _gr_set_pos_cb(void *ptr, int16_t x, int16_t y);

/* 纯显示控件：event_cb 为 NULL → 命中测试跳过，触摸穿透到下层 */
static const we_class_t _gr_class = {
    .draw_cb    = _gr_draw_cb,
    .event_cb   = NULL,
    .set_pos_cb = _gr_set_pos_cb
};

/* --------------------------------------------------------------------------
 * 脏区分块方案
 *
 * 环带（r_in² < d² < r_out²）用对称的水平条带覆盖，每条带的矩形宽度取该
 * 条带内圆周最宽的一行（贴圆），含空洞的条带拆成左右两块：
 *   4 块：中心带 [-m, m]（左右 2 块）+ 上下外带 [m+1, R']（各 1 块）
 *   6 块：上下外带再切一刀 t：[m+1, t] + [t+1, R']（各 1 块）
 *   8 块：[m+1, t] 整条落在空洞行内（t <= r_in），也拆成左右 2 块
 * R' = r_out − 1（d² < r_out² 的像素 |dx|,|dy| 都不超过 R'）。
 * 边界 m / t 在 init 时穷举：块数 4 为 O(R)，6 / 8 为 O(R²) 但内层用
 * "递减平方根"摊销（每层只做一次整数 sqrt），r_out = 255 也只需几十 k 次
 * 简单运算；块数 >= 6 时同时评估更少块数的方案，取面积最小者。
 * -------------------------------------------------------------------------- */

/**
 * @brief 整数平方根（floor(sqrt(v))），逐位法，无除法。
 * @param v 传入：被开方数（v < 0 视为无解，返回 -1）。
 * @return floor(sqrt(v))，v < 0 时返回 -1。
 */
static int32_t _gr_isqrt(int32_t v)
{
    uint32_t n, res = 0U, bit;

    if (v < 0)
        return -1;
    n = (uint32_t)v;
    bit = 1UL << 30;
    while (bit > n)
        bit >>= 2;
    while (bit != 0U)
    {
        if (n >= res + bit)
        {
            n  -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (int32_t)res;
}

/**
 * @brief 递减平方根：已知 x >= floor(sqrt(v))，向下步进到 floor(sqrt(v))。
 * @param x 传入：上一次的结果（单调递减序列的上界）。
 * @param v 传入：被开方数（< 0 返回 -1）。
 * @return floor(sqrt(v))。
 * @note 用于内层循环：v 随循环单调减小，总步进次数不超过起始 x，摊销 O(1)。
 */
static int32_t _gr_sqrt_down(int32_t x, int32_t v)
{
    if (v < 0)
        return -1;
    while (x > 0 && x * x > v)
        x--;
    return x;
}

/**
 * @brief 穷举最优条带边界并存入对象（init 时调用一次）。
 * @param o 传入/传出：控件对象（读 r_out / r_in，写 plan_kind 与 p_*）。
 * @return 无。
 */
static void _gr_plan_build(we_glowring_obj_t *o)
{
    int32_t R2 = (int32_t)o->r_out * o->r_out;
    int32_t I2 = (int32_t)o->r_in * o->r_in;
    int32_t Rp = (int32_t)o->r_out - 1;
    int32_t m_max = ((int32_t)o->r_in < Rp) ? (int32_t)o->r_in : Rp;
    int32_t best = 0x7FFFFFFF;
    int32_t m;

    o->plan_kind = 4U;
    o->p_m = o->p_t = o->p_xim = o->p_xo1 = o->p_xi1 = o->p_xo2 = 0;

    for (m = 0; m <= m_max; m++)
    {
        int32_t xim  = _gr_isqrt(I2 - m * m);              /* 中心带空洞半宽（最窄行在 |dy| = m） */
        int32_t mid  = 2 * (Rp - xim) * (2 * m + 1);       /* 中心带左右两块面积 */
        int32_t xo1  = (m + 1 <= Rp) ? _gr_isqrt(R2 - 1 - (m + 1) * (m + 1)) : 0;
        int32_t c4   = mid + 2 * (2 * xo1 + 1) * (Rp - m); /* 外带 [m+1, R'] 各 1 块 */

        if (c4 < best)
        {
            best = c4;
            o->plan_kind = 4U;
            o->p_m = (int16_t)m;  o->p_xim = (int16_t)xim;  o->p_xo1 = (int16_t)xo1;
        }
#if (WE_GLOWRING_DIRTY_RECTS >= 6)
        if (m + 1 <= Rp)
        {
            int32_t xo2 = xo1;  /* xo(t+1) 随 t 递减，用递减 sqrt 摊销 */
            int32_t xi1 = xim;  /* xi(t)  随 t 递减 */
            int32_t t;

            for (t = m + 1; t <= Rp - 1; t++)
            {
                int32_t c6;

                xo2 = _gr_sqrt_down(xo2, R2 - 1 - (t + 1) * (t + 1));
                c6  = mid + 2 * ((2 * xo1 + 1) * (t - m) + (2 * xo2 + 1) * (Rp - t));
                if (c6 < best)
                {
                    best = c6;
                    o->plan_kind = 6U;
                    o->p_m = (int16_t)m;  o->p_t = (int16_t)t;  o->p_xim = (int16_t)xim;
                    o->p_xo1 = (int16_t)xo1;  o->p_xo2 = (int16_t)xo2;
                }
#if (WE_GLOWRING_DIRTY_RECTS >= 8)
                if (t <= (int32_t)o->r_in)
                {
                    int32_t c8;

                    xi1 = _gr_sqrt_down(xi1, I2 - t * t);
                    c8  = mid + 2 * (2 * (xo1 - xi1) * (t - m) + (2 * xo2 + 1) * (Rp - t));
                    if (c8 < best)
                    {
                        best = c8;
                        o->plan_kind = 8U;
                        o->p_m = (int16_t)m;  o->p_t = (int16_t)t;  o->p_xim = (int16_t)xim;
                        o->p_xo1 = (int16_t)xo1;  o->p_xi1 = (int16_t)xi1;  o->p_xo2 = (int16_t)xo2;
                    }
                }
#endif
            }
        }
#endif
    }
}

/**
 * @brief 提交一个相对圆心的矩形（行列范围含端点）为脏区。
 * @param o 传入：控件对象。
 * @param cx 传入：圆心 X（屏幕绝对坐标）。
 * @param cy 传入：圆心 Y。
 * @param dx0 传入：列范围起点（相对圆心）。
 * @param dy0 传入：行范围起点。
 * @param dx1 传入：列范围终点（含）。
 * @param dy1 传入：行范围终点（含）。
 * @return 无。
 * @note we_obj_invalidate_area 以屏幕绝对坐标登记并沿父链裁剪。
 */
static void _gr_add_rect(we_glowring_obj_t *o, int16_t cx, int16_t cy,
                         int32_t dx0, int32_t dy0, int32_t dx1, int32_t dy1)
{
    if (dx1 < dx0 || dy1 < dy0)
        return;
    we_obj_invalidate_area((we_obj_t *)o, (int16_t)(cx + dx0), (int16_t)(cy + dy0),
                           (int16_t)(dx1 - dx0 + 1), (int16_t)(dy1 - dy0 + 1));
}

/**
 * @brief 以给定包围盒左上角为基准，按分块方案标脏环带。
 * @param o 传入：控件对象指针。
 * @param bx 传入：包围盒左上角 X（屏幕绝对坐标；set_pos 时可为旧位置）。
 * @param by 传入：包围盒左上角 Y。
 * @return 无。
 */
static void _gr_invalidate_ring(we_glowring_obj_t *o, int16_t bx, int16_t by)
{
    int16_t cx = (int16_t)(bx + o->base.w / 2);
    int16_t cy = (int16_t)(by + o->base.h / 2);
    int32_t Rp = (int32_t)o->r_out - 1;
    int32_t m  = o->p_m;
    int32_t t  = o->p_t;

    /* 中心带 [-m, m]：左右两块，挖掉空洞 [-xim, xim] */
    _gr_add_rect(o, cx, cy, -Rp, -m, -(int32_t)o->p_xim - 1, m);
    _gr_add_rect(o, cx, cy, (int32_t)o->p_xim + 1, -m, Rp, m);

    if (o->plan_kind == 4U)
    {
        _gr_add_rect(o, cx, cy, -o->p_xo1, m + 1, o->p_xo1, Rp);
        _gr_add_rect(o, cx, cy, -o->p_xo1, -Rp, o->p_xo1, -m - 1);
        return;
    }

    /* 侧带 1 [m+1, t]：6 块整条一块；8 块挖空洞拆左右 */
    if (o->plan_kind == 6U)
    {
        _gr_add_rect(o, cx, cy, -o->p_xo1, m + 1, o->p_xo1, t);
        _gr_add_rect(o, cx, cy, -o->p_xo1, -t, o->p_xo1, -m - 1);
    }
    else
    {
        _gr_add_rect(o, cx, cy, -o->p_xo1, m + 1, -(int32_t)o->p_xi1 - 1, t);
        _gr_add_rect(o, cx, cy, (int32_t)o->p_xi1 + 1, m + 1, o->p_xo1, t);
        _gr_add_rect(o, cx, cy, -o->p_xo1, -t, -(int32_t)o->p_xi1 - 1, -m - 1);
        _gr_add_rect(o, cx, cy, (int32_t)o->p_xi1 + 1, -t, o->p_xo1, -m - 1);
    }
    /* 侧带 2 [t+1, R'] */
    _gr_add_rect(o, cx, cy, -o->p_xo2, t + 1, o->p_xo2, Rp);
    _gr_add_rect(o, cx, cy, -o->p_xo2, -Rp, o->p_xo2, -t - 1);
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：逐像素合成透明渐变环带。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 窗口直接取内核已收窄的 lcd->clip（切片 ∩ 包围盒），再与自身包围盒
 *       求一次交集作防御。热路径无除法、无浮点：倒数表 + atan 表求角度，
 *       d² 行内增量递推，边缘渐隐用入口预算的 Q16 斜率。
 */
static WE_FAST_MEM void _gr_draw_cb(void *ptr)
{
    we_glowring_obj_t *o = (we_glowring_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t cx0, cy0, cx1, cy1;
    int32_t ccx, ccy;
    int32_t r_out2, r_out_f2, r_in2, r_in_f2;
    uint32_t k_out, k_in;
    uint16_t stride;
    uint16_t angle;
    colour_t *row;
    colour_t color;
    uint32_t fg_r, tl_r;
#if (LCD_DEEP != DEEP_GRAY8)
    uint32_t fg_g, fg_b, tl_g, tl_b;
#endif
    uint8_t eff_op;
    uint8_t two_tone, solid;
    const uint16_t *shape;
    uint32_t shape_mask, shape_shift;
    int16_t py;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL || o->base.w <= 0 || o->base.h <= 0)
        return;
    eff_op = we_opa_apply(lcd, o->opacity);
    if (eff_op == 0U)
        return;

    /* 内核已把 clip 收窄到包围盒 ∩ 切片；再与包围盒求交一次作防御（4 次比较） */
    cx0 = o->base.x;
    cy0 = o->base.y;
    cx1 = (int16_t)(o->base.x + o->base.w - 1);
    cy1 = (int16_t)(o->base.y + o->base.h - 1);
    if (cx0 < (int16_t)lcd->clip.x0) cx0 = (int16_t)lcd->clip.x0;
    if (cy0 < (int16_t)lcd->clip.y0) cy0 = (int16_t)lcd->clip.y0;
    if (cx1 > (int16_t)lcd->clip.x1) cx1 = (int16_t)lcd->clip.x1;
    if (cy1 > (int16_t)lcd->clip.y1) cy1 = (int16_t)lcd->clip.y1;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    ccx = (int32_t)o->base.x + o->base.w / 2;
    ccy = (int32_t)o->base.y + o->base.h / 2;

    /* 环带边界的平方值：外沿 / 内沿各 1px 渐隐带 */
    r_out2   = (int32_t)o->r_out * o->r_out;
    r_out_f2 = (int32_t)(o->r_out - 1U) * (o->r_out - 1U);
    r_in2    = (int32_t)o->r_in * o->r_in;
    r_in_f2  = (int32_t)(o->r_in + 1U) * (o->r_in + 1U);
    /* 渐隐斜率 Q16：alpha = (距离² 差) * k >> 16，入口两次除法，热路径零除法 */
    k_out = (255UL << 16) / (uint32_t)(r_out2 - r_out_f2); /* 分母 = 2·r_out − 1 > 0 */
    k_in  = (255UL << 16) / (uint32_t)(r_in_f2 - r_in2);   /* 分母 = 2·r_in + 1 > 0 */

    angle    = o->angle;
    color    = o->color;
    two_tone = o->two_tone;
    solid    = o->solid;
    shape    = o->shape_r_q4;
    if (o->shape_log2 == 0U || o->shape_log2 > 9U)
        shape = NULL;
    shape_mask  = shape ? ((1UL << o->shape_log2) - 1UL) : 0U;
    shape_shift = shape ? (9U - o->shape_log2) : 0U;   /* 512 步 → 表索引的右移位数 */
    stride   = lcd->pfb_width;
#if (LCD_DEEP == DEEP_RGB565)
    fg_r = (uint32_t)(color.dat16 >> 11) & 0x1FU;
    fg_g = (uint32_t)(color.dat16 >> 5) & 0x3FU;
    fg_b = (uint32_t)color.dat16 & 0x1FU;
    tl_r = (uint32_t)(o->tail_color.dat16 >> 11) & 0x1FU;
    tl_g = (uint32_t)(o->tail_color.dat16 >> 5) & 0x3FU;
    tl_b = (uint32_t)o->tail_color.dat16 & 0x1FU;
#elif (LCD_DEEP == DEEP_RGB888)
    fg_r = color.rgb.r;
    fg_g = color.rgb.g;
    fg_b = color.rgb.b;
    tl_r = o->tail_color.rgb.r;
    tl_g = o->tail_color.rgb.g;
    tl_b = o->tail_color.rgb.b;
#else
    fg_r = color.dat8;
    tl_r = o->tail_color.dat8;
#endif
    row = lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)lcd->clip.y0) * stride
        + (uint32_t)(cx0 - (int16_t)lcd->clip.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        int32_t dy  = (int32_t)py - ccy;
        int32_t dy2 = dy * dy;
        int32_t ady = (dy < 0) ? -dy : dy;
        int32_t dx, d2;
        int16_t px;
        colour_t *p;

        if (dy2 >= r_out2)
            continue; /* 整行在外圆之外 */

        px = cx0;
        p  = row;
        dx = (int32_t)px - ccx;
        d2 = dx * dx + dy2;

        while (px <= cx1)
        {
            if (d2 < r_out2)
            {
                if (d2 > r_in2)
                {
                    /* ---- 环带内像素 ---- */
                    uint32_t a;      /* 剖面值 0..255（实心时恒 255） */
                    uint32_t edge_a = 255U; /* 形状表外沿渐隐（无形状表时不用） */
                    uint32_t cr = fg_r;
#if (LCD_DEEP != DEEP_GRAY8)
                    uint32_t cg = fg_g, cb = fg_b;
#endif

                    if (solid && shape == NULL)
                    {
                        a = 255U;
                    }
                    else
                    {
                        int32_t ax = (dx < 0) ? -dx : dx;
                        uint32_t t;
                        int32_t base, theta, delta;

                        /* 八分区折叠：t = 短边/长边（Q8），查 atan 表得 0..64 */
                        if (ax >= ady)
                        {
                            t    = ((uint32_t)ady * _gr_recip[ax]) >> 8;
                            base = _gr_atan[t];
                        }
                        else
                        {
                            t    = ((uint32_t)ax * _gr_recip[ady]) >> 8;
                            base = 128 - _gr_atan[t];
                        }
                        /* 象限展开：0 = +X，顺时针（屏幕 Y 向下）增 */
                        if (dx >= 0)
                            theta = (dy >= 0) ? base : (512 - base);
                        else
                            theta = (dy >= 0) ? (256 - base) : (256 + base);

                        delta = (theta - (int32_t)angle) & 0x1FF;

                        /* 形状表：外沿半径随角度变化（项间线性插值，Q4），
                         * 越过外沿的像素直接丢弃，外沿 1px 内按同一渐隐斜率淡出 */
                        if (shape != NULL)
                        {
                            uint32_t idx  = (uint32_t)delta >> shape_shift;
                            uint32_t frac = (uint32_t)delta & ((1UL << shape_shift) - 1UL);
                            int32_t  r0   = shape[idx];
                            int32_t  r1   = shape[(idx + 1U) & shape_mask];
                            int32_t  rq4  = r0 + (((r1 - r0) * (int32_t)frac) >> shape_shift);
                            int32_t  diff = ((rq4 * rq4) >> 8) - d2;   /* 外沿半径² − d²（px²） */

                            if (diff <= 0)
                                goto _gr_pixel_skip;
                            if (diff < (rq4 >> 3))                     /* < 2R：外沿 1px 渐隐带 */
                                edge_a = ((uint32_t)diff * k_out) >> 16;
                        }
                        if (solid)
                        {
                            a = 255U;
                            goto _gr_profile_done;
                        }
#if (WE_GLOWRING_PROFILE == 1)
                        /* 余弦剖面：alpha = (1 − cos Δ)/2，即穿过圆心的线性渐变投到圆周
                         * （we_cos 为 Q15 查表；乘 255 后加半值舍入，Δ=0 → 0，Δ=180° → 255） */
                        a = (((uint32_t)(32768 - (int32_t)we_cos((int16_t)delta)) * 255U) + 32768U) >> 16;
#else
                        /* 三角剖面：|Δ| 折到 0..256，alpha 0 → 255 线性 */
                        if (delta > 256)
                            delta = 512 - delta;
                        a = ((uint32_t)delta * 255U) >> 8;
#endif
                        /* 双色：颜色沿同一剖面从尾色插到主色（与 SVG 渐变一样，色与透明度各自线性） */
                        if (two_tone)
                        {
                            uint32_t w  = a + (a >> 7);   /* 0..256 */
                            uint32_t iw = 256U - w;
                            cr = (fg_r * w + tl_r * iw + 128U) >> 8;
#if (LCD_DEEP != DEEP_GRAY8)
                            cg = (fg_g * w + tl_g * iw + 128U) >> 8;
                            cb = (fg_b * w + tl_b * iw + 128U) >> 8;
#endif
                        }
                    }

_gr_profile_done:
                    /* 内外边缘 1px 渐隐（用 d² 线性近似 d，1px 内视觉无差）；
                     * 有形状表时外沿由表决定（edge_a 已算好），只保留内沿 */
                    if (shape != NULL)
                    {
                        if (edge_a < 255U)
                            a = we_div255(a * edge_a);
                        else if (d2 < r_in_f2)
                            a = we_div255(a * (((uint32_t)(d2 - r_in2) * k_in) >> 16));
                    }
                    else if (d2 > r_out_f2)
                        a = we_div255(a * (((uint32_t)(r_out2 - d2) * k_out) >> 16));
                    else if (d2 < r_in_f2)
                        a = we_div255(a * (((uint32_t)(d2 - r_in2) * k_in) >> 16));

                    if (eff_op < 255U)
                        a = we_div255(a * eff_op);

                    /* 自带四舍五入混色：内核 we_store_blended_color 在 alpha>=250
                     * 直接写前景色、中段又截断取整，255 附近会跳 2 级形成高亮带；
                     * 这里把 0..255 扩到 0..256 后按 >>8 带舍入项混色，alpha=255
                     * 恰好等于前景色，全程单调无台阶。 */
                    if (a > 0U)
                    {
                        uint32_t a256 = a + (a >> 7);
                        uint32_t ia   = 256U - a256;
#if (LCD_DEEP == DEEP_RGB565)
                        /* 565 每通道只有 32/64 级，渐变会出 1 级宽台阶：用屏幕坐标索引
                         * 的 4x4 Bayer 阈值代替固定的 128 舍入项做有序抖动（与切片无关） */
#if (WE_GLOWRING_DITHER == 1)
                        uint32_t thr = _gr_bayer[((py & 3) << 2) | (px & 3)];
#else
                        uint32_t thr = 128U;
#endif
                        uint32_t bg = p->dat16;
                        uint32_t r5 = (cr * a256 + ((bg >> 11) & 0x1FU) * ia + thr) >> 8;
                        uint32_t g6 = (cg * a256 + ((bg >> 5) & 0x3FU) * ia + thr) >> 8;
                        uint32_t b5 = (cb * a256 + (bg & 0x1FU) * ia + thr) >> 8;
                        p->dat16 = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
#elif (LCD_DEEP == DEEP_RGB888)
                        p->rgb.r = (uint8_t)((cr * a256 + (uint32_t)p->rgb.r * ia + 128U) >> 8);
                        p->rgb.g = (uint8_t)((cg * a256 + (uint32_t)p->rgb.g * ia + 128U) >> 8);
                        p->rgb.b = (uint8_t)((cb * a256 + (uint32_t)p->rgb.b * ia + 128U) >> 8);
#else
                        p->dat8 = (uint8_t)((cr * a256 + (uint32_t)p->dat8 * ia + 128U) >> 8);
#endif
                    }
_gr_pixel_skip:;
                }
                else if (dx < 0)
                {
                    /* 踏入中心空洞：d² 关于 dx 对称，直接跳到镜像点（仍在空洞内，
                     * 下一步递增即落到右半环带首像素），空洞像素零遍历 */
                    int32_t jump = -dx - dx;
                    px = (int16_t)(px + jump);
                    p += jump;
                    dx = -dx;
                    if (px > cx1)
                        break;
                }
            }
            /* d² 增量递推：(dx+1)² = dx² + 2dx + 1 */
            d2 += dx + dx + 1;
            dx++;
            px++;
            p++;
        }
    }
}

/* --------------------------------------------------------------------------
 * 内部回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 容器/框架重定位回调。
 * @param ptr 回调透传对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
static void _gr_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_glowring_set_pos((we_glowring_obj_t *)ptr, x, y);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_glowring_obj_init(we_glowring_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t size, uint16_t band)
{
    uint16_t r_out;

    if (obj == NULL || lcd == NULL)
        return;

    if (size < 16U)
        size = 16U;
    if (size > _GR_R_MAX * 2U + 2U)
        size = (uint16_t)(_GR_R_MAX * 2U + 2U);

    obj->base.lcd     = lcd;
    obj->base.class_p = &_gr_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)size;
    obj->base.h       = (int16_t)size;

    /* 色环内切包围盒：外半径 = size/2 − 1（渐隐带不越界）；环带默认 size/5 */
    r_out = (uint16_t)(size / 2U - 1U);
    if (band == 0U)
        band = (uint16_t)(size / 5U);
    if (band < WE_GLOWRING_MIN_BAND)
        band = WE_GLOWRING_MIN_BAND;
    if (band + 2U > r_out)
        band = (uint16_t)(r_out - 2U); /* 内半径至少 2，保证 d² > 0 的像素才查表 */

    obj->r_out   = r_out;
    obj->r_in    = (uint16_t)(r_out - band);
    obj->color   = RGB888TODEV(WE_GLOWRING_DEF_R, WE_GLOWRING_DEF_G, WE_GLOWRING_DEF_B);
    obj->tail_color = obj->color;
    obj->angle    = 0U;
    obj->opacity  = 255U;
    obj->two_tone = 0U;
    obj->solid    = 0U;
    obj->shape_log2 = 0U;
    obj->shape_r_q4 = NULL;

    _gr_plan_build(obj);

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y);
}

void we_glowring_set_angle(we_glowring_obj_t *obj, uint16_t angle)
{
    if (obj == NULL)
        return;
    angle &= 0x1FFU;
    if (obj->angle == angle)
        return;
    obj->angle = angle;
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y);
}

void we_glowring_rotate(we_glowring_obj_t *obj, int16_t delta)
{
    if (obj == NULL)
        return;
    we_glowring_set_angle(obj, (uint16_t)((int32_t)obj->angle + delta));
}

uint16_t we_glowring_get_angle(const we_glowring_obj_t *obj)
{
    return (obj != NULL) ? obj->angle : 0U;
}

void we_glowring_set_color(we_glowring_obj_t *obj, colour_t color)
{
    if (obj == NULL || we_colour_eq(obj->color, color))
        return;
    obj->color = color;
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y);
}

void we_glowring_set_radii(we_glowring_obj_t *obj, uint16_t r_out, uint16_t r_in)
{
    uint16_t r_max;

    if (obj == NULL || obj->base.lcd == NULL)
        return;
    r_max = (uint16_t)(obj->base.w / 2 - 1);
    if (r_out > r_max)
        r_out = r_max;
    if (r_out > _GR_R_MAX)
        r_out = _GR_R_MAX;
    if (r_in + WE_GLOWRING_MIN_BAND > r_out)
        r_in = (uint16_t)(r_out - WE_GLOWRING_MIN_BAND);
    if (r_in < 2U)
        r_in = 2U;
    if (obj->r_out == r_out && obj->r_in == r_in)
        return;

    _gr_invalidate_ring(obj, obj->base.x, obj->base.y); /* 旧半径的条带 */
    obj->r_out = r_out;
    obj->r_in  = r_in;
    _gr_plan_build(obj);
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y); /* 新半径的条带 */
}

void we_glowring_set_shape(we_glowring_obj_t *obj, const uint16_t *r_q4, uint8_t log2_n)
{
    uint16_t r_out;

    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (r_q4 == NULL || log2_n == 0U || log2_n > 9U)
    {
        if (obj->shape_r_q4 == NULL)
            return;
        obj->shape_r_q4 = NULL;
        obj->shape_log2 = 0U;
        _gr_invalidate_ring(obj, obj->base.x, obj->base.y);
        return;
    }

    /* 外半径取表中最大值 +1 作 AA 余量（也是条带方案与行跳过的外界） */
    {
        uint32_t n = 1UL << log2_n, i;
        uint16_t m = 0U;
        for (i = 0U; i < n; i++)
            if (r_q4[i] > m)
                m = r_q4[i];
        r_out = (uint16_t)((m + 15U) >> 4) + 1U;
    }

    _gr_invalidate_ring(obj, obj->base.x, obj->base.y); /* 旧方案 */
    obj->shape_r_q4 = r_q4;
    obj->shape_log2 = log2_n;
    {
        uint16_t r_max = (uint16_t)(obj->base.w / 2 - 1);
        if (r_out > r_max)
            r_out = r_max;
        if (r_out > _GR_R_MAX)
            r_out = _GR_R_MAX;
        if (obj->r_in + WE_GLOWRING_MIN_BAND > r_out)
            obj->r_in = (uint16_t)(r_out - WE_GLOWRING_MIN_BAND);
        obj->r_out = r_out;
    }
    _gr_plan_build(obj);
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y); /* 新方案 */
}

void we_glowring_set_tail_color(we_glowring_obj_t *obj, colour_t tail, uint8_t enable)
{
    if (obj == NULL)
        return;
    enable = (enable != 0U) ? 1U : 0U;
    if (obj->two_tone == enable && (enable == 0U || we_colour_eq(obj->tail_color, tail)))
        return;
    obj->tail_color = tail;
    obj->two_tone   = enable;
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y);
}

void we_glowring_set_solid(we_glowring_obj_t *obj, uint8_t solid)
{
    if (obj == NULL)
        return;
    solid = (solid != 0U) ? 1U : 0U;
    if (obj->solid == solid)
        return;
    obj->solid = solid;
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y);
}

void we_glowring_set_opacity(we_glowring_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y);
}

void we_glowring_set_pos(we_glowring_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || (obj->base.x == x && obj->base.y == y))
        return;
    _gr_invalidate_ring(obj, obj->base.x, obj->base.y); /* 旧位置：只擦环带 */
    obj->base.x = x;
    obj->base.y = y;
    _gr_invalidate_ring(obj, x, y);                     /* 新位置：只画环带 */
}

void we_glowring_obj_delete(we_glowring_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
