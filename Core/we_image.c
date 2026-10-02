/**
 * @file  we_image.c
 * @brief 图片渲染：RGB565 / ARGB8565 原始图、A1/A2/A4/A8 透明位图、
 *        索引 QOI（RGB565 / ARGB8565）、索引 QOI_MASK，以及按格式分发。
 *        全部经由 we_lcd_t 的 PFB 窗口写入，只解码当前切片可见的行。
 */

#include "wegui.h"

/**
 * @brief  在局部帧缓冲(PFB)中绘制一张原始 RGB565 图片
 * @param  p_lcd    当前 LCD 上下文
 * @param  x0       图片左上角屏幕坐标 X
 * @param  y0       图片左上角屏幕坐标 Y
 * @param  img      图片资源描述
 * @param  opacity  全局透明度，0 为完全透明，255 为完全不透明
 */
void we_img_render_rgb565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity)
{
    // 容器级联后 <= 5 时什么也画不出，直接返回，避免进入后续裁剪和逐像素循环。
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity <= 5U)
        return;

    /* ---------------- 1. 局部变量准备 ---------------- */
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);
    int16_t x1, y1;
    int16_t draw_x0, draw_y0;
    uint16_t ix_start, iy_start;
    uint16_t draw_width, draw_height;
    uint16_t src_stride, dst_stride;
    uint16_t x, y;

    uint8_t *src_line;
    colour_t *dst_line;
    uint8_t *p_src;
    colour_t *p_dst;

    /* ---------------- 2. 先计算图片完整包围盒，再做快速裁剪 ---------------- */
    x1 = x0 + img_w - 1;
    y1 = y0 + img_h - 1;

    // 如果整张图片与当前 PFB 切片没有交集，直接跳过。
    if ((x0 > p_lcd->clip.x1) || (x1 < p_lcd->clip.x0) || (y0 > p_lcd->clip.y1) || (y1 < p_lcd->clip.y0))
    {
        return;
    }

    /* ---------------- 3. 计算源图起始偏移和实际可绘制尺寸 ---------------- */
    // 当图片左侧或上侧超出当前 PFB 切片时，需要跳过对应数量的源像素。
    ix_start = (x0 < p_lcd->clip.x0) ? (p_lcd->clip.x0 - x0) : 0;
    iy_start = (y0 < p_lcd->clip.y0) ? (p_lcd->clip.y0 - y0) : 0;

    // 实际绘制宽高 = 原始宽高 - 左上裁掉部分 - 右下超出部分。
    draw_width = img_w - ix_start - ((x1 > p_lcd->clip.x1) ? (x1 - p_lcd->clip.x1) : 0);
    draw_height = img_h - iy_start - ((y1 > p_lcd->clip.y1) ? (y1 - p_lcd->clip.y1) : 0);

    /* ---------------- 4. 预先计算源图/目标图起始指针与跨距 ---------------- */
    // RGB565 每像素 2 字节，因此源图偏移要乘 2。
    src_line = (uint8_t *)IMG_DAT_PIXELS(img) + (iy_start * img_w + ix_start) * 2;

    // 目标指针映射到当前 PFB 的局部坐标系。
    draw_x0 = x0 + ix_start;
    draw_y0 = y0 + iy_start;
    dst_line = p_lcd->pfb_gram + ((draw_y0 - p_lcd->clip.y0) * p_lcd->pfb_width) + (draw_x0 - p_lcd->clip.x0);

    // 把每行跨距提前算好，避免在内层循环里重复乘法。
    src_stride = img_w * 2;        // 源图每行字节数
    dst_stride = p_lcd->pfb_width; // PFB 每行像素数

    /* ---------------- 5. 核心逐像素绘制循环 ----------------
     * opacity 在整次绘制内是常量，先在循环外分类，避免每像素重复判定：
     *   不透明 → 退化为带大小端交换的逐行拷贝（图标/背景最常见场景）
     *   半透明 → 走逐像素混色 */
    if (opacity >= 250U)
    {
        for (y = 0; y < draw_height; y++)
        {
            p_dst = dst_line;
            p_src = src_line;

            for (x = 0; x < draw_width; x++)
            {
                uint16_t cur_pixel = ((uint16_t)p_src[0] << 8) | p_src[1];
                we_store_color(p_dst, we_color_from_rgb565(cur_pixel));

                p_dst++;
                p_src += 2;
            }

            src_line += src_stride;
            dst_line += dst_stride;
        }
    }
    else
    {
        for (y = 0; y < draw_height; y++)
        {
            p_dst = dst_line;
            p_src = src_line;

            for (x = 0; x < draw_width; x++)
            {
                // 从源图按大端读取一个 RGB565 像素（byte[0]=高字节，byte[1]=低字节）。
                uint16_t cur_pixel = ((uint16_t)p_src[0] << 8) | p_src[1];
                colour_t fg = we_color_from_rgb565(cur_pixel);
                we_store_blended_color(p_dst, fg, opacity);

                p_dst++;
                p_src += 2; // 源图前进 1 个像素
            }

            // 一行绘制结束后，切到下一行起始地址。
            src_line += src_stride;
            dst_line += dst_stride;
        }
    }
}

/**
 * @brief 渲染 ARGB8565 原始图片到屏幕（逐像素 [alpha][RGB565 大端] 3 字节）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：图片数据指针（ARGB8565 格式）
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 */
void we_img_render_argb8565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity)
{
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity <= 5U)
        return; /* 什么也画不出 */

    /* ---------------- 1. 局部变量准备 ---------------- */
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);
    int16_t x1, y1;
    int16_t draw_x0, draw_y0;
    uint16_t ix_start, iy_start;
    uint16_t draw_width, draw_height;
    uint16_t src_stride, dst_stride;
    uint16_t x, y;

    const uint8_t *src_line;
    colour_t *dst_line;
    const uint8_t *p_src;
    colour_t *p_dst;

    /* ---------------- 2. 包围盒与 PFB 切片求交 ---------------- */
    x1 = x0 + img_w - 1;
    y1 = y0 + img_h - 1;

    if ((x0 > p_lcd->clip.x1) || (x1 < p_lcd->clip.x0) || (y0 > p_lcd->clip.y1) || (y1 < p_lcd->clip.y0))
    {
        return;
    }

    /* ---------------- 3. 源图起始偏移与实际可绘制尺寸 ---------------- */
    ix_start = (x0 < p_lcd->clip.x0) ? (p_lcd->clip.x0 - x0) : 0;
    iy_start = (y0 < p_lcd->clip.y0) ? (p_lcd->clip.y0 - y0) : 0;

    draw_width = img_w - ix_start - ((x1 > p_lcd->clip.x1) ? (x1 - p_lcd->clip.x1) : 0);
    draw_height = img_h - iy_start - ((y1 > p_lcd->clip.y1) ? (y1 - p_lcd->clip.y1) : 0);

    /* ---------------- 4. 源/目标起始指针与跨距 ---------------- */
    // ARGB8565 每像素 3 字节：[alpha][RGB565 高字节][RGB565 低字节]。
    src_line = IMG_DAT_PIXELS(img) + ((uint32_t)iy_start * img_w + ix_start) * 3U;

    draw_x0 = x0 + ix_start;
    draw_y0 = y0 + iy_start;
    dst_line = p_lcd->pfb_gram + ((draw_y0 - p_lcd->clip.y0) * p_lcd->pfb_width) + (draw_x0 - p_lcd->clip.x0);

    src_stride = img_w * 3;
    dst_stride = p_lcd->pfb_width;

    /* ---------------- 5. 核心逐像素绘制循环 ----------------
     * opacity 在整次绘制内是常量，先在循环外分类：
     *   控件不透明 → 像素 alpha 直接使用，省掉每像素一次乘法
     *   控件半透明 → 像素 alpha 与 opacity 相乘后混色 */
    if (opacity >= 250U)
    {
        for (y = 0; y < draw_height; y++)
        {
            p_dst = dst_line;
            p_src = src_line;

            for (x = 0; x < draw_width; x++)
            {
                uint8_t a = p_src[0];

                if (a > 0)
                {
                    uint16_t cur_pixel = ((uint16_t)p_src[1] << 8) | p_src[2];
                    we_store_blended_color(p_dst, we_color_from_rgb565(cur_pixel), a);
                }
                p_dst++;
                p_src += 3;
            }

            src_line += src_stride;
            dst_line += dst_stride;
        }
    }
    else
    {
        for (y = 0; y < draw_height; y++)
        {
            p_dst = dst_line;
            p_src = src_line;

            for (x = 0; x < draw_width; x++)
            {
                uint8_t a = p_src[0];

                if (a > 0)
                {
                    uint16_t cur_pixel = ((uint16_t)p_src[1] << 8) | p_src[2];
                    we_store_blended_color(p_dst, we_color_from_rgb565(cur_pixel), we_div255((uint32_t)a * opacity));
                }
                p_dst++;
                p_src += 3;
            }

            src_line += src_stride;
            dst_line += dst_stride;
        }
    }
}

/**
 * @brief 渲染 A1/A2/A4/A8 透明位图到屏幕（仅 alpha 通道，以前景色混合）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：透明位图数据指针（IMG_A1/A2/A4/A8 格式）
 * @param fg_color 传入：前景色（位图 alpha 以该颜色对背景混合）
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 * @note 取模数据每行按字节对齐（行末补零）、位序高位在前；
 *       连续位流布局的取模数据与此不兼容，不能混用
 */
void we_img_render_alpha(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, colour_t fg_color, uint8_t opacity)
{
    uint8_t bpp;
    uint16_t img_w = IMG_DAT_WIDTH(img);

    switch (IMG_DAT_FORMAT(img))
    {
    case IMG_A8:
        bpp = 8U;
        break;
    case IMG_A4:
        bpp = 4U;
        break;
    case IMG_A2:
        bpp = 2U;
        break;
    default: /* IMG_A1 */
        bpp = 1U;
        break;
    }

    /* 与字形点阵同一格式（行按字节对齐、高位在前），交给公共 alpha 位图原语 */
    we_draw_alpha_mask(p_lcd, x0, y0, img_w, IMG_DAT_HEIGHT(img), IMG_DAT_PIXELS(img),
                       (((uint32_t)img_w * bpp) + 7U) >> 3, bpp, fg_color, opacity);
}
#if (WE_CFG_ENABLE_INDEXQOI_MASK == 1)
/**
 * @brief 渲染索引QOI_MASK 压缩的 A8 透明蒙版（仅 alpha 通道，以前景色混合）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：图片数据指针（IMG_A8_INDEXQOIMASK 格式）
 * @param fg_color 传入：前景色（蒙版 alpha 以该颜色对背景混合）
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 * @note 行独立编码（RUN/DIFF 不跨行）+ 行字节偏移索引：PFB 切片渲染只解码
 *       可见行，行内越过裁剪右缘立即停止，单行解码零额外 RAM（边解边混合）。
 *       流内数值全部是 q 位量化域值，输出时按高位复制还原 8bit。
 *       格式规格：tool/0.tool/windows/img2bin_indexqoimask/README。
 */
void we_img_render_indexqoi_mask(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img,
                                 colour_t fg_color, uint8_t opacity)
{
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity <= 5U)
        return; /* 什么也画不出 */

    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);

    /* ---------------- 1. 包围盒与 PFB 切片求交 ---------------- */
    int16_t x1 = x0 + img_w - 1;
    int16_t y1 = y0 + img_h - 1;

    if ((x0 > p_lcd->clip.x1) || (x1 < p_lcd->clip.x0) || (y0 > p_lcd->clip.y1) || (y1 < p_lcd->clip.y0))
    {
        return;
    }

    uint16_t ix_start = (x0 < p_lcd->clip.x0) ? (p_lcd->clip.x0 - x0) : 0;
    uint16_t iy_start = (y0 < p_lcd->clip.y0) ? (p_lcd->clip.y0 - y0) : 0;

    uint16_t draw_width = img_w - ix_start - ((x1 > p_lcd->clip.x1) ? (x1 - p_lcd->clip.x1) : 0);
    uint16_t draw_height = img_h - iy_start - ((y1 > p_lcd->clip.y1) ? (y1 - p_lcd->clip.y1) : 0);

    /* ---------------- 2. payload 解析 ----------------
     * 6 字节通用头之后：[标志 1B][u16 行索引项数 m 2B][u32 行索引项数 2B]
     * [u16 表 m×2B][u32 表 (h−m)×4B][字典数 n 1B][字典 n×1B][像素流]
     * 行偏移相对像素流起点，多字节字段一律大端。 */
    const uint8_t *pl = IMG_DAT_PIXELS(img);
    uint8_t q = (uint8_t)(8U - (pl[0] & 0x03U)); /* 量化位数：00b=8bit 01b=7 10b=6 11b=5 */
    uint8_t shift = (uint8_t)(8U - q);           /* 反量化（高位复制）左移量 */
    uint16_t m = (uint16_t)(((uint16_t)pl[1] << 8) | pl[2]);

    /* 损坏流防线（与 indexed-QOI 路径同级）：m 超高度会使下面的
     * (img_h - m) 无符号下溢出天文偏移，必须先拦 */
    if (m > img_h)
        return;

    const uint8_t *tab16 = pl + 5;
    const uint8_t *tab32 = tab16 + ((uint32_t)m << 1);
    const uint8_t *p_dict_n = tab32 + (((uint32_t)(img_h - m)) << 2);
    const uint8_t *dict = p_dict_n + 1;
    const uint8_t *base = dict + *p_dict_n; /* 像素流起点（行偏移的基准） */

    /* 字典数上限 = INDEX 操作码域宽（0x00..0x3F）；行偏移保守上界按
     * 最坏 2 字节/像素 + 行首字节推出，拦截损坏索引表的任意地址跳转。
     * 合法码流全部通过，解码逐位不变。 */
    uint32_t stream_span = ((uint32_t)img_w * 2U + 1U) * img_h;

    if (*p_dict_n > 64U)
        return;

    /* ---------------- 3. 目标起始指针与跨距 ---------------- */
    int16_t draw_x0 = x0 + ix_start;
    int16_t draw_y0 = y0 + iy_start;
    colour_t *dst_line = p_lcd->pfb_gram + ((draw_y0 - p_lcd->clip.y0) * p_lcd->pfb_width) + (draw_x0 - p_lcd->clip.x0);
    uint16_t dst_stride = p_lcd->pfb_width;

    uint16_t x_end = (uint16_t)(ix_start + draw_width); /* 行内绘制右缘（源列，开区间） */
    uint16_t row;

/* 输出 1 个 q 位域像素：高位复制还原 8bit → 乘控件透明度 → 前景色混合。
 * 只在可见列区间内落笔，但无论落笔与否列游标 x 都前进一格。
 * q=8 时 shift=0：v>>8 恒 0，表达式退化为 v 本身，无需特判。 */
#define _IQM_EMIT(v)                                                                             \
    do                                                                                           \
    {                                                                                            \
        if (x >= ix_start && x < x_end)                                                          \
        {                                                                                        \
            uint32_t a_ = ((uint32_t)(v) << shift) | ((uint32_t)(v) >> (q - shift));             \
            if (a_ > 0U)                                                                         \
            {                                                                                    \
                if (opacity != 255U)                                                             \
                    a_ = we_div255(a_ * opacity);                                                \
                we_store_blended_color(p_dst + (x - ix_start), fg_color, (uint8_t)a_);           \
            }                                                                                    \
        }                                                                                        \
        x++;                                                                                     \
    } while (0)

    /* ---------------- 4. 行循环：可见行经行索引定位后独立解码 ---------------- */
    for (row = iy_start; row < (uint16_t)(iy_start + draw_height); row++)
    {
        uint32_t ofs;
        const uint8_t *p;
        colour_t *p_dst = dst_line;
        uint16_t x = 0;
        uint8_t prev;

        if (row < m)
        {
            uint32_t i2 = (uint32_t)row << 1;

            ofs = ((uint32_t)tab16[i2] << 8) | tab16[i2 + 1U];
        }
        else
        {
            uint32_t i4 = (uint32_t)(row - m) << 2;

            ofs = ((uint32_t)tab32[i4] << 24) | ((uint32_t)tab32[i4 + 1U] << 16) |
                  ((uint32_t)tab32[i4 + 2U] << 8) | tab32[i4 + 3U];
        }
        if (ofs >= stream_span)
            return; /* 损坏的行偏移：越过保守流上界，放弃绘制 */
        p = base + ofs;

        prev = *p++; /* 行首字节 = 首像素原始值（无 tag），同时初始化 prev */
        _IQM_EMIT(prev);

        while (x < x_end) /* 越过裁剪右缘即止：行独立，剩余字节与后续行无关 */
        {
            uint8_t b = *p++;

            if (b == 0xFF) /* ALPHA：绝对值直设，后接 1 字节原始值 */
            {
                prev = *p++;
                _IQM_EMIT(prev);
            }
            else
            {
                switch (b >> 6)
                {
                case 0: /* INDEX：查静态字典 */
                    prev = dict[b & 0x3FU];
                    _IQM_EMIT(prev);
                    break;

                case 1: /* DIFF：连续 2 像素小渐变，各 3bit [-4,+3]（存储值 = d+4） */
                    prev = (uint8_t)(prev + (int16_t)((b >> 3) & 0x07U) - 4);
                    _IQM_EMIT(prev);
                    prev = (uint8_t)(prev + (int16_t)(b & 0x07U) - 4);
                    _IQM_EMIT(prev);
                    break;

                case 2: /* DELTA：1 像素大渐变 6bit [-32,+31]（存储值 = d+32） */
                    prev = (uint8_t)(prev + (int16_t)(b & 0x3FU) - 32);
                    _IQM_EMIT(prev);
                    break;

                default: /* RUN：复制 prev (计数+1) 次，1..63（0xFF 已被 ALPHA 特判占用） */
                {
                    uint8_t cnt = (uint8_t)((b & 0x3FU) + 1U);

                    if (prev == 0U)
                    {
                        x = (uint16_t)(x + cnt); /* 全透明跑段直接跳列（图标留白主路径） */
                    }
                    else
                    {
                        while (cnt--)
                            _IQM_EMIT(prev);
                    }
                    break;
                }
                }
            }
        }

        dst_line += dst_stride;
    }
#undef _IQM_EMIT
}
#endif /* WE_CFG_ENABLE_INDEXQOI_MASK */

/* --------------------------------------------------------------------------
 * 索引 QOI 绘制入口（V3 流：14 字节索引头 0x0F + u16/u32 行索引表 + 行去重
 * + 静态调色盘）
 *
 * V3 行自包含：行首 op 只会是调色盘 op 或 0xFF 原始全量、RUN 不跨行，
 * 因此每行经行索引空降解码，与 PFB 按行切片天然契合；内容相同的行共享
 * 同一偏移（编码侧行去重，解码器无感知）；调色盘全局有效、永不修改。
 * -------------------------------------------------------------------------- */
#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
/**
 * @brief 索引 QOI 解码（RGB565 / ARGB8565 共用）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：图片左上角屏幕 X 坐标
 * @param y0 传入：图片左上角屏幕 Y 坐标
 * @param img 传入：索引 QOI 图片资源描述
 * @param opacity 传入：整体透明度（已乘容器级联，> 5）
 * @param px_bytes 传入：每像素字节数（RGB565 = 2，ARGB8565 = 3）
 * @return 无
 * @note payload 布局：[14字节索引头][u16行索引表][u32行索引表][静态调色盘][QOI数据流]。
 *       ARGB8565 与 RGB565 的差异只在两处：调色盘条目 3 字节 [A][H][L]、0xFF 带
 *       1 字节 alpha（0xFE 只换颜色、alpha 不变）；输出 alpha = 像素 alpha × opacity / 255，
 *       RGB565 恒为 opacity。行首 op 必然重建完整状态（含 alpha）。
 */
static void _we_img_render_indexed_qoi(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img,
                                       uint8_t opacity, uint8_t px_bytes)
{
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);
    int16_t x1 = (int16_t)(x0 + img_w - 1);
    int16_t y1 = (int16_t)(y0 + img_h - 1);
    const uint8_t *dat = IMG_DAT_PIXELS(img);
    uint16_t m16;
    uint8_t pal_count;
    uint16_t ix_start;
    uint16_t clip_x_end;
    uint16_t cur_y;
    uint16_t clip_y_end;
    const uint8_t *idx_u16;
    const uint8_t *idx_u32;
    const uint8_t *palette;
    const uint8_t *qoi_start;
    const uint8_t *data_end;
    uint32_t stream_max;
    colour_t *row_dst;

    /* 宽或高为 0 视为损坏资源；整张图与当前 PFB 切片无交集时直接剔除 */
    if (img_w == 0U || img_h == 0U ||
        x0 > p_lcd->clip.x1 || x1 < p_lcd->clip.x0 || y0 > p_lcd->clip.y1 || y1 < p_lcd->clip.y0)
        return;

    /* V3 索引头固定 14 字节，byte0=0x0F 兼作版本标识（V2 的 0x0E、V1 的 0x0D 均不支持）；
     * m16 不可能超过行数；调色盘条目数上限 64，超出视为损坏流 */
    m16 = (uint16_t)(((uint16_t)dat[5] << 8) | dat[6]);
    pal_count = dat[13];
    if (dat[0] != 0x0FU || m16 > img_h || pal_count > 64U)
        return;

    /* 资源头没有总长度字段：用"最坏 4 字节/像素 + 余量"的保守流长上界拦截损坏索引表
     * 带来的任意地址跳转，并给解码循环一个硬性越界停止线（合法码流必然短于该上界） */
    if ((uint32_t)img_w * img_h > ((0xFFFFFFFFUL - 16UL) >> 2))
        return;
    stream_max = (((uint32_t)img_w * img_h) << 2) + 16UL;

    ix_start = (uint16_t)((x0 < p_lcd->clip.x0) ? (p_lcd->clip.x0 - x0) : 0);
    cur_y = (uint16_t)((y0 < p_lcd->clip.y0) ? (p_lcd->clip.y0 - y0) : 0);
    clip_x_end = (uint16_t)(((x1 > p_lcd->clip.x1) ? (p_lcd->clip.x1 - x0) : (img_w - 1)) + 1);
    clip_y_end = (uint16_t)(((y1 > p_lcd->clip.y1) ? (p_lcd->clip.y1 - y0) : (img_h - 1)) + 1);

    idx_u16 = dat + 14U;
    idx_u32 = idx_u16 + ((uint32_t)m16 << 1);
    palette = idx_u32 + (((uint32_t)(img_h - m16)) << 2);
    qoi_start = palette + (uint16_t)pal_count * px_bytes;
    data_end = qoi_start + stream_max;
    /* 行内以图片列号寻址 */
    row_dst = p_lcd->pfb_gram + ((int32_t)(y0 + (int16_t)cur_y - p_lcd->clip.y0) * p_lcd->pfb_width) +
              (x0 - p_lcd->clip.x0);

    /* ---------------- 行循环 ----------------
     * 逐可见行经行索引空降解码，行内越过裁剪右缘立即停止；
     * arry < data_end 是损坏资源的硬性越界停止线。 */
    for (; cur_y < clip_y_end; cur_y++, row_dst += p_lcd->pfb_width)
    {
        uint32_t row_off;
        const uint8_t *arry;
        uint16_t cur_x = 0;
        uint8_t r = 0, g = 0, b = 0;
        uint16_t cur_pixel = 0;
        uint8_t final_alpha = opacity; /* ARGB8565 行首 op 必然重建 alpha，这里只是缺省值 */

        if (cur_y < m16)
        {
            const uint8_t *e = idx_u16 + ((uint32_t)cur_y << 1);
            row_off = ((uint32_t)e[0] << 8) | e[1];
        }
        else
        {
            const uint8_t *e = idx_u32 + (((uint32_t)(cur_y - m16)) << 2);
            row_off = ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3];
        }
        if (row_off >= stream_max)
            return; /* 损坏索引表 */

        arry = qoi_start + row_off;
        while ((cur_x < clip_x_end) && (arry < data_end))
        {
            uint8_t flag = *arry++;

            if (flag >= 0xFEU)
            {
                /* 0xFF / 0xFE：原始像素（ARGB8565 的 0xFF 先带 1 字节 alpha；RGB565 流只用 0xFF，
                 * 0xFE 容错按全量处理） */
                uint8_t h;
                uint8_t l;

                if (flag == 0xFFU && px_bytes == 3U)
                    final_alpha = we_div255((uint32_t)(*arry++) * opacity);
                h = *arry++;
                l = *arry++;
                cur_pixel = ((uint16_t)h << 8) | l;
                r = h >> 3;
                g = (uint8_t)(((h & 0x07U) << 3) | (l >> 5));
                b = l & 0x1FU;
            }
            else if ((flag & 0xC0U) == 0x40U)
            {
                /* RGB 小差值，alpha 不变 */
                r = (uint8_t)((r + ((flag >> 4) & 0x03U) - 2U) & 0x1FU);
                g = (uint8_t)((g + ((flag >> 2) & 0x03U) - 2U) & 0x3FU);
                b = (uint8_t)((b + (flag & 0x03U) - 2U) & 0x1FU);
                cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
            }
            else if ((flag & 0xC0U) == 0x80U)
            {
                /* RGB luma 差值，alpha 不变 */
                int8_t vg = (int8_t)((flag & 0x3FU) - 32U);
                uint8_t next_byte = *arry++;
                r = (uint8_t)((r + vg - 8 + ((next_byte >> 4) & 0x0FU)) & 0x1FU);
                g = (uint8_t)((g + vg) & 0x3FU);
                b = (uint8_t)((b + vg - 8 + (next_byte & 0x0FU)) & 0x1FU);
                cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
            }
            else if (flag < 0x40U)
            {
                /* 0x00..0x3F：静态调色盘 op。条目 = 完整原始格式像素（ARGB8565 先带 alpha），
                 * 全局有效、解码中永不修改；查到即成为后续 DIFF/LUMA 的前像素。 */
                const uint8_t *pe;

                if (flag >= pal_count)
                    return; /* 超出条目数视为损坏流 */
                pe = palette + (uint16_t)flag * px_bytes;
                if (px_bytes == 3U)
                    final_alpha = we_div255((uint32_t)(*pe++) * opacity);
                cur_pixel = ((uint16_t)pe[0] << 8) | pe[1];
                r = pe[0] >> 3;
                g = (uint8_t)(((pe[0] & 0x07U) << 3) | (pe[1] >> 5));
                b = pe[1] & 0x1FU;
            }
            else /* 0xC0..0xFD：RUN（不跨行）：先跳过裁剪左缘以外的部分，再连续填到右缘 */
            {
                uint16_t end = (uint16_t)(cur_x + (flag & 0x3FU) + 1U);

                if (end > clip_x_end)
                    end = clip_x_end; /* 行内右缘截断：本行剩余像素不可见，填完即结束本行 */
                if (end > ix_start)
                {
                    colour_t fg = we_color_from_rgb565(cur_pixel);
                    uint16_t x = (cur_x > ix_start) ? cur_x : ix_start;

                    for (; x < end; x++)
                        we_store_blended_color(row_dst + x, fg, final_alpha);
                }
                cur_x = end;
                continue;
            }

            if (cur_x >= ix_start)
                we_store_blended_color(row_dst + cur_x, we_color_from_rgb565(cur_pixel), final_alpha);
            cur_x++;
        }
    }
}

/**
 * @brief  在局部帧缓冲(PFB)中绘制索引 QOI 压缩的 RGB565 图片
 * @param  p_lcd    传入，当前 GUI 屏幕上下文指针
 * @param  x0       传入，图片左上角屏幕坐标 X
 * @param  y0       传入，图片左上角屏幕坐标 Y
 * @param  img      传入，索引 QOI 图片资源描述
 * @param  opacity  传入，全局透明度，0 为完全透明，255 为完全不透明
 * @return 无
 */
void we_img_render_indexed_qoi_rgb565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity)
{
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity <= 5U)
        return; /* 什么也画不出 */
    _we_img_render_indexed_qoi(p_lcd, x0, y0, img, opacity, 2U);
}

/**
 * @brief  在局部帧缓冲(PFB)中绘制索引 QOI 压缩的 ARGB8565 图片
 * @param  p_lcd    传入，当前 GUI 屏幕上下文指针
 * @param  x0       传入，图片左上角屏幕坐标 X
 * @param  y0       传入，图片左上角屏幕坐标 Y
 * @param  img      传入，索引 QOI 图片资源描述
 * @param  opacity  传入，全局透明度，最终会与像素内 alpha 相乘
 * @return 无
 */
void we_img_render_indexed_qoi_argb8565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity)
{
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity <= 5U)
        return; /* 什么也画不出 */
    _we_img_render_indexed_qoi(p_lcd, x0, y0, img, opacity, 3U);
}
#endif

/* --------------------------------------------------------------------------
 * 图片格式分发
 *
 * 所有内置图片控件（img / imgbtn 等）共用这一份分发表：控件只决定"画哪张图、
 * 用什么透明度"，"支持哪些格式、走哪个解码器"由渲染层统一回答。新增像素格式
 * 时只改这两个函数，控件层不必跟着改。
 * -------------------------------------------------------------------------- */

/**
 * @brief 判断图片格式是否为当前编译配置支持（供控件在 init 阶段拦截）
 * @param fmt 传入，资源信息头里的格式码
 * @return 1 表示可渲染，0 表示不支持
 */
uint8_t we_img_format_supported(imgarry_type_t fmt)
{
    switch (fmt)
    {
    case IMG_RGB565:
    case IMG_ARGB8565:
    case IMG_A1:
    case IMG_A2:
    case IMG_A4:
    case IMG_A8:
        return 1U;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    case IMG_RGB565_INDEXQOI:
    case IMG_ARGB8565_INDEXQOI:
        return 1U;
#endif

#if (WE_CFG_ENABLE_INDEXQOI_MASK == 1)
    case IMG_A8_INDEXQOIMASK:
        return 1U;
#endif

    default:
        return 0U;
    }
}

/**
 * @brief 按资源格式自动选择渲染内核绘制图片
 * @param p_lcd 传入，GUI 屏幕上下文指针
 * @param x0 传入，目标左上角 X 坐标
 * @param y0 传入，目标左上角 Y 坐标
 * @param img 传入，图片数据指针（image_res.h 信息头格式）
 * @param fg_color 传入，前景色，仅 A1/A2/A4/A8 透明位图使用，其余格式忽略
 * @param opacity 传入，整体透明度（0~255）
 * @return 无
 */
void we_img_render_auto(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img,
                        colour_t fg_color, uint8_t opacity)
{
    if (p_lcd == NULL || img == NULL)
        return;

    switch (IMG_DAT_FORMAT(img))
    {
    case IMG_RGB565:
        we_img_render_rgb565(p_lcd, x0, y0, img, opacity);
        break;

    case IMG_ARGB8565:
        we_img_render_argb8565(p_lcd, x0, y0, img, opacity);
        break;

    case IMG_A1:
    case IMG_A2:
    case IMG_A4:
    case IMG_A8:
        we_img_render_alpha(p_lcd, x0, y0, img, fg_color, opacity);
        break;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    case IMG_RGB565_INDEXQOI:
        we_img_render_indexed_qoi_rgb565(p_lcd, x0, y0, img, opacity);
        break;

    case IMG_ARGB8565_INDEXQOI:
        we_img_render_indexed_qoi_argb8565(p_lcd, x0, y0, img, opacity);
        break;
#endif

#if (WE_CFG_ENABLE_INDEXQOI_MASK == 1)
    case IMG_A8_INDEXQOIMASK:
        we_img_render_indexqoi_mask(p_lcd, x0, y0, img, fg_color, opacity);
        break;
#endif

    default:
        break; /* 不支持的格式静默跳过（控件已在 init 拦截） */
    }
}

