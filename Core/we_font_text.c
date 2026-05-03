#include "we_font_text.h"
#include "we_render.h"

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
 * @brief 获取字库位深的内部实现
 * @param font_array 传入：字库数组指针（仅支持 font2c internal 格式）
 * @return 位深（bpp），失败返回 0
 */
static uint8_t _we_font_get_bpp(const unsigned char *font_array)
{
    const font_internal_t *font = (const font_internal_t *)font_array;

    if (font == NULL)
        return 0U;

    return font->bpp;
}

/**
 * @brief 统计 range 区间覆盖的字形总数
 * @param font 传入：font2c internal 字库对象指针
 * @return 区间字形总数，参数非法时返回 0
 */
static uint32_t _we_font2c_range_total(const font_internal_t *font)
{
    uint16_t i;
    uint32_t total = 0U;

    if (font == NULL || font->ranges == NULL)
        return 0U;

    for (i = 0; i < font->range_count; ++i)
    {
        if (font->ranges[i].end >= font->ranges[i].start)
            total += font->ranges[i].end - font->ranges[i].start + 1U;
    }
    return total;
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

    range_total = _we_font2c_range_total(font);
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

    out_info->adv_w = font->glyph_desc[glyph_index].adv_w;
    out_info->box_w = font->glyph_desc[glyph_index].box_w;
    out_info->box_h = font->glyph_desc[glyph_index].box_h;
    out_info->x_ofs = font->glyph_desc[glyph_index].x_ofs;
    out_info->y_ofs = font->glyph_desc[glyph_index].y_ofs;
    out_info->offset = font->glyph_desc[glyph_index].bitmap_offset;
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
    const font_internal_t *font;

    if (font_array == NULL || out_info == NULL)
        return 0U;

    font = (const font_internal_t *)font_array;
    return font != NULL ? _we_font_get_glyph_info_internal(font, code, out_info) : 0U;
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

    *out_bpp = _we_font_get_bpp(font_array);
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
    uint8_t alpha_mask;
    colour_t *gram;
    uint16_t pfb_stride;
    int16_t py;

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

    alpha_mask = (uint8_t)((1U << bpp) - 1U);
    gram       = (colour_t *)p_lcd->pfb_gram;
    pfb_stride = p_lcd->pfb_width;

    for (py = clip_y_start; py <= clip_y_end; py++)
    {
        int16_t mask_y = py - y;
        const uint8_t *src_row = src_data + (uint32_t)mask_y * row_stride;
        colour_t *dst = gram + ((py - (int16_t)p_lcd->pfb_y_start) * pfb_stride) +
                        (clip_x_start - (int16_t)p_lcd->pfb_area.x0);
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
    if (str == NULL || font_array == NULL || opacity == 0U)
        return;

    uint16_t line_height = _we_font_get_line_height_impl(font_array);
    int16_t cursor_x = x;
    int16_t cursor_y = y;
    we_glyph_info_t info;

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

        {
            uint16_t code = 0U;
            uint8_t c = (uint8_t)*str++;

            if (c < 0x80U)
                code = c;
            else if ((c & 0xE0U) == 0xC0U)
            {
                if (*str)
                    code = (uint16_t)(((c & 0x1FU) << 6) | ((uint8_t)*str++ & 0x3FU));
                else
                    break;
            }
            else if ((c & 0xF0U) == 0xE0U)
            {
                if (*str && *(str + 1))
                {
                    code = (uint16_t)(((c & 0x0FU) << 12) | (((uint8_t)str[0] & 0x3FU) << 6) | ((uint8_t)str[1] & 0x3FU));
                    str += 2;
                }
                else
                    break;
            }

            if (code == (uint16_t)'\n')
            {
                cursor_x = x;
                cursor_y += (int16_t)line_height;
                continue;
            }

            if (cursor_x > p_lcd->pfb_area.x1)
                continue;

            if (we_font_get_glyph_info(font_array, code, &info))
            {
                if (info.box_w > 0U && info.box_h > 0U)
                {
                    int16_t draw_x = cursor_x + info.x_ofs;
                    int16_t draw_y = cursor_y + info.y_ofs;
                    const uint8_t *bitmap = NULL;
                    uint8_t bpp = 0U;
                    uint32_t row_stride = 0U;

                    if ((draw_x + (int16_t)info.box_w > (int16_t)p_lcd->pfb_area.x0) &&
                        (draw_y + (int16_t)info.box_h > (int16_t)p_lcd->pfb_y_start) &&
                        we_font_get_bitmap_info(font_array, &info, &bitmap, &bpp, &row_stride))
                    {
                        _draw_alpha_mask_row_aligned(p_lcd, draw_x, draw_y, info.box_w, info.box_h,
                                                     bitmap, row_stride, bpp, fg_color, opacity);
                    }
                }
                cursor_x += (int16_t)info.adv_w;
            }
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
    if (str == NULL || font_array == NULL)
        return 0U;

    uint16_t total_width = 0U;
    we_glyph_info_t info;

    while (*str && *str != '\n')
    {
        uint16_t code = 0U;
        uint8_t c = (uint8_t)*str++;

        if (c < 0x80U)
            code = c;
        else if ((c & 0xE0U) == 0xC0U)
        {
            if (*str)
                code = (uint16_t)(((c & 0x1FU) << 6) | (*str++ & 0x3FU));
        }
        else if ((c & 0xF0U) == 0xE0U)
        {
            if (*str && *(str + 1))
            {
                code = (uint16_t)(((c & 0x0FU) << 12) | ((str[0] & 0x3FU) << 6) | (str[1] & 0x3FU));
                str += 2;
            }
        }

        if (we_font_get_glyph_info(font_array, code, &info))
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
    int8_t y_top = 127;
    int8_t y_bot = -128;
    we_glyph_info_t info;

    if (font_array == NULL || str == NULL || out_y_top == NULL || out_y_bottom == NULL)
        return;

    {
        uint16_t line_height = _we_font_get_line_height_impl(font_array);

        while (*str && *str != '\n')
        {
            uint16_t code = 0U;
            uint8_t c = (uint8_t)*str++;

            if (c < 0x80U)
                code = c;
            else if ((c & 0xE0U) == 0xC0U)
            {
                if (*str)
                    code = (uint16_t)(((c & 0x1FU) << 6) | (*str++ & 0x3FU));
            }
            else if ((c & 0xF0U) == 0xE0U)
            {
                if (*str && *(str + 1))
                {
                    code = (uint16_t)(((c & 0x0FU) << 12) | ((str[0] & 0x3FU) << 6) | (str[1] & 0x3FU));
                    str += 2;
                }
            }

            if (we_font_get_glyph_info(font_array, code, &info) && info.box_h > 0)
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
