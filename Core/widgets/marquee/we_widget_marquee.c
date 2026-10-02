/**
 * @file  we_widget_marquee.c
 * @brief 跑马灯标签控件（marquee）：固定窗口内循环滚动的单行文本
 *
 * 渲染：内核调用 draw_cb 前已把绘制窗口收窄到自身矩形，
 * 这里在偏移位置绘制 "文本 + 间隔 + 文本开头" 两段，越界部分被窗口裁掉。
 *
 * 滚动：单个中央动画节点（we_anim_t）推进，毫像素整数累计
 * frac_acc += elapsed_ms * speed，凑满 1000 前进 1px；
 * offset 到达 text_w + WE_MARQUEE_GAP 后回零并停留 pause_ms。
 *
 * 实现要点：
 *   1. 自建窗口化字形绘制循环（_marquee_draw_segment），不走 we_draw_string
 *      全量遍历：窗口左侧完全裁掉的字形只做 UTF-8 解码 + adv_w 游标快进
 *      （零位图取址、零像素扫描），游标越过窗口右缘立即 break；
 *      逐帧绘制成本只与可见字形数相关，与文本总长解耦；
 *   2. 字体参数化：we_marquee_set_font 支持运行时换字库（重测宽度、
 *      重置滚动、行高联动控件高度，先标脏旧区再改再标脏新区）；
 *   3. 单行截断语义与测宽同口径：绘制遇 '\n' 即止，只显示第一行。
 */

#include "we_widget_marquee.h"
#include "wegui.h"

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
 * 窗口化字形绘制（左侧游标快进 + 右缘提前退出）
 * -------------------------------------------------------------------------- */

/**
 * @brief 从 start_x 起窗口化绘制一段单行文本。
 * @param obj 控件对象指针。
 * @param lcd GUI 屏幕上下文指针（绘制窗口 = 控件矩形 ∩ 当前切片）。
 * @param start_x 本段文本光标起点 X（屏幕绝对坐标，可为窗口外负偏移）。
 * @param text_y 文本行顶 Y（字形按 y_ofs 相对此基准落位）。
 * @return 无。
 * @note 快进阶段：有效像素区右缘（cursor + x_ofs + box_w）未进窗口左缘的字形
 *       只做解码 + adv_w 游标累加，不取位图、不扫像素；绘制阶段逐字形
 *       blit；游标越过窗口右缘（cursor_x > win_x1，与 we_draw_string 同
 *       判据）立即结束本段。窗口 = 控件矩形 ∩ 当前脏区带，所以局部重绘
 *       时快进范围自动进一步收窄。遇 '\n' 即止（单行截断语义）。
 */
static void _marquee_draw_segment(const we_marquee_obj_t *obj, we_lcd_t *lcd,
                                  int16_t start_x, int16_t text_y)
{
    const char *p = obj->text;
    int16_t cursor_x = start_x;
    int16_t win_x0 = lcd->clip.x0;
    int16_t win_x1 = lcd->clip.x1;
    we_glyph_info_t info;
    uint16_t code;

    while (cursor_x <= win_x1 && we_utf8_next(&p, &code) != 0U)
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
                we_draw_alpha_mask(lcd, (int16_t)(cursor_x + info.x_ofs),
                                   (int16_t)(text_y + info.y_ofs),
                                   info.box_w, info.box_h,
                                   bitmap, row_stride, bpp, obj->color, obj->opacity);
            }
        }
        cursor_x = (int16_t)(cursor_x + (int16_t)info.adv_w);
    }
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：在自身矩形（内核已收窄的窗口）内绘制一段或两段文本。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _marquee_draw_cb(void *ptr)
{
    we_marquee_obj_t *obj = (we_marquee_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    int16_t text_y;

    if (obj->text == NULL || obj->font == NULL || obj->opacity == 0U)
        return;
    if (we_opa_apply(lcd, obj->opacity) == 0U)
        return; /* 容器整体透明：什么都不画 */

    text_y = (int16_t)(obj->base.y + WE_MARQUEE_PAD_Y);
    if (obj->text_w <= (uint16_t)obj->base.w)
    {
        /* 文本装得下：静止左对齐（对照态，不滚动） */
        _marquee_draw_segment(obj, lcd, obj->base.x, text_y);
    }
    else
    {
        /* 循环滚动："文本 + 间隔 + 文本开头" 两段，形成无缝循环 */
        int16_t seg_x = (int16_t)(obj->base.x - obj->offset);

        _marquee_draw_segment(obj, lcd, seg_x, text_y);
        _marquee_draw_segment(obj, lcd,
                              (int16_t)(seg_x + (int16_t)obj->text_w + WE_MARQUEE_GAP),
                              text_y);
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
    if (we_colour_eq(obj->color, color))
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
