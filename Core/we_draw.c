/**
 * @file  we_draw.c
 * @brief 绘图原语：矩形填充、四分之一圆/圆环/圆角矩形覆盖率蒙版、
 *        圆角矩形填充、像素、AA 线段（Xiaolin Wu）与宽线段、sin/cos 查表、
 *        整块 PFB 清底。零 malloc、零浮点热路径。
 */

#include "we_internal.h"

/**
 * @brief 从原始现场出发把 PFB 窗口收窄到指定矩形
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param sv 传入：save 保存的原始现场（收窄基准，始终相对它计算）
 * @param x0 传入：目标矩形左缘（屏幕绝对坐标）
 * @param y0 传入：目标矩形上缘
 * @param x1 传入：目标矩形右缘（含端点）
 * @param y1 传入：目标矩形下缘（含端点）
 * @return 1 收窄后窗口非空；0 无交集（窗口未改动，本次不必绘制）
 */
uint8_t we_pfb_clip_to(we_lcd_t *lcd, const we_pfb_clip_t *sv,
                       int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    int16_t cx0 = WE_MAX(sv->clip.x0, x0);
    int16_t cy0 = WE_MAX(sv->clip.y0, y0);
    int16_t cx1 = WE_MIN(sv->clip.x1, x1);
    int16_t cy1 = WE_MIN(sv->clip.y1, y1);

    if (cx0 > cx1 || cy0 > cy1)
        return 0U;

    lcd->clip.x0 = cx0;
    lcd->clip.x1 = cx1;
    lcd->clip.y0 = cy0;
    lcd->clip.y1 = cy1;
    lcd->pfb_gram = sv->gram + (int32_t)(cy0 - sv->clip.y0) * lcd->pfb_width + (cx0 - sv->clip.x0);
    return 1U;
}

/**
 * @brief 在局部帧缓冲(PFB)中绘制实心矩形
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param x 传入，矩形左上角 X 坐标
 * @param y 传入，矩形左上角 Y 坐标
 * @param w 传入，矩形宽度
 * @param h 传入，矩形高度
 * @param color 传入，填充颜色
 * @param opacity 传入，全局透明度(0~255)
 * @return 无
 */
/* 免级联缩放的内部实现：opacity 须为最终值（公开原语入口已缩放）。
 * 供 analytic_fill 等原语内部组合调用，避免 opa_scale 被双重应用
 * （否则圆角(×s¹)与矩形主体(×s²)在容器淡入淡出时出现亮度断层）。 */
static void _we_fill_rect_no_scale(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h, colour_t color, uint8_t opacity)
{
    int16_t x1, y1;
    int16_t draw_x_start, draw_y_start, draw_x_end, draw_y_end;
    uint16_t span;
    uint16_t dst_stride;
    colour_t *dst_line;
    int16_t py;

    if (w == 0 || h == 0 || opacity == 0)
        return;

    x1 = x + w - 1;
    y1 = y + h - 1;

    if ((x > p_lcd->clip.x1) || (x1 < p_lcd->clip.x0) || (y > p_lcd->clip.y1) || (y1 < p_lcd->clip.y0))
        return;

    draw_x_start = (x < p_lcd->clip.x0) ? p_lcd->clip.x0 : x;
    draw_y_start = (y < p_lcd->clip.y0) ? p_lcd->clip.y0 : y;
    draw_x_end = (x1 > p_lcd->clip.x1) ? p_lcd->clip.x1 : x1;
    draw_y_end = (y1 > p_lcd->clip.y1) ? p_lcd->clip.y1 : y1;

    span = (uint16_t)(draw_x_end - draw_x_start + 1);
    dst_stride = p_lcd->pfb_width;
    dst_line =
        p_lcd->pfb_gram + ((draw_y_start - p_lcd->clip.y0) * dst_stride) + (draw_x_start - p_lcd->clip.x0);

    if (opacity >= 250U)
    {
#if (LCD_DEEP == DEEP_RGB565)
        uint16_t color_val = color.dat16;
        /* 双像素打包成一个 32 位字，一次写 2 个像素，吞吐翻倍。
         * Cortex-M0 不支持非对齐 32 位访问，因此每行先补齐到 4 字节边界，
         * 再走 32 位主循环，最后收尾剩余像素。 */
        uint32_t color_val32 = ((uint32_t)color_val << 16) | color_val;

        for (py = draw_y_start; py <= draw_y_end; py++)
        {
            colour_t *p_dst = dst_line;
            uint16_t count = span;

            /* 1. 行首未 4 字节对齐时，先单独写一个像素补齐。 */
            if (((uintptr_t)p_dst & 0x3U) != 0U)
            {
                p_dst->dat16 = color_val;
                p_dst++;
                count--;
            }

            /* 2. 32 位主循环，每次写 2 个像素，再按 4 字一组展开。 */
            {
                uint32_t *p32 = (uint32_t *)(void *)p_dst; /* 上面已补齐到 4 字节边界 */
                uint16_t pairs = (uint16_t)(count >> 1);
                uint16_t blocks = (uint16_t)(pairs / 4U);
                uint16_t pair_rem = (uint16_t)(pairs % 4U);

                while (blocks--)
                {
                    p32[0] = color_val32;
                    p32[1] = color_val32;
                    p32[2] = color_val32;
                    p32[3] = color_val32;
                    p32 += 4;
                }
                while (pair_rem--)
                {
                    *p32++ = color_val32;
                }
                p_dst = (colour_t *)p32;
            }

            /* 3. 收尾：奇数像素剩 1 个时单独写。 */
            if (count & 1U)
            {
                p_dst->dat16 = color_val;
            }

            dst_line += dst_stride;
        }
#elif (LCD_DEEP == DEEP_RGB888)
        uint8_t r8 = color.rgb.r;
        uint8_t g8 = color.rgb.g;
        uint8_t b8 = color.rgb.b;

        for (py = draw_y_start; py <= draw_y_end; py++)
        {
            colour_t *p_dst = dst_line;
            uint16_t blocks = span / 4U;
            uint16_t rem = span % 4U;

            while (blocks--)
            {
                p_dst[0].rgb.r = r8;
                p_dst[0].rgb.g = g8;
                p_dst[0].rgb.b = b8;
                p_dst[1].rgb.r = r8;
                p_dst[1].rgb.g = g8;
                p_dst[1].rgb.b = b8;
                p_dst[2].rgb.r = r8;
                p_dst[2].rgb.g = g8;
                p_dst[2].rgb.b = b8;
                p_dst[3].rgb.r = r8;
                p_dst[3].rgb.g = g8;
                p_dst[3].rgb.b = b8;
                p_dst += 4;
            }

            while (rem--)
            {
                p_dst->rgb.r = r8;
                p_dst->rgb.g = g8;
                p_dst->rgb.b = b8;
                p_dst++;
            }

            dst_line += dst_stride;
        }
#elif (LCD_DEEP == DEEP_GRAY8)
        /* GRAY8：单字节亮度复制成 32 位字，行内按 4 像素批量写
         * （we_fill_gram 同款口径；M0 不允许非对齐 32 位访问，行首先补齐）。 */
        uint8_t color_val = color.dat8;
        uint32_t color_val32 = ((uint32_t)color_val << 24) | ((uint32_t)color_val << 16) |
                               ((uint32_t)color_val << 8) | color_val;

        for (py = draw_y_start; py <= draw_y_end; py++)
        {
            colour_t *p_dst = dst_line;
            uint16_t count = span;

            /* 1. 行首补齐到 4 字节边界。 */
            while (count > 0U && ((uintptr_t)p_dst & 0x3U) != 0U)
            {
                p_dst->dat8 = color_val;
                p_dst++;
                count--;
            }

            /* 2. 32 位主循环，每次写 4 个像素。 */
            {
                uint32_t *p32 = (uint32_t *)(void *)p_dst; /* 上面已补齐到 4 字节边界 */
                uint16_t words = (uint16_t)(count >> 2);

                while (words--)
                    *p32++ = color_val32;
                p_dst = (colour_t *)p32;
            }

            /* 3. 收尾不足 4 像素的部分。 */
            count &= 3U;
            while (count--)
            {
                p_dst->dat8 = color_val;
                p_dst++;
            }

            dst_line += dst_stride;
        }
#else
        for (py = draw_y_start; py <= draw_y_end; py++)
        {
            colour_t *p_dst = dst_line;
            uint16_t count = span;

            while (count--)
            {
                we_store_color(p_dst, color);
                p_dst++;
            }

            dst_line += dst_stride;
        }
#endif
        return;
    }

    for (py = draw_y_start; py <= draw_y_end; py++)
    {
        colour_t *p_dst = dst_line;
        uint16_t count = span;

        while (count--)
        {
            we_store_blended_color(p_dst, color, opacity);
            p_dst++;
        }

        dst_line += dst_stride;
    }
}

/**
 * @brief 计算轴对齐 90 度四分之一圆在单个像素上的 4x4 coverage
 * @param x 传入，外接正方形左上角 X 坐标
 * @param y 传入，外接正方形左上角 Y 坐标
 * @param radius 传入，圆角半径
 * @param quadrant 传入，象限标识，取值见 WE_MASK_QUADRANT_xx
 * @param px 传入，目标像素 X 坐标
 * @param py 传入，目标像素 Y 坐标
 * @return 返回 0~16 coverage（16 表示满覆盖）
 */
static uint8_t _we_mask_quarter_circle_cov16(int16_t x, int16_t y, uint16_t radius,
                                             uint8_t quadrant, int16_t px, int16_t py)
{
    int32_t cx16;
    int32_t cy16;
    int32_t r16;
    int32_t r16_sq;
    int32_t near_dx;
    int32_t near_dy;
    int32_t far_dx;
    int32_t far_dy;
    int32_t near_d2;
    int32_t far_d2;
    uint8_t coverage = 0U;
    uint8_t syi;
    uint8_t sxi;
    static const uint8_t sample_ofs[4] = { 2U, 6U, 10U, 14U };

    if (radius == 0U)
        return 0U;

    switch (quadrant)
    {
    case WE_MASK_QUADRANT_LT:
        cx16 = (int32_t)(x + radius) * 16;
        cy16 = (int32_t)(y + radius) * 16;
        near_dx = (int32_t)(px + 1 - (x + radius)) * 16;
        near_dy = (int32_t)(py + 1 - (y + radius)) * 16;
        far_dx = (int32_t)(px - (x + radius)) * 16;
        far_dy = (int32_t)(py - (y + radius)) * 16;
        break;
    case WE_MASK_QUADRANT_RT:
        cx16 = (int32_t)x * 16;
        cy16 = (int32_t)(y + radius) * 16;
        near_dx = (int32_t)(px - x) * 16;
        near_dy = (int32_t)(py + 1 - (y + radius)) * 16;
        far_dx = (int32_t)(px + 1 - x) * 16;
        far_dy = (int32_t)(py - (y + radius)) * 16;
        break;
    case WE_MASK_QUADRANT_LB:
        cx16 = (int32_t)(x + radius) * 16;
        cy16 = (int32_t)y * 16;
        near_dx = (int32_t)(px + 1 - (x + radius)) * 16;
        near_dy = (int32_t)(py - y) * 16;
        far_dx = (int32_t)(px - (x + radius)) * 16;
        far_dy = (int32_t)(py + 1 - y) * 16;
        break;
    case WE_MASK_QUADRANT_RB:
    default:
        cx16 = (int32_t)x * 16;
        cy16 = (int32_t)y * 16;
        near_dx = (int32_t)(px - x) * 16;
        near_dy = (int32_t)(py - y) * 16;
        far_dx = (int32_t)(px + 1 - x) * 16;
        far_dy = (int32_t)(py + 1 - y) * 16;
        break;
    }

    r16 = (int32_t)radius * 16;
    r16_sq = r16 * r16;
    near_d2 = near_dx * near_dx + near_dy * near_dy;
    if (near_d2 >= r16_sq)
        return 0U;

    far_d2 = far_dx * far_dx + far_dy * far_dy;
    if (far_d2 <= r16_sq)
        return 16U;

    for (syi = 0U; syi < 4U; syi++)
    {
        int32_t sy = (int32_t)py * 16 + sample_ofs[syi];
        int32_t dy = sy - cy16;
        int32_t dy2 = dy * dy;

        for (sxi = 0U; sxi < 4U; sxi++)
        {
            int32_t sx = (int32_t)px * 16 + sample_ofs[sxi];
            int32_t dx = sx - cx16;
            int32_t d2 = dx * dx + dy2;
            if (d2 <= r16_sq)
                coverage++;
        }
    }

    return coverage;
}

/**
 * @brief 计算轴对齐 90 度四分之一圆在单个像素上的 alpha mask
 * @param x 传入，外接正方形左上角 X 坐标
 * @param y 传入，外接正方形左上角 Y 坐标
 * @param radius 传入，圆角半径
 * @param quadrant 传入，象限标识，取值见 WE_MASK_QUADRANT_xx
 * @param px 传入，目标像素 X 坐标
 * @param py 传入，目标像素 Y 坐标
 * @return 返回 0~255 alpha（255 表示满覆盖）
 */
uint8_t we_mask_quarter_circle_alpha(int16_t x, int16_t y, uint16_t radius,
                                     uint8_t quadrant, int16_t px, int16_t py)
{
    uint8_t cov16 = _we_mask_quarter_circle_cov16(x, y, radius, quadrant, px, py);

    if (cov16 == 0U)
        return 0U;
    if (cov16 >= 16U)
        return 255U;
    return (uint8_t)(((uint32_t)cov16 * 255U + 8U) >> 4);
}

/**
 * @brief 计算同心内外两个四分之一圆在单个像素上的 alpha mask（单次子采样）
 * @param x 传入，外圆外接正方形左上角 X 坐标
 * @param y 传入，外圆外接正方形左上角 Y 坐标
 * @param r_out 传入，外圆半径
 * @param r_in 传入，内圆半径（<= r_out，0 表示无内圆）
 * @param quadrant 传入，象限标识，取值见 WE_MASK_QUADRANT_xx
 * @param px 传入，目标像素 X 坐标
 * @param py 传入，目标像素 Y 坐标
 * @param p_fill_alpha 传出，内圆覆盖 alpha（0~255，即“填充区”覆盖）
 * @return 返回外圆覆盖 alpha（0~255）；环带（边框）覆盖 = 返回值 - *p_fill_alpha
 * @note 供带边框圆角（如 box 控件）使用：内外圆同心，AA 带内 4x4 子采样只跑一遍，
 *       每个采样点的 d² 同时与内外半径比较，比分别调两次 quarter-circle mask 省一半。
 */
uint8_t we_mask_quarter_ring_alpha(int16_t x, int16_t y, uint16_t r_out, uint16_t r_in,
                                   uint8_t quadrant, int16_t px, int16_t py,
                                   uint8_t *p_fill_alpha)
{
    int32_t cx16;
    int32_t cy16;
    int32_t ro16_sq;
    int32_t ri16_sq;
    int32_t near_dx;
    int32_t near_dy;
    int32_t far_dx;
    int32_t far_dy;
    int32_t near_d2;
    int32_t far_d2;
    uint8_t cov_o = 0U;
    uint8_t cov_i = 0U;
    uint8_t out_full;
    uint8_t in_zero;
    uint8_t syi;
    uint8_t sxi;
    static const uint8_t sample_ofs[4] = { 2U, 6U, 10U, 14U };

    *p_fill_alpha = 0U;
    if (r_out == 0U)
        return 0U;

    switch (quadrant)
    {
    case WE_MASK_QUADRANT_LT:
        cx16 = (int32_t)(x + r_out) * 16;
        cy16 = (int32_t)(y + r_out) * 16;
        near_dx = (int32_t)(px + 1 - (x + r_out)) * 16;
        near_dy = (int32_t)(py + 1 - (y + r_out)) * 16;
        far_dx = (int32_t)(px - (x + r_out)) * 16;
        far_dy = (int32_t)(py - (y + r_out)) * 16;
        break;
    case WE_MASK_QUADRANT_RT:
        cx16 = (int32_t)x * 16;
        cy16 = (int32_t)(y + r_out) * 16;
        near_dx = (int32_t)(px - x) * 16;
        near_dy = (int32_t)(py + 1 - (y + r_out)) * 16;
        far_dx = (int32_t)(px + 1 - x) * 16;
        far_dy = (int32_t)(py - (y + r_out)) * 16;
        break;
    case WE_MASK_QUADRANT_LB:
        cx16 = (int32_t)(x + r_out) * 16;
        cy16 = (int32_t)y * 16;
        near_dx = (int32_t)(px + 1 - (x + r_out)) * 16;
        near_dy = (int32_t)(py - y) * 16;
        far_dx = (int32_t)(px - (x + r_out)) * 16;
        far_dy = (int32_t)(py + 1 - y) * 16;
        break;
    case WE_MASK_QUADRANT_RB:
    default:
        cx16 = (int32_t)x * 16;
        cy16 = (int32_t)y * 16;
        near_dx = (int32_t)(px - x) * 16;
        near_dy = (int32_t)(py - y) * 16;
        far_dx = (int32_t)(px + 1 - x) * 16;
        far_dy = (int32_t)(py + 1 - y) * 16;
        break;
    }

    ro16_sq = ((int32_t)r_out * 16) * ((int32_t)r_out * 16);
    ri16_sq = ((int32_t)r_in * 16) * ((int32_t)r_in * 16);
    near_d2 = near_dx * near_dx + near_dy * near_dy;
    if (near_d2 >= ro16_sq)
        return 0U; /* 整像素在外圆之外 */

    far_d2 = far_dx * far_dx + far_dy * far_dy;
    out_full = (uint8_t)(far_d2 <= ro16_sq);
    in_zero  = (uint8_t)(r_in == 0U || near_d2 >= ri16_sq);

    if (out_full)
    {
        if (in_zero)
            return 255U; /* 环带满覆盖、内圆无覆盖 */
        if (far_d2 <= ri16_sq)
        {
            *p_fill_alpha = 255U; /* 整像素在内圆之内 */
            return 255U;
        }
    }

    /* 至少一侧处于 AA 边界带：一遍 4x4 子采样同时统计内外覆盖 */
    for (syi = 0U; syi < 4U; syi++)
    {
        int32_t sy = (int32_t)py * 16 + sample_ofs[syi];
        int32_t dy = sy - cy16;
        int32_t dy2 = dy * dy;

        for (sxi = 0U; sxi < 4U; sxi++)
        {
            int32_t sx = (int32_t)px * 16 + sample_ofs[sxi];
            int32_t dx = sx - cx16;
            int32_t d2 = dx * dx + dy2;
            if (d2 <= ro16_sq)
            {
                cov_o++;
                if (!in_zero && d2 <= ri16_sq)
                    cov_i++;
            }
        }
    }
    if (out_full)
        cov_o = 16U;

    if (cov_i != 0U)
        *p_fill_alpha = (cov_i >= 16U) ? 255U : (uint8_t)(((uint32_t)cov_i * 255U + 8U) >> 4);
    if (cov_o == 0U)
        return 0U;
    return (cov_o >= 16U) ? 255U : (uint8_t)(((uint32_t)cov_o * 255U + 8U) >> 4);
}

uint8_t we_mask_round_rect_alpha(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                 uint16_t radius, int16_t px, int16_t py)
{
    int16_t x1;
    int16_t y1;
    uint16_t r;

    if (w == 0U || h == 0U)
        return 0U;

    x1 = (int16_t)(x + (int16_t)w - 1);
    y1 = (int16_t)(y + (int16_t)h - 1);
    if (px < x || px > x1 || py < y || py > y1)
        return 0U;

    r = radius;
    if (r > w / 2U)
        r = (uint16_t)(w / 2U);
    if (r > h / 2U)
        r = (uint16_t)(h / 2U);

    if (r == 0U)
        return 255U;

    if ((px >= x + (int16_t)r && px <= x1 - (int16_t)r) ||
        (py >= y + (int16_t)r && py <= y1 - (int16_t)r))
        return 255U;

    if (px < x + (int16_t)r)
    {
        if (py < y + (int16_t)r)
            return we_mask_quarter_circle_alpha(x, y, r, WE_MASK_QUADRANT_LT, px, py);
        return we_mask_quarter_circle_alpha(x, y1 - (int16_t)r + 1, r, WE_MASK_QUADRANT_LB, px, py);
    }

    if (py < y + (int16_t)r)
        return we_mask_quarter_circle_alpha(x1 - (int16_t)r + 1, y, r, WE_MASK_QUADRANT_RT, px, py);

    return we_mask_quarter_circle_alpha(x1 - (int16_t)r + 1, y1 - (int16_t)r + 1, r, WE_MASK_QUADRANT_RB, px, py);
}


/**
 * @brief 绘制单个抗锯齿圆角（仅处理一个 r×r 角落方块），可挖掉同心内圆成环
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param cx 传入：角落外接方块左上角 X 坐标（屏幕绝对坐标）
 * @param cy 传入：角落外接方块左上角 Y 坐标
 * @param r 传入：圆角半径（= 方块边长）
 * @param ri 传入：同心内圆半径（0 = 实心圆角；> 0 时只画内外圆之间的环带）
 * @param quadrant 传入：象限标识（WE_MASK_QUADRANT_LT/RT/LB/RB）
 * @param color 传入：填充颜色
 * @param opacity 传入：整体透明度（0~255，调用方已做容器级联）
 * @return 无
 * @note 仅在 r×r 角落区域执行 4x4 子采样抗锯齿，直边由调用方快速填充。
 *       实心时覆盖率与 we_mask_quarter_circle_alpha 逐像素相同，成环时与
 *       we_mask_quarter_ring_alpha 的“外圆覆盖 - 内圆覆盖”相同。四个象限按
 *       “离圆心第几格” (u, v) 计算时完全对称，所以象限判断提到循环外，
 *       逐像素只剩几次整数乘法。
 */
static void _we_draw_round_corner(we_lcd_t *p_lcd, int16_t cx, int16_t cy, uint16_t r, uint16_t ri,
                                  uint8_t quadrant, colour_t color, uint8_t opacity)
{
    static const uint8_t sample_ofs[4] = { 2U, 6U, 10U, 14U };
    int16_t cx0 = cx;
    int16_t cy0 = cy;
    int16_t cx1 = (int16_t)(cx + (int16_t)r - 1);
    int16_t cy1 = (int16_t)(cy + (int16_t)r - 1);
    int16_t py;
    uint16_t stride;
    colour_t *row;
    uint8_t left = (uint8_t)(quadrant == WE_MASK_QUADRANT_LT || quadrant == WE_MASK_QUADRANT_LB);
    uint8_t top = (uint8_t)(quadrant == WE_MASK_QUADRANT_LT || quadrant == WE_MASK_QUADRANT_RT);
    int32_t r2 = (int32_t)r * (int32_t)r;
    int32_t ri2 = (int32_t)ri * (int32_t)ri;
    int32_t r16_sq = r2 * 256;
    int32_t ri16_sq = ri2 * 256;
    uint8_t full_alpha = we_div255((uint32_t)opacity * 255U);

    /* 先把角落方块裁剪到当前 PFB 切片，避免越界写入。 */
    if (cx0 < (int16_t)p_lcd->clip.x0) cx0 = (int16_t)p_lcd->clip.x0;
    if (cy0 < (int16_t)p_lcd->clip.y0) cy0 = (int16_t)p_lcd->clip.y0;
    if (cx1 > (int16_t)p_lcd->clip.x1) cx1 = (int16_t)p_lcd->clip.x1;
    if (cy1 > (int16_t)p_lcd->clip.y1) cy1 = (int16_t)p_lcd->clip.y1;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    stride = p_lcd->pfb_width;
    row = p_lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)p_lcd->clip.y0) * stride
        + (uint32_t)(cx0 - (int16_t)p_lcd->clip.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        colour_t *p = row;
        /* v/u：像素离圆心的格数，紧贴圆心那一格为 0；左侧象限 u 随 px 增大而减小 */
        int32_t v = top ? (int32_t)(cy + (int16_t)r - 1 - py) : (int32_t)(py - cy);
        int32_t u = left ? (int32_t)(cx + (int16_t)r - 1 - cx0) : (int32_t)(cx0 - cx);
        int32_t v2_near = v * v;
        int32_t v2_far = (v + 1) * (v + 1);
        int32_t dy2[4];
        int16_t n;
        uint8_t j;

        for (j = 0U; j < 4U; j++)
        {
            int32_t d = v * 16 + sample_ofs[j];
            dy2[j] = d * d;
        }

        for (n = (int16_t)(cx1 - cx0); n >= 0; n--, p++, u = left ? (u - 1) : (u + 1))
        {
            int32_t near_d2 = u * u + v2_near;
            int32_t far_d2 = (u + 1) * (u + 1) + v2_far;
            uint8_t in_zero = (uint8_t)(ri == 0U || near_d2 >= ri2);

            if (near_d2 >= r2)
            {
                /* 整格在外圆外：右侧象限往后只会更远，整行结束 */
                if (!left)
                    break;
                continue;
            }
            if (far_d2 <= r2 && in_zero)
            {
                if (opacity >= 250U)
                    we_store_color(p, color);
                else
                    we_store_blended_color(p, color, full_alpha);
            }
            else if (ri != 0U && far_d2 <= ri2)
            {
                /* 整格在内圆里（环带挖空处）：左侧象限往后只会更靠圆心，整行结束 */
                if (left)
                    break;
            }
            else
            {
                uint8_t cov_o = 0U;
                uint8_t cov_i = 0U;
                uint8_t i;
                uint32_t a;

                for (i = 0U; i < 4U; i++)
                {
                    int32_t d = u * 16 + sample_ofs[i];
                    int32_t dx2 = d * d;
                    for (j = 0U; j < 4U; j++)
                    {
                        int32_t d2 = dx2 + dy2[j];
                        if (d2 <= r16_sq)
                        {
                            cov_o++;
                            if (!in_zero && d2 <= ri16_sq)
                                cov_i++;
                        }
                    }
                }
                if (far_d2 <= r2)
                    cov_o = 16U;
                a = (cov_o >= 16U) ? 255U : (((uint32_t)cov_o * 255U + 8U) >> 4);
                if (cov_i != 0U)
                    a -= (cov_i >= 16U) ? 255U : (((uint32_t)cov_i * 255U + 8U) >> 4);
                if (a != 0U)
                    we_store_blended_color(p, color, we_div255((uint32_t)opacity * a));
            }
        }
    }
}

/**
 * @brief 绘制抗锯齿实心圆角矩形
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param x 传入，矩形左上角 X 坐标
 * @param y 传入，矩形左上角 Y 坐标
 * @param w 传入，矩形宽度
 * @param h 传入，矩形高度
 * @param radius 传入，圆角半径
 * @param color 传入，填充颜色
 * @param opacity 传入，全局透明度(0~255)
 * @return 无
 */
void we_draw_round_rect_analytic_fill(we_lcd_t *p_lcd, int16_t x, int16_t y,
                                      uint16_t w, uint16_t h, uint16_t radius,
                                      colour_t color, uint8_t opacity)
{
    uint16_t r;
    int16_t x1;
    int16_t y1;

    if (p_lcd == NULL || w == 0U || h == 0U)
        return;
    x1 = (int16_t)(x + (int16_t)w - 1);
    y1 = (int16_t)(y + (int16_t)h - 1);
    /* 与当前窗口不相交就不必再拆 7 块逐块裁剪 */
    if (x1 < p_lcd->clip.x0 || x > p_lcd->clip.x1 || y1 < p_lcd->clip.y0 || y > p_lcd->clip.y1)
        return;
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0U)
        return;

    r = radius;
    if (r > w / 2U)
        r = (uint16_t)(w / 2U);
    if (r > h / 2U)
        r = (uint16_t)(h / 2U);

    if (r == 0U)
    {
        /* 入口已做级联缩放，内部组合走免缩放版避免双重应用 */
        _we_fill_rect_no_scale(p_lcd, x, y, w, h, color, opacity);
        return;
    }

    /* --- 1. 中间整块实心带（圆角上下两条带之间），直接走快速矩形填充 ---
     * 行范围 [y+r, y1-r]，高度 h-2r；当 h==2r（如胶囊/圆形）时高度为 0，
     * we_fill_rect 会因 h==0 直接返回。 */
    _we_fill_rect_no_scale(p_lcd, x, (int16_t)(y + (int16_t)r), w, (uint16_t)(h - 2U * r), color, opacity);

    /* --- 2. 上下两条带的中心实心区（去掉左右两个圆角列）---
     * 宽度 w-2r；当 w==2r 时宽度为 0，填充函数因 w==0 直接返回。
     * 注意走免缩放内部版：本函数入口已应用级联，主体与圆角必须同一份 opacity，
     * 否则容器淡入淡出时四角与矩形主体出现亮度断层。 */
    _we_fill_rect_no_scale(p_lcd, (int16_t)(x + (int16_t)r), y, (uint16_t)(w - 2U * r), r, color, opacity);
    _we_fill_rect_no_scale(p_lcd, (int16_t)(x + (int16_t)r), (int16_t)(y1 - (int16_t)r + 1),
                           (uint16_t)(w - 2U * r), r, color, opacity);

    /* --- 3. 仅在 4 个 r×r 角落方块内做抗锯齿，函数调用与子采样开销集中于此 --- */
    _we_draw_round_corner(p_lcd, x, y, r, 0U, WE_MASK_QUADRANT_LT, color, opacity);
    _we_draw_round_corner(p_lcd, (int16_t)(x1 - (int16_t)r + 1), y, r, 0U, WE_MASK_QUADRANT_RT, color, opacity);
    _we_draw_round_corner(p_lcd, x, (int16_t)(y1 - (int16_t)r + 1), r, 0U, WE_MASK_QUADRANT_LB, color, opacity);
    _we_draw_round_corner(p_lcd, (int16_t)(x1 - (int16_t)r + 1), (int16_t)(y1 - (int16_t)r + 1),
                          r, 0U, WE_MASK_QUADRANT_RB, color, opacity);
}

/**
 * @brief 绘制抗锯齿圆角矩形边框（只画环带，内部不动、露出下面的内容）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param x 传入，矩形左上角 X 坐标
 * @param y 传入，矩形左上角 Y 坐标
 * @param w 传入，矩形宽度
 * @param h 传入，矩形高度
 * @param radius 传入，外圆角半径（超过宽高一半时钳制）
 * @param bw 传入，边框宽度；bw*2 >= min(w,h) 时整块实心填充
 * @param color 传入，边框颜色
 * @param opacity 传入，全局透明度(0~255)
 * @return 无
 * @note 内轮廓是外轮廓内缩 bw、圆角半径 radius-bw（不足 0 取直角）的圆角矩形。
 *       按行带拆成互不重叠的块：直边走矩形快速填充，四角走同心双圆环带
 *       抗锯齿，半透明时各处只混合一次。
 */
void we_draw_round_rect_ring(we_lcd_t *p_lcd, int16_t x, int16_t y,
                             uint16_t w, uint16_t h, uint16_t radius, uint16_t bw,
                             colour_t color, uint8_t opacity)
{
    uint16_t r;
    uint16_t ri;
    uint16_t t;
    uint16_t m;
    int16_t x1;
    int16_t y1;

    if (p_lcd == NULL || w == 0U || h == 0U || bw == 0U)
        return;
    if ((uint32_t)bw * 2U >= w || (uint32_t)bw * 2U >= h)
    {
        we_draw_round_rect_analytic_fill(p_lcd, x, y, w, h, radius, color, opacity);
        return;
    }
    x1 = (int16_t)(x + (int16_t)w - 1);
    y1 = (int16_t)(y + (int16_t)h - 1);
    if (x1 < p_lcd->clip.x0 || x > p_lcd->clip.x1 || y1 < p_lcd->clip.y0 || y > p_lcd->clip.y1)
        return;
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0U)
        return;

    r = radius;
    if (r > w / 2U)
        r = (uint16_t)(w / 2U);
    if (r > h / 2U)
        r = (uint16_t)(h / 2U);
    ri = (r > bw) ? (uint16_t)(r - bw) : 0U;
    t = (bw < r) ? bw : r;   /* 圆角行带里属于上/下边框的行数 */
    m = (bw > r) ? bw : r;   /* 上/下两端不属于左右竖边的行数 */

    /* 1. 上下圆角行带里两角之间的直边 */
    _we_fill_rect_no_scale(p_lcd, (int16_t)(x + (int16_t)r), y, (uint16_t)(w - 2U * r), t, color, opacity);
    _we_fill_rect_no_scale(p_lcd, (int16_t)(x + (int16_t)r), (int16_t)(y1 - (int16_t)t + 1),
                           (uint16_t)(w - 2U * r), t, color, opacity);
    /* 2. 边框比圆角厚：圆角行带以下仍属上/下边框的整行 */
    if (bw > r)
    {
        _we_fill_rect_no_scale(p_lcd, x, (int16_t)(y + (int16_t)r), w, (uint16_t)(bw - r), color, opacity);
        _we_fill_rect_no_scale(p_lcd, x, (int16_t)(y1 - (int16_t)bw + 1), w, (uint16_t)(bw - r), color, opacity);
    }
    /* 3. 中段左右竖边 */
    _we_fill_rect_no_scale(p_lcd, x, (int16_t)(y + (int16_t)m), bw, (uint16_t)(h - 2U * m), color, opacity);
    _we_fill_rect_no_scale(p_lcd, (int16_t)(x1 - (int16_t)bw + 1), (int16_t)(y + (int16_t)m), bw,
                           (uint16_t)(h - 2U * m), color, opacity);
    /* 4. 四角环带 */
    if (r != 0U)
    {
        _we_draw_round_corner(p_lcd, x, y, r, ri, WE_MASK_QUADRANT_LT, color, opacity);
        _we_draw_round_corner(p_lcd, (int16_t)(x1 - (int16_t)r + 1), y, r, ri, WE_MASK_QUADRANT_RT, color, opacity);
        _we_draw_round_corner(p_lcd, x, (int16_t)(y1 - (int16_t)r + 1), r, ri, WE_MASK_QUADRANT_LB, color, opacity);
        _we_draw_round_corner(p_lcd, (int16_t)(x1 - (int16_t)r + 1), (int16_t)(y1 - (int16_t)r + 1),
                              r, ri, WE_MASK_QUADRANT_RB, color, opacity);
    }
}

void we_draw_round_rect_outline_fill(we_lcd_t *p_lcd, int16_t x, int16_t y,
                                     uint16_t w, uint16_t h, uint16_t radius,
                                     colour_t outline, colour_t fill, uint8_t opacity)
{
    we_draw_round_rect_analytic_fill(p_lcd, x, y, w, h, radius, outline, opacity);
    if (w > 2U && h > 2U)
        we_draw_round_rect_analytic_fill(p_lcd, (int16_t)(x + 1), (int16_t)(y + 1),
                                         (uint16_t)(w - 2U), (uint16_t)(h - 2U),
                                         (uint16_t)((radius > 0U) ? (radius - 1U) : 0U),
                                         fill, opacity);
}

/**
 * @brief 带裁剪的单像素写入（内联版，供 we_draw_pixel / we_draw_line 等热路径复用）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param px 传入：像素 X 坐标（屏幕绝对坐标）
 * @param py 传入：像素 Y 坐标
 * @param color 传入：像素颜色
 * @param opacity 传入：透明度（0~255）
 * @return 无
 * @note static inline，调用方循环内可被编译器内联，省去函数调用开销并复用 PFB 字段加载。
 */
static __inline void _we_put_pixel_clipped(we_lcd_t *p_lcd, int16_t px, int16_t py, colour_t color, uint8_t opacity)
{
    colour_t *dst;

    if (px < p_lcd->clip.x0 || px > p_lcd->clip.x1 || py < p_lcd->clip.y0 || py > p_lcd->clip.y1)
        return;

    dst = p_lcd->pfb_gram + (py - p_lcd->clip.y0) * p_lcd->pfb_width + (px - p_lcd->clip.x0);
    we_store_blended_color(dst, color, opacity);
}

/**
 * @brief 在 PFB 缓冲区内写入单个像素
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param px 传入：像素 X 坐标 (屏幕绝对坐标)
 * @param py 传入：像素 Y 坐标
 * @param color 传入：像素颜色
 * @param opacity 传入：透明度 (0~255)
 */
void we_draw_pixel(we_lcd_t *p_lcd, int16_t px, int16_t py, colour_t color, uint8_t opacity)
{
    _we_put_pixel_clipped(p_lcd, px, py, color, we_opa_apply(p_lcd, opacity));
}

/**
 * @brief 绘制线段（Xiaolin Wu 整数 Q8 抗锯齿，统一处理所有线宽）
 *
 * 沿主轴每步画 T 个实心像素 + 2 个 AA 边缘像素（T = thickness）：
 *   lo = -(T-1)/2，hi = T/2；实心范围 [yi+lo, yi+hi]（恰好 T 个像素）
 *   AA 上边 yi+lo-1（alpha 按 1-小数），AA 下边 yi+hi+1（alpha 按小数）
 * 两条 AA 边合计覆盖 1 像素，所以视觉线宽约为 T+1（T=1 时是 1 实心 + 2 边缘）。
 * 只迭代主轴、副轴都可能落进当前窗口的那一段，窗口外的步不进循环。
 *
 * @param p_lcd     传入：GUI 屏幕上下文指针
 * @param x0        传入：起点 X
 * @param y0        传入：起点 Y
 * @param x1        传入：终点 X
 * @param y1        传入：终点 Y
 * @param thickness 传入：实心核像素数（视觉线宽约 +1）
 * @param color     传入：线段颜色
 * @param opacity   传入：透明度 (0~255)
 */
void we_draw_line(we_lcd_t *p_lcd, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, colour_t color,
                  uint8_t opacity)
{
    int16_t dx, dy, adx, ady, tmp, grad, x;
    int32_t y_fp;
    uint8_t steep;
    int16_t lo, hi;
    int32_t xs, xe, n_lo, n_hi;

    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0)
        return;

    dx = x1 - x0;
    dy = y1 - y0;
    adx = (dx < 0) ? -dx : dx;
    ady = (dy < 0) ? -dy : dy;

    /* 陡线交换 xy，保证沿主轴（较长轴）迭代 */
    steep = (ady > adx);
    if (steep)
    {
        tmp = x0;
        x0 = y0;
        y0 = tmp;
        tmp = x1;
        x1 = y1;
        y1 = tmp;
    }

    /* 保证从左到右 */
    if (x0 > x1)
    {
        tmp = x0;
        x0 = x1;
        x1 = tmp;
        tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    dx = x1 - x0;
    dy = y1 - y0;
    grad = (dx > 0) ? (int16_t)((int32_t)dy * 256 / dx) : 0;

    /* 每步画副轴 [yi+lo-1, yi+hi+1] 共 T+2 个像素 */
    lo = -(int16_t)((thickness - 1U) / 2U);
    hi = (int16_t)(thickness / 2U);

    /* 主轴区间裁到窗口：陡线的主轴是屏幕 Y，否则是屏幕 X */
    if (steep)
    {
        xs = p_lcd->clip.y0;
        xe = p_lcd->clip.y1;
        n_lo = p_lcd->clip.x0;
        n_hi = p_lcd->clip.x1;
    }
    else
    {
        xs = p_lcd->clip.x0;
        xe = p_lcd->clip.x1;
        n_lo = p_lcd->clip.y0;
        n_hi = p_lcd->clip.y1;
    }
    if (xs < x0)
        xs = x0;
    if (xe > x1)
        xe = x1;

    /* 副轴裁剪：第 k 步 y_fp = y0*256 + grad*k，要画到窗口必须满足
     * y_fp - y0*256 ∈ [f_lo, f_hi]；由斜率反推 k 的区间。截断除法两端各放宽
     * 1 步，只会多算几步（逐像素仍会裁剪），不会漏画。 */
    {
        int32_t f_lo = (n_lo - hi - 1 - y0) * 256;
        int32_t f_hi = (n_hi - lo + 2 - y0) * 256 - 1;
        int32_t k_lo, k_hi;

        if (grad == 0)
        {
            if (f_lo > 0 || f_hi < 0)
                return;
        }
        else
        {
            if (grad > 0)
            {
                k_lo = f_lo / grad - 1;
                k_hi = f_hi / grad + 1;
            }
            else
            {
                k_lo = f_hi / grad - 1;
                k_hi = f_lo / grad + 1;
            }
            if (xs < x0 + k_lo)
                xs = x0 + k_lo;
            if (xe > x0 + k_hi)
                xe = x0 + k_hi;
        }
    }
    if (xs > xe)
        return;

    y_fp = (int32_t)y0 * 256 + (int32_t)grad * (xs - x0);

    for (x = (int16_t)xs; x <= (int16_t)xe; x++)
    {
        int16_t yi = (int16_t)(y_fp >> 8);
        uint8_t frac = (uint8_t)(y_fp & 0xFFU);
        uint8_t a1 = (uint8_t)(((uint16_t)(255U - frac) * opacity) >> 8);
        uint8_t a2 = (uint8_t)(((uint16_t)frac * opacity) >> 8);
        int16_t t;

        if (steep)
        {
            _we_put_pixel_clipped(p_lcd, (int16_t)(yi + lo - 1), x, color, a1);
            for (t = lo; t <= hi; t++)
                _we_put_pixel_clipped(p_lcd, (int16_t)(yi + t), x, color, opacity);
            _we_put_pixel_clipped(p_lcd, (int16_t)(yi + hi + 1), x, color, a2);
        }
        else
        {
            _we_put_pixel_clipped(p_lcd, x, (int16_t)(yi + lo - 1), color, a1);
            for (t = lo; t <= hi; t++)
                _we_put_pixel_clipped(p_lcd, x, (int16_t)(yi + t), color, opacity);
            _we_put_pixel_clipped(p_lcd, x, (int16_t)(yi + hi + 1), color, a2);
        }

        y_fp += grad;
    }
}

/**
 * @brief 整数平方根（向下取整），用于圆头线的端帽距离
 */
static uint32_t _we_isqrt32(uint32_t v)
{
    uint32_t res = 0U;
    uint32_t bit = 1UL << 30;

    while (bit > v)
        bit >>= 2;
    while (bit != 0U)
    {
        if (v >= res + bit)
        {
            v -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/**
 * @brief 绘制圆头抗锯齿线段（单遍胶囊覆盖）
 *
 * 把“带圆头的线”当成一个胶囊形状（到线段距离 ≤ 半线宽），逐像素只算一次
 * 覆盖率、只混合一次——因此任何透明度下都不会出现线身与圆头重叠处的二次
 * 叠色。线身垂距用预乘的 1/len 求得（无逐像素除法）；端帽先用距离平方比阈值，
 * 仅 1px 抗锯齿环才开方。逐扫描线做 x 区间裁剪，复杂度 ~ 线长×线宽。
 *
 * @param p_lcd   传入：GUI 屏幕上下文指针
 * @param x0/y0   传入：起点
 * @param x1/y1   传入：终点
 * @param width   传入：线宽（像素）
 * @param color   传入：线色
 * @param opacity 传入：透明度（0~255）
 */
void we_draw_line_round(we_lcd_t *p_lcd, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t width, colour_t color,
                        uint8_t opacity)
{
    int32_t   dx, dy, len2;
    uint32_t  len, inv_len;
    int32_t   r_fp, thr_in, thr_out, band_half;
    int16_t   ext, minx, miny, maxx, maxy, px, py;
    colour_t *gram;
    int16_t   bx, ys;
    uint16_t  pw;

    if (p_lcd == NULL || width == 0U)
        return;
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0U)
        return;

    ext  = (int16_t)(width / 2U + 2U);                    /* 外扩：半宽 + AA 余量 */
    minx = (int16_t)(((x0 < x1) ? x0 : x1) - ext);
    maxx = (int16_t)(((x0 > x1) ? x0 : x1) + ext);
    miny = (int16_t)(((y0 < y1) ? y0 : y1) - ext);
    maxy = (int16_t)(((y0 > y1) ? y0 : y1) + ext);

    /* 裁剪到当前窗口；胶囊外接框与窗口不相交就不必开方和求倒数 */
    if (minx < p_lcd->clip.x0) minx = p_lcd->clip.x0;
    if (maxx > p_lcd->clip.x1) maxx = p_lcd->clip.x1;
    if (miny < p_lcd->clip.y0) miny = p_lcd->clip.y0;
    if (maxy > p_lcd->clip.y1) maxy = p_lcd->clip.y1;
    if (minx > maxx || miny > maxy)
        return;

    dx = (int32_t)x1 - x0;
    dy = (int32_t)y1 - y0;
    len2 = dx * dx + dy * dy;
    len = _we_isqrt32((uint32_t)len2);
    inv_len = (len != 0U) ? ((1UL << 16) / len) : 0U;

    r_fp      = (int32_t)width * 128;                     /* 半线宽 width/2 的 Q8 */
    thr_in    = (int32_t)(width - 1) * (int32_t)(width - 1); /* 端帽实心阈值（4·d²） */
    thr_out   = (int32_t)(width + 1) * (int32_t)(width + 1); /* 端帽圈外阈值（4·d²） */
    band_half = (int32_t)ext * (int32_t)len;              /* 行裁剪：垂距≤R 的带半宽·len */

    gram = p_lcd->pfb_gram;
    bx   = p_lcd->clip.x0;
    ys   = p_lcd->clip.y0;
    pw   = p_lcd->pfb_width;

    for (py = miny; py <= maxy; py++)
    {
        int16_t   rx0 = minx, rx1 = maxx;
        int32_t   ey  = (int32_t)py - y0;
        colour_t *row;

        /* 逐扫描线 x 区间裁剪：保守取垂距≤R 的带（含端帽），对垂直/对角线大幅提速 */
        if (dy != 0)
        {
            int32_t A = ey * dx;
            int32_t a = x0 + (A - band_half) / dy;
            int32_t b = x0 + (A + band_half) / dy;
            if (a > b) { int32_t t = a; a = b; b = t; }  /* dy<0 时翻序 */
            a -= 1; b += 1;                              /* 取整安全余量 */
            if (a > (int32_t)minx) rx0 = (a > (int32_t)maxx) ? (int16_t)(maxx + 1) : (int16_t)a;
            if (b < (int32_t)maxx) rx1 = (b < (int32_t)minx) ? (int16_t)(minx - 1) : (int16_t)b;
            if (rx0 > rx1)
                continue;
        }

        row = gram + (int32_t)(py - ys) * pw;

        for (px = rx0; px <= rx1; px++)
        {
            int32_t ex  = (int32_t)px - x0;
            int32_t dot = ex * dx + ey * dy;
            int32_t cov;

            if (len2 == 0 || dot <= 0 || dot >= len2)
            {
                /* 端帽：先用 4·d² 比阈值，绝大多数像素免开方 */
                int32_t ddx = (dot >= len2 && len2 != 0) ? ((int32_t)px - x1) : ex;
                int32_t ddy = (dot >= len2 && len2 != 0) ? ((int32_t)py - y1) : ey;
                int32_t d2  = ddx * ddx + ddy * ddy;
                int32_t q4  = 4 * d2;
                if (q4 <= thr_in)
                    cov = 255;
                else if (q4 >= thr_out)
                    cov = 0;
                else
                {
                    int32_t df = (int32_t)_we_isqrt32(((uint32_t)d2) << 16);
                    cov = (int32_t)((uint32_t)(255 * ((r_fp + 128) - df)) >> 8);
                }
            }
            else
            {
                /* 线身：垂距 |cross|/len（预乘 1/len，无逐像素除法） */
                int32_t cr = ex * dy - ey * dx;
                int32_t df;
                if (cr < 0)
                    cr = -cr;
                df = (int32_t)(((uint32_t)cr * inv_len) >> 8);
                if (df <= r_fp - 128)
                    cov = 255;
                else if (df >= r_fp + 128)
                    cov = 0;
                else
                    cov = (int32_t)((uint32_t)(255 * ((r_fp + 128) - df)) >> 8);
            }

            if (cov > 0)
            {
                uint8_t pa = we_div255((uint32_t)cov * (uint32_t)opacity);
                if (pa > 0U)
                    we_store_blended_color(row + (px - bx), color, pa);
            }
        }
    }
}

/* =========================================================================
 * 预计算 0~128 步(对应 0°~90°)正弦表，Q15 格式，1.0 = 32767
 * 系统统一使用 512 步/圈，四分之一圆 = 128 步
 * Flash 占用：129 * 2 = 258 Bytes
 * 优势：归一化只需 & 0x1FF，无除法；象限边界 128/256/384 均为 2 的幂次倍数
 * ========================================================================= */
static const int16_t we_sin_table_128[129] = {
    0,     402,   804,   1206,  1608,  2009,  2410,  2811,  3212,  3612,  4011,  4410,  4808,  5205,  5602,
    5997,  6393,  6787,  7180,  7572,  7963,  8352,  8742,  9130,  9515,  9899,  10280, 10659, 11038, 11415,
    11790, 12163, 12539, 12910, 13279, 13645, 14010, 14372, 14732, 15090, 15446, 15800, 16151, 16499, 16846,
    17189, 17530, 17869, 18204, 18537, 18868, 19195, 19519, 19841, 20159, 20475, 20787, 21096, 21403, 21705,
    22005, 22301, 22594, 22884, 23170, 23452, 23731, 24007, 24279, 24547, 24811, 25072, 25329, 25582, 25832,
    26077, 26319, 26556, 26790, 27019, 27245, 27466, 27683, 27896, 28105, 28310, 28510, 28706, 28898, 29085,
    29268, 29447, 29621, 29791, 29956, 30117, 30273, 30424, 30571, 30714, 30852, 30985, 31113, 31237, 31356,
    31470, 31580, 31685, 31785, 31880, 31971, 32057, 32137, 32213, 32285, 32351, 32412, 32469, 32521, 32567,
    32609, 32646, 32678, 32705, 32728, 32745, 32757, 32765, 32767};

/**
 * @brief  快速整数正弦函数，返回 Q15 格式结果
 * @param  angle 512 步制角度值，支持负数和超出 512 的输入
 * @retval 放大 32767 倍后的正弦值
 */
int16_t we_sin(int16_t angle)
{
    // 1. 归一化到 0~511，位与运算替代取模，零除法开销。
    int16_t norm_angle = angle & 0x1FF;
    if (norm_angle < 0)
    {
        norm_angle += 512;
    }

    // 2. 利用四象限对称性，只查 0~128 步的表即可。
    if (norm_angle <= 128)
    {
        return we_sin_table_128[norm_angle];
    }
    else if (norm_angle <= 256)
    {
        return we_sin_table_128[256 - norm_angle];
    }
    else if (norm_angle <= 384)
    {
        return -we_sin_table_128[norm_angle - 256];
    }
    else
    {
        return -we_sin_table_128[512 - norm_angle];
    }
}

/**
 * @brief  快速整数余弦函数，返回 Q15 格式结果
 * @param  angle 512 步制角度值
 * @retval 放大 32767 倍后的余弦值
 */
int16_t we_cos(int16_t angle)
{
    // 利用公式：cos(a) = sin(a + 90°)，90° 在 512 步制中 = WE_ANGLE(90.0f) = 128
    return we_sin(angle + 128);
}

/**
 * @brief Q15 三角值 × Q16 倒数缩放 → Q12 步进（四舍五入，纯 32 位）
 * @param c 传入：Q15 三角值（|c| <= 32767）
 * @param inv 传入：Q16 的 1/缩放（<= 2^24）
 * @return round(c * inv / 2^19)
 * @note 把 inv 拆成高低两段分别相乘，中间量都在 2^28 以内。
 */
static int32_t _we_xform_step(int32_t c, int32_t inv)
{
    int32_t a = c * (inv >> 12);
    int32_t b = c * (inv & 0xFFF);

    return (a + (b >> 12) + 64) >> 7;
}

void we_xform_steps(int16_t angle, uint16_t scale_256, int32_t st[4])
{
    int32_t c = we_cos(angle);
    int32_t s = we_sin(angle);
    int32_t inv = (int32_t)(16777216UL / (scale_256 ? scale_256 : 1U));

    st[0] = _we_xform_step(c, inv);  /* 屏幕 X+1：源 X 增量 */
    st[1] = _we_xform_step(-s, inv); /* 屏幕 X+1：源 Y 增量 */
    st[2] = _we_xform_step(s, inv);  /* 屏幕 Y+1：源 X 增量 */
    st[3] = st[0];                   /* 屏幕 Y+1：源 Y 增量 */
}

void we_xform_origin(const int32_t st[4], uint16_t w, uint16_t h, int16_t ofs_x, int16_t ofs_y,
                     int32_t dx, int32_t dy, int32_t out[2])
{
    /* 偶数宽/高补半个步进，把采样点从像素左上角挪到像素中心 */
    int32_t hx = (w & 1U) ? 0 : 1;
    int32_t hy = (h & 1U) ? 0 : 1;

    out[0] = (int32_t)w * 2048 + (int32_t)ofs_x * 4096 + dx * st[0] + dy * st[2] + (hx * st[0] + hy * st[2]) / 2;
    out[1] = (int32_t)h * 2048 + (int32_t)ofs_y * 4096 + dx * st[1] + dy * st[3] + (hx * st[1] + hy * st[3]) / 2;
}

void we_xform_bbox(int16_t w, int16_t h, int16_t ofs_x, int16_t ofs_y,
                   int16_t angle, uint16_t scale_256,
                   int16_t *out_x, int16_t *out_y, int16_t *out_w, int16_t *out_h)
{
    int32_t c = we_cos(angle);
    int32_t s = we_sin(angle);
    int32_t sc = (scale_256 > 16384U) ? 16384 : (int32_t)scale_256;
    /* 源矩形四角相对旋转轴心的坐标，半像素单位 */
    int32_t xs[2];
    int32_t ys[2];
    int32_t min_x = 0x7FFFFFFF, max_x = -0x7FFFFFFF;
    int32_t min_y = 0x7FFFFFFF, max_y = -0x7FFFFFFF;
    uint8_t i;

    xs[0] = -(int32_t)w - 2 * (int32_t)ofs_x;
    xs[1] = (int32_t)w - 2 * (int32_t)ofs_x;
    ys[0] = -(int32_t)h - 2 * (int32_t)ofs_y;
    ys[1] = (int32_t)h - 2 * (int32_t)ofs_y;

    for (i = 0U; i < 4U; i++)
    {
        int32_t px = xs[i & 1U];
        int32_t py = ys[i >> 1];
        /* 旋转后仍是 Q15 半像素；屏幕像素 = r × 缩放 / 2^24。先右移 12 位防溢出，
         * 下界用算术右移（向下取整），上界取负后右移再取负（向上取整） */
        int32_t rx = px * c - py * s;
        int32_t ry = px * s + py * c;
        int32_t lo_x = ((rx >> 12) * sc) >> 12;
        int32_t hi_x = -((((-rx) >> 12) * sc) >> 12);
        int32_t lo_y = ((ry >> 12) * sc) >> 12;
        int32_t hi_y = -((((-ry) >> 12) * sc) >> 12);

        if (lo_x < min_x) min_x = lo_x;
        if (hi_x > max_x) max_x = hi_x;
        if (lo_y < min_y) min_y = lo_y;
        if (hi_y > max_y) max_y = hi_y;
    }

    /* 各留 1px 余量（覆盖像素中心取样的半像素偏移与步进取整误差） */
    *out_x = (int16_t)(min_x - 1);
    *out_y = (int16_t)(min_y - 1);
    *out_w = (int16_t)(max_x - min_x + 3);
    *out_h = (int16_t)(max_y - min_y + 3);
}
/**
 * @brief  使用指定颜色填充当前整个局部帧缓冲(PFB/GRAM)
 * @param  p_lcd  当前 LCD 上下文
 * @param  c      目标颜色
 * @note   这里做了循环展开，优先兼顾速度和代码体积
 */
void we_fill_gram(we_lcd_t *p_lcd, colour_t c)
{
    // 这里必须按当前切片真实大小计算像素总数，不能直接使用 GRAM_NUM。
    uint32_t total_pixels = (uint32_t)p_lcd->pfb_width * (p_lcd->clip.y1 - p_lcd->clip.y0 + 1);

    uint32_t blocks, rem, i;

#if (LCD_DEEP == DEEP_GRAY8)
    /* GRAY8：单字节亮度，复制成 32 位字后按 4 像素批量写。 */
    colour_t *gram8 = p_lcd->pfb_gram;
    uint8_t color_val = c.dat8;
    uint32_t color_val32 = ((uint32_t)color_val << 24) | ((uint32_t)color_val << 16) |
                           ((uint32_t)color_val << 8) | color_val;

    /* PFB 基址补齐到 4 字节边界（M0 不允许非对齐 32 位访问）。 */
    while (total_pixels > 0U && ((uintptr_t)gram8 & 0x3U) != 0U)
    {
        gram8->dat8 = color_val;
        gram8++;
        total_pixels--;
    }

    {
        uint32_t *gram32 = (uint32_t *)(void *)gram8; /* 上面已补齐到 4 字节边界 */
        uint32_t words = total_pixels >> 2;

        blocks = words / 8U; /* 每块 8 个 32 位字 = 32 像素 */
        rem = words % 8U;

        for (i = 0; i < blocks; i++)
        {
            gram32[0] = color_val32;
            gram32[1] = color_val32;
            gram32[2] = color_val32;
            gram32[3] = color_val32;
            gram32[4] = color_val32;
            gram32[5] = color_val32;
            gram32[6] = color_val32;
            gram32[7] = color_val32;
            gram32 += 8;
        }
        for (i = 0; i < rem; i++)
        {
            *gram32++ = color_val32;
        }
        gram8 = (colour_t *)gram32;
    }

    /* 收尾不足 4 像素的部分。 */
    for (i = 0; i < (total_pixels & 3U); i++)
    {
        gram8->dat8 = color_val;
        gram8++;
    }

#elif (LCD_DEEP == DEEP_RGB332)
#error ("Not support DEEP_RGB332 yet!")
#elif (LCD_DEEP == DEEP_RGB565)
    // RGB565 用 32 位双像素写入，吞吐翻倍。
    colour_t *gram16 = p_lcd->pfb_gram;
    uint16_t color_val = c.dat16; // 提前提取到局部变量，避免循环里重复解引用
    uint32_t color_val32 = ((uint32_t)color_val << 16) | color_val;

    /* PFB 基址未 4 字节对齐时先补一个像素，保证后续 32 位访问对齐（M0 必需）。 */
    if (total_pixels > 0U && ((uintptr_t)gram16 & 0x3U) != 0U)
    {
        gram16->dat16 = color_val;
        gram16++;
        total_pixels--;
    }

    {
        uint32_t *gram32 = (uint32_t *)(void *)gram16; /* 上面已补齐到 4 字节边界 */
        uint32_t pairs = total_pixels >> 1;

        blocks = pairs / 8U; // 每块 8 个 32 位字 = 16 像素
        rem = pairs % 8U;

        for (i = 0; i < blocks; i++)
        {
            gram32[0] = color_val32;
            gram32[1] = color_val32;
            gram32[2] = color_val32;
            gram32[3] = color_val32;
            gram32[4] = color_val32;
            gram32[5] = color_val32;
            gram32[6] = color_val32;
            gram32[7] = color_val32;
            gram32 += 8;
        }
        for (i = 0; i < rem; i++)
        {
            *gram32++ = color_val32;
        }
        gram16 = (colour_t *)gram32;
    }

    /* 收尾奇数像素。 */
    if (total_pixels & 1U)
    {
        gram16->dat16 = color_val;
    }

#elif (LCD_DEEP == DEEP_RGB888)
    colour_t *gram = p_lcd->pfb_gram;
    uint8_t r = c.rgb.r, g = c.rgb.g, b = c.rgb.b;

    blocks = total_pixels / 4;
    rem = total_pixels % 4;

    for (i = 0; i < blocks; i++)
    {
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
    }

    for (i = 0; i < rem; i++)
    {
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
    }
#endif
}

/**
 * @brief 在局部帧缓冲(PFB)中绘制实心矩形（公开入口，应用容器透明度级联）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param x 传入，矩形左上角 X 坐标
 * @param y 传入，矩形左上角 Y 坐标
 * @param w 传入，矩形宽度
 * @param h 传入，矩形高度
 * @param color 传入，填充颜色
 * @param opacity 传入，全局透明度(0~255)
 * @return 无
 */
void we_fill_rect(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h, colour_t color, uint8_t opacity)
{
    _we_fill_rect_no_scale(p_lcd, x, y, w, h, color, we_opa_apply(p_lcd, opacity));
}

/**
 * @brief 按行对齐方式绘制 alpha mask 位图（透明度已乘入容器级联，内核内部用）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：mask 左上角 X 坐标
 * @param y 传入：mask 左上角 Y 坐标
 * @param w 传入：mask 宽度
 * @param h 传入：mask 高度
 * @param src_data 传入：mask 数据首地址
 * @param row_stride 传入：mask 每行字节数
 * @param bpp 传入：mask 位深（1/2/4/8 bpp）
 * @param fg_color 传入：前景颜色
 * @param opacity 传入：整体透明度
 * @return 无
 */
void _we_draw_alpha_mask_raw(we_lcd_t *p_lcd,
                             int16_t x, int16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t *src_data,
                             uint32_t row_stride,
                             uint8_t bpp,
                             colour_t fg_color,
                             uint8_t opacity)
{
    int16_t draw_x_end;
    int16_t draw_y_end;
    int16_t clip_x_start;
    int16_t clip_y_start;
    int16_t clip_x_end;
    int16_t clip_y_end;
    colour_t *gram;
    uint16_t pfb_stride;

    if (p_lcd == NULL || w == 0U || h == 0U || src_data == NULL || opacity <= 5U)
        return; /* 逐像素 alpha 不超过 opacity，<= 5 时什么也画不出 */

    draw_x_end = x + (int16_t)w - 1;
    draw_y_end = y + (int16_t)h - 1;

    clip_x_start = (x < (int16_t)p_lcd->clip.x0) ? (int16_t)p_lcd->clip.x0 : x;
    clip_y_start = (y < (int16_t)p_lcd->clip.y0) ? (int16_t)p_lcd->clip.y0 : y;
    clip_x_end   = (draw_x_end > (int16_t)p_lcd->clip.x1) ? (int16_t)p_lcd->clip.x1 : draw_x_end;
    clip_y_end   = (draw_y_end > (int16_t)p_lcd->clip.y1) ? (int16_t)p_lcd->clip.y1 : draw_y_end;

    if (clip_x_start > clip_x_end || clip_y_start > clip_y_end)
        return;

    gram       = (colour_t *)p_lcd->pfb_gram;
    pfb_stride = p_lcd->pfb_width;

    switch (bpp)
    {
    case 8U:
    {
        uint32_t mask_x = (uint32_t)(clip_x_start - x);
        const uint8_t *src_row = src_data + (uint32_t)(clip_y_start - y) * row_stride + mask_x;
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->clip.y0) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->clip.x0);

        for (int16_t py = clip_y_start; py <= clip_y_end; py++)
        {
            const uint8_t *src = src_row;
            colour_t *dst = dst_row;

            for (int16_t px = clip_x_start; px <= clip_x_end; px++, src++, dst++)
            {
                uint8_t alpha = *src;
                if (alpha == 0U)
                    continue;
                if (opacity != 255U)
                    alpha = we_div255((uint32_t)alpha * opacity);
                we_store_blended_color(dst, fg_color, alpha);
            }

            src_row += row_stride;
            dst_row += pfb_stride;
        }
        break;
    }

    case 4U:
    {
        uint32_t bit_pos = (uint32_t)(clip_x_start - x) << 2;
        const uint8_t *src_row = src_data + (uint32_t)(clip_y_start - y) * row_stride + (bit_pos >> 3U);
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->clip.y0) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->clip.x0);
        uint8_t shift_start = (uint8_t)(4U - (bit_pos & 7U));

        if (opacity == 255U)
        {
            for (int16_t py = clip_y_start; py <= clip_y_end; py++)
            {
                const uint8_t *src = src_row;
                colour_t *dst = dst_row;
                uint8_t shift = shift_start;

                for (int16_t px = clip_x_start; px <= clip_x_end; px++, dst++)
                {
                    uint8_t a_raw = (uint8_t)((*src >> shift) & 0x0FU);
                    uint8_t alpha;

                    if (shift == 4U)
                    {
                        shift = 0U;
                    }
                    else
                    {
                        shift = 4U;
                        src++;
                    }

                    if (a_raw == 0U)
                        continue;
                    if (a_raw == 0x0FU)
                    {
                        we_store_color(dst, fg_color);
                        continue;
                    }

                    alpha = (uint8_t)((a_raw << 4U) | a_raw);
                    we_store_blended_color(dst, fg_color, alpha);
                }

                src_row += row_stride;
                dst_row += pfb_stride;
            }
        }
        else
        {
            for (int16_t py = clip_y_start; py <= clip_y_end; py++)
            {
                const uint8_t *src = src_row;
                colour_t *dst = dst_row;
                uint8_t shift = shift_start;

                for (int16_t px = clip_x_start; px <= clip_x_end; px++, dst++)
                {
                    uint8_t a_raw = (uint8_t)((*src >> shift) & 0x0FU);
                    uint8_t alpha;

                    if (shift == 4U)
                    {
                        shift = 0U;
                    }
                    else
                    {
                        shift = 4U;
                        src++;
                    }

                    if (a_raw == 0U)
                        continue;

                    alpha = (uint8_t)((a_raw << 4U) | a_raw);
                    alpha = we_div255((uint32_t)alpha * opacity);
                    we_store_blended_color(dst, fg_color, alpha);
                }

                src_row += row_stride;
                dst_row += pfb_stride;
            }
        }
        break;
    }

    case 2U:
    {
        uint32_t bit_pos = (uint32_t)(clip_x_start - x) << 1;
        const uint8_t *src_row = src_data + (uint32_t)(clip_y_start - y) * row_stride + (bit_pos >> 3U);
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->clip.y0) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->clip.x0);
        uint8_t shift_start = (uint8_t)(6U - (bit_pos & 7U));

        for (int16_t py = clip_y_start; py <= clip_y_end; py++)
        {
            const uint8_t *src = src_row;
            colour_t *dst = dst_row;
            uint8_t shift = shift_start;

            for (int16_t px = clip_x_start; px <= clip_x_end; px++, dst++)
            {
                uint8_t a_raw = (uint8_t)((*src >> shift) & 0x03U);
                uint8_t alpha;

                if (shift == 6U)
                {
                    shift = 4U;
                }
                else if (shift == 4U)
                {
                    shift = 2U;
                }
                else if (shift == 2U)
                {
                    shift = 0U;
                }
                else
                {
                    shift = 6U;
                    src++;
                }

                if (a_raw == 0U)
                    continue;

                alpha = (uint8_t)(a_raw * 85U);
                if (opacity != 255U)
                    alpha = we_div255((uint32_t)alpha * opacity);
                we_store_blended_color(dst, fg_color, alpha);
            }

            src_row += row_stride;
            dst_row += pfb_stride;
        }
        break;
    }

    default: /* 1bpp */
    {
        uint32_t bit_pos = (uint32_t)(clip_x_start - x);
        const uint8_t *src_row = src_data + (uint32_t)(clip_y_start - y) * row_stride + (bit_pos >> 3U);
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->clip.y0) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->clip.x0);
        uint8_t shift_start = (uint8_t)(7U - (bit_pos & 7U));

        for (int16_t py = clip_y_start; py <= clip_y_end; py++)
        {
            const uint8_t *src = src_row;
            colour_t *dst = dst_row;
            uint8_t shift = shift_start;

            for (int16_t px = clip_x_start; px <= clip_x_end; px++, dst++)
            {
                uint8_t bit = (uint8_t)((*src >> shift) & 0x01U);
                uint8_t alpha;

                if (shift == 0U)
                {
                    shift = 7U;
                    src++;
                }
                else
                {
                    shift--;
                }

                if (bit == 0U)
                    continue;

                alpha = 255U;
                if (opacity != 255U)
                    alpha = we_div255((uint32_t)alpha * opacity);
                we_store_blended_color(dst, fg_color, alpha);
            }

            src_row += row_stride;
            dst_row += pfb_stride;
        }
        break;
    }
    }
}

/**
 * @brief 绘制按行字节对齐、高位在前的 alpha 位图（字形点阵 / A1~A8 图标）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：位图左上角 X 坐标
 * @param y 传入：位图左上角 Y 坐标
 * @param w 传入：位图宽度（像素）
 * @param h 传入：位图高度（行数）
 * @param src_data 传入：第 0 行数据首地址
 * @param row_stride 传入：每行字节数
 * @param bpp 传入：位深（1/2/4/8）
 * @param fg_color 传入：前景颜色
 * @param opacity 传入：整体透明度（入口乘入容器透明度级联）
 * @return 无
 */
void we_draw_alpha_mask(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h,
                        const uint8_t *src_data, uint32_t row_stride, uint8_t bpp,
                        colour_t fg_color, uint8_t opacity)
{
    if (p_lcd == NULL)
        return;
    _we_draw_alpha_mask_raw(p_lcd, x, y, w, h, src_data, row_stride, bpp, fg_color,
                            we_opa_apply(p_lcd, opacity));
}
