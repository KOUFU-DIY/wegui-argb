/**
 * @file  we_render.c
 * @brief 渲染原语实现（从 we_gui_driver.c 纯搬移拆分，零语义变更）
 *
 * 收录：图片解码渲染（RGB565 raw / 索引 QOI RGB565 / 索引 QOI ARGB8565）、
 * 矩形填充、四分之一圆/圆环/圆角矩形覆盖率蒙版、圆角矩形解析填充、
 * 像素/AA 线段（Xiaolin Wu）/宽线段、sin/cos 查表、alpha 蒙版位图、
 * 整屏清底。全部经由 we_lcd_t 的 PFB 窗口写入，接口声明见
 * we_render.h / we_gui_driver.h（保持原有归属不动）。
 *
 * 约定与 we_gui_driver.c 相同：零 malloc、零浮点热路径、
 * 容器透明度级联经 we_opa_apply 在公开入口消费一次。
 */

#include "we_gui_driver.h"
#include "image_res.h"
#include "we_render.h"

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
    // 完全透明时直接返回，避免进入后续裁剪和逐像素循环。
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0)
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
    if ((x0 > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y0 > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
    {
        return;
    }

    /* ---------------- 3. 计算源图起始偏移和实际可绘制尺寸 ---------------- */
    // 当图片左侧或上侧超出当前 PFB 切片时，需要跳过对应数量的源像素。
    ix_start = (x0 < p_lcd->pfb_area.x0) ? (p_lcd->pfb_area.x0 - x0) : 0;
    iy_start = (y0 < p_lcd->pfb_y_start) ? (p_lcd->pfb_y_start - y0) : 0;

    // 实际绘制宽高 = 原始宽高 - 左上裁掉部分 - 右下超出部分。
    draw_width = img_w - ix_start - ((x1 > p_lcd->pfb_area.x1) ? (x1 - p_lcd->pfb_area.x1) : 0);
    draw_height = img_h - iy_start - ((y1 > p_lcd->pfb_y_end) ? (y1 - p_lcd->pfb_y_end) : 0);

    /* ---------------- 4. 预先计算源图/目标图起始指针与跨距 ---------------- */
    // RGB565 每像素 2 字节，因此源图偏移要乘 2。
    src_line = (uint8_t *)IMG_DAT_PIXELS(img) + (iy_start * img_w + ix_start) * 2;

    // 目标指针映射到当前 PFB 的局部坐标系。
    draw_x0 = x0 + ix_start;
    draw_y0 = y0 + iy_start;
    dst_line = p_lcd->pfb_gram + ((draw_y0 - p_lcd->pfb_y_start) * p_lcd->pfb_width) + (draw_x0 - p_lcd->pfb_area.x0);

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
    if (opacity == 0)
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

    const uint8_t *src_line;
    colour_t *dst_line;
    const uint8_t *p_src;
    colour_t *p_dst;

    /* ---------------- 2. 包围盒与 PFB 切片求交 ---------------- */
    x1 = x0 + img_w - 1;
    y1 = y0 + img_h - 1;

    if ((x0 > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y0 > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
    {
        return;
    }

    /* ---------------- 3. 源图起始偏移与实际可绘制尺寸 ---------------- */
    ix_start = (x0 < p_lcd->pfb_area.x0) ? (p_lcd->pfb_area.x0 - x0) : 0;
    iy_start = (y0 < p_lcd->pfb_y_start) ? (p_lcd->pfb_y_start - y0) : 0;

    draw_width = img_w - ix_start - ((x1 > p_lcd->pfb_area.x1) ? (x1 - p_lcd->pfb_area.x1) : 0);
    draw_height = img_h - iy_start - ((y1 > p_lcd->pfb_y_end) ? (y1 - p_lcd->pfb_y_end) : 0);

    /* ---------------- 4. 源/目标起始指针与跨距 ---------------- */
    // ARGB8565 每像素 3 字节：[alpha][RGB565 高字节][RGB565 低字节]。
    src_line = IMG_DAT_PIXELS(img) + ((uint32_t)iy_start * img_w + ix_start) * 3U;

    draw_x0 = x0 + ix_start;
    draw_y0 = y0 + iy_start;
    dst_line = p_lcd->pfb_gram + ((draw_y0 - p_lcd->pfb_y_start) * p_lcd->pfb_width) + (draw_x0 - p_lcd->pfb_area.x0);

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
 *       与 we_draw_alpha_mask 的连续位流布局不同，不能混用
 */
void we_img_render_alpha(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, colour_t fg_color, uint8_t opacity)
{
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0)
        return;

    /* ---------------- 1. 位深与 alpha 还原乘数 ----------------
     * raw * factor 恰好把各位深的满量程映射到 255（15*17=255，3*85=255，1*255=255），
     * 内层循环只需一次乘法，无除法无分支。 */
    uint8_t bpp;
    uint8_t factor;

    switch (IMG_DAT_FORMAT(img))
    {
    case IMG_A8:
        bpp = 8;
        factor = 1;
        break;

    case IMG_A4:
        bpp = 4;
        factor = 17;
        break;

    case IMG_A2:
        bpp = 2;
        factor = 85;
        break;

    default: /* IMG_A1 */
        bpp = 1;
        factor = 255;
        break;
    }

    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);

    /* ---------------- 2. 包围盒与 PFB 切片求交 ---------------- */
    int16_t x1 = x0 + img_w - 1;
    int16_t y1 = y0 + img_h - 1;

    if ((x0 > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y0 > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
    {
        return;
    }

    /* ---------------- 3. 源图起始偏移与实际可绘制尺寸 ---------------- */
    uint16_t ix_start = (x0 < p_lcd->pfb_area.x0) ? (p_lcd->pfb_area.x0 - x0) : 0;
    uint16_t iy_start = (y0 < p_lcd->pfb_y_start) ? (p_lcd->pfb_y_start - y0) : 0;

    uint16_t draw_width = img_w - ix_start - ((x1 > p_lcd->pfb_area.x1) ? (x1 - p_lcd->pfb_area.x1) : 0);
    uint16_t draw_height = img_h - iy_start - ((y1 > p_lcd->pfb_y_end) ? (y1 - p_lcd->pfb_y_end) : 0);

    /* ---------------- 4. 源/目标起始指针与跨距 ---------------- */
    // 源图行跨距按字节对齐：一行 = ceil(img_w * bpp / 8) 字节。
    uint16_t src_stride = (uint16_t)((((uint32_t)img_w * bpp) + 7U) >> 3);
    const uint8_t *src_line = IMG_DAT_PIXELS(img) + (uint32_t)iy_start * src_stride;

    int16_t draw_x0 = x0 + ix_start;
    int16_t draw_y0 = y0 + iy_start;
    colour_t *dst_line = p_lcd->pfb_gram + ((draw_y0 - p_lcd->pfb_y_start) * p_lcd->pfb_width) + (draw_x0 - p_lcd->pfb_area.x0);
    uint16_t dst_stride = p_lcd->pfb_width;

    uint8_t alpha_mask = (uint8_t)((1U << bpp) - 1U);
    uint16_t x, y;

    /* ---------------- 5. 核心逐像素绘制循环 ---------------- */
    for (y = 0; y < draw_height; y++)
    {
        colour_t *p_dst = dst_line;

        for (x = 0; x < draw_width; x++)
        {
            // 行内位偏移 = 源列号 * bpp，字节内高位在前。
            uint32_t bit_pos = (uint32_t)(ix_start + x) * bpp;
            uint8_t raw = (uint8_t)((src_line[bit_pos >> 3] >> (8U - bpp - (bit_pos & 7U))) & alpha_mask);

            if (raw > 0)
            {
                uint32_t alpha = (uint32_t)raw * factor;

                if (opacity != 255U)
                {
                    alpha = we_div255(alpha * opacity);
                }
                we_store_blended_color(p_dst, fg_color, (uint8_t)alpha);
            }
            p_dst++;
        }

        src_line += src_stride;
        dst_line += dst_stride;
    }
}

/* --------------------------------------------------------------------------
 * 索引 QOI 绘制入口
 *
 * 当前工程只保留“索引 QOI”这一条图片解码路径。
 * 原始 QOI 已经在编译期开关里裁掉，目的有三点：
 * 1. 减少 Flash 占用
 * 2. 简化图片控件的格式分发逻辑
 * 3. 只保留当前实际会用到的解码能力
 * -------------------------------------------------------------------------- */
#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
/* --------------------------------------------------------------------------
 * 索引 QOI 解码上下文
 *
 * RGB565 与 ARGB8565 两条解码路径共用同一套“头部解析 + 索引跳转 + 裁剪参数”
 * 前置逻辑，这里用一个上下文结构体收口，避免两个入口函数各维护一份完全相同
 * 的约 70 行解析代码，显著压缩 Flash 占用。
 * -------------------------------------------------------------------------- */
typedef struct
{
    uint16_t img_w;
    uint16_t img_h;
    uint16_t ix_start;
    uint16_t iy_start;
    uint16_t clip_x_end;
    uint16_t clip_y_end;
    int16_t base_dest_x;
    int16_t base_dest_y;
    uint16_t dst_stride;
    const uint8_t *arry;     /* 解码起点指针（已跳到最近的字节偏移） */
    const uint8_t *data_end; /* 解码字节流保守硬上界（最坏 4 字节/像素），防越界读 */
    uint16_t cur_x;
    uint16_t cur_y;
    uint32_t decoded_pixels;
    uint32_t max_pixels;
} _we_qoi_dec_t;

/**
 * @brief 解析索引 QOI 头部、按目标区域跳转解码起点并算好裁剪参数
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：图片左上角屏幕 X 坐标
 * @param y0 传入：图片左上角屏幕 Y 坐标
 * @param img 传入：索引 QOI 图片资源描述
 * @param dec 传出：解码上下文（裁剪范围、起点指针、起始像素坐标等）
 * @return 1 表示需要继续解码，0 表示整图不在当前 PFB 切片内或数据非法
 * @note RGB565 / ARGB8565 两条解码路径共用本函数完成全部前置准备。
 */
static uint8_t _we_qoi_parse_and_seek(we_lcd_t *p_lcd, int16_t x0, int16_t y0,
                                      const uint8_t *img, _we_qoi_dec_t *dec)
{
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);

    /* 防御：宽或高为 0 视为损坏资源，直接拒绝（同时避免后面 % img_w 除零）。 */
    if (img_w == 0U || img_h == 0U)
        return 0U;

    int16_t x1 = x0 + img_w - 1;
    int16_t y1 = y0 + img_h - 1;

    /* 整张图与当前 PFB 切片无交集时直接剔除。 */
    if ((x0 > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) ||
        (y0 > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
        return 0U;

    uint16_t ix_start = (x0 < p_lcd->pfb_area.x0) ? (p_lcd->pfb_area.x0 - x0) : 0;
    uint16_t iy_start = (y0 < p_lcd->pfb_y_start) ? (p_lcd->pfb_y_start - y0) : 0;
    uint16_t draw_width = img_w - ix_start - ((x1 > p_lcd->pfb_area.x1) ? (x1 - p_lcd->pfb_area.x1) : 0);
    uint16_t draw_height = img_h - iy_start - ((y1 > p_lcd->pfb_y_end) ? (y1 - p_lcd->pfb_y_end) : 0);

    const uint8_t *dat = IMG_DAT_PIXELS(img);
    if (dat == 0)
        return 0U;

    uint8_t head_size = dat[0];
    uint16_t interval = (dat[5] << 8) | dat[6];
    uint16_t u16_size = (dat[7] << 8) | dat[8];
    uint16_t u24_size = (dat[9] << 8) | dat[10];
    uint16_t u32_size = (dat[11] << 8) | dat[12];

    /* 防御：索引头固定读取 dat[0..12]，head_size 小于 13 说明头部已损坏。 */
    if (head_size < 13U)
        return 0U;

    uint16_t num_u16 = u16_size / 2;
    uint16_t num_u24 = u24_size / 3;
    uint16_t num_u32 = u32_size / 4;
    uint32_t total_indices = num_u16 + num_u24 + num_u32;

    const uint8_t *idx_u16 = dat + head_size;
    const uint8_t *idx_u24 = idx_u16 + u16_size;
    const uint8_t *idx_u32 = idx_u24 + u24_size;
    const uint8_t *qoi_start = idx_u32 + u32_size;

    uint32_t first_needed_pixel = (uint32_t)iy_start * img_w + ix_start;
    uint32_t skip_intervals = (interval > 0) ? (first_needed_pixel / interval) : 0;
    uint32_t byte_offset = 0;

    if (skip_intervals > 0)
    {
        uint32_t target_idx = skip_intervals;

        /* 索引越界保护，避免错误头部导致读取越界。 */
        if (target_idx >= total_indices)
        {
            target_idx = (total_indices > 0) ? (total_indices - 1) : 0;
            skip_intervals = target_idx;
        }

        if (target_idx < num_u16)
        {
            uint32_t i = target_idx * 2;
            byte_offset = (idx_u16[i] << 8) | idx_u16[i + 1];
        }
        else if (target_idx < num_u16 + num_u24)
        {
            uint32_t i = (target_idx - num_u16) * 3;
            byte_offset = (idx_u24[i] << 16) | (idx_u24[i + 1] << 8) | idx_u24[i + 2];
        }
        else
        {
            uint32_t i = (target_idx - num_u16 - num_u24) * 4;
            byte_offset = (idx_u32[i] << 24) | (idx_u32[i + 1] << 16) | (idx_u32[i + 2] << 8) | idx_u32[i + 3];
        }
    }

    /* 防御：资源头没有总长度字段，这里用"最坏 4 字节/像素 + 余量"的保守流长上界，
     * 拦截损坏索引表带来的任意地址跳转，并给解码循环一个硬性越界停止线。
     * 合法码流必然短于该上界，正常资源不受影响。 */
    uint32_t max_pixels = (uint32_t)img_w * img_h;
    if (max_pixels > ((0xFFFFFFFFUL - 16UL) >> 2))
        return 0U;
    uint32_t stream_max = (max_pixels << 2) + 16UL;
    if (byte_offset >= stream_max)
        return 0U;

    uint32_t jump_pixel_idx = skip_intervals * interval;

    dec->img_w = img_w;
    dec->img_h = img_h;
    dec->ix_start = ix_start;
    dec->iy_start = iy_start;
    dec->clip_x_end = (uint16_t)(ix_start + draw_width);
    dec->clip_y_end = (uint16_t)(iy_start + draw_height);
    dec->base_dest_x = (int16_t)(x0 - p_lcd->pfb_area.x0);
    dec->base_dest_y = (int16_t)(y0 - p_lcd->pfb_y_start);
    dec->dst_stride = p_lcd->pfb_width;
    dec->arry = qoi_start + byte_offset;
    dec->data_end = qoi_start + stream_max;
    dec->cur_x = (uint16_t)(jump_pixel_idx % img_w);
    dec->cur_y = (uint16_t)(jump_pixel_idx / img_w);
    dec->decoded_pixels = jump_pixel_idx;
    dec->max_pixels = max_pixels;
    return 1U;
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
    _we_qoi_dec_t dec;
    uint16_t img_w;
    uint16_t ix_start;
    uint16_t iy_start;
    uint16_t clip_x_end;
    uint16_t clip_y_end;
    int16_t base_dest_x;
    int16_t base_dest_y;
    uint16_t dst_stride;
    const uint8_t *arry;
    const uint8_t *data_end;
    uint16_t cur_x;
    uint16_t cur_y;
    uint32_t decoded_pixels;
    uint32_t max_pixels;

    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0)
        return;

    /* 头部解析 + 索引跳转 + 裁剪参数，与 ARGB8565 共用同一前置逻辑。 */
    if (!_we_qoi_parse_and_seek(p_lcd, x0, y0, img, &dec))
        return;

    img_w = dec.img_w;
    ix_start = dec.ix_start;
    iy_start = dec.iy_start;
    clip_x_end = dec.clip_x_end;
    clip_y_end = dec.clip_y_end;
    base_dest_x = dec.base_dest_x;
    base_dest_y = dec.base_dest_y;
    dst_stride = dec.dst_stride;
    arry = dec.arry;
    data_end = dec.data_end;
    cur_x = dec.cur_x;
    cur_y = dec.cur_y;
    decoded_pixels = dec.decoded_pixels;
    max_pixels = dec.max_pixels;

    uint8_t flag;
    uint8_t r = 0, g = 0, b = 0;
    uint16_t cur_pixel = 0;

    /* opacity 整次绘制为常量，预先分类，省掉每像素混色分支 */
    uint8_t opaque = (uint8_t)(opacity >= 250U);
    /* 行指针缓存：同一行内复用，避免每像素重算 (cur_y*dst_stride) 乘法。
     * 仅在通过裁剪判定后才计算，保证偏移非负（不会出现越界回绕） */
    colour_t *row_dst = 0;
    int32_t row_dst_y = -1;

    /* ---------------- 主解码循环 ----------------
     * arry < data_end 是损坏资源的硬性越界停止线（保守上界，正常码流不受影响）。 */
    while ((decoded_pixels < max_pixels) && (arry < data_end))
    {
        flag = *arry++;

        if ((flag == 0xFF) || (flag == 0xFE))
        {
            uint8_t h = *arry++;
            uint8_t l = *arry++;
            cur_pixel = (h << 8) | l;
            r = h >> 3;
            g = ((h & 0x07) << 3) | (l >> 5);
            b = l & 0x1F;
        }
        else if ((flag & 0xC0) == 0x40)
        {
            r = (r + ((flag >> 4) & 0x03) - 2) & 0x1F;
            g = (g + ((flag >> 2) & 0x03) - 2) & 0x3F;
            b = (b + (flag & 0x03) - 2) & 0x1F;
            cur_pixel = (r << 11) | (g << 5) | b;
        }
        else if ((flag & 0xC0) == 0x80)
        {
            int8_t vg = (flag & 0x3F) - 32;
            uint8_t next_byte = *arry++;
            r = (r + vg - 8 + ((next_byte >> 4) & 0x0F)) & 0x1F;
            g = (g + vg) & 0x3F;
            b = (b + vg - 8 + (next_byte & 0x0F)) & 0x1F;
            cur_pixel = (r << 11) | (g << 5) | b;
        }
        else if ((flag & 0xC0) == 0xC0)
        {
            uint8_t run = (flag & 0x3F) + 1;

            colour_t fg = we_color_from_rgb565(cur_pixel);

            while (run--)
            {
                if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
                {
                    if (row_dst_y != (int32_t)cur_y)
                    {
                        row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                        row_dst_y = (int32_t)cur_y;
                    }
                    if (opaque)
                        we_store_color(row_dst + cur_x, fg);
                    else
                        we_store_blended_color(row_dst + cur_x, fg, opacity);
                }
                decoded_pixels++;
                cur_x++;
                if (cur_x >= img_w)
                {
                    cur_x = 0;
                    cur_y++;
                    if (cur_y >= clip_y_end)
                        return;
                }
            }
            continue;
        }

        if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
        {
            if (row_dst_y != (int32_t)cur_y)
            {
                row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                row_dst_y = (int32_t)cur_y;
            }
            colour_t fg = we_color_from_rgb565(cur_pixel);
            if (opaque)
                we_store_color(row_dst + cur_x, fg);
            else
                we_store_blended_color(row_dst + cur_x, fg, opacity);
        }
        decoded_pixels++;
        cur_x++;
        if (cur_x >= img_w)
        {
            cur_x = 0;
            cur_y++;
            if (cur_y >= clip_y_end)
                return;
        }
    }
}

/* --------------------------------------------------------------------------
 * ARGB8565 索引 QOI 绘制入口
 *
 * 与 RGB565 版本的差异：
 * 1. 像素格式为 24 位：[A(8)][R5G6B5_高(8)][R5G6B5_低(8)]
 * 2. 解码状态额外维护 cur_alpha（当前像素 alpha，0~255）
 * 3. 操作码扩展：
 *    - 0xFF：读 3 字节 [A][H][L]，更新 alpha + RGB565
 *    - 0xFE：读 2 字节 [H][L]，alpha 不变
 *    - 0x40 / 0x80：RGB delta，alpha 不变
 *    - 0xC0：RLE，混色时使用 cur_alpha × opacity / 255
 * -------------------------------------------------------------------------- */
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
    _we_qoi_dec_t dec;
    uint16_t img_w;
    uint16_t ix_start;
    uint16_t iy_start;
    uint16_t clip_x_end;
    uint16_t clip_y_end;
    int16_t base_dest_x;
    int16_t base_dest_y;
    uint16_t dst_stride;
    const uint8_t *arry;
    const uint8_t *data_end;
    uint16_t cur_x;
    uint16_t cur_y;
    uint32_t decoded_pixels;
    uint32_t max_pixels;

    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0)
        return;

    /* 头部解析 + 索引跳转 + 裁剪参数，与 RGB565 共用同一前置逻辑。 */
    if (!_we_qoi_parse_and_seek(p_lcd, x0, y0, img, &dec))
        return;

    img_w = dec.img_w;
    ix_start = dec.ix_start;
    iy_start = dec.iy_start;
    clip_x_end = dec.clip_x_end;
    clip_y_end = dec.clip_y_end;
    base_dest_x = dec.base_dest_x;
    base_dest_y = dec.base_dest_y;
    dst_stride = dec.dst_stride;
    arry = dec.arry;
    data_end = dec.data_end;
    cur_x = dec.cur_x;
    cur_y = dec.cur_y;
    decoded_pixels = dec.decoded_pixels;
    max_pixels = dec.max_pixels;

    uint8_t flag;
    uint8_t r = 0, g = 0, b = 0;
    uint8_t cur_alpha = 255; // ARGB8565 额外维护当前像素 alpha
    uint16_t cur_pixel = 0;

    /* final_alpha = cur_alpha*opacity/255，只在 cur_alpha 变化时重算，
     * 用 we_div255 近似去掉每像素一次软件除法（M0 ~90+ cycle） */
    uint8_t final_alpha = we_div255((uint32_t)cur_alpha * opacity);
    /* 行指针缓存：同一行内复用，避免每像素重算 (cur_y*dst_stride) 乘法。
     * 仅在通过裁剪判定后才计算，保证偏移非负（不会出现越界回绕） */
    colour_t *row_dst = 0;
    int32_t row_dst_y = -1;

    /* ---------------- 主解码循环 ----------------
     * arry < data_end 是损坏资源的硬性越界停止线（保守上界，正常码流不受影响）。 */
    while ((decoded_pixels < max_pixels) && (arry < data_end))
    {
        flag = *arry++;

        if (flag == 0xFF)
        {
            /* 新像素：alpha + RGB565（3 字节） */
            cur_alpha = *arry++;
            final_alpha = we_div255((uint32_t)cur_alpha * opacity);
            uint8_t h = *arry++;
            uint8_t l = *arry++;
            cur_pixel = ((uint16_t)h << 8) | l;
            r = h >> 3;
            g = ((h & 0x07) << 3) | (l >> 5);
            b = l & 0x1F;
        }
        else if (flag == 0xFE)
        {
            /* 新 RGB565，alpha 不变（2 字节） */
            uint8_t h = *arry++;
            uint8_t l = *arry++;
            cur_pixel = ((uint16_t)h << 8) | l;
            r = h >> 3;
            g = ((h & 0x07) << 3) | (l >> 5);
            b = l & 0x1F;
        }
        else if ((flag & 0xC0) == 0x40)
        {
            /* RGB 小差值，alpha 不变 */
            r = (r + ((flag >> 4) & 0x03) - 2) & 0x1F;
            g = (g + ((flag >> 2) & 0x03) - 2) & 0x3F;
            b = (b + (flag & 0x03) - 2) & 0x1F;
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0) == 0x80)
        {
            /* RGB luma 差值，alpha 不变 */
            int8_t vg = (int8_t)(flag & 0x3F) - 32;
            uint8_t next_byte = *arry++;
            r = (r + vg - 8 + ((next_byte >> 4) & 0x0F)) & 0x1F;
            g = (g + vg) & 0x3F;
            b = (b + vg - 8 + (next_byte & 0x0F)) & 0x1F;
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0) == 0xC0)
        {
            /* RLE：重复当前 ARGB 像素，final_alpha 已在变 alpha 时算好 */
            uint8_t run = (flag & 0x3F) + 1;
            colour_t fg = we_color_from_rgb565(cur_pixel);

            while (run--)
            {
                if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
                {
                    if (row_dst_y != (int32_t)cur_y)
                    {
                        row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                        row_dst_y = (int32_t)cur_y;
                    }
                    we_store_blended_color(row_dst + cur_x, fg, final_alpha);
                }
                decoded_pixels++;
                cur_x++;
                if (cur_x >= img_w)
                {
                    cur_x = 0;
                    cur_y++;
                    if (cur_y >= clip_y_end)
                        return;
                }
            }
            continue;
        }

        /* 单像素输出，final_alpha 已在变 alpha 时算好 */
        if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
        {
            if (row_dst_y != (int32_t)cur_y)
            {
                row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                row_dst_y = (int32_t)cur_y;
            }
            colour_t fg = we_color_from_rgb565(cur_pixel);
            we_store_blended_color(row_dst + cur_x, fg, final_alpha);
        }
        decoded_pixels++;
        cur_x++;
        if (cur_x >= img_w)
        {
            cur_x = 0;
            cur_y++;
            if (cur_y >= clip_y_end)
                return;
        }
    }
}
#endif

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

    if ((x > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
        return;

    draw_x_start = (x < p_lcd->pfb_area.x0) ? p_lcd->pfb_area.x0 : x;
    draw_y_start = (y < p_lcd->pfb_y_start) ? p_lcd->pfb_y_start : y;
    draw_x_end = (x1 > p_lcd->pfb_area.x1) ? p_lcd->pfb_area.x1 : x1;
    draw_y_end = (y1 > p_lcd->pfb_y_end) ? p_lcd->pfb_y_end : y1;

    span = (uint16_t)(draw_x_end - draw_x_start + 1);
    dst_stride = p_lcd->pfb_width;
    dst_line =
        p_lcd->pfb_gram + ((draw_y_start - p_lcd->pfb_y_start) * dst_stride) + (draw_x_start - p_lcd->pfb_area.x0);

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
                uint32_t *p32 = (uint32_t *)p_dst;
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
 * @brief 绘制单个抗锯齿圆角（仅处理一个 r×r 角落方块）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param cx 传入：角落外接方块左上角 X 坐标（屏幕绝对坐标）
 * @param cy 传入：角落外接方块左上角 Y 坐标
 * @param r 传入：圆角半径（= 方块边长）
 * @param quadrant 传入：象限标识（WE_MASK_QUADRANT_LT/RT/LB/RB）
 * @param color 传入：填充颜色
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 * @note 仅在 r×r 角落区域执行 quarter-circle 子采样抗锯齿，
 *       直边和中心实心区域由调用方用 we_fill_rect 快速填充。
 */
static void _we_draw_round_corner(we_lcd_t *p_lcd, int16_t cx, int16_t cy, uint16_t r,
                                  uint8_t quadrant, colour_t color, uint8_t opacity)
{
    int16_t cx0 = cx;
    int16_t cy0 = cy;
    int16_t cx1 = (int16_t)(cx + (int16_t)r - 1);
    int16_t cy1 = (int16_t)(cy + (int16_t)r - 1);
    int16_t px;
    int16_t py;
    uint16_t stride;
    colour_t *row;

    /* 先把角落方块裁剪到当前 PFB 切片，避免越界写入。 */
    if (cx0 < (int16_t)p_lcd->pfb_area.x0) cx0 = (int16_t)p_lcd->pfb_area.x0;
    if (cy0 < (int16_t)p_lcd->pfb_y_start) cy0 = (int16_t)p_lcd->pfb_y_start;
    if (cx1 > (int16_t)p_lcd->pfb_area.x1) cx1 = (int16_t)p_lcd->pfb_area.x1;
    if (cy1 > (int16_t)p_lcd->pfb_y_end) cy1 = (int16_t)p_lcd->pfb_y_end;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    stride = p_lcd->pfb_width;
    row = p_lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)p_lcd->pfb_y_start) * stride
        + (uint32_t)(cx0 - (int16_t)p_lcd->pfb_area.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        colour_t *p = row;

        for (px = cx0; px <= cx1; px++, p++)
        {
            uint8_t mask_alpha = we_mask_quarter_circle_alpha(cx, cy, r, quadrant, px, py);
            if (mask_alpha == 0U)
                continue;
            if (mask_alpha >= 250U && opacity >= 250U)
            {
                we_store_color(p, color);
            }
            else
            {
                /* 用 we_div255 近似替换软件除法（M0 上每次 /255 约 90+ cycle）。 */
                uint8_t alpha = we_div255((uint32_t)opacity * mask_alpha);
                we_store_blended_color(p, color, alpha);
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

    x1 = (int16_t)(x + (int16_t)w - 1);
    y1 = (int16_t)(y + (int16_t)h - 1);

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
    _we_draw_round_corner(p_lcd, x, y, r, WE_MASK_QUADRANT_LT, color, opacity);
    _we_draw_round_corner(p_lcd, (int16_t)(x1 - (int16_t)r + 1), y, r, WE_MASK_QUADRANT_RT, color, opacity);
    _we_draw_round_corner(p_lcd, x, (int16_t)(y1 - (int16_t)r + 1), r, WE_MASK_QUADRANT_LB, color, opacity);
    _we_draw_round_corner(p_lcd, (int16_t)(x1 - (int16_t)r + 1), (int16_t)(y1 - (int16_t)r + 1),
                          r, WE_MASK_QUADRANT_RB, color, opacity);
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

    if (px < p_lcd->pfb_area.x0 || px > p_lcd->pfb_area.x1 || py < p_lcd->pfb_y_start || py > p_lcd->pfb_y_end)
        return;

    dst = p_lcd->pfb_gram + (py - p_lcd->pfb_y_start) * p_lcd->pfb_width + (px - p_lcd->pfb_area.x0);
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
 * thickness == 1：标准 Wu，每步 2 像素（上下边缘各按小数分配 alpha）
 * thickness  > 1：Wu + 实心核，每步 T 个实心像素 + 2 个 AA 边缘像素
 *   lo = -(T-1)/2，hi = T/2；实心范围 [yi+lo, yi+hi]（恰好 T 个像素）
 *   AA 上边 yi+lo-1，AA 下边 yi+hi+1
 *
 * @param p_lcd     传入：GUI 屏幕上下文指针
 * @param x0        传入：起点 X
 * @param y0        传入：起点 Y
 * @param x1        传入：终点 X
 * @param y1        传入：终点 Y
 * @param thickness 传入：线宽 (像素)
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
    y_fp = (int32_t)y0 * 256;

    /* T=1：lo=0,hi=0 → AA 上边 yi，AA 下边 yi+1，实心区间为空（标准 Wu）
     * T>1：实心 [yi+lo, yi+hi] 共 T 像素，两侧各 1 个 AA 像素              */
    lo = -(int16_t)((thickness - 1U) / 2U);
    hi = (int16_t)(thickness / 2U);

    for (x = x0; x <= x1; x++)
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

    dx = (int32_t)x1 - x0;
    dy = (int32_t)y1 - y0;
    len2 = dx * dx + dy * dy;
    len = _we_isqrt32((uint32_t)len2);
    inv_len = (len != 0U) ? ((1UL << 16) / len) : 0U;

    r_fp      = (int32_t)width * 128;                     /* 半线宽 width/2 的 Q8 */
    ext       = (int16_t)(width / 2U + 2U);               /* 外扩：半宽 + AA 余量 */
    thr_in    = (int32_t)(width - 1) * (int32_t)(width - 1); /* 端帽实心阈值（4·d²） */
    thr_out   = (int32_t)(width + 1) * (int32_t)(width + 1); /* 端帽圈外阈值（4·d²） */
    band_half = (int32_t)ext * (int32_t)len;              /* 行裁剪：垂距≤R 的带半宽·len */

    minx = (int16_t)(((x0 < x1) ? x0 : x1) - ext);
    maxx = (int16_t)(((x0 > x1) ? x0 : x1) + ext);
    miny = (int16_t)(((y0 < y1) ? y0 : y1) - ext);
    maxy = (int16_t)(((y0 > y1) ? y0 : y1) + ext);

    /* 裁剪到当前 PFB 行带 */
    if (minx < p_lcd->pfb_area.x0) minx = p_lcd->pfb_area.x0;
    if (maxx > p_lcd->pfb_area.x1) maxx = p_lcd->pfb_area.x1;
    if (miny < p_lcd->pfb_y_start) miny = p_lcd->pfb_y_start;
    if (maxy > p_lcd->pfb_y_end)   maxy = p_lcd->pfb_y_end;

    gram = p_lcd->pfb_gram;
    bx   = p_lcd->pfb_area.x0;
    ys   = p_lcd->pfb_y_start;
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
 * @brief  在当前 PFB 中绘制通用 Alpha 位图/蒙版，支持 A1/A2/A4/A8
 * @param  p_lcd     当前 LCD/PFB 上下文
 * @param  x, y      位图左上角屏幕坐标
 * @param  w, h      位图实际宽高
 * @param  src_data  位图像素数据
 * @param  bpp       位深，支持 1/2/4/8
 * @param  fg_color  前景色
 * @param  opacity   全局透明度
 */
void we_draw_alpha_mask(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h, const unsigned char *src_data,
                        uint8_t bpp, colour_t fg_color, uint8_t opacity)
{
    if (w == 0 || h == 0 || src_data == NULL || opacity == 0)
        return;

    // 1. 先算出位图完整物理边界。
    int16_t draw_x_end = x + w - 1;
    int16_t draw_y_end = y + h - 1;

    // 2. 与当前 PFB 切片求交，得到真正要处理的区域。
    int16_t clip_x_start = (x < p_lcd->pfb_area.x0) ? p_lcd->pfb_area.x0 : x;
    int16_t clip_y_start = (y < p_lcd->pfb_y_start) ? p_lcd->pfb_y_start : y;
    int16_t clip_x_end = (draw_x_end > p_lcd->pfb_area.x1) ? p_lcd->pfb_area.x1 : draw_x_end;
    int16_t clip_y_end = (draw_y_end > p_lcd->pfb_y_end) ? p_lcd->pfb_y_end : draw_y_end;

    // 如果与当前 PFB 没有交集，直接返回。
    if (clip_x_start > clip_x_end || clip_y_start > clip_y_end)
        return;

    uint8_t alpha_mask = (1 << bpp) - 1;

    colour_t *gram = (colour_t *)p_lcd->pfb_gram;
    uint16_t pfb_stride = p_lcd->pfb_width;

    // 3. 遍历裁剪后的有效区域。
    for (int16_t py = clip_y_start; py <= clip_y_end; py++)
    {
        colour_t *p_dst = gram + ((py - p_lcd->pfb_y_start) * pfb_stride) + (clip_x_start - p_lcd->pfb_area.x0);
        int16_t mask_y = py - y; // 位图内相对 Y

        for (int16_t px = clip_x_start; px <= clip_x_end; px++)
        {
            int16_t mask_x = px - x; // 位图内相对 X

            // 直接按绝对位偏移取出该像素的 alpha。
            uint32_t pixel_idx = mask_y * w + mask_x;
            uint32_t bit_pos = pixel_idx * bpp;
            uint32_t byte_idx = bit_pos >> 3;
            uint8_t shift = 8 - bpp - (bit_pos & 7);

            uint8_t a_raw = (src_data[byte_idx] >> shift) & alpha_mask;

            if (a_raw > 0)
            {
                // 把不同位深的 alpha 统一量化到 0~255。
                uint32_t alpha = 0;
                if (bpp == 8)
                    alpha = a_raw;
                else if (bpp == 4)
                    alpha = (a_raw << 4) | a_raw;
                else if (bpp == 2)
                    alpha = a_raw * 85;
                else if (bpp == 1)
                    alpha = 255;

                if (opacity != 255)
                    alpha = (alpha * opacity) >> 8;

                we_store_blended_color(p_dst, fg_color, (uint8_t)alpha);
            }
            p_dst++; // 目标指针前进 1 像素
        }
    }
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
    uint32_t total_pixels = (uint32_t)p_lcd->pfb_width * (p_lcd->pfb_y_end - p_lcd->pfb_y_start + 1);

    uint32_t blocks, rem, i;

#if (LCD_DEEP == DEEP_OLED)
#error ("Not support DEEP_OLED yet!")
#elif (LCD_DEEP == DEEP_GRAY8)
#error ("Not support DEEP_GRAY8 yet!")
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
        uint32_t *gram32 = (uint32_t *)gram16;
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
