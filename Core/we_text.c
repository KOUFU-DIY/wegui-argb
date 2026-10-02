#include "we_internal.h"

/**
 * @brief 字库数组指针转成 font2c internal 描述
 * @param font_array 传入：字库数组指针（font2c 生成的 font_internal_t 常量的地址）
 * @return 字库描述指针
 * @note 字库对象本身按结构体对齐生成，经 void* 转换即可，不存在对齐问题
 */
static __inline const font_internal_t *_we_font(const unsigned char *font_array)
{
    return (const font_internal_t *)(const void *)font_array;
}

/**
 * @brief 读取 UTF-8 字符串的下一个码点并推进指针（1~3 字节编码）
 * @param pp 传入传出：当前解析位置
 * @param out_code 传出：码点；非法字节得 0
 * @return 1 = 读到一个码点；0 = 字符串结束或多字节序列被截断
 */
uint8_t we_utf8_next(const char **pp, uint16_t *out_code)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint8_t c0;

    if (p == NULL || *p == 0U)
        return 0U;

    c0 = *p++;
    *out_code = 0U;

    if (c0 < 0x80U)
    {
        *out_code = c0;
    }
    else if ((c0 & 0xE0U) == 0xC0U)
    {
        if (*p == 0U)
            return 0U; /* 截断的双字节序列 */
        *out_code = (uint16_t)(((c0 & 0x1FU) << 6) | ((uint8_t)*p++ & 0x3FU));
    }
    else if ((c0 & 0xF0U) == 0xE0U)
    {
        if (p[0] == 0U || p[1] == 0U)
            return 0U; /* 截断的三字节序列 */
        *out_code = (uint16_t)(((c0 & 0x0FU) << 12) |
                               (((uint8_t)p[0] & 0x3FU) << 6) |
                               ((uint8_t)p[1] & 0x3FU));
        p += 2;
    }
    /* 其余首字节（含 4 字节编码）按非法单字节处理，code 保持 0 */
    *pp = (const char *)p;
    return 1U;
}
/**
 * @brief 获取字库行高的内部实现
 * @param font_array 传入：字库数组指针（仅支持 font2c internal 格式）
 * @return 行高（像素），失败返回 0
 */
static uint16_t _we_font_get_line_height_impl(const unsigned char *font_array)
{
    const font_internal_t *font = _we_font(font_array);

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
uint8_t we_font_code_to_index(const font_range_t *ranges, uint16_t range_count,
                              const uint32_t *sparse, uint16_t sparse_count,
                              uint32_t code, uint32_t *out_index)
{
    uint16_t i;
    uint32_t glyph_index = 0U;

    for (i = 0; i < range_count; ++i)
    {
        const font_range_t *r = &ranges[i];

        if (code >= r->start && code <= r->end)
        {
            *out_index = glyph_index + (code - r->start);
            return 1U;
        }
        glyph_index += r->end - r->start + 1U;
    }

    /* 走完全部区间后 glyph_index 即区间字形总数，稀疏码点的序号接在后面 */
    if (sparse != NULL && sparse_count > 0U)
    {
        int32_t lo = 0;
        int32_t hi = (int32_t)sparse_count - 1;
        while (lo <= hi)
        {
            int32_t mid = lo + ((hi - lo) >> 1);
            uint32_t v = sparse[mid];
            if (v == code)
            {
                *out_index = glyph_index + (uint32_t)mid;
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

static uint8_t _we_font_get_glyph_info_internal(const font_internal_t *font, uint16_t code, we_glyph_info_t *out_info)
{
    uint32_t glyph_index;
    const font_glyph_desc_t *desc;

    if (font == NULL || out_info == NULL || font->glyph_desc == NULL)
        return 0U;
    if (!we_font_code_to_index(font->ranges, font->range_count, font->sparse, font->sparse_count, code, &glyph_index) ||
        glyph_index >= font->glyph_count)
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

    return _we_font_get_glyph_info_internal(_we_font(font_array), code, out_info);
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

    font = _we_font(font_array);
    if (font == NULL || font->bitmap_data == NULL)
        return 0U;

    *out_bpp = font->bpp;
    *out_row_stride = (((uint32_t)info->box_w * (uint32_t)(*out_bpp)) + 7U) >> 3U;
    *out_bitmap = font->bitmap_data + info->offset;
    return 1U;
}

/**
 * @brief 绘制 UTF-8 文本的共用实现
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：起始绘制 X 坐标
 * @param y 传入：起始绘制 Y 坐标
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 文本起点
 * @param end 传入：结束位置（不含），NULL = 画到字符串结尾
 * @param fg_color 传入：前景颜色
 * @param opacity 传入：整体透明度
 * @return 无
 */
static void _we_draw_text(we_lcd_t *p_lcd, int16_t x, int16_t y, const unsigned char *font_array,
                          const char *str, const char *end, colour_t fg_color, uint8_t opacity)
{
    const font_internal_t *font;
    uint16_t line_height;
    we_glyph_info_t info;
    int16_t cursor_x;
    int16_t cursor_y;

    if (p_lcd == NULL || str == NULL || font_array == NULL)
        return;
    opacity = we_opa_apply(p_lcd, opacity); /* 容器透明度级联 */
    if (opacity == 0U)
        return;

    font = _we_font(font_array);
    if (font == NULL || font->bitmap_data == NULL)
        return;

    line_height = font->line_height;
    cursor_x = x;
    cursor_y = y;

    /* 行跳过的判定范围 = 行框 + 字形最多伸出行框底的行数。
     * font2c 的行高不保证包住所有字形框（y_ofs + box_h 可大于行高），只按
     * 行框判断会漏画伸出的那几行，而且漏不漏取决于切片边界落在哪；这里
     * 用 max_box_height 放宽（字形框顶不会低于本行行框底），伸出部分在
     * 任何切片高度下都画得一致。字形伸到行框顶以上的部分不画。 */
    while (*str && (end == NULL || str < end))
    {
        if (cursor_y > p_lcd->clip.y1)
            return;

        if (cursor_y + (int16_t)line_height + (int16_t)font->max_box_height <= p_lcd->clip.y0)
        {
            while (*str && *str != '\n' && (end == NULL || str < end))
                str++;
            if (*str == '\n' && (end == NULL || str < end))
            {
                cursor_x = x;
                cursor_y += (int16_t)line_height;
                str++;
            }
            continue;
        }

        uint16_t code = 0U;
        if (we_utf8_next(&str, &code) == 0U)
            break;

        if (code == (uint16_t)'\n')
        {
            cursor_x = x;
            cursor_y += (int16_t)line_height;
            continue;
        }

        if (cursor_x > p_lcd->clip.x1)
            continue;

        if (_we_font_get_glyph_info_internal(font, code, &info))
        {
            if (info.box_w > 0U && info.box_h > 0U)
            {
                int16_t draw_x = cursor_x + info.x_ofs;
                int16_t draw_y = cursor_y + info.y_ofs;
                uint16_t box_h = info.box_h;
                const uint8_t *bitmap = font->bitmap_data + info.offset;
                uint32_t row_stride = (((uint32_t)info.box_w * (uint32_t)font->bpp) + 7U) >> 3U;

                if (draw_y < cursor_y) /* 伸到行框顶以上的行不画 */
                {
                    uint16_t cut = (uint16_t)(cursor_y - draw_y);

                    box_h = (cut < box_h) ? (uint16_t)(box_h - cut) : 0U;
                    bitmap += (uint32_t)cut * row_stride;
                    draw_y = cursor_y;
                }
                if (box_h != 0U &&
                    draw_x <= (int16_t)p_lcd->clip.x1 &&
                    draw_y <= (int16_t)p_lcd->clip.y1 &&
                    (draw_x + (int16_t)info.box_w > (int16_t)p_lcd->clip.x0) &&
                    (draw_y + (int16_t)box_h > (int16_t)p_lcd->clip.y0))
                {
                    _we_draw_alpha_mask_raw(p_lcd, draw_x, draw_y, info.box_w, box_h,
                                            bitmap, row_stride, font->bpp, fg_color, opacity);
                }
            }
            cursor_x += (int16_t)info.adv_w;
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
    _we_draw_text(p_lcd, x, y, font_array, str, NULL, fg_color, opacity);
}

/**
 * @brief 绘制字符串的前 len 个字节（折行排版逐行绘制用，不必拷贝行缓冲）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：起始绘制 X 坐标
 * @param y 传入：起始绘制 Y 坐标
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @param len 传入：绘制的字节数（应落在字符边界上；遇到字符串结尾提前停止）
 * @param fg_color 传入：前景颜色
 * @param opacity 传入：整体透明度
 * @return 无
 */
void we_draw_string_n(we_lcd_t *p_lcd, int16_t x, int16_t y, const unsigned char *font_array, const char *str,
                      uint16_t len, colour_t fg_color, uint8_t opacity)
{
    if (str != NULL)
        _we_draw_text(p_lcd, x, y, font_array, str, str + len, fg_color, opacity);
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

    font = _we_font(font_array);
    if (font == NULL)
        return 0U;

    while (*str && *str != '\n')
    {
        uint16_t code = 0U;
        if (we_utf8_next(&str, &code) == 0U)
            break;

        if (_we_font_get_glyph_info_internal(font, code, &info))
            total_width = (uint16_t)(total_width + info.adv_w);
    }
    return total_width;
}

uint16_t we_get_text_ink_width(const unsigned char *font_array, const char *str)
{
    const font_internal_t *font;
    we_glyph_info_t info;
    int32_t pen = 0;
    int32_t right = 0;

    if (str == NULL || font_array == NULL)
        return 0U;

    font = _we_font(font_array);
    if (font == NULL)
        return 0U;

    while (*str && *str != '\n')
    {
        uint16_t code = 0U;
        if (we_utf8_next(&str, &code) == 0U)
            break;

        if (_we_font_get_glyph_info_internal(font, code, &info))
        {
            if (info.box_w != 0U && pen + info.x_ofs + (int32_t)info.box_w > right)
                right = pen + info.x_ofs + (int32_t)info.box_w;
            pen += info.adv_w;
        }
    }
    return (uint16_t)((pen > right) ? pen : right);
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

    font = _we_font(font_array);
    if (font == NULL)
        return;

    {
        uint16_t line_height = font->line_height;

        while (*str && *str != '\n')
        {
            uint16_t code = 0U;
            if (we_utf8_next(&str, &code) == 0U)
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
