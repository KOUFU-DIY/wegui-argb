#include "we_widget_img_ex.h"

#include "we_render.h"

#define FP_BITS 12
#define FP_ONE (1 << FP_BITS)
#define FP_MASK (FP_ONE - 1)

/* --------------------------------------------------------------------------
 * 双线性插值只在高质量模式下编译。
 *
 * 对低成本 MCU 而言，这一段会明显增加代码量和每像素运算量。
 * 默认改成最近邻后，可以同时压 Flash 和提升旋转缩放时的执行效率。
 * -------------------------------------------------------------------------- */
#if (WE_IMG_EX_SAMPLE_MODE == 1)

/**
 * @brief 执行 lerp_rgb565。
 * @param c00 左上采样点颜色（RGB565）。
 * @param c10 右上采样点颜色（RGB565）。
 * @param c01 左下采样点颜色（RGB565）。
 * @param c11 右下采样点颜色（RGB565）。
 * @param fx X 方向小数权重（Q12）。
 * @param fy Y 方向小数权重（Q12）。
 * @return 返回对应结果值。
 */
static inline uint16_t lerp_rgb565(uint16_t c00, uint16_t c10, uint16_t c01, uint16_t c11, uint16_t fx, uint16_t fy)
{
    uint32_t w10 = ((uint32_t)fx * (FP_ONE - fy)) >> FP_BITS;
    uint32_t w01 = ((uint32_t)(FP_ONE - fx) * fy) >> FP_BITS;
    uint32_t w11 = ((uint32_t)fx * fy) >> FP_BITS;
    uint32_t w00 = FP_ONE - w10 - w01 - w11;

    uint32_t r = (((c00 >> 11) * w00) + ((c10 >> 11) * w10) + ((c01 >> 11) * w01) + ((c11 >> 11) * w11)) >> FP_BITS;
    uint32_t g = ((((c00 >> 5) & 0x3F) * w00) + (((c10 >> 5) & 0x3F) * w10) + (((c01 >> 5) & 0x3F) * w01) +
                  (((c11 >> 5) & 0x3F) * w11)) >>
                 FP_BITS;
    uint32_t b = (((c00 & 0x1F) * w00) + ((c10 & 0x1F) * w10) + ((c01 & 0x1F) * w01) + ((c11 & 0x1F) * w11)) >> FP_BITS;

    return (uint16_t)((r << 11) | (g << 5) | b);
}
#endif

/*
 * colour_t 级别的通用混色只在两种情况下保留：
 * 1. 屏幕不是 RGB565，需要走 rgb 成员回写
 * 2. 开了边缘 AA，且某些路径仍可能回落到 colour_t 混色
 *
 * 对当前主目标平台来说，通常是：
 * - LCD_DEEP == DEEP_RGB565
 * - WE_IMG_EX_ENABLE_EDGE_AA == 0
 *
 * 这时如果还把通用混色常驻，会白白占一段代码空间。
 */
#if (LCD_DEEP != DEEP_RGB565) || (WE_IMG_EX_ENABLE_EDGE_AA)
/* _blend_fast 现在直接委托给 we_render.h 中的 we_colour_blend，
 * 统一精度（255 分母），消除原先 256 分母带来的微小偏色。 */

/**
 * @brief 内部辅助：blend_fast。
 * @param fg 前景颜色。
 * @param bg 背景颜色。
 * @param alpha 透明度参数。
 * @return 无。
 */
static __inline colour_t _blend_fast(colour_t fg, colour_t bg, uint8_t alpha)
{
/**
 * @brief 执行 we_colour_blend。
 * @param fg 前景颜色。
 * @param bg 背景颜色。
 * @param alpha 透明度参数。
 * @return 无。
 */
    return we_colour_blend(fg, bg, alpha);
}

#if (LCD_DEEP == DEEP_RGB888)

/**
 * @brief 内部辅助：img_ex_color_from_rgb565。
 * @param pixel RGB565 原始像素值。
 * @return 无。
 */
static colour_t _img_ex_color_from_rgb565(uint16_t pixel)
{
    colour_t out;
    out.rgb.r = (uint8_t)(((pixel >> 11) & 0x1FU) << 3);
    out.rgb.g = (uint8_t)(((pixel >> 5) & 0x3FU) << 2);
    out.rgb.b = (uint8_t)((pixel & 0x1FU) << 3);
    return out;
}
#endif
#endif

/* --------------------------------------------------------------------------
 * 根据角度和缩放后的四个顶点，重新计算图片最小包围盒。
 *
 * 实现步骤：
 * 1. 先把图片看成以中心点为原点的四个角点
 * 2. 对四个角分别做旋转和缩放
 * 3. 取变换后的 min/max 得到真正需要刷新的区域
 *
 * 这样能减少脏矩形面积，避免旋转后仍按原图大框去重绘。
 * -------------------------------------------------------------------------- */

/**
 * @brief 内部辅助：calc_img_bbox。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param pivot_ofs_x X 方向坐标或偏移值。
 * @param pivot_ofs_y Y 方向坐标或偏移值。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x）。
 * @param out_x X 方向坐标或偏移值。
 * @param out_y Y 方向坐标或偏移值。
 * @param out_w 输出包围盒宽度。
 * @param out_h 输出包围盒高度。
 * @return 无。
 */
static void _calc_img_bbox(int16_t w, int16_t h, int16_t pivot_ofs_x, int16_t pivot_ofs_y, int16_t angle,
                           uint16_t scale_256, int16_t *out_x, int16_t *out_y, int16_t *out_w, int16_t *out_h)
{
#if (WE_IMG_EX_USE_TIGHT_BBOX)
    int32_t cos_a;
    int32_t sin_a;
    int32_t hw;
    int32_t hh;
    int32_t pivot_x;
    int32_t pivot_y;
    int32_t pts[4][2];
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    int32_t rx;
    int32_t ry;
    uint8_t i;

cos_a = we_cos(angle);
sin_a = we_sin(angle);
    hw = w / 2;
    hh = h / 2;
    pivot_x = pivot_ofs_x;
    pivot_y = pivot_ofs_y;

    /* 这里的四个角点都要先减掉“源图内部旋转中心偏移”。
     *
     * 含义是：
     * 1. 默认情况下，图片几何中心就是旋转中心，此时 pivot_ofs_x/y = 0
     * 2. 如果用户把旋转中心往左上或右下偏移，就等于让整张图围绕一个偏心点转
     * 3. 包围盒必须基于这个新旋转中心重新计算，否则标脏范围会不准
     */
    pts[0][0] = -hw - pivot_x;
    pts[0][1] = -hh - pivot_y;
    pts[1][0] = hw - pivot_x;
    pts[1][1] = -hh - pivot_y;
    pts[2][0] = -hw - pivot_x;
    pts[2][1] = hh - pivot_y;
    pts[3][0] = hw - pivot_x;
    pts[3][1] = hh - pivot_y;

    min_x = 0x7FFFFFFF;
    max_x = -0x7FFFFFFF;
    min_y = 0x7FFFFFFF;
    max_y = -0x7FFFFFFF;

    for (i = 0; i < 4; i++)
    {
        rx = (pts[i][0] * cos_a - pts[i][1] * sin_a) >> 15;
        ry = (pts[i][0] * sin_a + pts[i][1] * cos_a) >> 15;

        rx = (rx * scale_256) >> 8;
        ry = (ry * scale_256) >> 8;

        if (rx < min_x)
        {
            min_x = rx;
        }
        if (rx > max_x)
        {
            max_x = rx;
        }
        if (ry < min_y)
        {
            min_y = ry;
        }
        if (ry > max_y)
        {
            max_y = ry;
        }
    }

    /* 给边缘留 1 像素冗余，避免插值或裁剪时出现边缘缺口。 */
    *out_x = (int16_t)(min_x - 1);
    *out_y = (int16_t)(min_y - 1);
    *out_w = (int16_t)((max_x - min_x) + 3);
    *out_h = (int16_t)((max_y - min_y) + 3);
#else
    int32_t scaled_w;
    int32_t scaled_h;
    int32_t half_extent;

    (void)angle;

    /* ----------------------------------------------------------------------
     * 简化包围盒模式：
     * 1. 不再按四个角逐点旋转
     * 2. 直接把缩放后的宽高取较大值，近似成一个外接正方形
     * 3. 这样代码更短，但会让刷新面积变大
     *
     * 这个模式更适合“优先压代码”的场景。
     * ---------------------------------------------------------------------- */
    scaled_w = ((int32_t)w * scale_256 + 255) >> 8;
    scaled_h = ((int32_t)h * scale_256 + 255) >> 8;
half_extent = WE_MAX(scaled_w, scaled_h) / 2 + 1;

    /* 简化包围盒模式下没有逐点旋转，所以这里把 pivot 偏移量也保守并入半径。
     * 这样会多刷一点区域，但能保证偏心旋转时不漏刷。 */
    half_extent += (WE_ABS(pivot_ofs_x) > WE_ABS(pivot_ofs_y) ? WE_ABS(pivot_ofs_x) : WE_ABS(pivot_ofs_y));

    *out_x = (int16_t)(-half_extent);
    *out_y = (int16_t)(-half_extent);
    *out_w = (int16_t)(half_extent * 2 + 1);
    *out_h = (int16_t)(half_extent * 2 + 1);
#endif
}

/* --------------------------------------------------------------------------
 * 根据当前中心点、旋转中心偏移、角度和缩放，统一刷新 img_ex 的包围盒。
 *
 * 这样做的目的：
 * 1. 避免 init / set_center / set_transform / set_pivot_offset 各自重复算一遍
 * 2. 保证所有入口都使用同一套几何逻辑
 * 3. 后面如果继续扩展 skew / anchor 等能力，只需要集中改这里
 * -------------------------------------------------------------------------- */

/**
 * @brief 内部辅助：img_ex_update_bbox。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _img_ex_update_bbox(we_img_ex_obj_t *obj)
{
    int16_t bx;
    int16_t by;
    int16_t bw;
    int16_t bh;

    _calc_img_bbox(IMG_DAT_WIDTH(obj->img), IMG_DAT_HEIGHT(obj->img), obj->pivot_ofs_x, obj->pivot_ofs_y, obj->angle, obj->scale_256,
                   &bx, &by, &bw, &bh);

    obj->base.x = obj->cx + bx;
    obj->base.y = obj->cy + by;
    obj->base.w = bw;
    obj->base.h = bh;
}

/* --------------------------------------------------------------------------
 * 旋转缩放图片绘制回调
 *
 * 这一段是 img_ex 的热点路径，所以这里刻意做成“编译期裁剪”：
 * 1. 默认最近邻取样，去掉双线性插值的大块代码
 * 2. 默认关闭边缘羽化，减少每像素 alpha 路径
 * 3. 把每行复用的矩阵步进量提前算好，内层循环只保留必要操作
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param obj_ptr 回调透传的 img_ex 对象指针。
 * @return 无。
 */
static void we_img_ex_draw_cb(void *obj_ptr)
{
    we_img_ex_obj_t *obj = (we_img_ex_obj_t *)obj_ptr;
    we_lcd_t *p_lcd = obj->base.lcd;
    const uint8_t *img = obj->img;
    const uint8_t *img_data;
    uint16_t img_w;
    int16_t start_x;
    int16_t end_x;
    int16_t start_y;
    int16_t end_y;
    int32_t cos_a;
    int32_t sin_a;
    int32_t inv_scale_q16;
    int32_t step_x_x;
    int32_t step_y_x;
    int32_t step_x_y;
    int32_t step_y_y;
    int32_t img_half_w_q12;
    int32_t img_half_h_q12;
    int32_t pivot_x_q12;
    int32_t pivot_y_q12;
    int32_t dx0;
    int32_t dy0;
    int64_t rot_x0_q15;
    int64_t rot_y0_q15;
    int32_t curr_row_src_x;
    int32_t curr_row_src_y;
    uint32_t max_x_u32;
    uint32_t max_y_u32;
    uint32_t img_w_u32;
    int16_t x;
    int16_t y;
#if (WE_IMG_EX_ENABLE_EDGE_AA)
    uint8_t opacity_u8;
#endif

    /* img_ex 当前只接受未压缩 RGB565 原图。
     *
     * 这里之所以直接限制 img->dat_type == IMG_RGB565，
     * 是因为旋转缩放时需要对源图做“逐像素随机访问”：
     * - 目标图上的每一个像素
     * - 都要反推到源图中的任意位置
     *
     * 如果传入 RLE / QOI 之类压缩图，就没法用这种低开销方式直接取样，
     * 只能先整图解码到 RAM，再旋转缩放，那样会非常吃内存，也违背这套 GUI 的定位。
     */
    if (!p_lcd || !img || !p_lcd->pfb_gram || obj->scale_256 == 0 || IMG_DAT_FORMAT(img) != IMG_RGB565)
    {
        return;
    }

#if (!WE_IMG_EX_ASSUME_OPAQUE)
    if (obj->opacity <= 5U)
    {
        return;
    }
#endif

img_data = IMG_DAT_PIXELS(img);
img_w = IMG_DAT_WIDTH(img);
#if (WE_IMG_EX_ENABLE_EDGE_AA)
    opacity_u8 = obj->opacity;
#endif

    /* 步骤1：先把图片在当前 PFB 页中真正可见的区域裁出来。 */
    start_x = WE_MAX(obj->base.x, p_lcd->pfb_area.x0);
    end_x = WE_MIN(obj->base.x + obj->base.w - 1, p_lcd->pfb_area.x1);
    start_y = WE_MAX(obj->base.y, p_lcd->pfb_y_start);
    end_y = WE_MIN(obj->base.y + obj->base.h - 1, p_lcd->pfb_y_end);
    if (start_x > end_x || start_y > end_y)
    {
        return;
    }

    cos_a = we_cos(obj->angle);
    sin_a = we_sin(obj->angle);
    inv_scale_q16 = 16777216 / obj->scale_256;
    step_x_x = (int32_t)(((int64_t)cos_a * inv_scale_q16) >> 19);
    step_y_x = (int32_t)(((int64_t)(-sin_a) * inv_scale_q16) >> 19);
    step_x_y = (int32_t)(((int64_t)sin_a * inv_scale_q16) >> 19);
    step_y_y = (int32_t)(((int64_t)cos_a * inv_scale_q16) >> 19);

    img_half_w_q12 = (IMG_DAT_WIDTH(img) * FP_ONE) / 2;
    img_half_h_q12 = (IMG_DAT_HEIGHT(img) * FP_ONE) / 2;
    pivot_x_q12 = img_half_w_q12 + (obj->pivot_ofs_x * FP_ONE);
    pivot_y_q12 = img_half_h_q12 + (obj->pivot_ofs_y * FP_ONE);

    dx0 = start_x - obj->cx;
    dy0 = start_y - obj->cy;
    rot_x0_q15 = (int64_t)dx0 * cos_a + (int64_t)dy0 * sin_a;
    rot_y0_q15 = (int64_t)(-dx0) * sin_a + (int64_t)dy0 * cos_a;

    /* 这里的 pivot_x_q12 / pivot_y_q12 代表“源图内部真正参与旋转的中心点”。
     *
     * 默认情况下：
     * - pivot_ofs_x = 0
     * - pivot_ofs_y = 0
     * 那它就退化回图片几何中心。
     *
     * 当用户把它改成偏心点后：
     * 1. 屏幕上的 obj->cx / obj->cy 仍然是外部锚点
     * 2. 但源图采样时会围绕偏移后的内部中心旋转
     * 3. 这就很适合仪表指针、摇杆、机械臂等应用
     */
    curr_row_src_x = (int32_t)((rot_x0_q15 * inv_scale_q16) >> 19) + pivot_x_q12;
    curr_row_src_y = (int32_t)((rot_y0_q15 * inv_scale_q16) >> 19) + pivot_y_q12;

    /* 步骤3：准备统一边界。
     *
     * 这里把可采样的最大边界先转成定点格式：
     * - 源图坐标在内部一直用 Q12 定点数流动
     * - 判断是否合法时，不先拆成整数和小数，而是直接比较定点值
     *
     * 这样做的好处是：
     * 1. 不需要为负数、左边界、右边界分别写很多分支
     * 2. 利用无符号比较，一条判断就能同时拦住“负数”和“越界”
     * 3. 内层循环更短，编译器更容易压缩代码
     */
    max_x_u32 = (uint32_t)(IMG_DAT_WIDTH(img) - 1) << FP_BITS;
    max_y_u32 = (uint32_t)(IMG_DAT_HEIGHT(img) - 1) << FP_BITS;
    img_w_u32 = (uint32_t)img_w;

    /* 步骤4：按行扫描目标区域。
     *
     * 对每一行来说：
     * 1. curr_row_src_x / curr_row_src_y 代表这一行第一个目标像素对应的源图坐标
     * 2. 行内每往右画一个像素，只累加一次 step_x_x / step_y_x
     * 3. 扫完一整行，再把行起点加上 step_x_y / step_y_y，切到下一行
     *
     * 这就是“增量式逆映射”的关键。
     */
    for (y = start_y; y <= end_y; y++)
    {
        int32_t src_x_fp = curr_row_src_x;
        int32_t src_y_fp = curr_row_src_y;
        colour_t *p_dst =
            p_lcd->pfb_gram + (y - p_lcd->pfb_y_start) * p_lcd->pfb_width + (start_x - p_lcd->pfb_area.x0);

        for (x = start_x; x <= end_x; x++)
        {
            /* 步骤4-1：先判断当前反推得到的源图坐标是否可采样。
             *
             * 注意这里判断的是“定点坐标”：
             * - 小于 0 的值，在转成 uint32_t 后会变成极大值，自动判越界
             * - 大于最大边界的值，也会被直接拦住
             *
             * 所以这一条 if 同时处理了：
             * 1. 左/上越界
             * 2. 右/下越界
             */
            if ((uint32_t)src_x_fp < max_x_u32 && (uint32_t)src_y_fp < max_y_u32)
            {
                uint32_t sx = (uint32_t)src_x_fp >> FP_BITS;
                uint32_t sy = (uint32_t)src_y_fp >> FP_BITS;
                uint32_t offset = sy * img_w_u32 + sx;
                uint16_t final_color;

#if (WE_IMG_EX_SAMPLE_MODE == 1)
                /* 步骤4-2A：高质量模式。
                 *
                 * 这里会取周围 2x2 的 4 个像素做双线性插值：
                 * - sx / sy 是整数像素坐标
                 * - fx / fy 是定点坐标的小数部分
                 * - 用 fx / fy 作为权重混合 4 个邻近像素
                 *
                 * 优点是旋转缩放后的边缘更平滑；
                 * 缺点是代码更大、乘法更多、速度更慢。
                 *
                 * 像素按大端格式存储：byte[0]=高字节，byte[1]=低字节。
                 */
                const uint8_t *src_ptr = img_data + offset * 2;
                uint16_t fx = (uint16_t)src_x_fp & FP_MASK;
                uint16_t fy = (uint16_t)src_y_fp & FP_MASK;
#define _RD_BE16(p) (((uint16_t)(p)[0] << 8) | (p)[1])
                final_color = lerp_rgb565(
                    _RD_BE16(src_ptr),
                    _RD_BE16(src_ptr + 2),
                    _RD_BE16(src_ptr + img_w_u32 * 2),
                    _RD_BE16(src_ptr + img_w_u32 * 2 + 2),
                    fx, fy);
#undef _RD_BE16
#else
                /* 步骤4-2B：快速模式。
                 *
                 * 直接取最近邻像素，不做插值。
                 * 这是当前默认模式，优先照顾低成本 MCU 的体积和速度。
                 *
                 * 像素按大端格式存储：byte[0]=高字节，byte[1]=低字节。
                 */
                {
                    const uint8_t *px = img_data + offset * 2;
                    final_color = ((uint16_t)px[0] << 8) | px[1];
                }
#endif

#if (WE_IMG_EX_ENABLE_EDGE_AA)
                /* 步骤4-3A：如果开启边缘羽化，就根据离边界的距离修正透明度。
                 *
                 * 思路是：
                 * 1. 求当前采样点离四条边最近的距离
                 * 2. 如果已经靠近 1 像素边缘内，就把 alpha 逐渐衰减
                 * 3. 这样旋转后的斜边不会太硬
                 *
                 * 这段逻辑观感会更好，但会多出一整套边界距离计算和 alpha 混合路径。
                 */
                uint32_t u_src_x = (uint32_t)src_x_fp;
                uint32_t u_src_y = (uint32_t)src_y_fp;
                uint32_t dist_l = u_src_x;
                uint32_t dist_r = max_x_u32 - u_src_x;
                uint32_t dist_t = u_src_y;
                uint32_t dist_b = max_y_u32 - u_src_y;
                uint32_t min_x = dist_l < dist_r ? dist_l : dist_r;
                uint32_t min_y = dist_t < dist_b ? dist_t : dist_b;
                uint32_t min_dist = min_x < min_y ? min_x : min_y;
                uint32_t final_alpha = opacity_u8;

                if (min_dist < FP_ONE)
                {
                    final_alpha = (((min_dist * 255U) >> FP_BITS) * opacity_u8) >> 8;
                }
#if (LCD_DEEP == DEEP_RGB565)
                if (final_alpha >= 250U)
                {
                    p_dst->dat16 = final_color;
                }
                else if (final_alpha > 5U)
                {
                    colour_t fg;
                    colour_t pixel_color;
                    fg.dat16 = final_color;
pixel_color = _blend_fast(fg, *p_dst, (uint8_t)final_alpha);
                    p_dst->dat16 = pixel_color.dat16;
                }
#elif (LCD_DEEP == DEEP_RGB888)
                if (final_alpha >= 250U)
                {
colour_t fg = _img_ex_color_from_rgb565(final_color);
                    p_dst->rgb.r = fg.rgb.r;
                    p_dst->rgb.g = fg.rgb.g;
                    p_dst->rgb.b = fg.rgb.b;
                }
                else if (final_alpha > 5U)
                {
colour_t fg = _img_ex_color_from_rgb565(final_color);
colour_t pixel_color = _blend_fast(fg, *p_dst, (uint8_t)final_alpha);
                    p_dst->rgb.r = pixel_color.rgb.r;
                    p_dst->rgb.g = pixel_color.rgb.g;
                    p_dst->rgb.b = pixel_color.rgb.b;
                }
#endif

#else
/* 步骤4-3B：默认快速路径。
 *
 * - opacity 接近 255：直接覆写目标像素
 * - opacity 在 5~249：走一次快速混色
 * - opacity 很小：直接跳过
 *
 * 这比“每像素都做完整 alpha 运算”更适合 MCU。
 */
#if (WE_IMG_EX_ASSUME_OPAQUE)
                /* --------------------------------------------------
                 * 纯不透明模式：
                 * 1. 直接覆写目标像素
                 * 2. 完全裁掉 alpha 判断与混色逻辑
                 * 3. 这是最适合当前性能测试页的路径
                 * -------------------------------------------------- */
                {
colour_t fg = _img_ex_color_from_rgb565(final_color);
#if (LCD_DEEP == DEEP_RGB565)
                    p_dst->dat16 = fg.dat16;
#else
                    p_dst->rgb.r = fg.rgb.r;
                    p_dst->rgb.g = fg.rgb.g;
                    p_dst->rgb.b = fg.rgb.b;
#endif
                }
#else
                if (obj->opacity >= 250U)
                {
colour_t fg = _img_ex_color_from_rgb565(final_color);
#if (LCD_DEEP == DEEP_RGB565)
                    p_dst->dat16 = fg.dat16;
#else
                    p_dst->rgb.r = fg.rgb.r;
                    p_dst->rgb.g = fg.rgb.g;
                    p_dst->rgb.b = fg.rgb.b;
#endif
                }
                else
                {
#if (LCD_DEEP == DEEP_RGB565) && (WE_IMG_EX_ENABLE_FAST_RGB565_BLEND)
p_dst->dat16 = we_blend_rgb565(final_color, p_dst->dat16, obj->opacity);
#else
                    colour_t fg;
                    colour_t pixel_color;

fg = _img_ex_color_from_rgb565(final_color);
pixel_color = _blend_fast(fg, *p_dst, obj->opacity);
#if (LCD_DEEP == DEEP_RGB565)
                    p_dst->dat16 = pixel_color.dat16;
#else
                    p_dst->rgb.r = pixel_color.rgb.r;
                    p_dst->rgb.g = pixel_color.rgb.g;
                    p_dst->rgb.b = pixel_color.rgb.b;
#endif
#endif
                }
#endif
#endif
            }

            /* 步骤4-4：行内前进一步。
             *
             * 目标图 X 向右走 1 像素时，对应源图坐标按预先算好的步进量推进。
             */
            src_x_fp += step_x_x;
            src_y_fp += step_y_x;
            p_dst++;
        }

        /* 步骤4-5：切到下一行。
         *
         * 不是简单地 src_y + 1，而是按逆变换矩阵推进整行起点。
         * 这样下一行第一个像素的源图坐标仍然是精确对应的。
         */
        curr_row_src_x += step_x_y;
        curr_row_src_y += step_y_y;
    }
}

/* --------------------------------------------------------------------------
 * 容器滚动时，img_ex 只需要跟随平移中心点。
 * 本控件本身不消费点击，让事件继续留给父容器处理。
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _img_ex_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    if (event == WE_EVENT_SCROLLED && data)
    {
        we_img_ex_obj_t *img_ex = (we_img_ex_obj_t *)ptr;
        img_ex->cx += data->x;
        img_ex->cy += data->y;
    }

    return 0;
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param p_lcd GUI 运行时 LCD 上下文指针。
 * @param cx 控件变换中心的 X 坐标。
 * @param cy 控件变换中心的 Y 坐标。
 * @param img RGB565 未压缩图片资源指针。
 * @param op 初始不透明度（0~255）。
 * @return 无。
 */
void we_img_ex_obj_init(we_img_ex_obj_t *obj, we_lcd_t *p_lcd, int16_t cx, int16_t cy, const uint8_t *img, uint8_t op)
{
    if (!obj || !p_lcd || !img)
    {
        return;
    }

    obj->base.lcd = p_lcd;
    obj->img = img;
    obj->cx = cx;
    obj->cy = cy;
    obj->pivot_ofs_x = 0;
    obj->pivot_ofs_y = 0;
    obj->angle = 0;
    obj->scale_256 = 256;
    obj->opacity = op;

    /* 初始化时先计算一次默认姿态的包围盒，后续刷新直接沿用对象坐标。
     *
     * 调用注意：
     * img_ex 这里要求传入的 img 必须是未压缩 RGB565 原图。
     * 如果后面你换测试素材，优先检查 lcd_res.c 里该资源的 dat_type。 */
    _img_ex_update_bbox(obj);

    static const we_class_t _img_ex_class = {.draw_cb = we_img_ex_draw_cb, .event_cb = _img_ex_event_cb};

    obj->base.class_p = &_img_ex_class;
    obj->base.next = NULL;

    if (p_lcd->obj_list_head == NULL)
    {
        p_lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = p_lcd->obj_list_head;

        while (tail->next != NULL)
        {
            tail = tail->next;
        }
        tail->next = (we_obj_t *)obj;
    }

we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param cx 控件变换中心的 X 坐标。
 * @param cy 控件变换中心的 Y 坐标。
 * @return 无。
 */
void we_img_ex_obj_set_center(we_img_ex_obj_t *obj, int16_t cx, int16_t cy)
{
    if (obj == NULL || obj->base.lcd == NULL)
    {
        return;
    }
    if (obj->cx == cx && obj->cy == cy)
    {
        return;
    }

we_obj_invalidate((we_obj_t *)obj);

    obj->cx = cx;
    obj->cy = cy;
_img_ex_update_bbox(obj);

we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param ofs_x 源图内部旋转中心 X 偏移（像素）。
 * @param ofs_y 源图内部旋转中心 Y 偏移（像素）。
 * @return 无。
 */
void we_img_ex_obj_set_pivot_offset(we_img_ex_obj_t *obj, int16_t ofs_x, int16_t ofs_y)
{
    if (obj == NULL || obj->base.lcd == NULL)
    {
        return;
    }
    if (obj->pivot_ofs_x == ofs_x && obj->pivot_ofs_y == ofs_y)
    {
        return;
    }

    /* 改内部旋转中心会直接影响：
     * 1. 源图采样基准点
     * 2. 旋转后的外接包围盒
     * 所以这里和 set_transform 一样，需要先标旧区域，再重算新区域。 */
we_obj_invalidate((we_obj_t *)obj);

    obj->pivot_ofs_x = ofs_x;
    obj->pivot_ofs_y = ofs_y;
_img_ex_update_bbox(obj);

we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x）。
 * @return 无。
 */
void we_img_ex_obj_set_transform(we_img_ex_obj_t *obj, int16_t angle, uint16_t scale_256)
{
    if (obj == NULL || obj->base.lcd == NULL || (obj->angle == angle && obj->scale_256 == scale_256))
    {
        return;
    }

    /* 步骤1：先把旧区域标脏，保证旧图像在下一轮刷新中被擦除。 */
we_obj_invalidate((we_obj_t *)obj);

    obj->angle = angle;
    obj->scale_256 = scale_256;

    /* 步骤2：根据新姿态重新计算最小包围盒。 */
_img_ex_update_bbox(obj);

    /* 步骤3：把新区域也标脏，让刷新范围只覆盖真正变化的部分。 */
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_img_ex_obj_set_opacity(we_img_ex_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == opacity)
    {
        return;
    }

    obj->opacity = opacity;

    /* 透明度不改变几何范围，只需要刷新当前包围盒。 */
we_obj_invalidate((we_obj_t *)obj);
}
