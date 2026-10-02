#include "we_widget_img_ex.h"

#include "wegui.h"

#define FP_BITS 12
#define FP_ONE (1 << FP_BITS)
#define FP_MASK (FP_ONE - 1)

/* --------------------------------------------------------------------------
 * 双线性插值只在高质量模式下编译。
 *
 * 对低成本 MCU 而言，这一段会明显增加代码量和每像素运算量。
 * 默认改成最近邻后，可以同时压 Flash 和提升旋转缩放时的执行效率。
 * -------------------------------------------------------------------------- */
#if (WE_IMG_EX_SAMPLE_MODE == 1)

/**
 * @brief 对 2x2 邻域 4 个 RGB565 像素做双线性插值。
 * @param c00 左上采样点颜色（RGB565）。
 * @param c10 右上采样点颜色（RGB565）。
 * @param c01 左下采样点颜色（RGB565）。
 * @param c11 右下采样点颜色（RGB565）。
 * @param fx X 方向小数权重（Q12）。
 * @param fy Y 方向小数权重（Q12）。
 * @return 混合后的 RGB565 像素。
 */
static inline uint16_t lerp_rgb565(uint16_t c00, uint16_t c10, uint16_t c01, uint16_t c11, uint16_t fx, uint16_t fy)
{
    uint32_t w10 = ((uint32_t)fx * (FP_ONE - fy)) >> FP_BITS;
    uint32_t w01 = ((uint32_t)(FP_ONE - fx) * fy) >> FP_BITS;
    uint32_t w11 = ((uint32_t)fx * fy) >> FP_BITS;
    uint32_t w00 = FP_ONE - w10 - w01 - w11;

    uint32_t r = (((c00 >> 11) * w00) + ((c10 >> 11) * w10) + ((c01 >> 11) * w01) + ((c11 >> 11) * w11)) >> FP_BITS;
    uint32_t g = ((((c00 >> 5) & 0x3F) * w00) + (((c10 >> 5) & 0x3F) * w10) + (((c01 >> 5) & 0x3F) * w01) +
                  (((c11 >> 5) & 0x3F) * w11)) >>
                 FP_BITS;
    uint32_t b = (((c00 & 0x1F) * w00) + ((c10 & 0x1F) * w10) + ((c01 & 0x1F) * w01) + ((c11 & 0x1F) * w11)) >> FP_BITS;

    return (uint16_t)((r << 11) | (g << 5) | b);
}
#endif

/**
 * @brief 按当前中心点、旋转轴心偏移、角度和缩放刷新包围盒（base.x/y/w/h）。
 * @param obj 目标控件对象指针。
 * @return 无。
 * @note init / set_center / set_transform / set_pivot_offset 共用同一套几何。
 *       精细模式按四角旋转缩放求外接框（与采样同一轴心）；简易模式取
 *       |x·cos − y·sin| ≤ |x| + |y| 的外接正方形，代码更短、刷新面积更大。
 */
static void _img_ex_update_bbox(we_img_ex_obj_t *obj)
{
    int16_t w = IMG_DAT_WIDTH(obj->img);
    int16_t h = IMG_DAT_HEIGHT(obj->img);
    int16_t bx;
    int16_t by;
    int16_t bw;
    int16_t bh;

#if (WE_IMG_EX_USE_TIGHT_BBOX)
    we_xform_bbox(w, h, obj->pivot_ofs_x, obj->pivot_ofs_y, obj->angle, obj->scale_256,
                  &bx, &by, &bw, &bh);
#else
    {
        int32_t half = (int32_t)((w + 1) >> 1) + WE_ABS(obj->pivot_ofs_x) +
                       (int32_t)((h + 1) >> 1) + WE_ABS(obj->pivot_ofs_y);

        half = ((half * (int32_t)obj->scale_256 + 255) >> 8) + 2;
        bx = (int16_t)(-half);
        by = (int16_t)(-half);
        bw = (int16_t)(half * 2 + 1);
        bh = (int16_t)(half * 2 + 1);
    }
#endif

    obj->base.x = (int16_t)(obj->cx + bx);
    obj->base.y = (int16_t)(obj->cy + by);
    obj->base.w = bw;
    obj->base.h = bh;
}

/* --------------------------------------------------------------------------
 * 旋转缩放图片绘制回调
 *
 * 这一段是 img_ex 的热点路径，所以这里刻意做成“编译期裁剪”：
 * 1. 默认最近邻取样，双线性插值由 WE_IMG_EX_SAMPLE_MODE 打开
 * 2. 边缘羽化由 WE_IMG_EX_ENABLE_EDGE_AA 控制（默认打开）
 * 3. 把每行复用的矩阵步进量提前算好，内层循环只保留必要操作
 * -------------------------------------------------------------------------- */

/**
 * @brief img_ex 绘制回调：对包围盒内像素逆映射采样源图并旋转缩放写入 PFB。
 * @param obj_ptr 回调透传的 img_ex 对象指针。
 * @return 无。
 * @note 内核已把窗口收窄到本控件包围盒内，窗口左上角即 pfb_gram 起点。
 *       源图格式在 init 时校验过（只接受未压缩 RGB565，不合格的对象 class_p 为 NULL，不进入绘制）。
 */
static void we_img_ex_draw_cb(void *obj_ptr)
{
    we_img_ex_obj_t *obj = (we_img_ex_obj_t *)obj_ptr;
    we_lcd_t *p_lcd = obj->base.lcd;
    /* 容器透明度级联：入口算一次有效透明度，内环零额外成本 */
    uint8_t eff_op = we_opa_apply(p_lcd, obj->opacity);
    const uint8_t *img = obj->img;
    const uint8_t *img_data = IMG_DAT_PIXELS(img);
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);
    int32_t st[4];
    int32_t org[2]; /* 当前行首像素的采样点 */
    uint32_t max_x_u32;
    uint32_t max_y_u32;
    uint32_t img_w_u32 = (uint32_t)img_w;
    colour_t *row = p_lcd->pfb_gram;
    int16_t x;
    int16_t y;

    (void)eff_op; /* 个别裁剪宏组合下可能不被引用 */
    if (obj->scale_256 == 0U)
        return;
#if (!WE_IMG_EX_ASSUME_OPAQUE)
    if (eff_op <= 5U)
        return;
#endif

    /* 步骤1：逆映射步进（屏幕走 1 像素，源坐标走多少，Q12 四舍五入）。
     * st[0]/st[1]：屏幕 X+1 时源 X/Y 增量；st[2]/st[3]：屏幕 Y+1 时源 X/Y 增量 */
    we_xform_steps(obj->angle, obj->scale_256, st);

    /* 步骤2：窗口左上角像素的采样点（屏幕变换中心 (cx, cy) 映射到源图轴心，
     * 取像素中心采样；0°、1.0x 时源图逐像素原样画出） */
    we_xform_origin(st, img_w, img_h, obj->pivot_ofs_x, obj->pivot_ofs_y,
                    (int32_t)p_lcd->clip.x0 - obj->cx, (int32_t)p_lcd->clip.y0 - obj->cy, org);

    /* 步骤3：可采样边界 [0, 宽)（Q12，无符号比较一条判断同时拦住负数与越界） */
    max_x_u32 = (uint32_t)img_w << FP_BITS;
    max_y_u32 = (uint32_t)img_h << FP_BITS;

    /* 步骤4：按行扫描窗口。行内每往右一个像素累加一次 st[0]/st[1]，
     * 换行时行起点累加 st[2]/st[3]（增量式逆映射）。 */
    for (y = p_lcd->clip.y0; y <= p_lcd->clip.y1; y++, row += p_lcd->pfb_width)
    {
        int32_t src_x_fp = org[0];
        int32_t src_y_fp = org[1];
        colour_t *p_dst = row;

        for (x = p_lcd->clip.x0; x <= p_lcd->clip.x1; x++, p_dst++, src_x_fp += st[0], src_y_fp += st[1])
        {
            uint16_t final_color;

            if ((uint32_t)src_x_fp >= max_x_u32 || (uint32_t)src_y_fp >= max_y_u32)
                continue;

#if (WE_IMG_EX_SAMPLE_MODE == 1)
            /* 高质量模式：双线性插值以源像素中心为基准，采样点先退半个像素；
             * 首末像素中心以外的半像素钳到边缘像素，邻点越界时退回自身（不越界读取）。
             * 0°、1.0x 时小数部分恒为 0，与最近邻逐像素一致 */
            {
                int32_t bx = src_x_fp - FP_ONE / 2;
                int32_t by = src_y_fp - FP_ONE / 2;
                uint32_t sx;
                uint32_t sy;
                uint32_t dx;
                uint32_t dy;
                const uint8_t *src_ptr;

                if (bx < 0)
                    bx = 0;
                if (by < 0)
                    by = 0;
                sx = (uint32_t)bx >> FP_BITS;
                sy = (uint32_t)by >> FP_BITS;
                dx = (sx + 1U < img_w) ? 2U : 0U;
                dy = (sy + 1U < img_h) ? img_w_u32 * 2U : 0U;
                /* 像素按大端格式存储：byte[0]=高字节，byte[1]=低字节 */
                src_ptr = img_data + (sy * img_w_u32 + sx) * 2U;
#define _RD_BE16(p) (((uint16_t)(p)[0] << 8) | (p)[1])
                final_color = lerp_rgb565(_RD_BE16(src_ptr), _RD_BE16(src_ptr + dx),
                                          _RD_BE16(src_ptr + dy), _RD_BE16(src_ptr + dx + dy),
                                          (uint16_t)(bx & FP_MASK), (uint16_t)(by & FP_MASK));
#undef _RD_BE16
            }
#else
            /* 快速模式：最近邻（像素按大端格式存储：byte[0]=高字节，byte[1]=低字节） */
            {
                const uint8_t *src_ptr = img_data + (((uint32_t)src_y_fp >> FP_BITS) * img_w_u32 +
                                                     ((uint32_t)src_x_fp >> FP_BITS)) * 2U;

                final_color = (uint16_t)(((uint16_t)src_ptr[0] << 8) | src_ptr[1]);
            }
#endif

#if (WE_IMG_EX_ENABLE_EDGE_AA)
            /* 边缘羽化：采样点离最近边界不足半个源像素时按距离衰减
             * （恰在边界上为一半），旋转后的斜边不再是硬锯齿 */
            {
                uint32_t u_src_x = (uint32_t)src_x_fp;
                uint32_t u_src_y = (uint32_t)src_y_fp;
                uint32_t min_x = (u_src_x < max_x_u32 - u_src_x) ? u_src_x : (max_x_u32 - u_src_x);
                uint32_t min_y = (u_src_y < max_y_u32 - u_src_y) ? u_src_y : (max_y_u32 - u_src_y);
                uint32_t min_dist = (min_x < min_y) ? min_x : min_y;
                uint32_t final_alpha = eff_op;

                if (min_dist < (uint32_t)(FP_ONE / 2))
                    final_alpha = ((min_dist + (uint32_t)(FP_ONE / 2)) * eff_op) >> FP_BITS;

#if (LCD_DEEP == DEEP_RGB565)
                if (final_alpha >= 250U)
                {
                    p_dst->dat16 = final_color;
                }
                else if (final_alpha > 5U)
                {
                    colour_t fg;

                    fg.dat16 = final_color;
                    *p_dst = we_colour_blend(fg, *p_dst, (uint8_t)final_alpha);
                }
#else /* RGB888 / GRAY8：统一共用体赋值 */
                if (final_alpha >= 250U)
                    *p_dst = we_color_from_rgb565(final_color);
                else if (final_alpha > 5U)
                    *p_dst = we_colour_blend(we_color_from_rgb565(final_color), *p_dst, (uint8_t)final_alpha);
#endif
            }
#elif (WE_IMG_EX_ASSUME_OPAQUE)
            /* 纯不透明模式：直接覆写目标像素，完全裁掉 alpha 判断与混色 */
#if (LCD_DEEP == DEEP_RGB565)
            p_dst->dat16 = final_color;
#else
            *p_dst = we_color_from_rgb565(final_color);
#endif
#else
            /* 整体透明度：接近 255 直接覆写，否则混色（<=5 已在入口挡掉） */
            if (eff_op >= 250U)
            {
#if (LCD_DEEP == DEEP_RGB565)
                p_dst->dat16 = final_color;
#else
                *p_dst = we_color_from_rgb565(final_color);
#endif
            }
            else
            {
#if (LCD_DEEP == DEEP_RGB565) && (WE_IMG_EX_ENABLE_FAST_RGB565_BLEND)
                p_dst->dat16 = we_blend_rgb565(final_color, p_dst->dat16, eff_op);
#else
                *p_dst = we_colour_blend(we_color_from_rgb565(final_color), *p_dst, eff_op);
#endif
            }
#endif
        }

        org[0] += st[2];
        org[1] += st[3];
    }
}

/* --------------------------------------------------------------------------
 * 容器平移（group/scroll_panel/slideshow 经 we_obj_set_pos）时，
 * 包围盒 base.x/y 与采样锚点 cx/cy 必须按同一位移差整体移动，
 * 否则采样锚点与裁剪/标脏窗口失配，表现为位置漂移、拖影、被错误裁剪。
 * 统一收口在 set_pos_cb 里，不经 WE_EVENT_SCROLLED 同步锚点
 * （we_group_shift_children 已先调 we_obj_set_pos，再发 SCROLLED 会双重位移）。
 * 本控件不消费点击。
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件移动回调，平移包围盒的同时同步平移采样锚点。
 * @param ptr 回调透传对象指针。
 * @param new_x 新的包围盒左上角 X 坐标。
 * @param new_y 新的包围盒左上角 Y 坐标。
 * @return 无。
 */
static void _img_ex_set_pos_cb(void *ptr, int16_t new_x, int16_t new_y)
{
    we_img_ex_obj_t *img_ex = (we_img_ex_obj_t *)ptr;

    we_obj_invalidate((we_obj_t *)img_ex);

    img_ex->cx += (int16_t)(new_x - img_ex->base.x);
    img_ex->cy += (int16_t)(new_y - img_ex->base.y);
    img_ex->base.x = new_x;
    img_ex->base.y = new_y;

    we_obj_invalidate((we_obj_t *)img_ex);
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param p_lcd GUI 运行时 LCD 上下文指针。
 * @param cx 控件变换中心的 X 坐标。
 * @param cy 控件变换中心的 Y 坐标。
 * @param img RGB565 未压缩图片资源指针。
 * @param op 初始不透明度（0~255）。
 * @return 无。
 * @note 源图必须是未压缩 RGB565（旋转缩放要逐像素随机访问源图）；其它格式
 *       的对象照常挂入链表占位，但 class_p 置 NULL（与 img 控件同口径），什么也不画。
 */
void we_img_ex_obj_init(we_img_ex_obj_t *obj, we_lcd_t *p_lcd, int16_t cx, int16_t cy, const uint8_t *img, uint8_t op)
{
    static const we_class_t _img_ex_class = {.draw_cb = we_img_ex_draw_cb, .event_cb = NULL, .set_pos_cb = _img_ex_set_pos_cb};

    if (!obj || !p_lcd || !img)
        return;

    obj->base.lcd = p_lcd;
    obj->base.parent = NULL;
    obj->img = img;
    obj->cx = cx;
    obj->cy = cy;
    obj->pivot_ofs_x = 0;
    obj->pivot_ofs_y = 0;
    obj->angle = 0;
    obj->scale_256 = 256;
    obj->opacity = op;

    /* 初始化时先计算一次默认姿态的包围盒，后续刷新直接沿用对象坐标 */
    _img_ex_update_bbox(obj);

    obj->base.class_p = (IMG_DAT_FORMAT(img) == IMG_RGB565) ? &_img_ex_class : NULL;
    obj->base.next = NULL;

    we_obj_attach_to_lcd(p_lcd, (we_obj_t *)obj);

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置变换中心 cx/cy 并重算包围盒。
 * @param obj 目标控件对象指针。
 * @param cx 控件变换中心的 X 坐标。
 * @param cy 控件变换中心的 Y 坐标。
 * @return 无。
 */
void we_img_ex_obj_set_center(we_img_ex_obj_t *obj, int16_t cx, int16_t cy)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->cx == cx && obj->cy == cy)
        return;

    we_obj_invalidate((we_obj_t *)obj);
    obj->cx = cx;
    obj->cy = cy;
    _img_ex_update_bbox(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置源图内部旋转轴心相对几何中心的偏移并重算包围盒。
 * @param obj 目标控件对象指针。
 * @param ofs_x 源图内部旋转中心 X 偏移（像素）。
 * @param ofs_y 源图内部旋转中心 Y 偏移（像素）。
 * @return 无。
 * @note 轴心同时影响源图采样基准点与旋转后的外接框：先标旧区域，再重算新区域。
 */
void we_img_ex_obj_set_pivot_offset(we_img_ex_obj_t *obj, int16_t ofs_x, int16_t ofs_y)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->pivot_ofs_x == ofs_x && obj->pivot_ofs_y == ofs_y)
        return;

    we_obj_invalidate((we_obj_t *)obj);
    obj->pivot_ofs_x = ofs_x;
    obj->pivot_ofs_y = ofs_y;
    _img_ex_update_bbox(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置旋转角度与缩放比例并重算包围盒。
 * @param obj 目标控件对象指针。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x，0 时不绘制）。
 * @return 无。
 */
void we_img_ex_obj_set_transform(we_img_ex_obj_t *obj, int16_t angle, uint16_t scale_256)
{
    if (obj == NULL || obj->base.lcd == NULL || (obj->angle == angle && obj->scale_256 == scale_256))
        return;

    we_obj_invalidate((we_obj_t *)obj); /* 旧区域 */
    obj->angle = angle;
    obj->scale_256 = scale_256;
    _img_ex_update_bbox(obj);
    we_obj_invalidate((we_obj_t *)obj); /* 新区域 */
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_img_ex_obj_set_opacity(we_img_ex_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == opacity)
        return;

    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj); /* 透明度不改变几何范围，只刷当前包围盒 */
}
