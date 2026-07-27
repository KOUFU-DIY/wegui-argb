/**
 * @file  we_widget_mlabel.c
 * @brief 多行文本标签控件（preview）：固定文本框内自动折行 + 断词 + 省略号截断
 *
 * 折行在 draw 时流式执行（不缓存行表）：
 *   1. 逐字符 UTF-8 解码（与 we_font_text.c 同口径的局部 helper），
 *      显式 '\n' 强制换行；
 *   2. 逐字符用 we_font_get_glyph_info 的 adv_w 累计行宽（找不到字形跳过）；
 *   3. 英文按空格断词：记录最近空格断点，行宽超出 w 时回退到该空格；
 *      行内无空格（长单词 / 中文）则按字符断行；
 *   4. 完整放不进 h 的行不画；ellipsis 开启时最后一个可容纳行若还有
 *      剩余文本，行末预留 "..." 宽度截断绘制。
 * 每行经内部栈缓冲拷贝出 nul 结尾片段后交给 we_draw_string；绘制期间
 * 把 PFB 窗口收窄到自身矩形（marquee/group 同款），越界墨迹自动被裁掉。
 */

#include "we_widget_mlabel.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _mlabel_draw_cb(void *ptr);
static uint8_t _mlabel_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _mlabel_class = {
    .draw_cb    = _mlabel_draw_cb,
    .event_cb   = _mlabel_event_cb,
    .set_pos_cb = NULL /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/* UTF-8 解码状态（与 we_font_text.c 的 _utf8_decode_next_u16 同口径） */
#define _MLABEL_U8_END       0U /* 字符串结束 */
#define _MLABEL_U8_OK        1U /* 成功读取 1 个码点（非法单字节 code 置 0） */
#define _MLABEL_U8_TRUNCATED 2U /* 截断的多字节序列 */

/**
 * @brief 读取 UTF-8 字符串的下一个码点并推进指针（局部实现，只读不依赖核心私有函数）。
 * @param pp 传入传出：当前解析位置；成功后移动到下一个字符起始处。
 * @param out_code 传出：解析得到的码点，非法单字节时返回 0。
 * @return _MLABEL_U8_END / _MLABEL_U8_OK / _MLABEL_U8_TRUNCATED。
 * @note 支持 1/2/3 字节编码；4 字节及以上按非法单字节处理（code 置 0，
 *       后续 continuation 字节同样各自按非法单字节消费），与核心行为一致。
 */
static uint8_t _mlabel_utf8_next(const char **pp, uint16_t *out_code)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint8_t c0;

    if (p == NULL || *p == 0U)
        return _MLABEL_U8_END;

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
            return _MLABEL_U8_TRUNCATED;
        }
        *out_code = (uint16_t)(((c0 & 0x1FU) << 6) | ((uint8_t)*p++ & 0x3FU));
    }
    else if ((c0 & 0xF0U) == 0xE0U)
    {
        if (p[0] == 0U || p[1] == 0U)
        {
            *pp = (const char *)p;
            return _MLABEL_U8_TRUNCATED;
        }
        *out_code = (uint16_t)(((c0 & 0x0FU) << 12) |
                               (((uint8_t)p[0] & 0x3FU) << 6) |
                               ((uint8_t)p[1] & 0x3FU));
        p += 2;
    }
    /* 4 字节及以上编码：按非法单字节处理，code 保持 0 */
    *pp = (const char *)p;
    return _MLABEL_U8_OK;
}

/**
 * @brief 查询单个码点的步进宽（找不到字形返回 0，调用方按跳过处理）。
 * @param font 字库指针。
 * @param code Unicode 码点。
 * @return 步进宽 adv_w（像素）。
 */
static uint16_t _mlabel_adv_w(const unsigned char *font, uint16_t code)
{
    we_glyph_info_t info;

    if (we_font_get_glyph_info(font, code, &info))
        return info.adv_w;
    return 0U;
}

/* 单行扫描结果 */
typedef struct
{
    uint32_t bytes;   /* 本行可绘制内容的字节数（不含断点空格 / '\n'） */
    int32_t  width;   /* 本行像素宽（adv_w 累计） */
    const char *next; /* 下一行起点；NULL 表示文本到此结束 */
} _mlabel_line_t;

/**
 * @brief 从 start 起流式扫描出一行：确定断点、行字节数与行像素宽。
 * @param obj 控件对象指针（取 w 与字库）。
 * @param start 本行起点（UTF-8 字符边界）。
 * @param out 传出：单行扫描结果。
 * @return 无。
 * @note 断行规则：
 *       1. '\n' 强制换行（换行符本身不计入行内容）；
 *       2. 行宽将超出 w 且行内已有内容时——有空格断点则回退到空格
 *          （该空格被丢弃），否则当前字符整体归下一行（字符断行）；
 *       3. 首字符步进宽即超 w 时仍收进本行（独占一行，防死循环）；
 *       4. 行字节数超过栈缓冲护栏时按字符断行（280px 屏实际达不到）。
 */
static void _mlabel_scan_line(const we_mlabel_obj_t *obj, const char *start,
                              _mlabel_line_t *out)
{
    const char *p = start;
    const char *sp_resume = NULL; /* 最近空格断点：下一行续点（空格后一字节） */
    uint32_t sp_bytes = 0U;       /* 空格断点处的行内容字节数（不含空格） */
    int32_t  sp_width = 0;        /* 空格断点处的行像素宽（不含空格） */
    uint32_t bytes = 0U;
    int32_t  width = 0;

    out->next = NULL;

    for (;;)
    {
        const char *prev = p;
        uint16_t code = 0U;
        uint8_t st = _mlabel_utf8_next(&p, &code);
        uint16_t adv;

        if (st != _MLABEL_U8_OK)
            break; /* 文本结束或截断序列：本行到此为止，next 保持 NULL */

        if (code == (uint16_t)'\n')
        {
            out->next = p; /* 显式换行：跳过 '\n' 本身 */
            break;
        }

        adv = _mlabel_adv_w(obj->font, code);

        /* 行宽溢出判定：仅当行内已有内容时触发，保证超宽单字符可独占一行 */
        if (adv > 0U && bytes > 0U && (width + (int32_t)adv) > (int32_t)obj->base.w)
        {
            if (sp_resume != NULL)
            {
                bytes = sp_bytes; /* 回退到最近空格断词，空格本身丢弃 */
                width = sp_width;
                out->next = sp_resume;
            }
            else
            {
                out->next = prev; /* 无空格可退：按字符断行，当前字符归下一行 */
            }
            break;
        }

        /* 栈缓冲护栏：给 "..." 与 '\0' 预留 4 字节 */
        if (bytes + (uint32_t)(p - prev) > (uint32_t)(WE_MLABEL_LINE_BUF - 4U))
        {
            out->next = prev;
            break;
        }

        bytes += (uint32_t)(p - prev);
        width += (int32_t)adv;

        if (code == (uint16_t)' ')
        {
            sp_bytes  = bytes - 1U;         /* ' ' 恒为 1 字节 */
            sp_width  = width - (int32_t)adv;
            sp_resume = p;
        }
    }

    out->bytes = bytes;
    out->width = width;
}

/**
 * @brief 求行内容在给定像素宽内能容纳的最大前缀（省略号截断用）。
 * @param obj 控件对象指针（取字库）。
 * @param start 行内容起点。
 * @param line_bytes 行内容总字节数（保证结束于字符边界）。
 * @param avail_w 可用像素宽（已减去 "..." 宽度）。
 * @param out_w 传出：前缀实际像素宽。
 * @return 前缀字节数。
 */
static uint32_t _mlabel_prefix_fit(const we_mlabel_obj_t *obj, const char *start,
                                   uint32_t line_bytes, int32_t avail_w, int32_t *out_w)
{
    const char *p = start;
    const char *line_end = start + line_bytes;
    uint32_t bytes = 0U;
    int32_t  width = 0;

    while (p < line_end)
    {
        const char *prev = p;
        uint16_t code = 0U;
        uint8_t st = _mlabel_utf8_next(&p, &code);
        uint16_t adv;

        if (st != _MLABEL_U8_OK)
            break;

        adv = _mlabel_adv_w(obj->font, code);
        if ((width + (int32_t)adv) > avail_w)
            break;

        bytes += (uint32_t)(p - prev);
        width += (int32_t)adv;
    }

    *out_w = width;
    return bytes;
}

/**
 * @brief 颜色相等比较（setter 幂等判断用）。
 * @param a 颜色 A。
 * @param b 颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _mlabel_col_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#else
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/* --------------------------------------------------------------------------
 * 绘制 / 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：收窄 PFB 窗口后流式折行并逐行绘制。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _mlabel_draw_cb(void *ptr)
{
    we_mlabel_obj_t *obj = (we_mlabel_obj_t *)ptr;
    we_lcd_t *lcd;
    uint16_t font_lh;

    if (obj == NULL || obj->text == NULL || obj->font == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    font_lh = we_font_get_line_height(obj->font);
    if (font_lh == 0U || obj->base.w <= 0 || obj->base.h <= 0)
        return;

    {
        /* marquee/group 同款 PFB 窗口收窄：窗口外的字形墨迹被自动裁掉 */
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;

        int16_t new_x0 = WE_MAX((int16_t)old_pfb_area.x0, obj->base.x);
        int16_t new_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
        int16_t new_x1 = WE_MIN((int16_t)old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
        int16_t new_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

        if (new_x0 <= new_x1 && new_y0 <= new_y1)
        {
            int16_t line_adv = (int16_t)(font_lh + obj->line_gap);
            int16_t y_end    = (int16_t)(obj->base.y + obj->base.h);
            int16_t line_y   = obj->base.y;
            const char *p    = obj->text;
            char buf[WE_MLABEL_LINE_BUF];
            int32_t ell_w;

            lcd->pfb_area.x0 = (uint16_t)new_x0;
            lcd->pfb_area.x1 = (uint16_t)new_x1;
            lcd->pfb_y_start = (uint16_t)new_y0;
            lcd->pfb_y_end   = (uint16_t)new_y1;
            lcd->pfb_gram    = old_gram + (new_y0 - (int16_t)old_y_start) * lcd->pfb_width +
                               (new_x0 - (int16_t)old_pfb_area.x0);

            ell_w = 3 * (int32_t)_mlabel_adv_w(obj->font, (uint16_t)'.');

            while (p != NULL && *p != '\0')
            {
                _mlabel_line_t line;
                int32_t draw_w;
                int16_t line_x;
                uint8_t remaining;

                if ((int16_t)(line_y + (int16_t)font_lh) > y_end)
                    break; /* 本行完整高度已放不下：不画，排版结束 */

                _mlabel_scan_line(obj, p, &line);
                remaining = (line.next != NULL && *line.next != '\0') ? 1U : 0U;

                if (obj->ellipsis != 0U && remaining != 0U &&
                    (int16_t)(line_y + line_adv + (int16_t)font_lh) > y_end)
                {
                    /* 最后一个可容纳行且还有剩余文本：预留 "..." 宽度截断 */
                    int32_t prefix_w = 0;
                    uint32_t prefix_bytes = _mlabel_prefix_fit(obj, p, line.bytes,
                                                               (int32_t)obj->base.w - ell_w,
                                                               &prefix_w);

                    memcpy(buf, p, prefix_bytes);
                    buf[prefix_bytes]      = '.';
                    buf[prefix_bytes + 1U] = '.';
                    buf[prefix_bytes + 2U] = '.';
                    buf[prefix_bytes + 3U] = '\0';
                    draw_w = prefix_w + ell_w;

                    line_x = (obj->align == WE_MLABEL_CENTER)
                             ? (int16_t)(obj->base.x + ((int32_t)obj->base.w - draw_w) / 2)
                             : obj->base.x;
                    we_draw_string(lcd, line_x, line_y, obj->font, buf,
                                   obj->color, obj->opacity);
                    break; /* 省略号行必为最后一行 */
                }

                memcpy(buf, p, line.bytes);
                buf[line.bytes] = '\0';
                draw_w = line.width;

                line_x = (obj->align == WE_MLABEL_CENTER)
                         ? (int16_t)(obj->base.x + ((int32_t)obj->base.w - draw_w) / 2)
                         : obj->base.x;
                we_draw_string(lcd, line_x, line_y, obj->font, buf,
                               obj->color, obj->opacity);

                line_y = (int16_t)(line_y + line_adv);
                p = line.next;
            }
        }

        lcd->pfb_area  = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram  = old_gram;
    }
}

/**
 * @brief 控件事件回调：装饰性控件，不消费事件，输入穿透给背后控件。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒为 0（穿透）。
 */
static uint8_t _mlabel_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_mlabel_obj_init(we_mlabel_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        const char *text, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = w;
    obj->base.h       = h;
    obj->base.class_p = &_mlabel_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    obj->text = text;
    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;
    {
        colour_t c = RGB888_CONST(220, 226, 235);
        obj->color = c;
    }
    obj->opacity  = 255U;
    obj->align    = WE_MLABEL_LEFT;
    obj->line_gap = (uint8_t)WE_MLABEL_DEF_LINE_GAP;
    obj->ellipsis = 1U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mlabel_set_text(we_mlabel_obj_t *obj, const char *new_text)
{
    if (obj == NULL || new_text == NULL)
        return;

    /* 不做指针相等短路：调用方可能在原缓冲区内改写内容后重新 set。
     * 折行为 draw 时流式执行，这里只需换指针并整框标脏。 */
    obj->text = new_text;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mlabel_set_color(we_mlabel_obj_t *obj, colour_t color)
{
    if (obj == NULL)
        return;
    if (_mlabel_col_eq(obj->color, color))
        return;

    obj->color = color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mlabel_set_opacity(we_mlabel_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;

    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mlabel_set_align(we_mlabel_obj_t *obj, uint8_t align)
{
    if (obj == NULL)
        return;
    align = (align == WE_MLABEL_CENTER) ? WE_MLABEL_CENTER : WE_MLABEL_LEFT;
    if (obj->align == align)
        return;

    obj->align = align;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mlabel_set_line_gap(we_mlabel_obj_t *obj, uint8_t gap_px)
{
    if (obj == NULL || obj->line_gap == gap_px)
        return;

    obj->line_gap = gap_px;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mlabel_set_ellipsis(we_mlabel_obj_t *obj, uint8_t on)
{
    if (obj == NULL)
        return;
    on = (on != 0U) ? 1U : 0U;
    if (obj->ellipsis == on)
        return;

    obj->ellipsis = on;
    we_obj_invalidate((we_obj_t *)obj);
}
