#include "we_widget_label_ex.h"
#include "we_font_text.h"
#include "we_render.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * 定点精度：与 img_ex 保持一致，使用 Q12（FP_BITS = 12）
 * -------------------------------------------------------------------------- */
#define FP_BITS 12
#define FP_ONE  (1 << FP_BITS)

/* --------------------------------------------------------------------------
 * 字形预扫描缓存：把版面位置、位图地址、bpp 等一次性算好，
 * 内层逐像素循环直接查表，彻底干掉每像素的 UTF-8 解码与字库接口调用。
 * 单条目 16 字节（按 4 字节对齐自然填满）。
 * 缓存容量由 WE_CFG_LABEL_EX_MAX_GLYPHS 决定（见 we_widget_label_ex.h）。
 * -------------------------------------------------------------------------- */
typedef struct
{
    const uint8_t *bitmap;   /* 已解析好的字形位图首地址 */
    uint16_t       row_stride; /* 字形位图每行字节数 */
    int16_t        draw_x;   /* 字形左上角相对文字基线的 X */
    int16_t        draw_y;   /* 字形左上角相对文字基线的 Y */
    uint16_t       box_w;    /* 字形宽度（像素） */
    uint16_t       box_h;    /* 字形高度（像素） */
    uint8_t        bpp;      /* 字形位深（1/2/4/8 bpp） */
    uint8_t        alpha_mask; /* (1<<bpp)-1，预算好省一次移位 */
} _glyph_cache_t;

/* --------------------------------------------------------------------------
 * 测量多行文本中最宽一行的像素宽度（用于包围盒，不渲染）
 * -------------------------------------------------------------------------- */

/**
 * @brief 统计多行文本中最长一行的像素宽度。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @return 最长行宽度（像素）。
 */
static uint16_t _measure_text_w(const unsigned char *font, const char *text)
{
    uint16_t max_w = 0;
    const char *p  = text;
    while (*p)
    {
uint16_t w = we_get_text_width(font, p);
        if (w > max_w)
            max_w = w;
        while (*p && *p != '\n')
            p++;
        if (*p == '\n')
            p++;
    }
    return max_w;
}

/* --------------------------------------------------------------------------
 * 预扫描整段文字，把每个字形的版面信息和位图地址解析到栈数组里。
 * 之后整帧逐像素采样都直接查表，不再触碰 UTF-8 解码 / 字库 flash 查表。
 * -------------------------------------------------------------------------- */

/**
 * @brief 解析整段文字到字形缓存数组。
 * @param obj 目标控件对象指针（提供 font/text）。
 * @param cache 传出：字形缓存数组（由调用方在栈上分配）。
 * @param cap cache 容量（数组长度上限）。
 * @return 实际写入的字形数（≤cap，多余字符被截断）。
 */
static uint16_t _build_glyph_cache(const we_label_ex_obj_t *obj,
                                   _glyph_cache_t *cache, uint16_t cap)
{
    const unsigned char *font_array = obj->font;
    const char          *str        = obj->text;
    uint16_t             count      = 0U;
    int16_t              cursor_x   = 0;
    we_glyph_info_t      info;

    while (*str && count < cap)
    {
        uint16_t code = 0U;
        uint8_t  c    = (uint8_t)*str++;

        /* UTF-8 解码（与 we_draw_string 保持一致） */
        if (c < 0x80U)
        {
            code = c;
        }
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
                code = (uint16_t)(((c & 0x0FU) << 12) |
                                  (((uint8_t)str[0] & 0x3FU) << 6) |
                                   ((uint8_t)str[1] & 0x3FU));
                str += 2;
            }
            else
                break;
        }

        /* label_ex 仅支持单行旋转，遇到换行直接收尾 */
        if (code == (uint16_t)'\n')
            break;

        if (!we_font_get_glyph_info(font_array, code, &info))
            continue;

        if (info.box_w == 0U || info.box_h == 0U)
        {
            cursor_x += info.adv_w;
            continue;
        }

        {
            const uint8_t *bitmap     = NULL;
            uint8_t        bpp        = 0U;
            uint32_t       row_stride = 0U;

            if (we_font_get_bitmap_info(font_array, &info, &bitmap, &bpp, &row_stride) && bitmap != NULL)
            {
                cache[count].bitmap     = bitmap;
                cache[count].row_stride = (uint16_t)row_stride;
                cache[count].draw_x     = cursor_x + info.x_ofs;
                cache[count].draw_y     = info.y_ofs;
                cache[count].box_w      = info.box_w;
                cache[count].box_h      = info.box_h;
                cache[count].bpp        = bpp;
                cache[count].alpha_mask = (uint8_t)((1U << bpp) - 1U);
                count++;
            }
        }

        cursor_x += info.adv_w;
    }
    return count;
}

/**
 * @brief 从字形缓存查询文字局部坐标 (tx, ty) 的 alpha 值。
 * @param cache 字形缓存数组（来自 _build_glyph_cache）。
 * @param count 字形缓存有效条目数。
 * @param tx 文本局部 X 坐标。
 * @param ty 文本局部 Y 坐标。
 * @return 字形覆盖 alpha（0~255）。
 */
static __inline uint8_t _alpha_from_cache(const _glyph_cache_t *cache, uint16_t count,
                                          int32_t tx, int32_t ty)
{
    for (uint16_t i = 0U; i < count; i++)
    {
        const _glyph_cache_t *g = &cache[i];

        /* 字形按 cursor 顺序排布，后续 draw_x 只会更靠右，可以提前退出 */
        if (tx < (int32_t)g->draw_x)
            break;
        if (tx >= (int32_t)(g->draw_x + (int16_t)g->box_w))
            continue;
        if (ty < (int32_t)g->draw_y || ty >= (int32_t)(g->draw_y + (int16_t)g->box_h))
            continue;

        {
            uint32_t px       = (uint32_t)(tx - (int32_t)g->draw_x);
            uint32_t py       = (uint32_t)(ty - (int32_t)g->draw_y);
            uint32_t bit_pos  = py * (uint32_t)g->row_stride * 8U + px * (uint32_t)g->bpp;
            uint8_t  shift    = (uint8_t)(8U - g->bpp - (uint8_t)(bit_pos & 7U));
            uint8_t  a_raw    = (g->bitmap[bit_pos >> 3U] >> shift) & g->alpha_mask;

            switch (g->bpp)
            {
            case 8U:  return a_raw;
            case 4U:  return (uint8_t)((a_raw << 4) | a_raw);
            case 2U:  return (uint8_t)(a_raw * 85U);
            default:  return a_raw ? 255U : 0U;
            }
        }
    }
    return 0U;
}

/* --------------------------------------------------------------------------
 * 计算旋转缩放后的包围盒（相对变换中心的偏移），逻辑同 img_ex。
 * -------------------------------------------------------------------------- */

/**
 * @brief 内部辅助：calc_label_ex_bbox。
 * @param text_w 文本字符串。
 * @param line_h 单行文本高度（像素）。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x）。
 * @param out_x X 方向坐标或偏移值。
 * @param out_y Y 方向坐标或偏移值。
 * @param out_w 输出包围盒宽度。
 * @param out_h 输出包围盒高度。
 * @return 无。
 */
static void _calc_label_ex_bbox(int16_t text_w, int16_t line_h,
                                int16_t angle, uint16_t scale_256,
                                int16_t *out_x, int16_t *out_y,
                                int16_t *out_w, int16_t *out_h)
{
int32_t cos_a = we_cos(angle);
int32_t sin_a = we_sin(angle);
    int32_t hw    = text_w / 2;
    int32_t hh    = line_h / 2;
    int32_t pts[4][2];
    int32_t min_x;
    int32_t max_x;
    int32_t min_y;
    int32_t max_y;
    int32_t rx;
    int32_t ry;
    uint8_t i;

    pts[0][0] = -hw; pts[0][1] = -hh;
    pts[1][0] =  hw; pts[1][1] = -hh;
    pts[2][0] = -hw; pts[2][1] =  hh;
    pts[3][0] =  hw; pts[3][1] =  hh;

    min_x =  0x7FFFFFFF;
    max_x = -0x7FFFFFFF;
    min_y =  0x7FFFFFFF;
    max_y = -0x7FFFFFFF;

    for (i = 0U; i < 4U; i++)
    {
        rx = (pts[i][0] * cos_a - pts[i][1] * sin_a) >> 15;
        ry = (pts[i][0] * sin_a + pts[i][1] * cos_a) >> 15;
        rx = (rx * (int32_t)scale_256) >> 8;
        ry = (ry * (int32_t)scale_256) >> 8;

        if (rx < min_x) min_x = rx;
        if (rx > max_x) max_x = rx;
        if (ry < min_y) min_y = ry;
        if (ry > max_y) max_y = ry;
    }

    /* 边缘留 1px 冗余，防止插值/裁剪时出现缺口 */
    *out_x = (int16_t)(min_x - 1);
    *out_y = (int16_t)(min_y - 1);
    *out_w = (int16_t)((max_x - min_x) + 3);
    *out_h = (int16_t)((max_y - min_y) + 3);
}

/* --------------------------------------------------------------------------
 * 根据 cx/cy/angle/scale/text_w 统一更新包围盒。
 * -------------------------------------------------------------------------- */

/**
 * @brief 内部辅助：label_ex_update_bbox。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _label_ex_update_bbox(we_label_ex_obj_t *obj)
{
    int16_t bx;
    int16_t by;
    int16_t bw;
    int16_t bh;
    int16_t line_h = (obj->font != NULL) ? (int16_t)we_font_get_line_height(obj->font) : 16;

    _calc_label_ex_bbox((int16_t)obj->text_w, line_h,
                        obj->angle, obj->scale_256,
                        &bx, &by, &bw, &bh);

    obj->base.x = obj->cx + bx;
    obj->base.y = obj->cy + by;
    obj->base.w = bw;
    obj->base.h = bh;
}

/* --------------------------------------------------------------------------
 * 旋转缩放文字绘制回调
 *
 * 工作方式：
 *   1. 入口一次性把字符串解析到 _glyph_cache_t cache[]（栈数组），
 *      之后整帧渲染都直接查表，零 UTF-8 解码、零字库 flash 接口调用。
 *   2. 对包围盒内每个屏幕像素做逆变换，得到文字局部坐标 (tx, ty)，
 *      查表获取 alpha，与前景色混色写回 PFB。
 *   3. PFB 目标地址用指针递增维护，避免每像素 row*stride+col 重算。
 *   4. /255 软除法替换为 we_div255 近似，省 M0 ~90 cycle/像素。
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _label_ex_draw_cb(void *ptr)
{
    we_label_ex_obj_t *obj     = (we_label_ex_obj_t *)ptr;
    we_lcd_t          *p_lcd   = obj->base.lcd;
    int32_t            cos_a;
    int32_t            sin_a;
    int32_t            inv_scale_q16;
    int32_t            step_x_x;
    int32_t            step_y_x;
    int32_t            step_x_y;
    int32_t            step_y_y;
    int32_t            buf_cx_q12;
    int32_t            buf_cy_q12;
    int32_t            dx0;
    int32_t            dy0;
    int64_t            rot_x0_q15;
    int64_t            rot_y0_q15;
    int32_t            src_x0_q12;
    int32_t            src_y0_q12;
    int32_t            curr_src_x;
    int32_t            curr_src_y;
    int16_t            start_x;
    int16_t            end_x;
    int16_t            start_y;
    int16_t            end_y;
    int16_t            x;
    int16_t            y;
    int32_t            max_x_i32;
    int32_t            max_y_i32;
    colour_t          *line_base;
    uint16_t           pfb_stride;
    uint8_t            opacity;
    colour_t           fg_color;
    _glyph_cache_t     cache[WE_CFG_LABEL_EX_MAX_GLYPHS];
    uint16_t           glyph_count;

    if (!p_lcd || obj->scale_256 == 0U || obj->opacity <= 5U ||
        !obj->font || !obj->text || obj->text_w == 0U)
        return;

    /* 裁剪到当前 PFB 可见区域 */
start_x = WE_MAX(obj->base.x, (int16_t)p_lcd->pfb_area.x0);
end_x   = WE_MIN(obj->base.x + obj->base.w - 1, (int16_t)p_lcd->pfb_area.x1);
start_y = WE_MAX(obj->base.y, (int16_t)p_lcd->pfb_y_start);
end_y   = WE_MIN(obj->base.y + obj->base.h - 1, (int16_t)p_lcd->pfb_y_end);

    if (start_x > end_x || start_y > end_y)
        return;

    /* --------------------------------------------------------------------------
     * 关键一步：进入像素扫描前先把整段文字预解析到栈缓存，
     * 后续逐像素采样就只用查表，不再走 UTF-8 解码/字库 flash 接口。
     * -------------------------------------------------------------------------- */
    glyph_count = _build_glyph_cache(obj, cache, (uint16_t)WE_CFG_LABEL_EX_MAX_GLYPHS);
    if (glyph_count == 0U)
        return;

    /* --------------------------------------------------------------------------
     * 预计算逆变换步进量（与 img_ex 相同）。
     * -------------------------------------------------------------------------- */
cos_a         = we_cos(obj->angle);
sin_a         = we_sin(obj->angle);
    inv_scale_q16 = (int32_t)(16777216 / obj->scale_256);

    step_x_x = (int32_t)(((int64_t) cos_a * inv_scale_q16) >> 19);
    step_y_x = (int32_t)(((int64_t)(-sin_a) * inv_scale_q16) >> 19);
    step_x_y = (int32_t)(((int64_t) sin_a * inv_scale_q16) >> 19);
    step_y_y = (int32_t)(((int64_t) cos_a * inv_scale_q16) >> 19);

    {
uint16_t line_h = we_font_get_line_height(obj->font);
        buf_cx_q12 = ((int32_t)obj->text_w * FP_ONE) / 2;
        buf_cy_q12 = ((int32_t)(int16_t)line_h * FP_ONE) / 2;
        max_x_i32  = (int32_t)obj->text_w;
        max_y_i32  = (int32_t)(int16_t)line_h;
    }

    dx0        = start_x - obj->cx;
    dy0        = start_y - obj->cy;
    rot_x0_q15 = (int64_t)dx0 * cos_a + (int64_t)dy0 * sin_a;
    rot_y0_q15 = -(int64_t)dx0 * sin_a + (int64_t)dy0 * cos_a;

    src_x0_q12 = (int32_t)((rot_x0_q15 * inv_scale_q16) >> 19) + buf_cx_q12;
    src_y0_q12 = (int32_t)((rot_y0_q15 * inv_scale_q16) >> 19) + buf_cy_q12;

    pfb_stride = p_lcd->pfb_width;
    opacity    = obj->opacity;
    fg_color   = obj->color;

    /* 行首 PFB 指针，每行递增 pfb_stride，免去 row*stride+col 的乘加 */
    line_base = (colour_t *)p_lcd->pfb_gram
              + (uint32_t)(start_y - (int16_t)p_lcd->pfb_y_start) * pfb_stride
              + (uint32_t)(start_x - (int16_t)p_lcd->pfb_area.x0);

    for (y = start_y; y <= end_y; y++)
    {
        colour_t *p_dst = line_base;

        curr_src_x = src_x0_q12;
        curr_src_y = src_y0_q12;

        /* for-loop 增量段的 p_dst++ 在 continue 时也会执行，地址始终对齐 */
        for (x = start_x; x <= end_x; x++, p_dst++)
        {
            int32_t tx = curr_src_x >> FP_BITS;
            int32_t ty = curr_src_y >> FP_BITS;
            uint8_t alpha8;
            uint8_t final_alpha;

            curr_src_x += step_x_x;
            curr_src_y += step_y_x;

            /* 快速越界过滤（负数和超上界一并排除） */
            if (tx < 0 || tx >= max_x_i32 || ty < 0 || ty >= max_y_i32)
                continue;

            alpha8 = _alpha_from_cache(cache, glyph_count, tx, ty);
            if (alpha8 == 0U)
                continue;

            /* opacity 全开时省一次乘法；否则用 we_div255 近似代替软件 /255 */
            final_alpha = (opacity == 255U) ? alpha8 : we_div255((uint32_t)alpha8 * opacity);

            we_store_blended_color(p_dst, fg_color, final_alpha);
        }

        line_base  += pfb_stride;
        src_x0_q12 += step_x_y;
        src_y0_q12 += step_y_y;
    }
}

/* =========================================================
 * 公开 API 实现
 * ========================================================= */

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param cx 控件变换中心的 X 坐标。
 * @param cy 控件变换中心的 Y 坐标。
 * @param text UTF-8 文本字符串。
 * @param font 字体资源指针。
 * @param color 目标颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_label_ex_obj_init(we_label_ex_obj_t *obj, we_lcd_t *lcd,
                          int16_t cx, int16_t cy,
                          const char *text, const unsigned char *font,
                          colour_t color, uint8_t opacity)
{
    static const we_class_t _label_ex_class = { .draw_cb = _label_ex_draw_cb, .event_cb = NULL };
    we_obj_t *tail;

    if (!obj || !lcd)
        return;

    obj->text      = text;
    obj->font      = font;
    obj->color     = color;
    obj->opacity   = opacity;
    obj->cx        = cx;
    obj->cy        = cy;
    obj->angle     = 0;
    obj->scale_256 = 256U;
    obj->text_w    = (font && text) ? _measure_text_w(font, text) : 0U;

_label_ex_update_bbox(obj);

    obj->base.class_p = &_label_ex_class;
    obj->base.lcd     = lcd;
    obj->base.next    = NULL;
    obj->base.parent  = NULL;

    /* 挂入渲染链表尾部 */
    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        tail = lcd->obj_list_head;
        while (tail->next)
            tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }

    if (opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param new_text 新的文本字符串。
 * @return 无。
 */
void we_label_ex_set_text(we_label_ex_obj_t *obj, const char *new_text)
{
    if (!obj || !new_text)
        return;
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
    obj->text   = new_text;
    obj->text_w = (obj->font) ? _measure_text_w(obj->font, new_text) : 0U;
_label_ex_update_bbox(obj);
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param cx 控件变换中心的 X 坐标。
 * @param cy 控件变换中心的 Y 坐标。
 * @return 无。
 */
void we_label_ex_set_center(we_label_ex_obj_t *obj, int16_t cx, int16_t cy)
{
    if (!obj)
        return;
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
    obj->cx = cx;
    obj->cy = cy;
_label_ex_update_bbox(obj);
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x）。
 * @return 无。
 */
void we_label_ex_set_transform(we_label_ex_obj_t *obj, int16_t angle, uint16_t scale_256)
{
    if (!obj || scale_256 == 0U)
        return;
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
    obj->angle     = angle & 0x1FF;
    obj->scale_256 = scale_256;
_label_ex_update_bbox(obj);
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param new_color 新的颜色值。
 * @return 无。
 */
void we_label_ex_set_color(we_label_ex_obj_t *obj, colour_t new_color)
{
    if (!obj)
        return;
#if (LCD_DEEP == DEEP_RGB565)
    if (obj->color.dat16 == new_color.dat16)
        return;
#elif (LCD_DEEP == DEEP_RGB888)
    if (obj->color.rgb.r == new_color.rgb.r &&
        obj->color.rgb.g == new_color.rgb.g &&
        obj->color.rgb.b == new_color.rgb.b)
        return;
#endif
    obj->color = new_color;
    if (obj->opacity > 0U)
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_label_ex_set_opacity(we_label_ex_obj_t *obj, uint8_t opacity)
{
    if (!obj || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
we_obj_invalidate((we_obj_t *)obj);
}
