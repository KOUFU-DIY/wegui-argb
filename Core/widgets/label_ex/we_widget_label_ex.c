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
 * 根据文字局部坐标 (tx, ty) 直接从字模 flash 数据查询 alpha 值。
 *
 * tx, ty 为文字局部整数坐标：x 轴原点在文字左边，y 轴原点在行顶。
 * 此函数零额外 RAM 占用，仅使用栈上局部变量。
 * -------------------------------------------------------------------------- */

/**
 * @brief 读取当前属性值或计算结果。
 * @param obj 目标控件对象指针。
 * @param tx 文本局部坐标系中的 X 坐标。
 * @param ty 文本局部坐标系中的 Y 坐标。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _get_alpha_at(const we_label_ex_obj_t *obj, int32_t tx, int32_t ty)
{
    const unsigned char *font_array = obj->font;
    const char          *str        = obj->text;
uint16_t             line_h     = we_font_get_line_height(font_array);
    int16_t              cursor_x   = 0;
    we_glyph_info_t      info;

    /* Y 轴越界快速退出 */
    if (ty < 0 || ty >= (int32_t)line_h)
        return 0U;

    while (*str)
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

        /* 换行只处理单行（label_ex 不支持多行旋转） */
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
            int16_t draw_x = cursor_x + info.x_ofs;
            int16_t draw_y = info.y_ofs;

            /* tx 已超过当前字形右边界，继续向后找 */
            if (tx >= (int32_t)(draw_x + (int16_t)info.box_w))
            {
                cursor_x += info.adv_w;
                continue;
            }

            /* tx 还在当前字形左边界之前，后续字形 x 只会更大，提前退出 */
            if (tx < (int32_t)draw_x)
                break;

            /* Y 范围检测 */
            if (ty < (int32_t)draw_y || ty >= (int32_t)(draw_y + (int16_t)info.box_h))
            {
                cursor_x += info.adv_w;
                continue;
            }

            /* 命中：通过统一字体接口提取 alpha */
            {
                const uint8_t *bitmap = NULL;
                uint8_t bpp = 0U;
                uint32_t row_stride = 0U;

                if (!we_font_get_bitmap_info(font_array, &info, &bitmap, &bpp, &row_stride) || bitmap == NULL)
                    return 0U;

                {
                    uint8_t alpha_mask = (uint8_t)((1U << bpp) - 1U);
                    uint32_t px = (uint32_t)(tx - (int32_t)draw_x);
                    uint32_t py = (uint32_t)(ty - (int32_t)draw_y);
                    uint32_t bit_pos = py * row_stride * 8U + px * (uint32_t)bpp;
                    uint32_t byte_idx = bit_pos >> 3U;
                    uint8_t shift = (uint8_t)(8U - bpp - (uint8_t)(bit_pos & 7U));
                    uint8_t a_raw = (bitmap[byte_idx] >> shift) & alpha_mask;

                    if (bpp == 8U) return a_raw;
                    if (bpp == 4U) return (uint8_t)((a_raw << 4) | a_raw);
                    if (bpp == 2U) return (uint8_t)(a_raw * 85U);
                    return a_raw ? 255U : 0U;
                }
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
 * 无离屏缓冲：对包围盒内每个屏幕像素做逆变换，直接调用 _get_alpha_at
 * 从字模 flash 数据读取 alpha，再与前景色混色写回 PFB。
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
    colour_t          *gram;
    uint16_t           pfb_stride;
    uint8_t            opacity;
    colour_t           fg_color;

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

    /* 有效范围上界（用有符号比较，_get_alpha_at 内部负数已过滤）*/

    gram       = p_lcd->pfb_gram;
    pfb_stride = p_lcd->pfb_width;
    opacity    = obj->opacity;
    fg_color   = obj->color;

    for (y = start_y; y <= end_y; y++)
    {
        int16_t pfb_row = y - (int16_t)p_lcd->pfb_y_start;

        curr_src_x = src_x0_q12;
        curr_src_y = src_y0_q12;

        for (x = start_x; x <= end_x; x++)
        {
            int32_t tx = curr_src_x >> FP_BITS;
            int32_t ty = curr_src_y >> FP_BITS;

            curr_src_x += step_x_x;
            curr_src_y += step_y_x;

            /* 快速越界过滤（负数和超上界均排除） */
            if (tx < 0 || tx >= max_x_i32 || ty < 0 || ty >= max_y_i32)
                continue;

            {
uint8_t alpha8 = _get_alpha_at(obj, tx, ty);
                if (alpha8 == 0U)
                    continue;

                uint8_t final_alpha = (opacity == 255U)
                    ? alpha8
                    : (uint8_t)(((uint32_t)alpha8 * opacity + 127U) / 255U);

                colour_t *dst = &gram[(uint32_t)pfb_row * pfb_stride +
                                      (uint32_t)(x - (int16_t)p_lcd->pfb_area.x0)];

#if (LCD_DEEP == DEEP_RGB565)
dst->dat16 = we_blend_rgb565(fg_color.dat16, dst->dat16, final_alpha);
#elif (LCD_DEEP == DEEP_RGB888)
                {
colour_t blended = we_colour_blend(fg_color, *dst, final_alpha);
                    dst->rgb.r = blended.rgb.r;
                    dst->rgb.g = blended.rgb.g;
                    dst->rgb.b = blended.rgb.b;
                }
#endif
            }
        }

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
