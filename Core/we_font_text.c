#include "we_font_text.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * UTF-8 读取：仅保留当前 text path 需要的 1/2/3 字节码点
 *
 * 返回值：
 *   0 = 字符串结束
 *   1 = 成功解析 1 个码点（包含非法单字节，code 置 0）
 *   2 = 截断的多字节序列（draw 路径会直接停止）
 * -------------------------------------------------------------------------- */
typedef enum
{
    _WE_UTF8_NEXT_END = 0U,
    _WE_UTF8_NEXT_OK = 1U,
    _WE_UTF8_NEXT_TRUNCATED = 2U
} _we_utf8_next_state_t;

/**
 * @brief 读取 UTF-8 字符串中的下一个码点并推进指针。
 * @param pp 输入输出：当前解析位置；成功后移动到下一个字符起始处。
 * @param out_code 传出：解析得到的码点，非法单字节时返回 0。
 * @return 0=字符串结束，1=成功读取，2=截断的多字节序列。
 */
static __inline _we_utf8_next_state_t _utf8_decode_next_u16(const char **pp, uint16_t *out_code)
{
    const unsigned char *p;
    uint8_t c0;

    /* pp / out_code 由内部调用方以 &str / &code 形式传入，恒为有效地址，
     * 这里只需校验字符串内容本身。 */
    p = (const unsigned char *)*pp;
    if (p == NULL || *p == 0U)
        return _WE_UTF8_NEXT_END;

    c0 = *p++;
    *out_code = 0U;

    if (c0 < 0x80U)
    {
        *out_code = c0;
    }
    else if ((c0 & 0xE0U) == 0xC0U)
    {
        if (*p == 0U)
        {
            *pp = (const char *)p;
            return _WE_UTF8_NEXT_TRUNCATED;
        }
        *out_code = (uint16_t)(((c0 & 0x1FU) << 6) | ((uint8_t)*p++ & 0x3FU));
    }
    else if ((c0 & 0xF0U) == 0xE0U)
    {
        if (p[0] == 0U || p[1] == 0U)
        {
            *pp = (const char *)p;
            return _WE_UTF8_NEXT_TRUNCATED;
        }
        *out_code = (uint16_t)(((c0 & 0x0FU) << 12) |
                               (((uint8_t)p[0] & 0x3FU) << 6) |
                               ((uint8_t)p[1] & 0x3FU));
        p += 2;
    }
    /* 4 字节及以上编码保持与旧逻辑一致：按非法单字节处理，code 置 0。 */
    *pp = (const char *)p;
    return _WE_UTF8_NEXT_OK;
}

/**
 * @brief 获取字库行高的内部实现
 * @param font_array 传入：字库数组指针（仅支持 font2c internal 格式）
 * @return 行高（像素），失败返回 0
 */
static uint16_t _we_font_get_line_height_impl(const unsigned char *font_array)
{
    const font_internal_t *font = (const font_internal_t *)font_array;

    if (font == NULL)
        return 0U;

    return font->line_height;
}

/**
 * @brief 获取字库的标准行高
 * @param font_array 传入：字库数组指针
 * @return 行高（像素），失败返回 0
 */
uint16_t we_font_get_line_height(const unsigned char *font_array)
{
    return _we_font_get_line_height_impl(font_array);
}

/**
 * @brief 在 font2c internal 字库中按码点检索字形信息
 * @param font 传入：font2c internal 字库对象指针
 * @param code 传入：Unicode 码点
 * @param out_info 传出：字形信息结构体
 * @return 1 表示找到字形，0 表示未找到或参数非法
 */
static uint8_t _we_font_get_glyph_info_internal(const font_internal_t *font, uint16_t code, we_glyph_info_t *out_info)
{
    uint16_t i;
    uint32_t glyph_index = 0U;
    uint32_t range_total;
    const font_glyph_desc_t *desc;

    if (font == NULL || out_info == NULL || font->glyph_desc == NULL)
        return 0U;

    for (i = 0; i < font->range_count; ++i)
    {
        const font_range_t *r = &font->ranges[i];
        uint32_t range_len = r->end - r->start + 1U;

        if (code >= r->start && code <= r->end)
        {
            glyph_index += (uint32_t)(code - r->start);
            goto found_internal_glyph;
        }
        glyph_index += range_len;
    }

    range_total = glyph_index;
    if (font->sparse != NULL && font->sparse_count > 0U)
    {
        int32_t lo = 0;
        int32_t hi = (int32_t)font->sparse_count - 1;
        while (lo <= hi)
        {
            int32_t mid = lo + ((hi - lo) >> 1);
            uint32_t v = font->sparse[mid];
            if (v == code)
            {
                glyph_index = range_total + (uint32_t)mid;
                goto found_internal_glyph;
            }
            if (v < code)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
    }
    return 0U;

found_internal_glyph:
    if (glyph_index >= font->glyph_count)
        return 0U;

    desc = &font->glyph_desc[glyph_index];
    out_info->adv_w = desc->adv_w;
    out_info->box_w = desc->box_w;
    out_info->box_h = desc->box_h;
    out_info->x_ofs = desc->x_ofs;
    out_info->y_ofs = desc->y_ofs;
    out_info->offset = desc->bitmap_offset;
    return 1U;
}

/**
 * @brief 查询字形信息
 * @param font_array 传入：字库数组指针（仅支持 font2c internal 格式）
 * @param code 传入：Unicode 码点
 * @param out_info 传出：字形信息结构体
 * @return 1 表示找到字形，0 表示未找到或参数非法
 */
uint8_t we_font_get_glyph_info(const unsigned char *font_array, uint16_t code, we_glyph_info_t *out_info)
{
    if (font_array == NULL || out_info == NULL)
        return 0U;

    return _we_font_get_glyph_info_internal((const font_internal_t *)font_array, code, out_info);
}

/**
 * @brief 查询字形位图地址、位深与行跨度
 * @param font_array 传入：字库数组指针（仅支持 font2c internal 格式）
 * @param info 传入：已查询到的字形信息结构体
 * @param out_bitmap 传出：字形位图首地址
 * @param out_bpp 传出：位图位深（bpp）
 * @param out_row_stride 传出：位图每行字节数
 * @return 1 表示成功，0 表示失败或参数非法
 */
uint8_t we_font_get_bitmap_info(const unsigned char *font_array, const we_glyph_info_t *info,
                                const uint8_t **out_bitmap, uint8_t *out_bpp, uint32_t *out_row_stride)
{
    const font_internal_t *font;

    if (font_array == NULL || info == NULL || out_bitmap == NULL || out_bpp == NULL || out_row_stride == NULL)
        return 0U;

    font = (const font_internal_t *)font_array;
    if (font == NULL || font->bitmap_data == NULL)
        return 0U;

    *out_bpp = font->bpp;
    *out_row_stride = (((uint32_t)info->box_w * (uint32_t)(*out_bpp)) + 7U) >> 3U;
    *out_bitmap = font->bitmap_data + info->offset;
    return 1U;
}

/**
 * @brief 按行对齐方式绘制 alpha mask 位图
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
static void _draw_alpha_mask_row_aligned(we_lcd_t *p_lcd,
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

    if (p_lcd == NULL || w == 0U || h == 0U || src_data == NULL || opacity == 0U)
        return;

    draw_x_end = x + (int16_t)w - 1;
    draw_y_end = y + (int16_t)h - 1;

    clip_x_start = (x < (int16_t)p_lcd->pfb_area.x0) ? (int16_t)p_lcd->pfb_area.x0 : x;
    clip_y_start = (y < (int16_t)p_lcd->pfb_y_start) ? (int16_t)p_lcd->pfb_y_start : y;
    clip_x_end   = (draw_x_end > (int16_t)p_lcd->pfb_area.x1) ? (int16_t)p_lcd->pfb_area.x1 : draw_x_end;
    clip_y_end   = (draw_y_end > (int16_t)p_lcd->pfb_y_end) ? (int16_t)p_lcd->pfb_y_end : draw_y_end;

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
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->pfb_y_start) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->pfb_area.x0);

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
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->pfb_y_start) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->pfb_area.x0);
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
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->pfb_y_start) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->pfb_area.x0);
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
        colour_t *dst_row = gram + ((clip_y_start - (int16_t)p_lcd->pfb_y_start) * pfb_stride) +
                            (clip_x_start - (int16_t)p_lcd->pfb_area.x0);
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
 * @brief 在指定位置绘制一段 UTF-8 字符串
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：起始绘制 X 坐标
 * @param y 传入：起始绘制 Y 坐标
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @param fg_color 传入：前景颜色
 * @param opacity 传入：整体透明度
 * @return 无
 */
void we_draw_string(we_lcd_t *p_lcd, int16_t x, int16_t y, const unsigned char *font_array, const char *str,
                    colour_t fg_color, uint8_t opacity)
{
    const font_internal_t *font;
    uint16_t line_height;
    we_glyph_info_t info;
    int16_t cursor_x;
    int16_t cursor_y;

    if (p_lcd == NULL || str == NULL || font_array == NULL || opacity == 0U)
        return;

    font = (const font_internal_t *)font_array;
    if (font == NULL || font->bitmap_data == NULL)
        return;

    line_height = font->line_height;
    cursor_x = x;
    cursor_y = y;

    while (*str)
    {
        if (cursor_y > p_lcd->pfb_y_end)
            return;

        if (cursor_y + (int16_t)line_height <= p_lcd->pfb_y_start)
        {
            while (*str && *str != '\n')
                str++;
            if (*str == '\n')
            {
                cursor_x = x;
                cursor_y += (int16_t)line_height;
                str++;
            }
            continue;
        }

        uint16_t code = 0U;
        _we_utf8_next_state_t st = _utf8_decode_next_u16(&str, &code);
        if (st == _WE_UTF8_NEXT_END)
            break;
        if (st == _WE_UTF8_NEXT_TRUNCATED)
            break;

        if (code == (uint16_t)'\n')
        {
            cursor_x = x;
            cursor_y += (int16_t)line_height;
            continue;
        }

        if (cursor_x > p_lcd->pfb_area.x1)
            continue;

        if (_we_font_get_glyph_info_internal(font, code, &info))
        {
            if (info.box_w > 0U && info.box_h > 0U)
            {
                int16_t draw_x = cursor_x + info.x_ofs;
                int16_t draw_y = cursor_y + info.y_ofs;

                if (draw_x <= (int16_t)p_lcd->pfb_area.x1 &&
                    draw_y <= (int16_t)p_lcd->pfb_y_end &&
                    (draw_x + (int16_t)info.box_w > (int16_t)p_lcd->pfb_area.x0) &&
                    (draw_y + (int16_t)info.box_h > (int16_t)p_lcd->pfb_y_start))
                {
                    const uint8_t *bitmap = font->bitmap_data + info.offset;
                    uint8_t bpp = font->bpp;
                    uint32_t row_stride = (((uint32_t)info.box_w * (uint32_t)bpp) + 7U) >> 3U;
                    _draw_alpha_mask_row_aligned(p_lcd, draw_x, draw_y, info.box_w, info.box_h,
                                                 bitmap, row_stride, bpp, fg_color, opacity);
                }
            }
            cursor_x += (int16_t)info.adv_w;
        }
    }
}

/**
 * @brief 计算文本第一行的像素宽度
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @return 第一行总宽度（像素）
 */
uint16_t we_get_text_width(const unsigned char *font_array, const char *str)
{
    const font_internal_t *font;
    we_glyph_info_t info;
    uint16_t total_width = 0U;

    if (str == NULL || font_array == NULL)
        return 0U;

    font = (const font_internal_t *)font_array;
    if (font == NULL)
        return 0U;

    while (*str && *str != '\n')
    {
        uint16_t code = 0U;
        _we_utf8_next_state_t st = _utf8_decode_next_u16(&str, &code);
        if (st == _WE_UTF8_NEXT_END || st == _WE_UTF8_NEXT_TRUNCATED)
            break;

        if (_we_font_get_glyph_info_internal(font, code, &info))
            total_width = (uint16_t)(total_width + info.adv_w);
    }
    return total_width;
}

/**
 * @brief 测量文本各行的包围矩形
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @param out_rects 传出：逐行矩形数组，可为 NULL
 * @param max_rects 传入：out_rects 的可写入容量
 * @return 文本总行数
 */
uint16_t we_text_measure_line_rects(const unsigned char *font_array, const char *str,
                                    we_text_line_rect_t *out_rects, uint16_t max_rects)
{
    uint16_t count = 0U;
    uint16_t line_height;
    const char *p;
    int16_t y;

    if (font_array == NULL || str == NULL)
        return 0U;

    line_height = we_font_get_line_height(font_array);
    p = str;
    y = 0;

    while (1)
    {
        uint16_t line_w = we_get_text_width(font_array, p);

        if (out_rects != NULL && count < max_rects)
        {
            out_rects[count].y = y;
            out_rects[count].w = (int16_t)line_w;
            out_rects[count].h = (int16_t)line_height;
        }
        count++;

        while (*p && *p != '\n')
            p++;
        if (*p != '\n')
            break;

        p++;
        y = (int16_t)(y + (int16_t)line_height);
        if (*p == '\0')
        {
            if (out_rects != NULL && count < max_rects)
            {
                out_rects[count].y = y;
                out_rects[count].w = 0;
                out_rects[count].h = (int16_t)line_height;
            }
            count++;
            break;
        }
    }

    return count;
}

/**
 * @brief 按文本逐行区域执行标脏
 * @param obj 传入：所属控件对象指针
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @param base_x 传入：文本绘制基准 X 坐标
 * @param base_y 传入：文本绘制基准 Y 坐标
 * @return 无
 */
void we_text_invalidate_lines(we_obj_t *obj, const unsigned char *font_array, const char *str,
                              int16_t base_x, int16_t base_y)
{
    const char *p;
    uint16_t line_height;
    int16_t y;

    if (obj == NULL || obj->lcd == NULL || font_array == NULL || str == NULL)
        return;

    line_height = we_font_get_line_height(font_array);
    p = str;
    y = 0;

    while (1)
    {
        uint16_t line_w = we_get_text_width(font_array, p);
        we_obj_invalidate_area(obj, base_x, (int16_t)(base_y + y), (int16_t)line_w, (int16_t)line_height);

        while (*p && *p != '\n')
            p++;
        if (*p != '\n')
            break;

        p++;
        y = (int16_t)(y + (int16_t)line_height);
        if (*p == '\0')
        {
            we_obj_invalidate_area(obj, base_x, (int16_t)(base_y + y), 0, (int16_t)line_height);
            break;
        }
    }
}

/**
 * @brief 获取文本第一行可见内容的垂直包围范围
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串，仅测量第一行
 * @param out_y_top 传出：相对绘制基准 y 的顶部偏移
 * @param out_y_bottom 传出：相对绘制基准 y 的底部偏移
 * @return 无
 */
void we_get_text_bbox(const unsigned char *font_array, const char *str, int8_t *out_y_top, int8_t *out_y_bottom)
{
    const font_internal_t *font;
    int8_t y_top = 127;
    int8_t y_bot = -128;
    we_glyph_info_t info;

    if (font_array == NULL || str == NULL || out_y_top == NULL || out_y_bottom == NULL)
        return;

    font = (const font_internal_t *)font_array;
    if (font == NULL)
        return;

    {
        uint16_t line_height = font->line_height;

        while (*str && *str != '\n')
        {
            uint16_t code = 0U;
            _we_utf8_next_state_t st = _utf8_decode_next_u16(&str, &code);
            if (st == _WE_UTF8_NEXT_END || st == _WE_UTF8_NEXT_TRUNCATED)
                break;

            if (_we_font_get_glyph_info_internal(font, code, &info) && info.box_h > 0)
            {
                int16_t top = info.y_ofs;
                int16_t bot = (int16_t)(info.y_ofs + info.box_h);
                if (top < y_top)
                    y_top = (int8_t)top;
                if (bot > y_bot)
                    y_bot = (int8_t)bot;
            }
        }

        if (y_top == 127)
        {
            *out_y_top = 0;
            *out_y_bottom = (int8_t)line_height;
        }
        else
        {
            *out_y_top = y_top;
            *out_y_bottom = y_bot;
        }
    }
}
