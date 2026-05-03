
#include "we_widget_img_flash.h"
#include "we_render.h"
#include <string.h>

typedef struct
{
    we_storage_read_cb_t cb;
    uint32_t             base;       /* QOI 数据段在 flash 中的绝对起始地址 */
    uint32_t             cur_pos;    /* 当前读取位置（相对 base 的字节偏移） */
    uint8_t              buf[WE_FLASH_IMG_CHUNK];
    uint16_t             buf_pos;    /* buf 中当前游标 */
    uint16_t             buf_valid;  /* buf 中有效字节数 */
} flash_stream_t;

/**
 * @brief 初始化 Flash 字节流读取器。
 * @param s 流对象。
 * @param cb 外部存储读取回调。
 * @param base 数据段在 Flash 中的基地址。
 */
static void flash_stream_init(flash_stream_t *s, we_storage_read_cb_t cb, uint32_t base)
{
    s->cb        = cb;
    s->base      = base;
    s->cur_pos   = 0U;
    s->buf_pos   = 0U;
    s->buf_valid = 0U;
}

/**
 * @brief 将流定位到指定字节偏移并使内部缓冲失效。
 * @param s 流对象。
 * @param offset 相对 base 的目标偏移。
 */
static void flash_stream_seek(flash_stream_t *s, uint32_t offset)
{
    s->cur_pos   = offset;
    s->buf_pos   = 0U;
    s->buf_valid = 0U;   /* 使缓冲区失效，下次 get 时重新加载 */
}

/**
 * @brief 从 Flash 流读取下一个字节。
 * @param s 流对象。
 * @return 当前读取到的字节值。
 */
static uint8_t flash_stream_get(flash_stream_t *s)
{
    if (s->buf_pos >= s->buf_valid)
    {
        s->cb(s->base + s->cur_pos, s->buf, WE_FLASH_IMG_CHUNK);
        s->buf_pos   = 0U;
        s->buf_valid = WE_FLASH_IMG_CHUNK;
    }
    s->cur_pos++;
    return s->buf[s->buf_pos++];
}

/* --------------------------------------------------------------------------
 * IMG_RGB565 Flash 渲染器（无压缩）
 *
 * 逐行直接寻址：每行可见像素地址 = flash_addr + 6 + (y * img_w + ix_start) * 2
 * 每次读取不超过 WE_FLASH_IMG_CHUNK 字节，大端拼色后写入 PFB。
 * -------------------------------------------------------------------------- */

/**
 * @brief 渲染未压缩 RGB565 Flash 图片到当前 PFB 切片。
 * @param p_lcd 当前 LCD 上下文。
 * @param obj 图片控件实例。
 * @param opacity 图片整体透明度（0~255）。
 */
static void _flash_render_rgb565(we_lcd_t *p_lcd,
                                  we_flash_img_obj_t *obj,
                                  uint8_t opacity)
{
    int16_t  x0    = obj->base.x;
    int16_t  y0    = obj->base.y;
    uint16_t img_w = (uint16_t)obj->base.w;
    uint16_t img_h = (uint16_t)obj->base.h;
    int16_t  x1    = x0 + (int16_t)img_w - 1;
    int16_t  y1    = y0 + (int16_t)img_h - 1;

    if ((x0 > (int16_t)p_lcd->pfb_area.x1) || (x1 < (int16_t)p_lcd->pfb_area.x0) ||
        (y0 > (int16_t)p_lcd->pfb_y_end)   || (y1 < (int16_t)p_lcd->pfb_y_start))
    {
        return;
    }

    uint16_t ix_start = (x0 < (int16_t)p_lcd->pfb_area.x0) ?
                        (uint16_t)((int16_t)p_lcd->pfb_area.x0 - x0) : 0U;
    uint16_t iy_start = (y0 < (int16_t)p_lcd->pfb_y_start) ?
                        (uint16_t)((int16_t)p_lcd->pfb_y_start - y0) : 0U;

    uint16_t draw_w = img_w - ix_start -
                      ((x1 > (int16_t)p_lcd->pfb_area.x1) ?
                       (uint16_t)(x1 - (int16_t)p_lcd->pfb_area.x1) : 0U);
    uint16_t draw_h = img_h - iy_start -
                      ((y1 > (int16_t)p_lcd->pfb_y_end) ?
                       (uint16_t)(y1 - (int16_t)p_lcd->pfb_y_end) : 0U);

    uint16_t clip_y_end  = iy_start + draw_h;
    int16_t  base_dest_x = x0 - (int16_t)p_lcd->pfb_area.x0;
    int16_t  base_dest_y = y0 - (int16_t)p_lcd->pfb_y_start;
    uint16_t dst_stride  = p_lcd->pfb_width;

    /* 每次最多读 WE_FLASH_IMG_CHUNK/2 个像素 */
    uint16_t pix_per_chunk = WE_FLASH_IMG_CHUNK / 2U;
    uint8_t  chunk[WE_FLASH_IMG_CHUNK];

    for (uint16_t cur_y = iy_start; cur_y < clip_y_end; cur_y++)
    {
        uint32_t row_addr = obj->flash_addr + 6U +
                            ((uint32_t)cur_y * img_w + ix_start) * 2U;
        uint16_t pix_left = draw_w;
        uint16_t dst_x    = 0U;

        while (pix_left > 0U)
        {
            uint16_t n = (pix_left < pix_per_chunk) ? pix_left : pix_per_chunk;
            p_lcd->storage_read_cb(row_addr, chunk, (uint32_t)n * 2U);

            for (uint16_t i = 0U; i < n; i++)
            {
                uint16_t raw = ((uint16_t)chunk[i * 2U] << 8) | chunk[i * 2U + 1U];
colour_t fg  = we_color_from_rgb565(raw);
                colour_t *dst = p_lcd->pfb_gram +
                                ((base_dest_y + cur_y) * dst_stride) +
                                (base_dest_x + ix_start + dst_x + i);
we_store_blended_color(dst, fg, opacity);
            }

            dst_x    += n;
            row_addr += (uint32_t)n * 2U;
            pix_left -= n;
        }
    }
}

/* --------------------------------------------------------------------------
 * IMG_RGB565_INDEXQOI Flash 渲染器
 *
 * 与 we_img_render_indexed_qoi_rgb565 逻辑完全相同，
 * 差别仅在于：数据来自 flash_stream，而非内存指针。
 * -------------------------------------------------------------------------- */

/**
 * @brief 渲染 RGB565_INDEXQOI 格式 Flash 图片到当前 PFB 切片。
 * @param p_lcd 当前 LCD 上下文。
 * @param obj 图片控件实例。
 * @param opacity 图片整体透明度（0~255）。
 */
static void _flash_render_indexed_qoi_rgb565(we_lcd_t *p_lcd,
                                              we_flash_img_obj_t *obj,
                                              uint8_t opacity)
{
    /* 每次绘制时读取 13 字节内层头，省去结构体缓存字段 */
    uint8_t  inner[13];
    p_lcd->storage_read_cb(obj->flash_addr + 6U, inner, 13U);
    uint8_t  head_size = inner[0];
    uint16_t interval  = ((uint16_t)inner[5]  << 8) | inner[6];
    uint16_t u16_size  = ((uint16_t)inner[7]  << 8) | inner[8];
    uint16_t u24_size  = ((uint16_t)inner[9]  << 8) | inner[10];
    uint16_t u32_size  = ((uint16_t)inner[11] << 8) | inner[12];
    uint32_t idx_off   = head_size;
    uint32_t dat_off   = (uint32_t)head_size + u16_size + u24_size + u32_size;

    int16_t  x0    = obj->base.x;
    int16_t  y0    = obj->base.y;
    uint16_t img_w = (uint16_t)obj->base.w;
    uint16_t img_h = (uint16_t)obj->base.h;
    int16_t  x1    = x0 + (int16_t)img_w - 1;
    int16_t  y1    = y0 + (int16_t)img_h - 1;

    /* 与当前 PFB 切片的可见性检查 */
    if ((x0 > (int16_t)p_lcd->pfb_area.x1) || (x1 < (int16_t)p_lcd->pfb_area.x0) ||
        (y0 > (int16_t)p_lcd->pfb_y_end)   || (y1 < (int16_t)p_lcd->pfb_y_start))
    {
        return;
    }

    uint16_t ix_start = (x0 < (int16_t)p_lcd->pfb_area.x0) ?
                        (uint16_t)((int16_t)p_lcd->pfb_area.x0 - x0) : 0U;
    uint16_t iy_start = (y0 < (int16_t)p_lcd->pfb_y_start) ?
                        (uint16_t)((int16_t)p_lcd->pfb_y_start - y0) : 0U;

    uint16_t draw_w = img_w - ix_start -
                      ((x1 > (int16_t)p_lcd->pfb_area.x1) ?
                       (uint16_t)(x1 - (int16_t)p_lcd->pfb_area.x1) : 0U);
    uint16_t draw_h = img_h - iy_start -
                      ((y1 > (int16_t)p_lcd->pfb_y_end) ?
                       (uint16_t)(y1 - (int16_t)p_lcd->pfb_y_end) : 0U);

    uint16_t clip_x_end  = ix_start + draw_w;
    uint16_t clip_y_end  = iy_start + draw_h;
    int16_t  base_dest_x = x0 - (int16_t)p_lcd->pfb_area.x0;
    int16_t  base_dest_y = y0 - (int16_t)p_lcd->pfb_y_start;
    uint16_t dst_stride  = p_lcd->pfb_width;

    /* ---- 读索引表中的单条条目，得到 byte_offset ---- */
    uint32_t first_needed   = (uint32_t)iy_start * img_w + ix_start;
    uint32_t skip_intervals = (interval > 0U) ?
                              (first_needed / interval) : 0U;
    uint32_t byte_offset    = 0U;

    if (skip_intervals > 0U)
    {
        uint32_t num_u16 = u16_size / 2U;
        uint32_t num_u24 = u24_size / 3U;
        uint32_t num_u32 = u32_size / 4U;
        uint32_t total   = num_u16 + num_u24 + num_u32;
        uint32_t ti      = skip_intervals;
        uint8_t  idx_buf[4];

        if (ti >= total)
        {
            ti = (total > 0U) ? (total - 1U) : 0U;
            skip_intervals = ti;
        }

        if (ti < num_u16)
        {
            uint32_t addr = obj->flash_addr + 6U + idx_off + ti * 2U;
            p_lcd->storage_read_cb(addr, idx_buf, 2U);
            byte_offset = ((uint32_t)idx_buf[0] << 8) | idx_buf[1];
        }
        else if (ti < num_u16 + num_u24)
        {
            uint32_t i    = ti - num_u16;
            uint32_t addr = obj->flash_addr + 6U + idx_off + u16_size + i * 3U;
            p_lcd->storage_read_cb(addr, idx_buf, 3U);
            byte_offset = ((uint32_t)idx_buf[0] << 16) |
                          ((uint32_t)idx_buf[1] << 8)  | idx_buf[2];
        }
        else
        {
            uint32_t i    = ti - num_u16 - num_u24;
            uint32_t addr = obj->flash_addr + 6U + idx_off +
                            u16_size + u24_size + i * 4U;
            p_lcd->storage_read_cb(addr, idx_buf, 4U);
            byte_offset = ((uint32_t)idx_buf[0] << 24) |
                          ((uint32_t)idx_buf[1] << 16) |
                          ((uint32_t)idx_buf[2] << 8)  | idx_buf[3];
        }
    }

    uint32_t jump_pixel = skip_intervals * interval;
    uint16_t cur_x      = (uint16_t)(jump_pixel % img_w);
    uint16_t cur_y      = (uint16_t)(jump_pixel / img_w);

    /* ---- 初始化流，定位到解码起点 ---- */
    flash_stream_t stream;
    flash_stream_init(&stream, p_lcd->storage_read_cb,
                      obj->flash_addr + 6U + dat_off);
flash_stream_seek(&stream, byte_offset);

    uint8_t  flag;
    uint8_t  r = 0, g = 0, b = 0;
    uint16_t cur_pixel   = 0;
    uint32_t max_pixels  = (uint32_t)img_w * img_h;
    uint32_t decoded     = jump_pixel;

    /* ---- 主解码循环（与 RAM 版本完全相同，只是读字节换成 flash_stream_get） ---- */
    while (decoded < max_pixels)
    {
flag = flash_stream_get(&stream);

        if ((flag == 0xFFU) || (flag == 0xFEU))
        {
uint8_t h = flash_stream_get(&stream);
uint8_t l = flash_stream_get(&stream);
            cur_pixel = ((uint16_t)h << 8) | l;
            r = h >> 3;
            g = (uint8_t)(((h & 0x07U) << 3) | (l >> 5));
            b = l & 0x1FU;
        }
        else if ((flag & 0xC0U) == 0x40U)
        {
            r = (uint8_t)((r + ((flag >> 4) & 0x03U) - 2U) & 0x1FU);
            g = (uint8_t)((g + ((flag >> 2) & 0x03U) - 2U) & 0x3FU);
            b = (uint8_t)((b + ( flag       & 0x03U) - 2U) & 0x1FU);
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0U) == 0x80U)
        {
            int8_t  vg   = (int8_t)((flag & 0x3FU) - 32U);
uint8_t nb   = flash_stream_get(&stream);
            r = (uint8_t)((r + vg - 8 + ((nb >> 4) & 0x0FU)) & 0x1FU);
            g = (uint8_t)((g + vg)                            & 0x3FU);
            b = (uint8_t)((b + vg - 8 + (nb & 0x0FU))        & 0x1FU);
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0U) == 0xC0U)
        {
            uint8_t run = (flag & 0x3FU) + 1U;
colour_t fg = we_color_from_rgb565(cur_pixel);

            while (run--)
            {
                if (cur_y >= iy_start)
                {
                    if (cur_x >= ix_start && cur_x < clip_x_end)
                    {
                        colour_t *dst = p_lcd->pfb_gram +
                                        ((base_dest_y + cur_y) * dst_stride) +
                                        (base_dest_x + cur_x);
we_store_blended_color(dst, fg, opacity);
                    }
                }
                decoded++;
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

        if (cur_y >= iy_start)
        {
            if (cur_x >= ix_start && cur_x < clip_x_end)
            {
                colour_t *dst = p_lcd->pfb_gram +
                                ((base_dest_y + cur_y) * dst_stride) +
                                (base_dest_x + cur_x);
colour_t fg = we_color_from_rgb565(cur_pixel);
we_store_blended_color(dst, fg, opacity);
            }
        }
        decoded++;
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
 * IMG_ARGB8565_INDEXQOI Flash 渲染器
 *
 * 与 RGB565 版本的差异：
 *   0xFF → 读 3 字节 [A][H][L]，更新 cur_alpha + RGB565
 *   0xFE → 读 2 字节 [H][L]，alpha 不变
 *   输出时 final_alpha = cur_alpha × opacity / 255
 *
 * 前提：取模工具须保证每个 index entry point 处的第一个 opcode 为 0xFF，
 * 使解码器 seek 后能立即重建完整状态（包括 cur_alpha）。
 * -------------------------------------------------------------------------- */
#if (WE_CFG_ENABLE_INDEXED_QOI == 1)

/**
 * @brief 渲染 ARGB8565_INDEXQOI 格式 Flash 图片到当前 PFB 切片。
 * @param p_lcd 当前 LCD 上下文。
 * @param obj 图片控件实例。
 * @param opacity 图片整体透明度（0~255）。
 */
static void _flash_render_indexed_qoi_argb8565(we_lcd_t *p_lcd,
                                               we_flash_img_obj_t *obj,
                                               uint8_t opacity)
{
    uint8_t  inner[13];
    p_lcd->storage_read_cb(obj->flash_addr + 6U, inner, 13U);
    uint8_t  head_size = inner[0];
    uint16_t interval  = ((uint16_t)inner[5]  << 8) | inner[6];
    uint16_t u16_size  = ((uint16_t)inner[7]  << 8) | inner[8];
    uint16_t u24_size  = ((uint16_t)inner[9]  << 8) | inner[10];
    uint16_t u32_size  = ((uint16_t)inner[11] << 8) | inner[12];
    uint32_t idx_off   = head_size;
    uint32_t dat_off   = (uint32_t)head_size + u16_size + u24_size + u32_size;

    int16_t  x0    = obj->base.x;
    int16_t  y0    = obj->base.y;
    uint16_t img_w = (uint16_t)obj->base.w;
    uint16_t img_h = (uint16_t)obj->base.h;
    int16_t  x1    = x0 + (int16_t)img_w - 1;
    int16_t  y1    = y0 + (int16_t)img_h - 1;

    if ((x0 > (int16_t)p_lcd->pfb_area.x1) || (x1 < (int16_t)p_lcd->pfb_area.x0) ||
        (y0 > (int16_t)p_lcd->pfb_y_end)   || (y1 < (int16_t)p_lcd->pfb_y_start))
    {
        return;
    }

    uint16_t ix_start = (x0 < (int16_t)p_lcd->pfb_area.x0) ?
                        (uint16_t)((int16_t)p_lcd->pfb_area.x0 - x0) : 0U;
    uint16_t iy_start = (y0 < (int16_t)p_lcd->pfb_y_start) ?
                        (uint16_t)((int16_t)p_lcd->pfb_y_start - y0) : 0U;

    uint16_t draw_w = img_w - ix_start -
                      ((x1 > (int16_t)p_lcd->pfb_area.x1) ?
                       (uint16_t)(x1 - (int16_t)p_lcd->pfb_area.x1) : 0U);
    uint16_t draw_h = img_h - iy_start -
                      ((y1 > (int16_t)p_lcd->pfb_y_end) ?
                       (uint16_t)(y1 - (int16_t)p_lcd->pfb_y_end) : 0U);

    uint16_t clip_x_end  = ix_start + draw_w;
    uint16_t clip_y_end  = iy_start + draw_h;
    int16_t  base_dest_x = x0 - (int16_t)p_lcd->pfb_area.x0;
    int16_t  base_dest_y = y0 - (int16_t)p_lcd->pfb_y_start;
    uint16_t dst_stride  = p_lcd->pfb_width;

    uint32_t first_needed   = (uint32_t)iy_start * img_w + ix_start;
    uint32_t skip_intervals = (interval > 0U) ? (first_needed / interval) : 0U;
    uint32_t byte_offset    = 0U;

    if (skip_intervals > 0U)
    {
        uint32_t num_u16 = u16_size / 2U;
        uint32_t num_u24 = u24_size / 3U;
        uint32_t num_u32 = u32_size / 4U;
        uint32_t total   = num_u16 + num_u24 + num_u32;
        uint32_t ti      = skip_intervals;
        uint8_t  idx_buf[4];

        if (ti >= total)
        {
            ti = (total > 0U) ? (total - 1U) : 0U;
            skip_intervals = ti;
        }

        if (ti < num_u16)
        {
            uint32_t addr = obj->flash_addr + 6U + idx_off + ti * 2U;
            p_lcd->storage_read_cb(addr, idx_buf, 2U);
            byte_offset = ((uint32_t)idx_buf[0] << 8) | idx_buf[1];
        }
        else if (ti < num_u16 + num_u24)
        {
            uint32_t i    = ti - num_u16;
            uint32_t addr = obj->flash_addr + 6U + idx_off + u16_size + i * 3U;
            p_lcd->storage_read_cb(addr, idx_buf, 3U);
            byte_offset = ((uint32_t)idx_buf[0] << 16) |
                          ((uint32_t)idx_buf[1] << 8)  | idx_buf[2];
        }
        else
        {
            uint32_t i    = ti - num_u16 - num_u24;
            uint32_t addr = obj->flash_addr + 6U + idx_off +
                            u16_size + u24_size + i * 4U;
            p_lcd->storage_read_cb(addr, idx_buf, 4U);
            byte_offset = ((uint32_t)idx_buf[0] << 24) |
                          ((uint32_t)idx_buf[1] << 16) |
                          ((uint32_t)idx_buf[2] << 8)  | idx_buf[3];
        }
    }

    uint32_t jump_pixel = skip_intervals * interval;
    uint16_t cur_x      = (uint16_t)(jump_pixel % img_w);
    uint16_t cur_y      = (uint16_t)(jump_pixel / img_w);

    flash_stream_t stream;
    flash_stream_init(&stream, p_lcd->storage_read_cb,
                      obj->flash_addr + 6U + dat_off);
flash_stream_seek(&stream, byte_offset);

    uint8_t  flag;
    uint8_t  r = 0, g = 0, b = 0;
    uint8_t  cur_alpha  = 255U;
    uint16_t cur_pixel  = 0U;
    uint32_t max_pixels = (uint32_t)img_w * img_h;
    uint32_t decoded    = jump_pixel;

    while (decoded < max_pixels)
    {
flag = flash_stream_get(&stream);

        if (flag == 0xFFU)
        {
            /* 新像素：alpha + RGB565（3 字节） */
cur_alpha = flash_stream_get(&stream);
uint8_t h = flash_stream_get(&stream);
uint8_t l = flash_stream_get(&stream);
            cur_pixel = ((uint16_t)h << 8) | l;
            r = h >> 3;
            g = (uint8_t)(((h & 0x07U) << 3) | (l >> 5));
            b = l & 0x1FU;
        }
        else if (flag == 0xFEU)
        {
            /* 新 RGB565，alpha 不变（2 字节） */
uint8_t h = flash_stream_get(&stream);
uint8_t l = flash_stream_get(&stream);
            cur_pixel = ((uint16_t)h << 8) | l;
            r = h >> 3;
            g = (uint8_t)(((h & 0x07U) << 3) | (l >> 5));
            b = l & 0x1FU;
        }
        else if ((flag & 0xC0U) == 0x40U)
        {
            r = (uint8_t)((r + ((flag >> 4) & 0x03U) - 2U) & 0x1FU);
            g = (uint8_t)((g + ((flag >> 2) & 0x03U) - 2U) & 0x3FU);
            b = (uint8_t)((b + ( flag       & 0x03U) - 2U) & 0x1FU);
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0U) == 0x80U)
        {
            int8_t  vg = (int8_t)((flag & 0x3FU) - 32U);
uint8_t nb = flash_stream_get(&stream);
            r = (uint8_t)((r + vg - 8 + ((nb >> 4) & 0x0FU)) & 0x1FU);
            g = (uint8_t)((g + vg)                            & 0x3FU);
            b = (uint8_t)((b + vg - 8 + (nb & 0x0FU))        & 0x1FU);
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0U) == 0xC0U)
        {
            uint8_t  run         = (flag & 0x3FU) + 1U;
            uint8_t  final_alpha = (uint8_t)(((uint16_t)cur_alpha * opacity) / 255U);
colour_t fg          = we_color_from_rgb565(cur_pixel);

            while (run--)
            {
                if (cur_y >= iy_start)
                {
                    if (cur_x >= ix_start && cur_x < clip_x_end)
                    {
                        colour_t *dst = p_lcd->pfb_gram +
                                        ((base_dest_y + cur_y) * dst_stride) +
                                        (base_dest_x + cur_x);
we_store_blended_color(dst, fg, final_alpha);
                    }
                }
                decoded++;
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

        if (cur_y >= iy_start)
        {
            if (cur_x >= ix_start && cur_x < clip_x_end)
            {
                uint8_t  final_alpha = (uint8_t)(((uint16_t)cur_alpha * opacity) / 255U);
                colour_t *dst = p_lcd->pfb_gram +
                                ((base_dest_y + cur_y) * dst_stride) +
                                (base_dest_x + cur_x);
colour_t fg = we_color_from_rgb565(cur_pixel);
we_store_blended_color(dst, fg, final_alpha);
            }
        }
        decoded++;
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
#endif /* WE_CFG_ENABLE_INDEXED_QOI */

/* --------------------------------------------------------------------------
 * 控件绘制回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 图片控件绘制回调：按资源格式选择对应 Flash 渲染路径。
 * @param ptr 回调透传的 we_flash_img_obj_t 指针。
 */
static void _flash_img_draw_cb(void *ptr)
{
    we_flash_img_obj_t *obj = (we_flash_img_obj_t *)ptr;

    if (obj == NULL || obj->opacity == 0 || obj->base.lcd == NULL ||
        obj->base.lcd->storage_read_cb == NULL)
    {
        return;
    }

    switch (obj->fmt)
    {
    case IMG_RGB565:
_flash_render_rgb565(obj->base.lcd, obj, obj->opacity);
        break;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    case IMG_RGB565_INDEXQOI:
_flash_render_indexed_qoi_rgb565(obj->base.lcd, obj, obj->opacity);
        break;

    case IMG_ARGB8565_INDEXQOI:
_flash_render_indexed_qoi_argb8565(obj->base.lcd, obj, obj->opacity);
        break;
#endif

    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化外部 Flash 图片控件并加入 LCD 对象链表。
 * @param obj 待初始化的图片控件实例。
 * @param lcd 当前 LCD 上下文，需提供 storage_read_cb。
 * @param x 图片左上角 X 坐标。
 * @param y 图片左上角 Y 坐标。
 * @param flash_addr 图片资源头在外部 Flash 的地址。
 * @param opacity 图片整体透明度（0~255）。
 * @return 1 表示初始化成功，0 表示参数或资源头不合法。
 */
uint8_t we_flash_img_obj_init(we_flash_img_obj_t *obj, we_lcd_t *lcd,
                               int16_t x, int16_t y,
                               uint32_t flash_addr, uint8_t opacity)
{
    static const we_class_t _flash_img_class = {.draw_cb = _flash_img_draw_cb, .event_cb = NULL};

    uint8_t hdr[6]; /* 资源头：[res_type][format][w_H][w_L][h_H][h_L] */

    if (obj == NULL || lcd == NULL || lcd->storage_read_cb == NULL)
    {
        return 0U;
    }

    lcd->storage_read_cb(flash_addr, hdr, sizeof(hdr));

    if (hdr[0] != (uint8_t)FILE_TYPE_IMG)
    {
        return 0U;
    }

    obj->fmt = (imgarry_type_t)hdr[1];

    /* 仅接受已支持的格式 */
    if (obj->fmt != IMG_RGB565
#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
        && obj->fmt != IMG_RGB565_INDEXQOI
        && obj->fmt != IMG_ARGB8565_INDEXQOI
#endif
       )
    {
        return 0U;
    }

    /* 填充 we_obj_t 基类字段；w/h 复用存储图片宽高 */
    obj->flash_addr    = flash_addr;
    obj->opacity       = opacity;
    obj->base.lcd      = lcd;
    obj->base.x        = x;
    obj->base.y        = y;
    obj->base.w        = (int16_t)(((uint16_t)hdr[2] << 8) | hdr[3]);
    obj->base.h        = (int16_t)(((uint16_t)hdr[4] << 8) | hdr[5]);
    obj->base.class_p  = &_flash_img_class;
    obj->base.next     = NULL;
    obj->base.parent   = NULL;

    /* 挂入 LCD 控件链表 */
    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = lcd->obj_list_head;
        while (tail->next != NULL)
        {
            tail = tail->next;
        }
        tail->next = (we_obj_t *)obj;
    }

    if (opacity > 0U)
    {
we_obj_invalidate((we_obj_t *)obj);
    }

    return 1U;
}

/**
 * @brief 设置图片控件透明度并触发重绘。
 * @param obj 图片控件实例。
 * @param opacity 新透明度（0~255）。
 */
void we_flash_img_obj_set_opacity(we_flash_img_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    obj->opacity = opacity;
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置图片控件位置（封装 we_obj_set_pos）。
 * @param obj 图片控件实例。
 * @param x 新的左上角 X 坐标。
 * @param y 新的左上角 Y 坐标。
 */
void we_flash_img_obj_set_pos(we_flash_img_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL)
        return;
we_obj_set_pos((we_obj_t *)obj, x, y);
}
