
#include "we_widget_font_flash.h"
#include "we_render.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * 大端整数读取
 * -------------------------------------------------------------------------- */

/**
 * @brief 按大端序读取 16 位无符号整数。
 * @param buf 指向至少 2 字节的输入缓冲区。
 * @return 解析得到的 uint16_t 数值。
 */
static uint16_t _read_u16_be(const uint8_t *buf)
{
    return (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
}

/**
 * @brief 按大端序读取 32 位无符号整数。
 * @param buf 指向至少 4 字节的输入缓冲区。
 * @return 解析得到的 uint32_t 数值。
 */
static uint32_t _read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  | (uint32_t)buf[3];
}

/* --------------------------------------------------------------------------
 * 内部字形描述（与 font2c external blob 中的 14B 描述项一致）
 * -------------------------------------------------------------------------- */
typedef struct
{
    uint16_t adv_w;
    uint16_t box_w;
    uint16_t box_h;
    int16_t  x_ofs;
    int16_t  y_ofs;
    uint32_t bitmap_offset; /* 相对 bitmap_data 段起始 */
} _flash_glyph_t;

/* --------------------------------------------------------------------------
 * UTF-8 解码：支持 1~4 字节，非法序列返回 0
 * -------------------------------------------------------------------------- */

/**
 * @brief 从 UTF-8 字符串读取下一个码点并推进指针。
 * @param pp 输入输出参数，指向当前解析位置；成功后移动到下一个字符起始处。
 * @return 成功返回 Unicode 码点，遇到字符串结尾或非法序列返回 0。
 */
static uint32_t _utf8_decode_next(const char **pp)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint32_t code = 0;
    uint8_t c0;

    if (*p == 0U)
        return 0U;

    c0 = *p++;
    if (c0 < 0x80U)
    {
        code = c0;
    }
    else if ((c0 & 0xE0U) == 0xC0U)
    {
        if ((p[0] & 0xC0U) == 0x80U)
        {
            code = (uint32_t)(((c0 & 0x1FU) << 6) | (p[0] & 0x3FU));
            p += 1;
        }
    }
    else if ((c0 & 0xF0U) == 0xE0U)
    {
        if ((p[0] & 0xC0U) == 0x80U && (p[1] & 0xC0U) == 0x80U)
        {
            code = (uint32_t)(((c0 & 0x0FU) << 12) | ((p[0] & 0x3FU) << 6) | (p[1] & 0x3FU));
            p += 2;
        }
    }
    else if ((c0 & 0xF8U) == 0xF0U)
    {
        if ((p[0] & 0xC0U) == 0x80U && (p[1] & 0xC0U) == 0x80U && (p[2] & 0xC0U) == 0x80U)
        {
            code = (uint32_t)(((c0 & 0x07U) << 18) | ((p[0] & 0x3FU) << 12) |
                              ((p[1] & 0x3FU) << 6)  | (p[2] & 0x3FU));
            p += 3;
        }
    }

    *pp = (const char *)p;
    return code;
}

/* --------------------------------------------------------------------------
 * 码点 -> 字形索引
 * -------------------------------------------------------------------------- */

/**
 * @brief 将 Unicode 码点映射到字体字形索引。
 * @param face 已初始化的字体面对象。
 * @param code 待查询的 Unicode 码点。
 * @param out_index 输出字形索引（相对 glyph 描述表）。
 * @return 1 表示找到字形，0 表示该码点未收录或输入无效。
 */
static uint8_t _lookup_glyph_index(const we_flash_font_face_t *face,
                                   uint32_t code, uint32_t *out_index)
{
    uint16_t i;
    uint32_t glyph_cursor = 0U;

    if (face == NULL || out_index == NULL)
        return 0U;

    for (i = 0; i < face->range_count; ++i)
    {
        const font_range_t *r = &face->ranges[i];
        uint32_t range_len = r->end - r->start + 1U;

        if (code >= r->start && code <= r->end)
        {
            *out_index = glyph_cursor + (code - r->start);
            return 1U;
        }
        glyph_cursor += range_len;
    }

    if (face->sparse_count > 0U && face->sparse_codes != NULL)
    {
        int32_t lo = 0;
        int32_t hi = (int32_t)face->sparse_count - 1;
        while (lo <= hi)
        {
            int32_t mid = lo + ((hi - lo) >> 1);
            uint32_t v = face->sparse_codes[mid];
            if (v == code)
            {
                *out_index = face->range_total + (uint32_t)mid;
                return 1U;
            }
            if (v < code)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
    }

    return 0U;
}

/* --------------------------------------------------------------------------
 * 从外部 blob 读取单个字形描述符（14B）
 * -------------------------------------------------------------------------- */

/**
 * @brief 从外部 blob 读取并解析单个字形描述项。
 * @param face 已初始化的字体面对象。
 * @param glyph_index 待读取的字形索引。
 * @param out 输出字形描述结构（宽高、偏移、位图偏移等）。
 * @return 1 表示读取解析成功，0 表示越界或依赖回调缺失。
 */
static uint8_t _load_glyph_desc(const we_flash_font_face_t *face,
                                uint32_t glyph_index, _flash_glyph_t *out)
{
    uint8_t buf[WE_FLASH_FONT_GLYPH_DESC_SIZE];
    uint32_t offset;
    uint32_t addr;

    if (face == NULL || out == NULL || face->lcd == NULL || face->lcd->storage_read_cb == NULL)
        return 0U;
    if (glyph_index >= face->glyph_count)
        return 0U;

    offset = face->glyph_desc_offset + glyph_index * WE_FLASH_FONT_GLYPH_DESC_SIZE;
    if (offset + WE_FLASH_FONT_GLYPH_DESC_SIZE > face->bitmap_data_offset)
        return 0U;

    addr = face->blob_addr + offset;
    face->lcd->storage_read_cb(addr, buf, sizeof(buf));

out->adv_w         = _read_u16_be(&buf[0]);
out->box_w         = _read_u16_be(&buf[2]);
out->box_h         = _read_u16_be(&buf[4]);
    out->x_ofs         = (int16_t)_read_u16_be(&buf[6]);
    out->y_ofs         = (int16_t)_read_u16_be(&buf[8]);
out->bitmap_offset = _read_u32_be(&buf[10]);
    return 1U;
}

/* --------------------------------------------------------------------------
 * 直接绘制“按行字节对齐”的 alpha 点阵
 * -------------------------------------------------------------------------- */

/**
 * @brief 将行对齐的 alpha 位图混合到当前 PFB。
 * @param lcd 当前 LCD 上下文（提供 PFB 区域与像素缓冲）。
 * @param x 位图目标左上角 X 坐标。
 * @param y 位图目标左上角 Y 坐标。
 * @param w 位图宽度（像素）。
 * @param h 位图高度（像素）。
 * @param src_data 源 alpha 位图起始地址（按行紧凑存放）。
 * @param row_stride 源位图每行字节数。
 * @param bpp 源 alpha 位图位深（1/2/4/8）。
 * @param fg_color 前景色。
 * @param opacity 额外整体透明度（0~255）。
 */
static void _draw_alpha_mask_row_aligned(we_lcd_t *lcd,
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
    uint8_t alpha_mask;
    colour_t *gram;
    uint16_t pfb_stride;
    int16_t py;

    if (lcd == NULL || w == 0U || h == 0U || src_data == NULL || opacity == 0U)
        return;

    draw_x_end = x + (int16_t)w - 1;
    draw_y_end = y + (int16_t)h - 1;

    clip_x_start = (x < (int16_t)lcd->pfb_area.x0) ? (int16_t)lcd->pfb_area.x0 : x;
    clip_y_start = (y < (int16_t)lcd->pfb_y_start) ? (int16_t)lcd->pfb_y_start : y;
    clip_x_end   = (draw_x_end > (int16_t)lcd->pfb_area.x1) ? (int16_t)lcd->pfb_area.x1 : draw_x_end;
    clip_y_end   = (draw_y_end > (int16_t)lcd->pfb_y_end) ? (int16_t)lcd->pfb_y_end : draw_y_end;

    if (clip_x_start > clip_x_end || clip_y_start > clip_y_end)
        return;

    alpha_mask = (uint8_t)((1U << bpp) - 1U);
    gram       = (colour_t *)lcd->pfb_gram;
    pfb_stride = lcd->pfb_width;

    for (py = clip_y_start; py <= clip_y_end; py++)
    {
        int16_t mask_y = py - y;
        const uint8_t *src_row = src_data + (uint32_t)mask_y * row_stride;
        colour_t *dst = gram + ((py - (int16_t)lcd->pfb_y_start) * pfb_stride) +
                        (clip_x_start - (int16_t)lcd->pfb_area.x0);
        int16_t px;

        for (px = clip_x_start; px <= clip_x_end; px++)
        {
            int16_t mask_x = px - x;
            uint32_t bit_pos = (uint32_t)mask_x * bpp;
            uint32_t byte_idx = bit_pos >> 3U;
            uint8_t shift = (uint8_t)(8U - bpp - (bit_pos & 7U));
            uint8_t a_raw = (uint8_t)((src_row[byte_idx] >> shift) & alpha_mask);

            if (a_raw > 0U)
            {
                uint32_t alpha;

                if (bpp == 8U)
                    alpha = a_raw;
                else if (bpp == 4U)
                    alpha = (uint32_t)((a_raw << 4U) | a_raw);
                else if (bpp == 2U)
                    alpha = (uint32_t)a_raw * 85U;
                else
                    alpha = 255U;

                if (opacity != 255U)
                    alpha = (alpha * opacity) >> 8U;

we_store_blended_color(dst, fg_color, (uint8_t)alpha);
            }
            dst++;
        }
    }
}

/* --------------------------------------------------------------------------
 * 分块加载字形点阵并直接绘制到当前 PFB
 * -------------------------------------------------------------------------- */

/**
 * @brief 从外部存储分块读取字形位图并绘制到 PFB。
 * @param face 字体面对象（含 storage 回调与 scratch 缓冲）。
 * @param draw_x 字形绘制左上角 X 坐标（已包含 x_ofs）。
 * @param draw_y 字形绘制左上角 Y 坐标（已包含 y_ofs）。
 * @param g 字形描述（宽高、位图偏移等）。
 * @param color 字形绘制颜色。
 * @param opacity 字形整体透明度（0~255）。
 * @return 1 表示读取并绘制成功，0 表示参数无效或读区间越界。
 */
static uint8_t _stream_draw_glyph(we_flash_font_face_t *face,
                                  int16_t draw_x,
                                  int16_t draw_y,
                                  const _flash_glyph_t *g,
                                  colour_t color,
                                  uint8_t opacity)
{
    uint32_t row_stride;
    uint16_t rows_per_chunk;
    uint16_t row0;

    if (face == NULL || g == NULL || face->glyph_buf == NULL || face->glyph_buf_size == 0U)
        return 0U;
    if (g->box_w == 0U || g->box_h == 0U)
        return 0U;

    row_stride = ((uint32_t)g->box_w * face->bpp + 7U) >> 3U;
    if (row_stride == 0U || row_stride > face->glyph_buf_size)
        return 0U;

    rows_per_chunk = (uint16_t)(face->glyph_buf_size / row_stride);
    if (rows_per_chunk == 0U)
        return 0U;

    for (row0 = 0U; row0 < g->box_h; row0 = (uint16_t)(row0 + rows_per_chunk))
    {
        uint16_t chunk_rows = rows_per_chunk;
        uint32_t chunk_size;
        uint32_t offset;
        uint32_t addr;

        if ((uint16_t)(row0 + chunk_rows) > g->box_h)
            chunk_rows = (uint16_t)(g->box_h - row0);

        chunk_size = row_stride * (uint32_t)chunk_rows;
        offset = face->bitmap_data_offset + g->bitmap_offset + (uint32_t)row0 * row_stride;
        if (offset + chunk_size > face->blob_size)
            return 0U;

        addr = face->blob_addr + offset;
        face->lcd->storage_read_cb(addr, face->glyph_buf, chunk_size);

        _draw_alpha_mask_row_aligned(face->lcd,
                                     draw_x,
                                     (int16_t)(draw_y + (int16_t)row0),
                                     g->box_w,
                                     chunk_rows,
                                     face->glyph_buf,
                                     row_stride,
                                     face->bpp,
                                     color,
                                     opacity);
    }

    return 1U;
}

/* --------------------------------------------------------------------------
 * 计算文本包围盒
 * -------------------------------------------------------------------------- */

/**
 * @brief 计算文本控件当前文本的包围盒尺寸。
 * @param obj 文本控件实例。
 * @param out_w 输出文本总宽度（像素）。
 * @param out_h 输出文本总高度（像素，含多行 line_height）。
 */
static void _compute_bbox(we_flash_font_obj_t *obj, int16_t *out_w, int16_t *out_h)
{
    we_flash_font_face_t *face = obj->face;
    int16_t max_w = 0;
    int16_t cur_w = 0;
    int16_t lines = 1;
    const char *p = obj->text;
    uint32_t glyph_index;
    _flash_glyph_t g;

    if (p == NULL || *p == 0)
    {
        *out_w = 0;
        *out_h = (int16_t)face->line_height;
        return;
    }

    while (*p)
    {
uint32_t code = _utf8_decode_next(&p);
        if (code == 0U)
            break;
        if (code == (uint32_t)'\n')
        {
            if (cur_w > max_w)
                max_w = cur_w;
            cur_w = 0;
            lines++;
            continue;
        }
        if (_lookup_glyph_index(face, code, &glyph_index) && _load_glyph_desc(face, glyph_index, &g))
        {
            cur_w += (int16_t)g.adv_w;
        }
    }

    if (cur_w > max_w)
        max_w = cur_w;

    *out_w = max_w;
    *out_h = (int16_t)(lines * (int16_t)face->line_height);
}

/**
 * @brief 使控件当前包围盒区域失效，触发后续重绘。
 * @param obj 文本控件实例。
 */
static void _invalidate_obj_bbox(we_flash_font_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == 0U)
        return;

we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y, obj->base.w, obj->base.h);
}

/**
 * @brief 计算指定文本的包围盒并使该区域失效。
 * @param obj 文本控件实例。
 * @param text 用于估算尺寸的 UTF-8 文本。
 */
static void _invalidate_text_bbox(we_flash_font_obj_t *obj, const char *text)
{
    int16_t w = 0;
    int16_t h = 0;
    const char *saved_text;

    if (obj == NULL || obj->face == NULL || obj->base.lcd == NULL || text == NULL || obj->opacity == 0U)
        return;

    saved_text = obj->text;
    obj->text = text;
_compute_bbox(obj, &w, &h);
    obj->text = saved_text;
we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y, w, h);
}

/**
 * @brief 失效指定文本区域（_invalidate_text_bbox 的简化封装）。
 * @param obj 文本控件实例。
 * @param text 用于估算尺寸的 UTF-8 文本。
 */
static void _invalidate_text(we_flash_font_obj_t *obj, const char *text)
{
_invalidate_text_bbox(obj, text);
}

/* --------------------------------------------------------------------------
 * 绘制回调：每次 PFB tile 调用
 * -------------------------------------------------------------------------- */

/**
 * @brief 文本控件绘制回调：按当前 PFB 切片流式解码并绘制字形。
 * @param ptr 回调透传的 we_flash_font_obj_t 指针。
 */
static void _font_flash_draw_cb(void *ptr)
{
    we_flash_font_obj_t *obj = (we_flash_font_obj_t *)ptr;
    we_flash_font_face_t *face;
    we_lcd_t *lcd;
    int16_t cursor_x;
    int16_t cursor_y;
    const char *p;

    if (obj == NULL || obj->face == NULL || obj->text == NULL || obj->opacity == 0U)
        return;

    face = obj->face;
    lcd = obj->base.lcd;
    if (lcd == NULL || face->lcd == NULL || face->lcd->storage_read_cb == NULL || face->line_height == 0U)
        return;

    cursor_x = obj->base.x;
    cursor_y = obj->base.y;
    p = obj->text;

    while (*p)
    {
        uint32_t code;
        uint32_t glyph_index;
        _flash_glyph_t g;

        if (cursor_y > (int16_t)lcd->pfb_y_end)
            return;

        if (cursor_y + (int16_t)face->line_height <= (int16_t)lcd->pfb_y_start)
        {
            while (*p && *p != '\n')
                p++;
            if (*p == '\n')
            {
                p++;
                cursor_x = obj->base.x;
                cursor_y += (int16_t)face->line_height;
            }
            continue;
        }

code = _utf8_decode_next(&p);
        if (code == 0U)
            break;

        if (code == (uint32_t)'\n')
        {
            cursor_x = obj->base.x;
            cursor_y += (int16_t)face->line_height;
            continue;
        }

        if (_lookup_glyph_index(face, code, &glyph_index) && _load_glyph_desc(face, glyph_index, &g))
        {
            if (g.box_w > 0U && g.box_h > 0U)
            {
                int16_t draw_x = cursor_x + g.x_ofs;
                int16_t draw_y = cursor_y + g.y_ofs;
                int16_t box_r = draw_x + (int16_t)g.box_w - 1;
                int16_t box_b = draw_y + (int16_t)g.box_h - 1;

                if (box_r >= (int16_t)lcd->pfb_area.x0 &&
                    box_b >= (int16_t)lcd->pfb_y_start &&
                    draw_x <= (int16_t)lcd->pfb_area.x1 &&
                    draw_y <= (int16_t)lcd->pfb_y_end)
                {
                    if (_stream_draw_glyph(face, draw_x, draw_y, &g, obj->color, obj->opacity))
                    {
                    }
                }
            }
            cursor_x += (int16_t)g.adv_w;
        }
    }
}

static const we_class_t _flash_font_class = { .draw_cb = _font_flash_draw_cb, .event_cb = NULL };

/**
 * @brief 校验字体句柄与 blob 头信息，并填充 face 运行时字段。
 * @param face 待初始化的字体面对象。
 * @param lcd LCD 上下文，需提供 storage_read_cb。
 * @param handle font2c 导出的字体句柄与 blob 地址。
 * @return 1 表示校验通过且绑定成功，0 表示任一字段或格式不合法。
 */
static uint8_t _validate_and_bind_face(we_flash_font_face_t *face,
                                       we_lcd_t *lcd,
                                       const font_external_handle_t *handle)
{
    uint8_t hdr[WE_FLASH_FONT_BLOB_HEADER_SIZE];
    uint16_t i;
    uint32_t glyph_cursor = 0U;
    uint32_t expected_bitmap_data_offset;

    if (face == NULL || lcd == NULL || handle == NULL || handle->font == NULL)
        return 0U;
    if (lcd->storage_read_cb == NULL)
        return 0U;
    if (handle->blob_addr > 0xFFFFFFFFu)
        return 0U;

memset(face, 0, sizeof(*face));

    face->lcd = lcd;
    face->font = handle->font;
    face->blob_addr = (uint32_t)handle->blob_addr;
    face->blob_size = handle->font->blob_size;

    face->line_height = handle->font->line_height;
    face->baseline = handle->font->baseline;
    face->bpp = handle->font->bpp;
    face->range_count = handle->font->range_count;
    face->sparse_count = handle->font->sparse_count;
    face->glyph_count = handle->font->glyph_count;
    face->max_box_width = handle->font->max_box_width;
    face->max_box_height = handle->font->max_box_height;
    face->ranges = handle->font->ranges;
    face->sparse_codes = handle->font->sparse;
    face->glyph_desc_offset = WE_FLASH_FONT_BLOB_HEADER_SIZE;

    if (face->bpp != 1U && face->bpp != 2U && face->bpp != 4U && face->bpp != 8U)
        return 0U;
    if (face->line_height == 0U)
        return 0U;
    if (face->glyph_count == 0U)
        return 0U;
    if (face->range_count > 0U && face->ranges == NULL)
        return 0U;
    if (face->sparse_count > 0U && face->sparse_codes == NULL)
        return 0U;
    if (face->blob_size < WE_FLASH_FONT_BLOB_HEADER_SIZE)
        return 0U;

    lcd->storage_read_cb(face->blob_addr, hdr, sizeof(hdr));
    if (hdr[0] != 0x02U)
        return 0U;
    if (hdr[1] != 0x00U)
        return 0U;
    if (_read_u32_be(&hdr[2]) != handle->font->blob_crc32)
        return 0U;

    for (i = 0; i < face->range_count; ++i)
    {
        const font_range_t *r = &face->ranges[i];
        uint32_t range_len;

        if (r->end < r->start)
            return 0U;
        if (i > 0U && r->start <= face->ranges[i - 1U].end)
            return 0U;

        range_len = r->end - r->start + 1U;
        glyph_cursor += range_len;
    }
    face->range_total = glyph_cursor;

    if ((uint32_t)face->glyph_count != face->range_total + (uint32_t)face->sparse_count)
        return 0U;

    if (face->glyph_desc_offset + (uint32_t)face->glyph_count * WE_FLASH_FONT_GLYPH_DESC_SIZE > face->blob_size)
        return 0U;

    expected_bitmap_data_offset = face->glyph_desc_offset + (uint32_t)face->glyph_count * WE_FLASH_FONT_GLYPH_DESC_SIZE;
    face->bitmap_data_offset = expected_bitmap_data_offset;
    if (face->bitmap_data_offset > face->blob_size)
        return 0U;

    return 1U;
}

/* ==========================================================================
 * 公开 API — Face（静态缓冲版）
 * ========================================================================== */

/**
 * @brief 初始化字体面对象并绑定字形流式读取缓冲。
 * @param face 待初始化的字体面对象。
 * @param lcd LCD 上下文，需已绑定 storage_read_cb。
 * @param handle font2c 导出的字体句柄。
 * @param glyph_buf 外部分配的字形 scratch 缓冲区。
 * @param glyph_buf_size scratch 缓冲容量（字节）。
 * @return 1 表示初始化成功，0 表示校验失败或缓冲区不足。
 */
uint8_t we_flash_font_face_init(we_flash_font_face_t *face,
                                we_lcd_t *lcd,
                                const font_external_handle_t *handle,
                                uint8_t *glyph_buf,
                                uint32_t glyph_buf_size)
{
    uint32_t min_row_bytes;

    if (!_validate_and_bind_face(face, lcd, handle))
        return 0U;

    min_row_bytes = ((uint32_t)face->max_box_width * face->bpp + 7U) >> 3U;
    if (min_row_bytes > 0U && (glyph_buf == NULL || glyph_buf_size < min_row_bytes))
        return 0U;

    face->glyph_buf = glyph_buf;
    face->glyph_buf_size = glyph_buf_size;
    return 1U;
}

/* ==========================================================================
 * 公开 API — Obj
 * ========================================================================== */

/**
 * @brief 初始化外部字库文本控件并挂入 LCD 对象链表。
 * @param obj 待初始化的文本控件实例。
 * @param face 已初始化字体面对象。
 * @param x 控件左上角 X 坐标。
 * @param y 控件左上角 Y 坐标。
 * @param text 初始 UTF-8 文本。
 * @param color 文本颜色。
 * @param opacity 控件透明度（0~255）。
 * @return 1 表示初始化成功，0 表示参数无效。
 */
uint8_t we_flash_font_obj_init(we_flash_font_obj_t *obj,
                               we_flash_font_face_t *face,
                               int16_t x,
                               int16_t y,
                               const char *text,
                               colour_t color,
                               uint8_t opacity)
{
    int16_t w = 0;
    int16_t h = 0;

    if (obj == NULL || face == NULL || face->lcd == NULL)
        return 0U;
    if (face->line_height == 0U)
        return 0U;

memset(obj, 0, sizeof(*obj));
    obj->base.lcd = face->lcd;
    obj->face = face;
    obj->text = text;
    obj->color = color;
    obj->opacity = opacity;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.class_p = &_flash_font_class;

_compute_bbox(obj, &w, &h);
    obj->base.w = w;
    obj->base.h = h;

    if (face->lcd->obj_list_head == NULL)
    {
        face->lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = face->lcd->obj_list_head;
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }

    if (opacity > 0U && w > 0 && h > 0)
we_obj_invalidate((we_obj_t *)obj);

    return 1U;
}

/**
 * @brief 更新文本内容并重算包围盒后触发重绘。
 * @param obj 文本控件实例。
 * @param text 新的 UTF-8 文本。
 */
void we_flash_font_obj_set_text(we_flash_font_obj_t *obj, const char *text)
{
    int16_t new_w = 0;
    int16_t new_h = 0;
    const char *saved_text;

    if (obj == NULL || obj->face == NULL)
        return;
    if (obj->text == text)
        return;

    saved_text = obj->text;
    obj->text = text;
_compute_bbox(obj, &new_w, &new_h);
    obj->text = saved_text;

    if (obj->opacity > 0U)
_invalidate_obj_bbox(obj);

    obj->text = text;
    obj->base.w = new_w;
    obj->base.h = new_h;

    if (obj->opacity > 0U)
_invalidate_text_bbox(obj, obj->text);
}

/**
 * @brief 设置文本颜色；颜色发生变化时使控件失效重绘。
 * @param obj 文本控件实例。
 * @param color 新文本颜色。
 */
void we_flash_font_obj_set_color(we_flash_font_obj_t *obj, colour_t color)
{
    if (obj == NULL)
        return;

#if (LCD_DEEP == DEEP_RGB565)
    if (obj->color.dat16 == color.dat16)
        return;
#elif (LCD_DEEP == DEEP_RGB888)
    if (obj->color.rgb.r == color.rgb.r &&
        obj->color.rgb.g == color.rgb.g &&
        obj->color.rgb.b == color.rgb.b)
        return;
#endif

    obj->color = color;
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置文本控件透明度，并对旧/新显示区域做失效处理。
 * @param obj 文本控件实例。
 * @param opacity 新透明度（0~255）。
 */
void we_flash_font_obj_set_opacity(we_flash_font_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;

    if (obj->opacity > 0U)
_invalidate_text(obj, obj->text);

    obj->opacity = opacity;

    if (obj->opacity > 0U)
_invalidate_text(obj, obj->text);
}

/**
 * @brief 删除文本控件并从对象链表中移除。
 * @param obj 文本控件实例。
 */
void we_flash_font_obj_delete(we_flash_font_obj_t *obj)
{
    if (obj == NULL)
        return;
we_obj_delete((we_obj_t *)obj);
}
