#include "we_widget_label_ex.h"
#include "wegui.h"

/* --------------------------------------------------------------------------
 * 定点精度：与 img_ex 保持一致，使用 Q12（FP_BITS = 12）
 * -------------------------------------------------------------------------- */
#define FP_BITS 12

/* --------------------------------------------------------------------------
 * 字形预扫描缓存：把版面位置、位图地址、bpp 等一次性算好，
 * 内层逐像素循环直接查表，彻底干掉每像素的 UTF-8 解码与字库接口调用。
 * 单条目 16 字节（按 4 字节对齐自然填满）。
 * 缓存容量由 WE_CFG_LABEL_EX_MAX_GLYPHS 决定（见 we_widget_label_ex.h）。
 * -------------------------------------------------------------------------- */
typedef struct
{
    const uint8_t *bitmap;     /* 已解析好的字形位图首地址 */
    uint16_t row_stride;       /* 字形位图每行字节数 */
    int16_t draw_x;            /* 字形左上角在文字框内的 X */
    int16_t draw_y;            /* 字形左上角在文字框内的 Y */
    uint16_t box_w;            /* 字形宽度（像素） */
    uint16_t box_h;            /* 字形高度（像素） */
    uint8_t bpp;               /* 字形位深（1/2/4/8 bpp） */
    uint8_t alpha_mask;        /* (1<<bpp)-1，预算好省一次移位 */
} _glyph_cache_t;

/**
 * @brief 解析首行文字，把与采样窗口相交的字形写入缓存。
 * @param obj 目标控件对象指针（提供 font/text）。
 * @param cache 传出：字形缓存数组（由调用方在栈上分配，容量 WE_CFG_LABEL_EX_MAX_GLYPHS）。
 * @param win 传入：本次绘制窗口在文字框内的范围 {x_lo, x_hi, y_lo, y_hi}（闭区间）。
 * @return 写入缓存的字形数。
 * @note 只排首行（label_ex 单行旋转）的前 WE_CFG_LABEL_EX_MAX_GLYPHS 个有墨字形；
 *       上限按字符串顺序计数，与窗口无关，所以画出哪些字不随脏矩形的切法变化。
 */
static uint16_t _build_glyph_cache(const we_label_ex_obj_t *obj, _glyph_cache_t *cache, const int32_t win[4])
{
    const char *str = obj->text;
    uint16_t inked = 0U;
    uint16_t count = 0U;
    int32_t cursor_x = 0;
    we_glyph_info_t info;
    uint16_t code;

    while (inked < (uint16_t)WE_CFG_LABEL_EX_MAX_GLYPHS && we_utf8_next(&str, &code) != 0U && code != (uint16_t)'\n')
    {
        if (!we_font_get_glyph_info(obj->font, code, &info))
            continue;

        if (info.box_w != 0U && info.box_h != 0U)
        {
            int32_t gx = cursor_x + info.x_ofs;

            inked++;
            /* 字形按笔位顺序排布，之后的只会更靠右 */
            if (gx > win[1])
                break;
            if (gx + (int32_t)info.box_w > win[0] && (int32_t)info.y_ofs <= win[3] &&
                (int32_t)info.y_ofs + (int32_t)info.box_h > win[2])
            {
                const uint8_t *bitmap = NULL;
                uint8_t bpp = 0U;
                uint32_t row_stride = 0U;

                if (we_font_get_bitmap_info(obj->font, &info, &bitmap, &bpp, &row_stride) && bitmap != NULL)
                {
                    cache[count].bitmap = bitmap;
                    cache[count].row_stride = (uint16_t)row_stride;
                    cache[count].draw_x = (int16_t)gx;
                    cache[count].draw_y = info.y_ofs;
                    cache[count].box_w = info.box_w;
                    cache[count].box_h = info.box_h;
                    cache[count].bpp = bpp;
                    cache[count].alpha_mask = (uint8_t)((1U << bpp) - 1U);
                    count++;
                }
            }
        }

        cursor_x += info.adv_w;
    }
    return count;
}

/**
 * @brief 从字形缓存查询文字框内坐标 (tx, ty) 的 alpha 值。
 * @param cache 字形缓存数组（来自 _build_glyph_cache）。
 * @param count 字形缓存有效条目数。
 * @param tx 文字框内 X 坐标（>= 0）。
 * @param ty 文字框内 Y 坐标（>= 0）。
 * @return 字形覆盖 alpha（0~255）。
 */
static __inline uint8_t _alpha_from_cache(const _glyph_cache_t *cache, uint16_t count,
                                          int32_t tx, int32_t ty)
{
    for (uint16_t i = 0U; i < count; i++)
    {
        const _glyph_cache_t *g = &cache[i];

        /* 字形按笔位顺序排布，后续 draw_x 只会更靠右，可以提前退出 */
        if (tx < (int32_t)g->draw_x)
            break;
        if (tx >= (int32_t)(g->draw_x + (int16_t)g->box_w))
            continue;
        if (ty < (int32_t)g->draw_y || ty >= (int32_t)(g->draw_y + (int16_t)g->box_h))
            continue;

        {
            uint32_t px = (uint32_t)(tx - (int32_t)g->draw_x);
            uint32_t py = (uint32_t)(ty - (int32_t)g->draw_y);
            uint32_t bit_pos = py * (uint32_t)g->row_stride * 8U + px * (uint32_t)g->bpp;
            uint8_t shift = (uint8_t)(8U - g->bpp - (uint8_t)(bit_pos & 7U));
            uint8_t a_raw = (g->bitmap[bit_pos >> 3U] >> shift) & g->alpha_mask;

            switch (g->bpp)
            {
            case 8U:
                return a_raw;
            case 4U:
                return (uint8_t)((a_raw << 4) | a_raw);
            case 2U:
                return (uint8_t)(a_raw * 85U);
            default:
                return a_raw ? 255U : 0U;
            }
        }
    }
    return 0U;
}

/**
 * @brief 按 cx/cy/angle/scale/text_w 重算并写回控件包围盒。
 * @param obj 目标控件对象指针。
 * @return 无。
 * @note 文字框 text_w × 行高，以几何中心为轴心，与采样同一套几何（we_xform_bbox）。
 */
static void _label_ex_update_bbox(we_label_ex_obj_t *obj)
{
    int16_t bx;
    int16_t by;
    int16_t bw;
    int16_t bh;
    int16_t line_h = (obj->font != NULL) ? (int16_t)we_font_get_line_height(obj->font) : 16;

    we_xform_bbox((int16_t)obj->text_w, line_h, 0, 0, obj->angle, obj->scale_256, &bx, &by, &bw, &bh);

    obj->base.x = (int16_t)(obj->cx + bx);
    obj->base.y = (int16_t)(obj->cy + by);
    obj->base.w = bw;
    obj->base.h = bh;
}

/* --------------------------------------------------------------------------
 * 旋转缩放文字绘制回调
 *
 * 工作方式：
 *   1. 把本次绘制窗口（内核收窄后的 lcd->clip）的四角逆映射回文字框，
 *      只把与之相交的字形解析到栈上的 _glyph_cache_t cache[]，
 *      之后逐像素采样直接查表，零 UTF-8 解码、零字库接口调用。
 *   2. 对窗口内每个屏幕像素做逆变换，得到文字框内坐标 (tx, ty)，
 *      查表获取 alpha，与前景色混色写回 PFB。
 *   3. PFB 目标地址用指针递增维护，避免每像素 row*stride+col 重算。
 * 缓存在每次绘制（每个 PFB 切片）入口重建。
 * -------------------------------------------------------------------------- */

/**
 * @brief label_ex 绘制回调：预扫描字形到栈缓存后逆映射采样并旋转缩放写入 PFB。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _label_ex_draw_cb(void *ptr)
{
    we_label_ex_obj_t *obj = (we_label_ex_obj_t *)ptr;
    we_lcd_t *p_lcd = obj->base.lcd;
    uint8_t opacity = we_opa_apply(p_lcd, obj->opacity); /* 容器透明度级联 */
    colour_t fg_color = obj->color;
    colour_t *row = p_lcd->pfb_gram;
    uint16_t line_h;
    int32_t st[4];
    int32_t org[2]; /* 当前行首像素的采样点 */
    int32_t win[4];
    uint32_t max_x_u32;
    uint32_t max_y_u32;
    _glyph_cache_t cache[WE_CFG_LABEL_EX_MAX_GLYPHS];
    uint16_t glyph_count;
    int16_t x;
    int16_t y;

    /* font 或 text 为空时 text_w 恒为 0 */
    if (opacity <= 5U || obj->text_w == 0U)
        return;

    line_h = we_font_get_line_height(obj->font);

    /* 逆映射步进与窗口左上角像素的采样点（与 img_ex 同一套几何：
     * 屏幕变换中心 (cx, cy) 映射到文字框中心，取像素中心采样） */
    we_xform_steps(obj->angle, obj->scale_256, st);
    we_xform_origin(st, obj->text_w, line_h, 0, 0,
                    (int32_t)p_lcd->clip.x0 - obj->cx, (int32_t)p_lcd->clip.y0 - obj->cy, org);

    /* 窗口四角的采样点围成本次要用到的文字框范围（采样点是行列的线性函数，
     * 极值必在四角），只解析与之相交的字形 */
    {
        int32_t ex = (int32_t)(p_lcd->clip.x1 - p_lcd->clip.x0);
        int32_t ey = (int32_t)(p_lcd->clip.y1 - p_lcd->clip.y0);
        int32_t ax = ex * st[0];
        int32_t bx = ey * st[2];
        int32_t ay = ex * st[1];
        int32_t by = ey * st[3];

        win[0] = (org[0] + WE_MIN(ax, 0) + WE_MIN(bx, 0)) >> FP_BITS;
        win[1] = (org[0] + WE_MAX(ax, 0) + WE_MAX(bx, 0)) >> FP_BITS;
        win[2] = (org[1] + WE_MIN(ay, 0) + WE_MIN(by, 0)) >> FP_BITS;
        win[3] = (org[1] + WE_MAX(ay, 0) + WE_MAX(by, 0)) >> FP_BITS;
    }
    glyph_count = _build_glyph_cache(obj, cache, win);
    if (glyph_count == 0U)
        return;

    /* 文字框边界（Q12，无符号比较一条判断同时拦住负数与越界） */
    max_x_u32 = (uint32_t)obj->text_w << FP_BITS;
    max_y_u32 = (uint32_t)line_h << FP_BITS;

    for (y = p_lcd->clip.y0; y <= p_lcd->clip.y1; y++, row += p_lcd->pfb_width)
    {
        int32_t src_x = org[0];
        int32_t src_y = org[1];
        colour_t *p_dst = row;

        for (x = p_lcd->clip.x0; x <= p_lcd->clip.x1; x++, p_dst++, src_x += st[0], src_y += st[1])
        {
            uint8_t alpha8;

            if ((uint32_t)src_x >= max_x_u32 || (uint32_t)src_y >= max_y_u32)
                continue;

            alpha8 = _alpha_from_cache(cache, glyph_count, src_x >> FP_BITS, src_y >> FP_BITS);
            if (alpha8 == 0U)
                continue;

            /* opacity 全开时省一次乘法；否则 we_div255 精确求 alpha8 × opacity / 255 */
            we_store_blended_color(p_dst, fg_color,
                                   (opacity == 255U) ? alpha8 : we_div255((uint32_t)alpha8 * opacity));
        }

        org[0] += st[2];
        org[1] += st[3];
    }
}

/**
 * @brief 控件移动回调，平移包围盒的同时同步平移变换中心。
 * @param ptr 回调透传对象指针。
 * @param new_x 新的包围盒左上角 X 坐标。
 * @param new_y 新的包围盒左上角 Y 坐标。
 * @return 无。
 * @note 容器平移（group/scroll_panel/slideshow 经 we_obj_set_pos）时，包围盒与
 *       采样锚点 cx/cy 必须按同一位移差整体移动，否则文字会被新包围盒裁掉。
 */
static void _label_ex_set_pos_cb(void *ptr, int16_t new_x, int16_t new_y)
{
    we_label_ex_obj_t *obj = (we_label_ex_obj_t *)ptr;

    we_obj_invalidate((we_obj_t *)obj);

    obj->cx += (int16_t)(new_x - obj->base.x);
    obj->cy += (int16_t)(new_y - obj->base.y);
    obj->base.x = new_x;
    obj->base.y = new_y;

    we_obj_invalidate((we_obj_t *)obj);
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
    static const we_class_t _label_ex_class = {.draw_cb = _label_ex_draw_cb, .event_cb = NULL, .set_pos_cb = _label_ex_set_pos_cb};

    if (!obj || !lcd)
        return;

    obj->text = text;
    obj->font = font;
    obj->color = color;
    obj->opacity = opacity;
    obj->cx = cx;
    obj->cy = cy;
    obj->angle = 0;
    obj->scale_256 = 256U;
    obj->text_w = we_get_text_ink_width(font, text);

    _label_ex_update_bbox(obj);

    obj->base.class_p = &_label_ex_class;
    obj->base.lcd = lcd;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    /* 挂入渲染链表尾部 */
    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param new_text 新的文本字符串。
 * @return 无。
 * @note 不比较指针：同一缓冲区原地改写内容后再次调用也会重新测量并重绘。
 */
void we_label_ex_set_text(we_label_ex_obj_t *obj, const char *new_text)
{
    uint16_t w;

    if (!obj || !new_text)
        return;

    w = we_get_text_ink_width(obj->font, new_text);
    obj->text = new_text;
    if (w == obj->text_w)
    {
        /* 宽度不变包围盒就不变，只重绘原区域 */
        if (obj->opacity > 0U)
            we_obj_invalidate((we_obj_t *)obj);
        return;
    }

    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
    obj->text_w = w;
    _label_ex_update_bbox(obj);
    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置变换中心坐标并重算包围盒。
 * @param obj 目标控件对象指针。
 * @param cx 控件变换中心的 X 坐标。
 * @param cy 控件变换中心的 Y 坐标。
 * @return 无。
 */
void we_label_ex_set_center(we_label_ex_obj_t *obj, int16_t cx, int16_t cy)
{
    if (!obj || (obj->cx == cx && obj->cy == cy))
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
 * @brief 设置旋转角度与缩放比例并重算包围盒。
 * @param obj 目标控件对象指针。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x，传 0 时忽略本次调用）。
 * @return 无。
 */
void we_label_ex_set_transform(we_label_ex_obj_t *obj, int16_t angle, uint16_t scale_256)
{
    if (!obj || scale_256 == 0U)
        return;
    angle &= 0x1FF;
    if (obj->angle == angle && obj->scale_256 == scale_256)
        return;
    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
    obj->angle = angle;
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
    if (!obj || we_colour_eq(obj->color, new_color))
        return;
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
