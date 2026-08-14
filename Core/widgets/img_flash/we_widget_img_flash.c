
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
 * 与 we_img_render_indexed_qoi_rgb565 逻辑完全相同（V3 行索引 + 行去重），
 * 差别仅在于：数据来自 flash_stream，而非内存指针。
 * 每个可见行：读一条行索引（2/4 字节）→ seek 流 → 解码一行，
 * 行内越过裁剪右缘立即停止；去重行回指旧偏移对 flash 只是普通随机读。
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
    /* 每次绘制时读取 14 字节 V3 索引头，省去结构体缓存字段 */
    uint8_t  inner[14];
    p_lcd->storage_read_cb(obj->flash_addr + 6U, inner, 14U);
    uint8_t  head_size = inner[0];
    uint16_t u16_rows  = ((uint16_t)inner[5] << 8) | inner[6];
    uint8_t  pal_count = inner[13];

    /* V3 索引头固定 14 字节，byte0=0x0F 兼作版本标识（V2 的 0x0E、V1 的
     * 0x0D 均不支持）；调色盘条目数上限 64，超出视为损坏流。 */
    if (head_size != 0x0FU || pal_count > 64U)
        return;

    int16_t  x0    = obj->base.x;
    int16_t  y0    = obj->base.y;
    uint16_t img_w = (uint16_t)obj->base.w;
    uint16_t img_h = (uint16_t)obj->base.h;
    int16_t  x1    = x0 + (int16_t)img_w - 1;
    int16_t  y1    = y0 + (int16_t)img_h - 1;

    /* m16 不可能超过行数，超出视为损坏流 */
    if (u16_rows > img_h)
        return;

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

    /* ---- payload 各区偏移（相对 6 字节通用头之后） ---- */
    uint32_t idx_off = 14U;
    uint32_t u32_off = idx_off + (uint32_t)u16_rows * 2U;
    uint32_t pal_off = u32_off + ((uint32_t)(img_h - u16_rows)) * 4U;
    uint32_t dat_off = pal_off + (uint32_t)pal_count * 2U;

    /* ---- 静态调色盘一次读入栈缓存（≤64 项 × 2 字节 = 128B）：
     * 命中调色盘 op 时直接查表，避免每次命中都发起一笔外挂读 ---- */
    uint8_t pal[128];
    if (pal_count > 0U)
        p_lcd->storage_read_cb(obj->flash_addr + 6U + pal_off, pal, (uint32_t)pal_count * 2U);

    /* 防御：用"最坏 4 字节/像素 + 余量"的保守流长上界拦截损坏索引表的
     * 任意偏移跳转，并给解码循环一个硬性停止线（合法码流必然短于上界）。 */
    uint32_t max_pixels = (uint32_t)img_w * img_h;
    uint32_t stream_max = (max_pixels << 2) + 16U;

    flash_stream_t stream;
    flash_stream_init(&stream, p_lcd->storage_read_cb,
                      obj->flash_addr + 6U + dat_off);

    /* ---- 行循环：逐可见行经行索引空降解码（RUN 不跨行、行自包含） ---- */
    for (uint16_t cur_y = iy_start; cur_y < clip_y_end; cur_y++)
    {
        uint8_t  idx_buf[4];
        uint32_t row_off;

        if (cur_y < u16_rows)
        {
            p_lcd->storage_read_cb(obj->flash_addr + 6U + idx_off + (uint32_t)cur_y * 2U, idx_buf, 2U);
            row_off = ((uint32_t)idx_buf[0] << 8) | idx_buf[1];
        }
        else
        {
            p_lcd->storage_read_cb(obj->flash_addr + 6U + u32_off +
                                   ((uint32_t)(cur_y - u16_rows)) * 4U, idx_buf, 4U);
            row_off = ((uint32_t)idx_buf[0] << 24) | ((uint32_t)idx_buf[1] << 16) |
                      ((uint32_t)idx_buf[2] << 8)  | idx_buf[3];
        }
        if (row_off >= stream_max)
            return; /* 损坏索引表 */

flash_stream_seek(&stream, row_off);

        colour_t *row_dst = p_lcd->pfb_gram +
                            ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
        uint16_t cur_x = 0U;
        uint8_t  flag;
        uint8_t  r = 0, g = 0, b = 0;
        uint16_t cur_pixel = 0;

        while ((cur_x < clip_x_end) && (stream.cur_pos < stream_max))
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
            else if (flag < 0x40U)
            {
                /* 0x00..0x3F：静态调色盘 op，查栈缓存的调色盘 */
                uint8_t h;
                uint8_t l;

                if (flag >= pal_count)
                    return; /* 超出条目数视为损坏流 */
                h = pal[(uint16_t)flag << 1];
                l = pal[((uint16_t)flag << 1) + 1U];
                cur_pixel = ((uint16_t)h << 8) | l;
                r = h >> 3;
                g = (uint8_t)(((h & 0x07U) << 3) | (l >> 5));
                b = l & 0x1FU;
            }
            else /* 0xC0..0xFD：RUN（不跨行） */
            {
                uint8_t run = (flag & 0x3FU) + 1U;
colour_t fg = we_color_from_rgb565(cur_pixel);

                while (run--)
                {
                    if (cur_x >= ix_start && cur_x < clip_x_end)
                    {
we_store_blended_color(row_dst + cur_x, fg, opacity);
                    }
                    cur_x++;
                    if (cur_x >= clip_x_end)
                        break; /* 行内右缘截断：本行剩余像素不可见 */
                }
                continue;
            }

            if (cur_x >= ix_start && cur_x < clip_x_end)
            {
colour_t fg = we_color_from_rgb565(cur_pixel);
we_store_blended_color(row_dst + cur_x, fg, opacity);
            }
            cur_x++;
        }
    }
}

/* --------------------------------------------------------------------------
 * IMG_ARGB8565_INDEXQOI Flash 渲染器
 *
 * 与 RGB565 版本的差异：
 *   0x00..0x3F → 静态调色盘 op，条目 3 字节 [A][H][L]（含 Alpha）
 *   0xFF → 读 3 字节 [A][H][L]，更新 cur_alpha + RGB565
 *   0xFE → 读 2 字节 [H][L]，alpha 不变
 *   输出时 final_alpha = cur_alpha × opacity / 255
 *
 * V3 行首 op 只会是调色盘 op 或 0xFF 原始全量，两者都不依赖上文，
 * 每行 seek 后即自包含重建完整状态（包括 cur_alpha；调色盘全局有效）。
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
    /* 每次绘制时读取 14 字节 V3 索引头，省去结构体缓存字段 */
    uint8_t  inner[14];
    p_lcd->storage_read_cb(obj->flash_addr + 6U, inner, 14U);
    uint8_t  head_size = inner[0];
    uint16_t u16_rows  = ((uint16_t)inner[5] << 8) | inner[6];
    uint8_t  pal_count = inner[13];

    /* V3 索引头固定 14 字节，byte0=0x0F 兼作版本标识（V2 的 0x0E、V1 的
     * 0x0D 均不支持）；调色盘条目数上限 64，超出视为损坏流。 */
    if (head_size != 0x0FU || pal_count > 64U)
        return;

    int16_t  x0    = obj->base.x;
    int16_t  y0    = obj->base.y;
    uint16_t img_w = (uint16_t)obj->base.w;
    uint16_t img_h = (uint16_t)obj->base.h;
    int16_t  x1    = x0 + (int16_t)img_w - 1;
    int16_t  y1    = y0 + (int16_t)img_h - 1;

    /* m16 不可能超过行数，超出视为损坏流 */
    if (u16_rows > img_h)
        return;

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

    /* ---- payload 各区偏移（相对 6 字节通用头之后） ---- */
    uint32_t idx_off = 14U;
    uint32_t u32_off = idx_off + (uint32_t)u16_rows * 2U;
    uint32_t pal_off = u32_off + ((uint32_t)(img_h - u16_rows)) * 4U;
    uint32_t dat_off = pal_off + (uint32_t)pal_count * 3U;

    /* ---- 静态调色盘一次读入栈缓存（≤64 项 × 3 字节 = 192B）：
     * 命中调色盘 op 时直接查表，避免每次命中都发起一笔外挂读 ---- */
    uint8_t pal[192];
    if (pal_count > 0U)
        p_lcd->storage_read_cb(obj->flash_addr + 6U + pal_off, pal, (uint32_t)pal_count * 3U);

    /* 防御：与 RGB565 版本相同的保守流长上界，拦截损坏索引表 + 停止越界解码。 */
    uint32_t max_pixels = (uint32_t)img_w * img_h;
    uint32_t stream_max = (max_pixels << 2) + 16U;

    flash_stream_t stream;
    flash_stream_init(&stream, p_lcd->storage_read_cb,
                      obj->flash_addr + 6U + dat_off);

    /* ---- 行循环：逐可见行经行索引空降解码（RUN 不跨行、行自包含） ---- */
    for (uint16_t cur_y = iy_start; cur_y < clip_y_end; cur_y++)
    {
        uint8_t  idx_buf[4];
        uint32_t row_off;

        if (cur_y < u16_rows)
        {
            p_lcd->storage_read_cb(obj->flash_addr + 6U + idx_off + (uint32_t)cur_y * 2U, idx_buf, 2U);
            row_off = ((uint32_t)idx_buf[0] << 8) | idx_buf[1];
        }
        else
        {
            p_lcd->storage_read_cb(obj->flash_addr + 6U + u32_off +
                                   ((uint32_t)(cur_y - u16_rows)) * 4U, idx_buf, 4U);
            row_off = ((uint32_t)idx_buf[0] << 24) | ((uint32_t)idx_buf[1] << 16) |
                      ((uint32_t)idx_buf[2] << 8)  | idx_buf[3];
        }
        if (row_off >= stream_max)
            return; /* 损坏索引表 */

flash_stream_seek(&stream, row_off);

        colour_t *row_dst = p_lcd->pfb_gram +
                            ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
        uint16_t cur_x = 0U;
        uint8_t  flag;
        uint8_t  r = 0, g = 0, b = 0;
        uint8_t  cur_alpha   = 255U; /* 行首 op 必然重建 alpha，这里只是缺省值 */
        uint16_t cur_pixel   = 0U;
        uint8_t  final_alpha = we_div255((uint32_t)cur_alpha * opacity);

        while ((cur_x < clip_x_end) && (stream.cur_pos < stream_max))
        {
flag = flash_stream_get(&stream);

            if (flag == 0xFFU)
            {
                /* 新像素：alpha + RGB565（3 字节） */
cur_alpha = flash_stream_get(&stream);
                final_alpha = we_div255((uint32_t)cur_alpha * opacity);
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
            else if (flag < 0x40U)
            {
                /* 0x00..0x3F：静态调色盘 op，条目 3 字节 [A][H][L]（含 Alpha） */
                uint16_t pi;
                uint8_t h;
                uint8_t l;

                if (flag >= pal_count)
                    return; /* 超出条目数视为损坏流 */
                pi = (uint16_t)flag * 3U;
                cur_alpha = pal[pi];
                final_alpha = we_div255((uint32_t)cur_alpha * opacity);
                h = pal[pi + 1U];
                l = pal[pi + 2U];
                cur_pixel = ((uint16_t)h << 8) | l;
                r = h >> 3;
                g = (uint8_t)(((h & 0x07U) << 3) | (l >> 5));
                b = l & 0x1FU;
            }
            else /* 0xC0..0xFD：RUN（不跨行） */
            {
                uint8_t  run = (flag & 0x3FU) + 1U;
colour_t fg  = we_color_from_rgb565(cur_pixel);

                while (run--)
                {
                    if (cur_x >= ix_start && cur_x < clip_x_end)
                    {
we_store_blended_color(row_dst + cur_x, fg, final_alpha);
                    }
                    cur_x++;
                    if (cur_x >= clip_x_end)
                        break; /* 行内右缘截断：本行剩余像素不可见 */
                }
                continue;
            }

            if (cur_x >= ix_start && cur_x < clip_x_end)
            {
colour_t fg = we_color_from_rgb565(cur_pixel);
we_store_blended_color(row_dst + cur_x, fg, final_alpha);
            }
            cur_x++;
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

    /* 容器透明度级联：入口算一次有效透明度，避免白读 flash */
    uint8_t eff_op = we_opa_apply(obj->base.lcd, obj->opacity);
    if (eff_op == 0U)
        return;

    switch (obj->fmt)
    {
    case IMG_RGB565:
_flash_render_rgb565(obj->base.lcd, obj, eff_op);
        break;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    case IMG_RGB565_INDEXQOI:
_flash_render_indexed_qoi_rgb565(obj->base.lcd, obj, eff_op);
        break;

    case IMG_ARGB8565_INDEXQOI:
_flash_render_indexed_qoi_argb8565(obj->base.lcd, obj, eff_op);
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
    static const we_class_t _flash_img_class = {.draw_cb = _flash_img_draw_cb, .event_cb = NULL, .set_pos_cb = NULL};

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

    /* 防御：宽或高为 0 视为损坏资源/错误 flash 地址，拒绝初始化，
     * 否则 INDEX-QOI 解码路径的 % img_w、/ img_w 会触发除零硬 fault。 */
    uint16_t img_w = (uint16_t)(((uint16_t)hdr[2] << 8) | hdr[3]);
    uint16_t img_h = (uint16_t)(((uint16_t)hdr[4] << 8) | hdr[5]);
    if (img_w == 0U || img_h == 0U)
    {
        return 0U;
    }

    /* 填充 we_obj_t 基类字段；w/h 复用存储图片宽高 */
    obj->flash_addr    = flash_addr;
    obj->opacity       = opacity;
    obj->base.lcd      = lcd;
    obj->base.x        = x;
    obj->base.y        = y;
    obj->base.w        = (int16_t)img_w;
    obj->base.h        = (int16_t)img_h;
    obj->base.class_p  = &_flash_img_class;
    obj->base.next     = NULL;
    obj->base.parent   = NULL;

    /* 挂入 LCD 控件链表 */
    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

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
