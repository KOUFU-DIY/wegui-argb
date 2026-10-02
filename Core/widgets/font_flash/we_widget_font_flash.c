
#include "we_widget_font_flash.h"
#include "wegui.h"
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
 * 码点 -> 字形描述
 * -------------------------------------------------------------------------- */

/**
 * @brief 查码点对应的字形并读出其描述项。
 * @param face 已初始化的字体面对象。
 * @param code 待查询的 Unicode 码点。
 * @param out 传出：字形描述（宽高、偏移、位图偏移等）。
 * @return 1 表示找到字形，0 表示该码点未收录。
 * @note 字形序号与描述表范围在 face 初始化时已校验过，这里不再逐字检查。
 *       打开 WE_FLASH_FONT_DESC_CACHE 时先查 face 内的直接映射缓存，命中不读外挂。
 */
static uint8_t _get_glyph(we_flash_font_face_t *face, uint16_t code, _flash_glyph_t *out)
{
    uint32_t index;
    uint32_t addr;
#if (WE_FLASH_FONT_DESC_CACHE > 0)
    we_flash_font_desc_slot_t *slot;
    const uint8_t *buf;
#else
    uint8_t buf[WE_FLASH_FONT_GLYPH_DESC_SIZE];
#endif

    if (!we_font_code_to_index(face->ranges, face->range_count, face->sparse_codes, face->sparse_count, code, &index))
        return 0U;

    addr = face->blob_addr + WE_FLASH_FONT_BLOB_HEADER_SIZE + index * WE_FLASH_FONT_GLYPH_DESC_SIZE;
#if (WE_FLASH_FONT_DESC_CACHE > 0)
    slot = &face->desc_cache[index & (WE_FLASH_FONT_DESC_CACHE - 1U)];
    if (slot->index != index)
    {
        we_gui_storage(face->lcd)(addr, slot->desc, WE_FLASH_FONT_GLYPH_DESC_SIZE);
        slot->index = index;
    }
    buf = slot->desc;
#else
    we_gui_storage(face->lcd)(addr, buf, WE_FLASH_FONT_GLYPH_DESC_SIZE);
#endif

    out->adv_w = _read_u16_be(&buf[0]);
    out->box_w = _read_u16_be(&buf[2]);
    out->box_h = _read_u16_be(&buf[4]);
    out->x_ofs = (int16_t)_read_u16_be(&buf[6]);
    out->y_ofs = (int16_t)_read_u16_be(&buf[8]);
    out->bitmap_offset = _read_u32_be(&buf[10]);
    return 1U;
}

/* --------------------------------------------------------------------------
 * 分块加载字形点阵并直接绘制到当前 PFB
 * -------------------------------------------------------------------------- */

/**
 * @brief 从外部存储读取字形落在当前切片内的那几行并绘制到 PFB。
 * @param face 字体面对象（含 storage 回调与 scratch 缓冲）。
 * @param line_top 本行行框顶边 Y（字形伸到它上方的行不画）。
 * @param draw_x 字形绘制左上角 X 坐标（已包含 x_ofs）。
 * @param draw_y 字形绘制左上角 Y 坐标（已包含 y_ofs）。
 * @param g 字形描述（宽高、位图偏移等）。
 * @param color 字形绘制颜色。
 * @param opacity 控件透明度（绘制原语入口再乘容器级联）。
 */
static void _stream_draw_glyph(we_flash_font_face_t *face, int16_t line_top,
                               int16_t draw_x, int16_t draw_y,
                               const _flash_glyph_t *g, colour_t color, uint8_t opacity)
{
    we_lcd_t *lcd = face->lcd;
    uint32_t row_stride;
    uint16_t rows_per_chunk;
    int16_t row0;
    int16_t row_end;

    if (g->box_w == 0U || g->box_h == 0U)
        return;
    if (draw_x > (int16_t)lcd->clip.x1 || draw_x + (int16_t)g->box_w - 1 < (int16_t)lcd->clip.x0)
        return;

    /* 只读落在切片内的那几行：小切片下不必整字读出再丢弃 */
    row0 = (int16_t)(WE_MAX(line_top, (int16_t)lcd->clip.y0) - draw_y);
    if (row0 < 0)
        row0 = 0;
    row_end = (int16_t)((int16_t)lcd->clip.y1 - draw_y);
    if (row_end > (int16_t)g->box_h - 1)
        row_end = (int16_t)g->box_h - 1;
    if (row0 > row_end)
        return;

    row_stride = ((uint32_t)g->box_w * face->bpp + 7U) >> 3U;
    if (row_stride == 0U || row_stride > face->glyph_buf_size)
        return;
    /* 通常一次就能读完（切片只有几行），装不下时才按缓冲容量分块 */
    rows_per_chunk = (uint16_t)(row_end - row0 + 1);
    if ((uint32_t)rows_per_chunk * row_stride > face->glyph_buf_size)
        rows_per_chunk = (uint16_t)(face->glyph_buf_size / row_stride);

    while (row0 <= row_end)
    {
        uint16_t chunk_rows = (uint16_t)(row_end - row0 + 1);
        uint32_t offset;

        if (chunk_rows > rows_per_chunk)
            chunk_rows = rows_per_chunk;
        offset = face->bitmap_data_offset + g->bitmap_offset + (uint32_t)row0 * row_stride;
        if (offset + row_stride * chunk_rows > face->blob_size)
            return;

        we_gui_storage(lcd)(face->blob_addr + offset, face->glyph_buf, row_stride * chunk_rows);
        we_draw_alpha_mask(lcd, draw_x, (int16_t)(draw_y + row0), g->box_w, chunk_rows,
                           face->glyph_buf, row_stride, face->bpp, color, opacity);
        row0 = (int16_t)(row0 + (int16_t)chunk_rows);
    }
}

/* --------------------------------------------------------------------------
 * 计算文本包围盒
 * -------------------------------------------------------------------------- */

/**
 * @brief 计算文本控件当前文本的包围盒尺寸。
 * @param obj 文本控件实例。
 * @param out_w 输出文本总宽度（像素，各行步进宽度与向右伸出的墨迹取最大）。
 * @param out_h 输出文本总高度（像素，行数 × line_height + 伸出量）。
 * @param out_ink 输出字形伸出行框底边的最大行数（字库行高小于字形框时非 0）。
 */
static void _compute_bbox(we_flash_font_obj_t *obj, int16_t *out_w, int16_t *out_h, uint8_t *out_ink)
{
    we_flash_font_face_t *face = obj->face;
    int32_t max_w = 0;
    int32_t pen = 0;
    int16_t lines = 1;
    int16_t ink = 0;
    const char *p = obj->text;
    _flash_glyph_t g;

    *out_ink = 0U;
    if (p == NULL || *p == 0)
    {
        *out_w = 0;
        *out_h = (int16_t)face->line_height;
        return;
    }

    while (*p)
    {
        uint16_t code;

        if (we_utf8_next(&p, &code) == 0U)
            break;
        if (code == (uint16_t)'\n')
        {
            pen = 0;
            lines++;
            continue;
        }
        if (_get_glyph(face, code, &g))
        {
            if (g.box_w != 0U && g.box_h != 0U)
            {
                int16_t below = (int16_t)(g.y_ofs + (int16_t)g.box_h - (int16_t)face->line_height);
                int32_t right = pen + g.x_ofs + (int32_t)g.box_w;

                if (below > ink)
                    ink = below;
                if (right > max_w)
                    max_w = right; /* 字形墨迹比步进宽时不被包围盒裁掉 */
            }
            pen += g.adv_w;
            if (pen > max_w)
                max_w = pen;
        }
    }

    if (ink > 255)
        ink = 255;

    *out_w = (int16_t)max_w;
    *out_h = (int16_t)(lines * (int16_t)face->line_height + ink);
    *out_ink = (uint8_t)ink;
}

/* --------------------------------------------------------------------------
 * 绘制回调：每次 PFB tile 调用
 * -------------------------------------------------------------------------- */

/**
 * @brief 文本控件绘制回调：按当前 PFB 切片流式解码并绘制字形。
 * @param ptr 回调透传的 we_flash_font_obj_t 指针。
 * @note 行跳过按"行框 + 字形伸出行框底的行数（ink_below，包围盒已计入）"
 *       判断，字形伸出行框的部分在任何切片高度下都画得一致。
 */
static void _font_flash_draw_cb(void *ptr)
{
    we_flash_font_obj_t *obj = (we_flash_font_obj_t *)ptr;
    we_flash_font_face_t *face = obj->face;
    we_lcd_t *lcd = obj->base.lcd;
    int16_t line_h = (int16_t)face->line_height;
    int16_t cursor_x = obj->base.x;
    int16_t cursor_y = obj->base.y;
    const char *p = obj->text;

    /* face 重新初始化失败时 lcd 被清空；容器级联后 <= 5 时什么也画不出：连外挂读取也省掉 */
    if (p == NULL || face->lcd == NULL || we_opa_apply(lcd, obj->opacity) <= 5U)
        return;

    while (*p)
    {
        uint16_t code;
        _flash_glyph_t g;

        if (cursor_y > (int16_t)lcd->clip.y1)
            return;

        /* 本行整体在切片上方，或后面的字已全在切片右侧：跳到下一行 */
        if (cursor_y + line_h + (int16_t)obj->ink_below <= (int16_t)lcd->clip.y0 ||
            cursor_x - (int16_t)face->max_box_width > (int16_t)lcd->clip.x1)
        {
            while (*p && *p != '\n')
                p++;
            if (*p == '\n')
            {
                p++;
                cursor_x = obj->base.x;
                cursor_y += line_h;
            }
            continue;
        }

        if (we_utf8_next(&p, &code) == 0U)
            break;

        if (code == (uint16_t)'\n')
        {
            cursor_x = obj->base.x;
            cursor_y += line_h;
            continue;
        }

        if (_get_glyph(face, code, &g))
        {
            _stream_draw_glyph(face, cursor_y, (int16_t)(cursor_x + g.x_ofs), (int16_t)(cursor_y + g.y_ofs), &g,
                               obj->color, obj->opacity);
            cursor_x += (int16_t)g.adv_w;
        }
    }
}
static const we_class_t _flash_font_class = { .draw_cb = _font_flash_draw_cb, .event_cb = NULL, .set_pos_cb = NULL};

/**
 * @brief 校验字体句柄与 blob 头信息，并填充 face 运行时字段。
 * @param face 待初始化的字体面对象。
 * @param lcd LCD 上下文，端口需提供 storage_read。
 * @param handle font2c 导出的字体句柄与 blob 地址。
 * @return 1 表示校验通过且绑定成功，0 表示任一字段或格式不合法。
 */
static uint8_t _validate_and_bind_face(we_flash_font_face_t *face,
                                       we_lcd_t *lcd,
                                       const font_external_handle_t *handle)
{
    const font_external_t *font;
    uint8_t hdr[WE_FLASH_FONT_BLOB_HEADER_SIZE];
    uint16_t i;
    uint32_t range_total = 0U;

    if (lcd == NULL || handle == NULL || handle->font == NULL)
        return 0U;
    if (we_gui_storage(lcd) == NULL)
        return 0U;
    if (handle->blob_addr > 0xFFFFFFFFu)
        return 0U;

    font = handle->font;
    memset(face, 0, sizeof(*face));

    face->lcd = lcd;
    face->blob_addr = (uint32_t)handle->blob_addr;
    face->blob_size = font->blob_size;
    face->line_height = font->line_height;
    face->bpp = font->bpp;
    face->range_count = font->range_count;
    face->sparse_count = font->sparse_count;
    face->max_box_width = font->max_box_width;
    face->ranges = font->ranges;
    face->sparse_codes = font->sparse;

    if (face->bpp != 1U && face->bpp != 2U && face->bpp != 4U && face->bpp != 8U)
        return 0U;
    if (face->line_height == 0U)
        return 0U;
    if (font->glyph_count == 0U)
        return 0U;
    if (face->range_count > 0U && face->ranges == NULL)
        return 0U;
    if (face->sparse_count > 0U && face->sparse_codes == NULL)
        return 0U;
    if (face->blob_size < WE_FLASH_FONT_BLOB_HEADER_SIZE)
        return 0U;

    we_gui_storage(lcd)(face->blob_addr, hdr, sizeof(hdr));
    if (hdr[0] != 0x02U)
        return 0U;
    if (hdr[1] != 0x00U)
        return 0U;
    if (_read_u32_be(&hdr[2]) != font->blob_crc32)
        return 0U;

    for (i = 0; i < face->range_count; ++i)
    {
        const font_range_t *r = &face->ranges[i];

        if (r->end < r->start)
            return 0U;
        if (i > 0U && r->start <= face->ranges[i - 1U].end)
            return 0U;
        range_total += r->end - r->start + 1U;
    }

    /* 码点查到的字形序号 < 区间总数 + 稀疏数 = glyph_count，描述表整体落在 blob 内：
     * 运行时读描述项不必再逐字检查越界 */
    if ((uint32_t)font->glyph_count != range_total + (uint32_t)face->sparse_count)
        return 0U;

    face->bitmap_data_offset = WE_FLASH_FONT_BLOB_HEADER_SIZE + (uint32_t)font->glyph_count * WE_FLASH_FONT_GLYPH_DESC_SIZE;
    if (face->bitmap_data_offset > face->blob_size)
        return 0U;

#if (WE_FLASH_FONT_DESC_CACHE > 0)
    for (i = 0; i < (uint16_t)WE_FLASH_FONT_DESC_CACHE; ++i)
        face->desc_cache[i].index = 0xFFFFFFFFUL;
#endif
    return 1U;
}

/* ==========================================================================
 * 公开 API — Face（静态缓冲版）
 * ========================================================================== */

/**
 * @brief 初始化字体面对象并绑定字形流式读取缓冲。
 * @param face 待初始化的字体面对象。
 * @param lcd LCD 上下文，端口需已提供 storage_read。
 * @param handle font2c 导出的字体句柄。
 * @param glyph_buf 外部分配的字形 scratch 缓冲区。
 * @param glyph_buf_size scratch 缓冲容量（字节）。
 * @return 1 表示初始化成功，0 表示校验失败或缓冲区不足。
 * @note 失败时 face 被置为不可用（lcd 清空），拿它初始化文本控件会直接失败。
 */
uint8_t we_flash_font_face_init(we_flash_font_face_t *face,
                                we_lcd_t *lcd,
                                const font_external_handle_t *handle,
                                uint8_t *glyph_buf,
                                uint32_t glyph_buf_size)
{
    uint32_t min_row_bytes;

    if (face == NULL)
        return 0U;
    if (!_validate_and_bind_face(face, lcd, handle))
    {
        face->lcd = NULL;
        return 0U;
    }

    min_row_bytes = ((uint32_t)face->max_box_width * face->bpp + 7U) >> 3U;
    if (min_row_bytes > 0U && (glyph_buf == NULL || glyph_buf_size < min_row_bytes))
    {
        face->lcd = NULL;
        return 0U;
    }

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

    memset(obj, 0, sizeof(*obj));
    obj->base.lcd = face->lcd;
    obj->face = face;
    obj->text = text;
    obj->color = color;
    obj->opacity = opacity;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.class_p = &_flash_font_class;

    _compute_bbox(obj, &w, &h, &obj->ink_below);
    obj->base.w = w;
    obj->base.h = h;

    we_obj_attach_to_lcd(face->lcd, (we_obj_t *)obj);

    if (opacity > 0U && w > 0 && h > 0)
        we_obj_invalidate((we_obj_t *)obj);

    return 1U;
}

/**
 * @brief 更新文本内容并重算包围盒后触发重绘。
 * @param obj 文本控件实例。
 * @param text 新的 UTF-8 文本。
 * @note 不比较指针：同一缓冲区原地改写内容后再次调用也会重新测量并重绘。
 */
void we_flash_font_obj_set_text(we_flash_font_obj_t *obj, const char *text)
{
    int16_t w = 0;
    int16_t h = 0;

    if (obj == NULL || obj->face == NULL || obj->face->lcd == NULL)
        return;

    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj); /* 旧区域 */

    obj->text = text;
    _compute_bbox(obj, &w, &h, &obj->ink_below);
    obj->base.w = w;
    obj->base.h = h;

    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj); /* 新区域 */
}

/**
 * @brief 设置文本颜色；颜色发生变化时使控件失效重绘。
 * @param obj 文本控件实例。
 * @param color 新文本颜色。
 */
void we_flash_font_obj_set_color(we_flash_font_obj_t *obj, colour_t color)
{
    if (obj == NULL || we_colour_eq(obj->color, color))
        return;

    obj->color = color;
    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置文本控件透明度并重绘。
 * @param obj 文本控件实例。
 * @param opacity 新透明度（0~255）。
 * @note 透明度不改变几何范围，只刷当前包围盒，不必重新测量文字。
 */
void we_flash_font_obj_set_opacity(we_flash_font_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;

    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}
