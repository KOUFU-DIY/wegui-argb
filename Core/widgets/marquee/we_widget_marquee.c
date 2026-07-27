/**
 * @file  we_widget_marquee.c
 * @brief 跑马灯标签控件（preview）：固定窗口内循环滚动的单行文本
 *
 * 渲染：draw_cb 先按 group 的标准套路把 PFB 窗口收窄到自身矩形
 * （save/restore pfb_area / pfb_y_start / pfb_y_end / pfb_gram），
 * 再在偏移位置绘制 "文本 + 间隔 + 文本开头" 两段，越界部分被窗口裁掉。
 *
 * 滚动：单个中央动画节点（we_anim_t）推进，毫像素整数累计
 * frac_acc += elapsed_ms * speed，凑满 1000 前进 1px；
 * offset 到达 text_w + WE_MARQUEE_GAP 后回零并停留 pause_ms。
 *
 * 毕业级优化（本轮）：
 *   1. 自建窗口化字形绘制循环（_marquee_draw_segment）取代 we_draw_string
 *      全量遍历：窗口左侧完全裁掉的字形只做 UTF-8 解码 + adv_w 游标快进
 *      （零位图取址、零像素扫描），游标越过窗口右缘立即 break；
 *      逐帧绘制成本只与可见字形数相关，与文本总长解耦；
 *   2. 字体参数化：we_marquee_set_font 支持运行时换字库（重测宽度、
 *      重置滚动、行高联动控件高度，先标脏旧区再改再标脏新区）；
 *   3. 单行截断语义与测宽同口径：绘制遇 '\n' 即止，只显示第一行。
 */

#include "we_widget_marquee.h"
#include "we_render.h" /* we_store_blended_color / we_div255（字形混色用） */

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void _marquee_draw_cb(void *ptr);

/* 装饰性控件：event_cb 置 NULL，核心输入分发完全跳过本控件（真穿透） */
static const we_class_t _marquee_class = {
    .draw_cb    = _marquee_draw_cb,
    .event_cb   = NULL,
    .set_pos_cb = NULL
};

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 颜色相等比较（RGB565/RGB888），供 setter 的“值未变则跳过”守卫使用。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 表示相等，0 表示不等。
 */
static __inline uint8_t _marquee_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#elif (LCD_DEEP == DEEP_RGB888)
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 复位滚动运行态：偏移回起点、清空毫像素累计与停留状态。
 * @param obj 控件对象指针。
 * @return 无。
 */
static void _marquee_reset_scroll(we_marquee_obj_t *obj)
{
    obj->offset = 0;
    obj->frac_acc = 0;
    obj->paused = 0U;
    obj->pause_acc = 0U;
}

/**
 * @brief 中央动画引擎回调：按毫像素整数累计推进滚动偏移。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本调度周期经过的毫秒数。
 * @return 无。
 * @note 停留阶段只累计时间不重绘；偏移未跨过整像素时也不重绘。
 */
static void _marquee_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_marquee_obj_t *obj = (we_marquee_obj_t *)owner;
    int32_t adv;
    int32_t cycle;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    /* 接缝停留阶段：只吃时间，画面静止 */
    if (obj->paused)
    {
        uint32_t acc = (uint32_t)obj->pause_acc + elapsed_ms;
        if (acc >= obj->pause_ms)
        {
            obj->paused = 0U;
            obj->pause_acc = 0U;
        }
        else
        {
            obj->pause_acc = (uint16_t)acc;
        }
        return; /* 本周期结束停留，下周期起步，避免跨相位半步推进 */
    }

    /* 毫像素累计：frac_acc 单位 = px/1000，int32 防溢出
     * （elapsed_ms 与 speed 都远小于 2^15，乘积安全） */
    obj->frac_acc += (int32_t)elapsed_ms * (int32_t)obj->speed;
    adv = obj->frac_acc / 1000;
    if (adv == 0)
        return;
    obj->frac_acc -= adv * 1000;
    obj->offset = (int16_t)(obj->offset + adv);

    /* 一轮滚完（第二段文本头回到窗口原点）：回零 + 进入停留 */
    cycle = (int32_t)obj->text_w + WE_MARQUEE_GAP;
    if ((int32_t)obj->offset >= cycle)
    {
        obj->offset = 0;
        obj->frac_acc = 0;
        if (obj->pause_ms > 0U)
        {
            obj->paused = 1U;
            obj->pause_acc = 0U;
        }
    }

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 按当前文本宽/控件宽/透明度决定是否需要滚动，同步动画节点挂链状态。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 从滚动切回静止时会复位偏移并标脏一次，保证静止画面回到左对齐。
 */
static void _marquee_update_anim(we_marquee_obj_t *obj)
{
    uint8_t need = (uint8_t)(obj->text != NULL &&
                             obj->text_w > (uint16_t)obj->base.w &&
                             obj->opacity > 0U);

    if (need && !obj->scrolling)
    {
        we_anim_start(obj->base.lcd, &obj->anim, _marquee_anim_step_cb, obj);
        obj->scrolling = 1U;
    }
    else if (!need && obj->scrolling)
    {
        we_anim_stop(obj->base.lcd, &obj->anim);
        obj->scrolling = 0U;
        _marquee_reset_scroll(obj);
        we_obj_invalidate((we_obj_t *)obj);
    }
}

/* --------------------------------------------------------------------------
 * 窗口化字形绘制（毕业优化核心：左侧游标快进 + 右缘提前退出）
 * -------------------------------------------------------------------------- */

/**
 * @brief 读取 UTF-8 字符串下一个码点并推进游标（1/2/3 字节）。
 * @param pp 传入传出：解析位置；成功后移动到下一字符起始处。
 * @param out_code 传出：码点；非法单字节返回 0（后续字库查询自然失败跳过）。
 * @return 1 = 成功读取，0 = 字符串结束或多字节序列截断（调用方应停止）。
 * @note 与 we_font_text.c 的核心解码器同口径：4 字节及以上编码按
 *       非法单字节处理（code 置 0），截断序列直接终止绘制。
 */
static uint8_t _marquee_utf8_next(const char **pp, uint16_t *out_code)
{
    const unsigned char *p = (const unsigned char *)*pp;
    uint8_t c0;

    if (*p == 0U)
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
            return 0U; /* 截断的双字节序列：停止 */
        *out_code = (uint16_t)(((c0 & 0x1FU) << 6) | ((uint8_t)*p++ & 0x3FU));
    }
    else if ((c0 & 0xF0U) == 0xE0U)
    {
        if (p[0] == 0U || p[1] == 0U)
            return 0U; /* 截断的三字节序列：停止 */
        *out_code = (uint16_t)(((c0 & 0x0FU) << 12) |
                               (((uint8_t)p[0] & 0x3FU) << 6) |
                               ((uint8_t)p[1] & 0x3FU));
        p += 2;
    }

    *pp = (const char *)p;
    return 1U;
}

/**
 * @brief 将行对齐的字形 alpha 位图混合到当前 PFB（marquee 私有精简 blit）。
 * @param lcd GUI 屏幕上下文指针（PFB 窗口已收窄到控件矩形 ∩ 脏区带）。
 * @param x 字形墨迹左上角 X（屏幕绝对坐标）。
 * @param y 字形墨迹左上角 Y。
 * @param w 字形墨迹宽（像素）。
 * @param h 字形墨迹高（像素）。
 * @param src 字形位图首地址（按行对齐紧凑存放）。
 * @param row_stride 位图每行字节数。
 * @param bpp 位深（1/2/4/8）。
 * @param fg 前景色。
 * @param opacity 有效不透明度（已含容器级联，调用方保证 > 0）。
 * @return 无。
 * @note 与 we_font_text.c 行对齐 blit 同语义的紧凑实现：任意 bpp 共用
 *       一条位游标路径（游标按 bpp 递增，行内零乘法），alpha 展开用
 *       a_raw * (255 / ((1<<bpp)-1)) 单次乘法完成（255/85/17/1）。
 */
static void _marquee_blit_glyph(we_lcd_t *lcd, int16_t x, int16_t y,
                                uint16_t w, uint16_t h,
                                const uint8_t *src, uint32_t row_stride,
                                uint8_t bpp, colour_t fg, uint8_t opacity)
{
    int16_t x_end = (int16_t)(x + (int16_t)w - 1);
    int16_t y_end = (int16_t)(y + (int16_t)h - 1);
    int16_t cx0 = (x < (int16_t)lcd->pfb_area.x0) ? (int16_t)lcd->pfb_area.x0 : x;
    int16_t cy0 = (y < (int16_t)lcd->pfb_y_start) ? (int16_t)lcd->pfb_y_start : y;
    int16_t cx1 = (x_end > (int16_t)lcd->pfb_area.x1) ? (int16_t)lcd->pfb_area.x1 : x_end;
    int16_t cy1 = (y_end > (int16_t)lcd->pfb_y_end) ? (int16_t)lcd->pfb_y_end : y_end;
    uint8_t a_mask;
    uint8_t a_mult;
    int16_t py;

    if (cx0 > cx1 || cy0 > cy1)
        return;

    a_mask = (uint8_t)((1U << bpp) - 1U);
    a_mult = (uint8_t)(255U / a_mask); /* 1/2/4/8 bpp → 255/85/17/1 */

    for (py = cy0; py <= cy1; py++)
    {
        const uint8_t *src_row = src + (uint32_t)(py - y) * row_stride;
        uint32_t bit_pos = (uint32_t)(cx0 - x) * bpp;
        colour_t *dst = lcd->pfb_gram +
                        (py - (int16_t)lcd->pfb_y_start) * lcd->pfb_width +
                        (cx0 - (int16_t)lcd->pfb_area.x0);
        int16_t px;

        for (px = cx0; px <= cx1; px++, dst++)
        {
            uint8_t a_raw = (uint8_t)((src_row[bit_pos >> 3] >>
                                       (8U - bpp - (bit_pos & 7U))) & a_mask);

            bit_pos += bpp;
            if (a_raw == 0U)
                continue;
            {
                uint32_t alpha = (uint32_t)a_raw * a_mult;

                if (opacity != 255U)
                    alpha = we_div255(alpha * opacity);
                we_store_blended_color(dst, fg, (uint8_t)alpha);
            }
        }
    }
}

/**
 * @brief 从 start_x 起窗口化绘制一段单行文本。
 * @param obj 控件对象指针。
 * @param lcd GUI 屏幕上下文指针（PFB 窗口已收窄）。
 * @param start_x 本段文本光标起点 X（屏幕绝对坐标，可为窗口外负偏移）。
 * @param text_y 文本行顶 Y（字形按 y_ofs 相对此基准落位）。
 * @param opacity 有效不透明度（已含容器级联，> 0）。
 * @return 无。
 * @note 快进阶段：墨迹右缘（cursor + x_ofs + box_w）未进窗口左缘的字形
 *       只做解码 + adv_w 游标累加，不取位图、不扫像素；绘制阶段逐字形
 *       blit；游标越过窗口右缘（cursor_x > win_x1，与 we_draw_string 同
 *       判据）立即结束本段。窗口 = 控件矩形 ∩ 当前脏区带，所以局部重绘
 *       时快进范围自动进一步收窄。遇 '\n' 即止（单行截断语义）。
 */
static void _marquee_draw_segment(const we_marquee_obj_t *obj, we_lcd_t *lcd,
                                  int16_t start_x, int16_t text_y, uint8_t opacity)
{
    const char *p = obj->text;
    int16_t cursor_x = start_x;
    int16_t win_x0 = (int16_t)lcd->pfb_area.x0;
    int16_t win_x1 = (int16_t)lcd->pfb_area.x1;
    we_glyph_info_t info;
    uint16_t code;

    while (cursor_x <= win_x1 && _marquee_utf8_next(&p, &code) != 0U)
    {
        if (code == (uint16_t)'\n')
            break; /* 只画第一行：与 we_get_text_width 测宽口径一致 */
        if (we_font_get_glyph_info(obj->font, code, &info) == 0U)
            continue; /* 字库缺字：跳过且不推游标（与 we_draw_string 一致） */

        if (info.box_w > 0U && info.box_h > 0U &&
            (int16_t)(cursor_x + info.x_ofs + (int16_t)info.box_w) > win_x0 &&
            (int16_t)(cursor_x + info.x_ofs) <= win_x1)
        {
            const uint8_t *bitmap = NULL;
            uint8_t bpp = 0U;
            uint32_t row_stride = 0U;

            if (we_font_get_bitmap_info(obj->font, &info, &bitmap, &bpp, &row_stride) != 0U)
            {
                _marquee_blit_glyph(lcd, (int16_t)(cursor_x + info.x_ofs),
                                    (int16_t)(text_y + info.y_ofs),
                                    info.box_w, info.box_h,
                                    bitmap, row_stride, bpp, obj->color, opacity);
            }
        }
        cursor_x = (int16_t)(cursor_x + (int16_t)info.adv_w);
    }
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：收窄 PFB 窗口到自身矩形后窗口化绘制一段或两段文本。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _marquee_draw_cb(void *ptr)
{
    we_marquee_obj_t *obj = (we_marquee_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t opacity;

    if (obj->text == NULL || obj->font == NULL || obj->opacity == 0U)
        return;
    opacity = we_opa_apply(lcd, obj->opacity); /* 容器透明度级联：入口消费一次 */
    if (opacity == 0U)
        return;

    {
        /* group 同款 PFB 窗口收窄：窗口外的字形像素被自动裁掉 */
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;

        int16_t new_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
        int16_t new_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
        int16_t new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
        int16_t new_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

        if (new_x0 <= new_x1 && new_y0 <= new_y1)
        {
            int16_t text_y = (int16_t)(obj->base.y + WE_MARQUEE_PAD_Y);

            lcd->pfb_area.x0 = (uint16_t)new_x0;
            lcd->pfb_area.x1 = (uint16_t)new_x1;
            lcd->pfb_y_start = (uint16_t)new_y0;
            lcd->pfb_y_end = (uint16_t)new_y1;
            lcd->pfb_gram = old_gram + (new_y0 - (int16_t)old_y_start) * lcd->pfb_width +
                            (new_x0 - (int16_t)old_pfb_area.x0);

            if (obj->text_w <= (uint16_t)obj->base.w)
            {
                /* 文本装得下：静止左对齐（对照态，不滚动） */
                _marquee_draw_segment(obj, lcd, obj->base.x, text_y, opacity);
            }
            else
            {
                /* 循环滚动："文本 + 间隔 + 文本开头" 两段，形成无缝循环 */
                int16_t seg_x = (int16_t)(obj->base.x - obj->offset);

                _marquee_draw_segment(obj, lcd, seg_x, text_y, opacity);
                _marquee_draw_segment(obj, lcd,
                                      (int16_t)(seg_x + (int16_t)obj->text_w + WE_MARQUEE_GAP),
                                      text_y, opacity);
            }
        }

        lcd->pfb_area = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram = old_gram;
    }
}

/* --------------------------------------------------------------------------
 * 生命周期与 setter
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化跑马灯标签并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 可视窗口左上角 X（屏幕绝对坐标）。
 * @param y 可视窗口左上角 Y。
 * @param w 可视窗口宽度（像素）。
 * @param text UTF-8 文本字符串（调用方持有）。
 * @param color 文字前景色。
 * @return 无。
 */
void we_marquee_obj_init(we_marquee_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w,
                         const char *text, const unsigned char *font, colour_t color)
{
    uint16_t line_h;

    if (obj == NULL || lcd == NULL || font == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.class_p = &_marquee_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->font = font;
    line_h = we_font_get_line_height(obj->font);
    obj->base.h = (int16_t)(line_h + 2 * WE_MARQUEE_PAD_Y);

    obj->text = text;
    obj->text_w = (text != NULL) ? we_get_text_width(obj->font, text) : 0U;
    obj->color = color;
    obj->opacity = 255U;
    obj->speed = WE_MARQUEE_DEF_SPEED;
    obj->pause_ms = WE_MARQUEE_DEF_PAUSE;

    obj->scrolling = 0U;
    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    _marquee_reset_scroll(obj);

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    _marquee_update_anim(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 更换文本并重置滚动位置。
 * @param obj 控件对象指针。
 * @param new_text 新的 UTF-8 文本字符串（调用方持有）。
 * @return 无。
 */
void we_marquee_set_text(we_marquee_obj_t *obj, const char *new_text)
{
    if (obj == NULL || obj->base.lcd == NULL || new_text == NULL)
        return;

    obj->text = new_text;
    obj->text_w = we_get_text_width(obj->font, new_text);
    _marquee_reset_scroll(obj);
    _marquee_update_anim(obj);
    we_obj_invalidate((we_obj_t *)obj); /* 窗口矩形不变，一次标脏覆盖新旧内容 */
}

/**
 * @brief 更换字库：重测文本宽、重置滚动位置，控件高度随行高更新。
 * @param obj 控件对象指针。
 * @param font 新字库指针（仅支持 font2c internal 字库）；指针未变直接返回。
 * @return 无。
 * @note 高度变化的标脏顺序：先按旧矩形标脏（擦除旧高度残留），再更新
 *       h/text_w/滚动态，最后按新矩形标脏——旧高大于新高时下沿不留残影。
 *       行高读不出来（非法字库）时忽略本次调用，控件状态不变。
 */
void we_marquee_set_font(we_marquee_obj_t *obj, const unsigned char *font)
{
    uint16_t line_h;

    if (obj == NULL || obj->base.lcd == NULL || font == NULL)
        return;
    if (obj->font == font)
        return;

    line_h = we_font_get_line_height(font);
    if (line_h == 0U)
        return; /* 非法字库：拒绝切换，保持原字体 */

    we_obj_invalidate((we_obj_t *)obj); /* 1. 先标脏旧矩形（旧高度） */

    obj->font = font;
    obj->base.h = (int16_t)(line_h + 2 * WE_MARQUEE_PAD_Y);
    obj->text_w = (obj->text != NULL) ? we_get_text_width(font, obj->text) : 0U;
    _marquee_reset_scroll(obj);
    _marquee_update_anim(obj); /* 新字体宽度可能跨过滚动阈值：同步挂/摘动画链 */

    we_obj_invalidate((we_obj_t *)obj); /* 2. 再标脏新矩形（新高度） */
}

/**
 * @brief 设置滚动速度（像素/秒），钳制到 1..WE_MARQUEE_SPEED_MAX。
 * @param obj 控件对象指针。
 * @param px_per_s 新速度；值未变直接返回。
 * @return 无。
 */
void we_marquee_set_speed(we_marquee_obj_t *obj, uint16_t px_per_s)
{
    if (obj == NULL)
        return;
    if (px_per_s < 1U)
        px_per_s = 1U;
    if (px_per_s > WE_MARQUEE_SPEED_MAX)
        px_per_s = WE_MARQUEE_SPEED_MAX;
    if (obj->speed == px_per_s)
        return;

    obj->speed = px_per_s;
    obj->frac_acc = 0; /* 丢弃不足 1px 的旧余量，避免新旧速度单位混算 */
}

/**
 * @brief 设置循环接缝处的停留时长（毫秒）。
 * @param obj 控件对象指针。
 * @param ms 停留毫秒数（0 = 不停留）；值未变直接返回。
 * @return 无。
 */
void we_marquee_set_pause(we_marquee_obj_t *obj, uint16_t ms)
{
    if (obj == NULL || obj->pause_ms == ms)
        return;

    obj->pause_ms = ms;
    if (ms == 0U)
        obj->paused = 0U; /* 关停留：正在停留的立即恢复滚动 */
}

/**
 * @brief 设置文字颜色并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param color 新的文字前景色。
 * @return 无。
 */
void we_marquee_set_color(we_marquee_obj_t *obj, colour_t color)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (_marquee_colour_eq(obj->color, color))
        return;

    obj->color = color;
    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255；0 时停止滚动动画）。
 * @return 无。
 */
void we_marquee_set_opacity(we_marquee_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == opacity)
        return;

    obj->opacity = opacity;
    _marquee_update_anim(obj); /* 0 → 停滚动省周期；>0 → 需要时恢复 */
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除控件：先摘除滚动动画节点再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_marquee_obj_delete(we_marquee_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;

    we_anim_stop(obj->base.lcd, &obj->anim); /* 动画节点归控件所有，删除前必须摘链 */
    obj->scrolling = 0U;
    we_obj_delete((we_obj_t *)obj);
}
