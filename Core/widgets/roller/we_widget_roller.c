#include "we_widget_roller.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * roller —— 滚轮选值器（preview 孵化区，毕业级优化版）
 *
 * 结构：面板背景（圆角矩形）+ 中心高亮条 + 逐行文字。
 * 行文字统一经 PFB 窗口收窄裁剪在控件矩形内（scroll_panel 同款套路），
 * 半露行不会渗出控件边界。滚动为像素级 scroll_px（int32 累计）。
 *
 * 本轮毕业优化（相对首版 preview）：
 *   1. 惯性甩动：STAY 步进测速（scroll_panel 同款约定），松手速度超阈值
 *      时按几何衰减级数外推落点取整到行，速度作为吸附动画初速种子；
 *   2. 点击直达：轻点上/下可见行经吸附动画滚到该行，点中心行不动作；
 *   3. 精细标脏：滚动位移只标"文本列带"（排除面板圆角外区域），
 *      吸附 commit 只标中心行条带；
 *   4. 文字测量缓存：y 方向 bbox 字体常量化 + 行宽直接映射缓存，
 *      绘制内环零 we_get_text_width / we_get_text_bbox 调用；
 *   5. we_roller_set_font：字体参数化（几何重推导 + scroll 比例换算）。
 * -------------------------------------------------------------------------- */

#if (WE_ROLLER_WCACHE_SIZE & (WE_ROLLER_WCACHE_SIZE - 1U)) != 0U
#error "WE_ROLLER_WCACHE_SIZE must be a power of two"
#endif

/* 行宽缓存空槽标记：option_cnt 为 uint16，合法索引最大 65534，0xFFFF 永不冲突 */
#define _ROLLER_WCACHE_EMPTY 0xFFFFU

/* 距中心 0/1/2/3/4+ 行的文字透明度分档（档间按像素线性插值） */
static const uint8_t _roller_alpha_tab[5] = { 255U, 160U, 90U, 55U, 40U };

/**
 * @brief 将透明度按控件整体不透明度缩放。
 * @param a 传入：原始透明度（0~255）。
 * @param opacity 传入：控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _roller_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/**
 * @brief 按距中心的像素距离计算行文字透明度（分档 + 线性插值）。
 * @param dist_px 传入：行中心距控件中心的像素距离（>= 0）。
 * @param row_h 传入：行高（像素，> 0）。
 * @return 行文字透明度（0~255）。
 */
static uint8_t _roller_row_alpha(int32_t dist_px, uint16_t row_h)
{
    int32_t level = dist_px / (int32_t)row_h;
    int32_t frac = dist_px % (int32_t)row_h;
    int32_t a0;
    int32_t a1;

    if (level >= 4)
        return _roller_alpha_tab[4];

    a0 = (int32_t)_roller_alpha_tab[level];
    a1 = (int32_t)_roller_alpha_tab[level + 1];
    return (uint8_t)(a0 + ((a1 - a0) * frac) / (int32_t)row_h);
}

/**
 * @brief 最大可滚动像素（末项对准中心时的 scroll_px）。
 * @param obj 传入：控件对象指针。
 * @return 最大 scroll_px（无选项时为 0）。
 */
static int32_t _roller_max_scroll(const we_roller_obj_t *obj)
{
    if (obj->option_cnt == 0U)
        return 0;
    return (int32_t)(obj->option_cnt - 1U) * (int32_t)obj->row_h;
}

/* --------------------------------------------------------------------------
 * 文字测量缓存
 * -------------------------------------------------------------------------- */

/**
 * @brief 读取指定选项的行宽（经直接映射缓存，未命中才测量一次）。
 * @param obj 传入：控件对象指针（须保证 options / font 有效）。
 * @param idx 传入：选项索引（须保证 < option_cnt）。
 * @return 行文字宽度（像素；文本为 NULL 时为 0）。
 * @note 缓存方案权衡：槽位 = 索引 & (SIZE-1) 的直接映射，查找 / 替换均
 *       O(1) 无除法。滚轮索引访问天然连续（可见窗逐行 + 滚动窗口平移），
 *       任意 SIZE 个连续索引槽位互不冲突，SIZE >= 可见行数 + 2 即保证
 *       同一帧窗口内零互逐——LRU 的链表/时间戳维护开销在此访问模式下
 *       买不到额外命中率。选项数无上限（旧槽被新索引自然覆盖）、零 malloc。
 */
static uint16_t _roller_text_width_cached(we_roller_obj_t *obj, int32_t idx)
{
    uint16_t slot = (uint16_t)((uint32_t)idx & (WE_ROLLER_WCACHE_SIZE - 1U));
    const char *text;

    if (obj->wcache_idx[slot] == (uint16_t)idx)
        return obj->wcache_w[slot];

    text = obj->options[idx];
    obj->wcache_w[slot] = (text != NULL) ? we_get_text_width(obj->font, text) : 0U;
    obj->wcache_idx[slot] = (uint16_t)idx;
    return obj->wcache_w[slot];
}

/**
 * @brief 刷新文字测量缓存：清空行宽缓存并重算 y 方向 bbox 常量。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note y bbox 对同一字体 + 同一选项集是常量：扫描前 WE_ROLLER_BBOX_SCAN_MAX
 *       个选项取墨迹纵向并集，缓存 y_top / y_bot 与派生的行内垂直居中偏移
 *       text_dy。所有行共用同一垂直基准，滚动中不会因逐行独立 bbox 产生
 *       基线抖动。row_h 或字体或选项变化后必须调用本函数。
 */
static void _roller_refresh_text_metrics(we_roller_obj_t *obj)
{
    uint16_t k;
    int16_t top = 127;
    int16_t bot = -128;

    for (k = 0U; k < WE_ROLLER_WCACHE_SIZE; k++)
        obj->wcache_idx[k] = _ROLLER_WCACHE_EMPTY;

    if (obj->font != NULL && obj->options != NULL && obj->option_cnt > 0U)
    {
        uint16_t n = obj->option_cnt;
        uint16_t i;

        if (n > WE_ROLLER_BBOX_SCAN_MAX)
            n = (uint16_t)WE_ROLLER_BBOX_SCAN_MAX;
        for (i = 0U; i < n; i++)
        {
            int8_t t;
            int8_t b;

            if (obj->options[i] == NULL)
                continue;
            we_get_text_bbox(obj->font, obj->options[i], &t, &b);
            if ((int16_t)t < top)
                top = (int16_t)t;
            if ((int16_t)b > bot)
                bot = (int16_t)b;
        }
    }

    if (top > bot)
    {
        /* 未扫到任何墨迹（无选项/全空串）：回退整行高，
         * 与 we_get_text_bbox 的空墨迹回退语义一致 */
        uint16_t lh = (obj->font != NULL) ? we_font_get_line_height(obj->font) : 0U;

        if (lh > 127U)
            lh = 127U;
        top = 0;
        bot = (int16_t)lh;
    }

    obj->text_y_top = (int8_t)top;
    obj->text_y_bot = (int8_t)bot;
    /* 行内垂直居中：ty = 行顶 + row_h/2 - (top+bot)/2，后两项均为常量，
     * 合并成 text_dy 后绘制内环只剩一次加法 */
    obj->text_dy = (int16_t)((int16_t)(obj->row_h / 2U) - (int16_t)((top + bot) / 2));
}

/* --------------------------------------------------------------------------
 * 精细标脏
 * -------------------------------------------------------------------------- */

/**
 * @brief 按当前 scroll_px 计算可见窗（中心行 ± (半窗+1)）内的最大行宽。
 * @param obj 传入：控件对象指针（须保证 options / row_h 有效）。
 * @return 窗口内最大行宽（像素）。
 * @note 行宽读取全部走缓存：窗口平移时仅新进窗的行触发一次实测，
 *       满足"每行至多一次测量/滚动步"。
 */
static uint16_t _roller_window_max_text_w(we_roller_obj_t *obj)
{
    int16_t half_rows = (int16_t)(obj->visible_rows / 2U);
    int32_t center_idx = obj->scroll_px / (int32_t)obj->row_h;
    int32_t i0 = center_idx - (int32_t)half_rows - 1;
    int32_t i1 = center_idx + (int32_t)half_rows + 1;
    uint16_t max_w = 0U;
    int32_t i;

    if (i0 < 0)
        i0 = 0;
    if (i1 > (int32_t)(obj->option_cnt - 1U))
        i1 = (int32_t)(obj->option_cnt - 1U);

    for (i = i0; i <= i1; i++)
    {
        uint16_t tw = _roller_text_width_cached(obj, i);

        if (tw > max_w)
            max_w = tw;
    }
    return max_w;
}

/**
 * @brief 滚动位移的精细标脏：只标"文本列带"（水平居中、全控件高的竖条）。
 * @param obj 传入：控件对象指针（scroll_px 已更新为新值）。
 * @return 无。
 * @note 滚动中面板背景与中心高亮条像素不变，唯一变化的是逐行水平居中的
 *       文字，其外接区即"窗口最大行宽 + 2*WE_ROLLER_DIRTY_PAD"的居中列带。
 *       列带宽度取 max(新窗口列带, 上一帧在屏列带 disp_band_w)：窗口平移
 *       时旧文字（曾按旧带宽绘制）与新文字都被覆盖。列带窄于控件宽时，
 *       面板圆角所在的左右边缘列完全不进脏区（即"排除面板圆角外的区域，
 *       只标内容裁剪矩形"）；文本宽逼近控件宽时退化为整件标脏（此时文字
 *       本来就会侵入圆角区，无法再排除）。
 */
static void _roller_invalidate_scroll_band(we_roller_obj_t *obj)
{
    uint16_t new_band;
    uint16_t dirty_w;
    uint16_t max_w;
    int16_t bx;

    if (obj->options == NULL || obj->option_cnt == 0U ||
        obj->font == NULL || obj->row_h == 0U)
    {
        /* 无文本内容：保守整件标脏（实际不可达——无选项时量程为 0） */
        obj->disp_band_w = (uint16_t)obj->base.w;
        we_obj_invalidate((we_obj_t *)obj);
        return;
    }

    max_w = _roller_window_max_text_w(obj);
    new_band = (max_w > 0U) ? (uint16_t)(max_w + 2U * (uint16_t)WE_ROLLER_DIRTY_PAD) : 0U;

    dirty_w = (new_band > obj->disp_band_w) ? new_band : obj->disp_band_w;
    obj->disp_band_w = new_band;

    if (dirty_w == 0U)
        return; /* 可见窗内全为空文本，滚动无可见变化 */

    if (dirty_w >= (uint16_t)obj->base.w)
    {
        we_obj_invalidate((we_obj_t *)obj);
        return;
    }

    bx = (int16_t)(obj->base.x + (obj->base.w - (int16_t)dirty_w) / 2);
    we_obj_invalidate_area((we_obj_t *)obj, bx, obj->base.y,
                           (int16_t)dirty_w, obj->base.h);
}

/**
 * @brief 标脏中心行条带（吸附完成 commit 专用）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _roller_invalidate_center_row(we_roller_obj_t *obj)
{
    int16_t half_rows = (int16_t)(obj->visible_rows / 2U);
    int16_t center_top = (int16_t)(obj->base.y + half_rows * (int16_t)obj->row_h);

    we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, center_top,
                           obj->base.w, (int16_t)obj->row_h);
}

/* --------------------------------------------------------------------------
 * 滚动 / 吸附 / 惯性
 * -------------------------------------------------------------------------- */

/**
 * @brief 将 scroll_px 硬夹紧到 [0, max] 后应用并按需标脏（文本列带）。
 * @param obj 传入：控件对象指针。
 * @param new_scroll 传入：目标滚动像素。
 * @return 无。
 */
static void _roller_apply_scroll(we_roller_obj_t *obj, int32_t new_scroll)
{
    int32_t max_scroll = _roller_max_scroll(obj);

    if (new_scroll < 0)
        new_scroll = 0;
    if (new_scroll > max_scroll)
        new_scroll = max_scroll;

    if (new_scroll == obj->scroll_px)
        return;

    obj->scroll_px = new_scroll;
    _roller_invalidate_scroll_band(obj);
}

/**
 * @brief 停止吸附动画并复位速度累计（摘链停表）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _roller_stop_anim(we_roller_obj_t *obj)
{
    obj->snap_animating = 0U;
    obj->snap_v = 0;
    we_anim_stop(obj->base.lcd, &obj->anim);
}

/**
 * @brief 吸附到位后提交选中索引，索引变化时触发 changed_cb。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 只标脏中心行条带：滚动过程的位移已由文本列带标脏覆盖，
 *       commit 补标中心行保证终帧高亮条 + 主色文字完整重绘。
 */
static void _roller_commit_selection(we_roller_obj_t *obj)
{
    int16_t idx;

    if (obj->option_cnt == 0U || obj->row_h == 0U)
        return;

    _roller_invalidate_center_row(obj);

    idx = (int16_t)((obj->scroll_px + (int32_t)(obj->row_h / 2U)) / (int32_t)obj->row_h);
    if (idx < 0)
        idx = 0;
    if (idx >= (int16_t)obj->option_cnt)
        idx = (int16_t)(obj->option_cnt - 1U);

    if (idx != obj->sel_idx)
    {
        obj->sel_idx = idx;
        if (obj->changed_cb != NULL)
            obj->changed_cb(obj, (uint16_t)idx);
    }
}

/**
 * @brief 吸附动画步进回调：纯整数"拉力 + 阻尼"缓动逼近目标行。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次调度经过的毫秒数。
 * @return 无。
 * @note 参数与 slideshow COMPLEX 吸附模式保持一致；到位后自行摘链，
 *       并提交选中索引（索引变化时回调）。惯性甩动时 snap_v 以松手
 *       速度作初速种子进入本回调，速度曲线在松手瞬间连续。
 */
static void _roller_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_roller_obj_t *obj = (we_roller_obj_t *)owner;
    int32_t diff;
    int32_t pull;
    int32_t move;
    int32_t max_step;
    uint16_t step_ms;

    if (obj == NULL || elapsed_ms == 0U)
        return;
    if (!obj->snap_animating || obj->dragging)
        return;

    step_ms = (elapsed_ms > 64U) ? 64U : elapsed_ms;
    diff = obj->snap_target - obj->scroll_px;

    if (diff != 0)
    {
        /* 拉力：距离越远拉力越大，按帧时长折算到 16ms 基准 */
        pull = diff / WE_ROLLER_SNAP_PULL_DIV;
        if (pull != 0)
        {
            pull = (pull * (int32_t)step_ms + 15) / 16;
            if (pull == 0)
                pull = (diff > 0) ? 1 : -1;
        }

        /* 阻尼：速度累计后按 NUM/DEN 衰减 */
        obj->snap_v += pull;
        obj->snap_v = (obj->snap_v * WE_ROLLER_SNAP_DAMP_NUM) / WE_ROLLER_SNAP_DAMP_DEN;

        move = obj->snap_v;
        if (move == 0)
            move = (diff > 0) ? 1 : -1;

        /* 单帧位移限幅（16ms 基准按帧时长缩放） */
        max_step = ((int32_t)WE_ROLLER_SNAP_MAX_STEP * (int32_t)step_ms + 15) / 16;
        if (max_step < 1)
            max_step = 1;
        if (move > max_step)
            move = max_step;
        if (move < -max_step)
            move = -max_step;
        if (WE_ABS(move) > WE_ABS(diff))
            move = diff; /* 防过冲越过目标 */

        _roller_apply_scroll(obj, obj->scroll_px + move);
    }

    if (obj->scroll_px == obj->snap_target)
    {
        _roller_stop_anim(obj);
        _roller_commit_selection(obj);
    }
}

/**
 * @brief 以指定 scroll 目标启动吸附动画（已在目标上则直接提交）。
 * @param obj 传入：控件对象指针。
 * @param target 传入：目标 scroll_px（调用方须保证已行对齐 + 已夹紧）。
 * @param seed_v 传入：吸附初速种子（px/调度周期；非甩动场景传 0）。
 * @return 无。
 */
static void _roller_start_snap_to(we_roller_obj_t *obj, int32_t target, int32_t seed_v)
{
    obj->snap_target = target;
    obj->snap_v = seed_v;

    if (target == obj->scroll_px)
    {
        /* 已经对准目标行（例如轻点未拖），无需动画，立即提交 */
        _roller_stop_anim(obj);
        _roller_commit_selection(obj);
        return;
    }

    obj->snap_animating = 1U;
    we_anim_start(obj->base.lcd, &obj->anim, _roller_anim_step_cb, obj);
}

/**
 * @brief 以当前 scroll_px 计算最近行并启动吸附动画（慢速松手路径）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _roller_begin_snap(we_roller_obj_t *obj)
{
    int32_t target;
    int32_t max_scroll;

    if (obj->option_cnt == 0U || obj->row_h == 0U)
        return;

    target = ((obj->scroll_px + (int32_t)(obj->row_h / 2U)) / (int32_t)obj->row_h)
             * (int32_t)obj->row_h;
    max_scroll = _roller_max_scroll(obj);
    if (target < 0)
        target = 0;
    if (target > max_scroll)
        target = max_scroll;

    _roller_start_snap_to(obj, target, 0);
}

/**
 * @brief 惯性甩动：按松手速度外推落点取整到行，速度连续地进入吸附动画。
 * @param obj 传入：控件对象指针（fling_v 为松手速度，px/调度周期）。
 * @return 无。
 * @note 整数外推推导：设松手后速度按每周期 f 几何衰减，总滑行距离为
 *       等比级数和 S = v*f/(1-f)；取 f = 6/7 得 S = 6*v，即
 *       落点 = scroll_px + v * WE_ROLLER_FLING_PROJ_NUM / PROJ_DEN
 *       （默认 6/1，纯整数乘除）。落点夹紧到 [0, max_scroll] 后按
 *       (落点 + row_h/2) / row_h 取整到最近行（落点非负，C 整除即
 *       floor，无需负数修正）。max_scroll 本身行对齐，取整结果不会
 *       越界。减速过程不做逐帧速度积分：落点交给现有拉力+阻尼动画
 *       趋近，fling_v 作 snap_v 初速种子（两者同为 px/调度周期，
 *       松手瞬间速度连续），外推系数只决定"落在哪一行"。
 */
static void _roller_begin_fling(we_roller_obj_t *obj)
{
    int32_t landing;
    int32_t target;
    int32_t max_scroll;
    int32_t seed;

    if (obj->option_cnt == 0U || obj->row_h == 0U)
        return;

    landing = obj->scroll_px
              + ((int32_t)obj->fling_v * WE_ROLLER_FLING_PROJ_NUM) / WE_ROLLER_FLING_PROJ_DEN;
    max_scroll = _roller_max_scroll(obj);
    if (landing < 0)
        landing = 0;
    if (landing > max_scroll)
        landing = max_scroll;

    target = ((landing + (int32_t)(obj->row_h / 2U)) / (int32_t)obj->row_h)
             * (int32_t)obj->row_h;

    /* 初速种子仅在与目标方向一致时保留：落点被量程夹紧（或超大行高下
     * 取整回卷）可能使目标落在速度反方向，此时按普通吸附从 0 起速 */
    seed = (int32_t)obj->fling_v;
    if ((target > obj->scroll_px && seed < 0) || (target < obj->scroll_px && seed > 0))
        seed = 0;

    _roller_start_snap_to(obj, target, seed);
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 绘制回调：面板背景 + 中心高亮条 + PFB 收窄裁剪的逐行文字。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 * @note 行宽与垂直居中偏移全部来自测量缓存，内环零字体测量调用。
 */
static void _roller_draw_cb(void *ptr)
{
    we_roller_obj_t *obj = (we_roller_obj_t *)ptr;
    we_lcd_t *lcd;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    int16_t clip_x0;
    int16_t clip_y0;
    int16_t clip_x1;
    int16_t clip_y1;
    int16_t half_rows;
    int16_t center_top;
    int32_t center_idx;
    int32_t i0;
    int32_t i1;
    int32_t i;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->row_h == 0U)
        return;

    half_rows = (int16_t)(obj->visible_rows / 2U);
    center_top = (int16_t)(obj->base.y + half_rows * (int16_t)obj->row_h);

    /* 1. 面板背景 */
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     WE_ROLLER_PANEL_RADIUS, obj->bg_color,
                                     obj->opacity);

    /* 2. 中心行高亮条（上下各收 1px 制造行距感） */
    we_draw_round_rect_analytic_fill(lcd,
                                     (int16_t)(obj->base.x + WE_ROLLER_BAR_INSET),
                                     (int16_t)(center_top + 1),
                                     (uint16_t)(obj->base.w - 2 * WE_ROLLER_BAR_INSET),
                                     (uint16_t)(obj->row_h - 2U),
                                     WE_ROLLER_BAR_RADIUS, obj->bar_color,
                                     obj->opacity);

    if (obj->options == NULL || obj->option_cnt == 0U || obj->font == NULL)
        return;

    /* 3. PFB 窗口收窄：把行文字裁剪在控件矩形内（save/restore 套路） */
    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    clip_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
    clip_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
    clip_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
    clip_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

    if (clip_x0 <= clip_x1 && clip_y0 <= clip_y1)
    {
        lcd->pfb_area.x0 = clip_x0;
        lcd->pfb_area.x1 = clip_x1;
        lcd->pfb_y_start = (uint16_t)clip_y0;
        lcd->pfb_y_end = (uint16_t)clip_y1;
        lcd->pfb_gram = old_gram + (clip_y0 - (int16_t)old_y_start) * lcd->pfb_width
                                 + (clip_x0 - old_pfb_area.x0);

        /* 只遍历可能可见的行：中心行索引 ± (半屏 + 1) */
        center_idx = obj->scroll_px / (int32_t)obj->row_h;
        i0 = center_idx - (int32_t)half_rows - 1;
        i1 = center_idx + (int32_t)half_rows + 1;
        if (i0 < 0)
            i0 = 0;
        if (i1 > (int32_t)(obj->option_cnt - 1U))
            i1 = (int32_t)(obj->option_cnt - 1U);

        for (i = i0; i <= i1; i++)
        {
            const char *text = obj->options[i];
            int32_t dist = i * (int32_t)obj->row_h - obj->scroll_px; /* 行相对中心偏移 */
            int32_t ady = WE_ABS(dist);
            int16_t ry = (int16_t)(center_top + dist);               /* 行顶部屏幕 Y */
            uint8_t alpha;
            colour_t tc;
            uint16_t text_w;
            int16_t tx;
            int16_t ty;

            if (text == NULL)
                continue;

            /* 距中心半行以内视为"中心行"：主色 + 满透明度 */
            if (ady <= (int32_t)(obj->row_h / 2U))
            {
                tc = obj->text_sel_color;
                alpha = 255U;
            }
            else
            {
                tc = obj->text_color;
                alpha = _roller_row_alpha(ady, obj->row_h);
            }
            alpha = _roller_scale_opa(alpha, obj->opacity);

            /* 水平居中（行宽走缓存）+ 行内垂直居中（bbox 常量派生 text_dy） */
            text_w = _roller_text_width_cached(obj, i);
            tx = (int16_t)(obj->base.x + (obj->base.w - (int16_t)text_w) / 2);
            ty = (int16_t)(ry + obj->text_dy);

            we_draw_string(lcd, tx, ty, obj->font, text, tc, alpha);
        }
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;
}

/* --------------------------------------------------------------------------
 * 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 事件回调主状态机：按下记录起点，跟手拖拽 + 步进测速，松手吸附/甩动。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 1 表示消费事件，0 表示穿透。
 */
static uint8_t _roller_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_roller_obj_t *obj = (we_roller_obj_t *)ptr;

    if (obj == NULL || data == NULL)
        return 0U;

    if (event == WE_EVENT_PRESSED)
    {
        _roller_stop_anim(obj); /* 打断进行中的吸附，立即接管 */
        obj->tracking = 1U;
        obj->dragging = 0U;
        obj->tap_armed = 1U;    /* 后续 CLICKED 若未经历拖拽 → 点击直达 */
        obj->press_y = data->y;
        obj->last_y = data->y;
        obj->fling_v = 0;
        obj->press_scroll = obj->scroll_px;
        return 1U;
    }

    if (!obj->tracking)
        return 1U; /* 无有效按压序列（例如按下发生在控件外），仅消费 */

    if (event == WE_EVENT_STAY)
    {
        int16_t dy = (int16_t)(data->y - obj->press_y);
        int16_t ady = (dy >= 0) ? dy : (int16_t)(-dy);

        if (!obj->dragging && ady >= WE_ROLLER_DRAG_THRESHOLD)
        {
            obj->dragging = 1U;
            obj->tap_armed = 0U; /* 进入拖拽即失去点击直达资格 */
        }

        if (obj->dragging)
        {
            /* 步进测速（scroll_panel 同款约定）：一次 STAY ≈ 一个调度周期，
             * 速度 = scroll 方向步进 = last_y - 当前 y（px/周期），与吸附
             * 动画 snap_v 同单位，松手时可直接作初速种子。指尖停驻
             * （步进为 0）时速度每周期减半归零，避免"拖快→停稳→松手"
             * 仍被甩飞。 */
            int16_t step = (int16_t)(obj->last_y - data->y);

            if (step != 0)
                obj->fling_v = step;
            else
                obj->fling_v = (int16_t)(obj->fling_v / 2);

            /* 手指下移 dy>0 → 内容跟随下移 → scroll_px 减小（跟手） */
            _roller_apply_scroll(obj, obj->press_scroll - (int32_t)dy);
        }
        obj->last_y = data->y;
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        uint8_t was_dragging = obj->dragging;
        int16_t av = (obj->fling_v >= 0) ? obj->fling_v : (int16_t)(-obj->fling_v);

        obj->dragging = 0U;
        obj->tracking = 0U;

        if (was_dragging && av >= WE_ROLLER_FLING_MIN_V)
            _roller_begin_fling(obj); /* 快速甩动：外推落点，惯性滑行 */
        else
            _roller_begin_snap(obj);  /* 慢速松手：就近吸附（已对准则立即提交） */
        return 1U;
    }

    return 1U;
}

/**
 * @brief 快速轻扫（无 STAY 的 SWIPE）：向滑动方向翻 1 行。
 * @param obj 传入：控件对象指针。
 * @param event 传入：SWIPE_UP / SWIPE_DOWN 事件。
 * @return 无。
 * @note SWIPE 在 RELEASED 之后派发，此时 tracking 已清零，故单独处理。
 */
static void _roller_handle_swipe(we_roller_obj_t *obj, we_event_t event)
{
    int32_t target;
    int32_t max_scroll;

    if (obj->option_cnt == 0U || obj->row_h == 0U)
        return;

    /* 以当前吸附目标为基准翻行，避免与 RELEASED 已启动的吸附冲突 */
    target = obj->snap_animating ? obj->snap_target : obj->scroll_px;
    if (event == WE_EVENT_SWIPE_UP)
        target += (int32_t)obj->row_h;   /* 手指上滑 → 内容上移 → 选下一项 */
    else
        target -= (int32_t)obj->row_h;   /* 手指下滑 → 选上一项 */

    max_scroll = _roller_max_scroll(obj);
    if (target < 0)
        target = 0;
    if (target > max_scroll)
        target = max_scroll;

    _roller_start_snap_to(obj, target, 0);
}

/**
 * @brief 点击直达：轻点上/下方可见行，经吸附动画滚到该行。
 * @param obj 传入：控件对象指针。
 * @param data 传入：输入数据（释放点坐标）。
 * @return 无。
 * @note CLICKED 在 RELEASED 之后派发（内核保证位移未达 SWIPE 阈值且未拖拽
 *       后仍落在本控件上）。tap_armed 仅在本按压序列从未进入拖拽时存活，
 *       拖拽后位移回落到阈值内的"假点击"不会触发。点中心行不动作。
 */
static void _roller_handle_tap(we_roller_obj_t *obj, we_indev_data_t *data)
{
    int16_t half_rows;
    int16_t row;
    int16_t delta;
    int32_t base_scroll;
    int32_t target;
    int32_t max_scroll;

    if (!obj->tap_armed)
        return;
    obj->tap_armed = 0U;

    if (obj->option_cnt == 0U || obj->row_h == 0U)
        return;

    half_rows = (int16_t)(obj->visible_rows / 2U);
    row = (int16_t)((data->y - obj->base.y) / (int16_t)obj->row_h);
    if (row < 0)
        row = 0;
    if (row >= (int16_t)obj->visible_rows)
        row = (int16_t)(obj->visible_rows - 1U);

    delta = (int16_t)(row - half_rows);
    if (delta == 0)
        return; /* 点中心行：不动作 */

    /* 以当前吸附基准行外推目标：RELEASED 已启动就近吸附时用其目标，
     * 否则以当前 scroll 就近取整（轻点场景 scroll 本就行对齐） */
    base_scroll = obj->snap_animating
                      ? obj->snap_target
                      : ((obj->scroll_px + (int32_t)(obj->row_h / 2U)) / (int32_t)obj->row_h)
                            * (int32_t)obj->row_h;
    target = base_scroll + (int32_t)delta * (int32_t)obj->row_h;

    max_scroll = _roller_max_scroll(obj);
    if (target < 0)
        target = 0;
    if (target > max_scroll)
        target = max_scroll;

    _roller_start_snap_to(obj, target, 0);
}

/**
 * @brief 事件回调外壳：把 SWIPE / CLICKED 分流，其余交给主状态机。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 1 表示消费事件，0 表示穿透。
 */
static uint8_t _roller_event_dispatch(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_roller_obj_t *obj = (we_roller_obj_t *)ptr;

    if (obj == NULL)
        return 0U;

    if (event == WE_EVENT_SWIPE_UP || event == WE_EVENT_SWIPE_DOWN)
    {
        obj->tap_armed = 0U; /* 判定为轻扫后不再是点击 */
        _roller_handle_swipe(obj, event);
        return 1U;
    }

    if (event == WE_EVENT_CLICKED)
    {
        if (data != NULL)
            _roller_handle_tap(obj, data);
        return 1U;
    }

    return _roller_event_cb(ptr, event, data);
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_ROLLER_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 进/退编辑态，编辑态上下键逐行吸附滚动。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 复用触摸路径的拉力+阻尼吸附动画（_roller_start_snap_to）；
 *       连按基于当前吸附目标行推进，滚动途中也能连续步进；
 *       落定且选中变化时照常触发 changed_cb。
 */
static uint8_t _roller_key_cb(void *ptr, uint8_t key_evt)
{
    we_roller_obj_t *obj = (we_roller_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U && obj->option_cnt > 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (we_focus_edit_active(lcd))
            we_focus_edit_exit(lcd);
        else
            we_focus_edit_enter(lcd);
        return 1U;
    case WE_KEY_UP:
    case WE_KEY_DOWN:
        if (!we_focus_edit_active(lcd))
            return 0U;
        if (obj->option_cnt == 0U || obj->row_h == 0U)
            return 1U;
        {
            int32_t row = obj->snap_animating ? (obj->snap_target / (int32_t)obj->row_h)
                                              : (int32_t)obj->sel_idx;
            row += (key_evt == WE_KEY_DOWN) ? 1 : -1;
            if (row < 0)
                row = 0;
            if (row > (int32_t)(obj->option_cnt - 1U))
                row = (int32_t)(obj->option_cnt - 1U);
            _roller_start_snap_to(obj, row * (int32_t)obj->row_h, 0);
        }
        return 1U;
    default:
        return 0U;
    }
}
#endif

static const we_class_t _roller_class = {
    .draw_cb = _roller_draw_cb,
    .event_cb = _roller_event_dispatch,
    .set_pos_cb = NULL, /* 通用移动逻辑（旧区标脏 + 新区标脏）已足够 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_ROLLER_USE_KEY == 1)
    .key_cb = _roller_key_cb,
#endif
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化滚轮控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param visible_rows 传入：可见行数；0 = 默认 5；偶数自动 +1 保持奇数。
 * @return 无。
 */
void we_roller_obj_init(we_roller_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, uint8_t visible_rows,
                        const unsigned char *font)
{
    uint16_t line_h;

    if (obj == NULL || lcd == NULL || font == NULL)
        return;

    if (visible_rows == 0U)
        visible_rows = WE_ROLLER_DEF_VISIBLE_ROWS;
    if ((visible_rows & 1U) == 0U)
        visible_rows++; /* 偶数补成奇数，保证有唯一中心行 */

    obj->font = font;
    line_h = we_font_get_line_height(obj->font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->row_h = (uint16_t)(line_h + 2U * WE_ROLLER_ROW_PAD);
    obj->visible_rows = visible_rows;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = (int16_t)((int16_t)visible_rows * (int16_t)obj->row_h);
    obj->base.class_p = &_roller_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->options = NULL;
    obj->option_cnt = 0U;
    obj->opacity = 255U;
    obj->sel_idx = -1;
    obj->scroll_px = 0;

    obj->bg_color = RGB888TODEV(30, 36, 48);
    obj->bar_color = RGB888TODEV(56, 70, 96);
    obj->text_color = RGB888TODEV(206, 214, 228);
    obj->text_sel_color = RGB888TODEV(112, 184, 255);
    obj->changed_cb = NULL;

    obj->tracking = 0U;
    obj->dragging = 0U;
    obj->tap_armed = 0U;
    obj->press_y = 0;
    obj->last_y = 0;
    obj->fling_v = 0;
    obj->press_scroll = 0;

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    obj->snap_animating = 0U;
    obj->snap_v = 0;
    obj->snap_target = 0;

    obj->disp_band_w = (uint16_t)obj->base.w; /* 整件重绘中，列带按全宽起步 */
    _roller_refresh_text_metrics(obj);        /* 清空行宽缓存 + bbox 回退值 */

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 绑定选项字符串数组（控件只保存指针，不复制内容）。
 * @param obj 传入：控件对象指针。
 * @param options 传入：字符串指针数组，需在控件生命周期内保持有效。
 * @param count 传入：选项个数。
 * @return 无。
 */
void we_roller_set_options(we_roller_obj_t *obj,
                           const char *const *options, uint16_t count)
{
    if (obj == NULL)
        return;
    if (obj->options == options && obj->option_cnt == count)
        return;

    _roller_stop_anim(obj);
    obj->options = options;
    obj->option_cnt = (options != NULL) ? count : 0U;
    obj->sel_idx = (obj->option_cnt > 0U) ? 0 : -1;
    obj->scroll_px = 0;
    obj->tracking = 0U;
    obj->dragging = 0U;
    obj->tap_armed = 0U;

    _roller_refresh_text_metrics(obj);        /* 新选项集：重算 bbox 常量 + 清行宽缓存 */
    obj->disp_band_w = (uint16_t)obj->base.w; /* 整件重绘，列带按全宽起步 */
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 立即定位到指定选项（无动画，不触发 changed_cb）。
 * @param obj 传入：控件对象指针。
 * @param index 传入：目标选项索引（越界时钳制到最后一项）。
 * @return 无。
 */
void we_roller_set_selected(we_roller_obj_t *obj, uint16_t index)
{
    int32_t target;

    if (obj == NULL || obj->option_cnt == 0U)
        return;
    if (index >= obj->option_cnt)
        index = (uint16_t)(obj->option_cnt - 1U);

    target = (int32_t)index * (int32_t)obj->row_h;
    if (obj->sel_idx == (int16_t)index && obj->scroll_px == target)
        return; /* 值未变直接返回 */

    _roller_stop_anim(obj);
    obj->sel_idx = (int16_t)index;
    obj->scroll_px = target;
    obj->disp_band_w = (uint16_t)obj->base.w; /* 任意跳变：整件重绘，列带按全宽起步 */
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 获取当前选中项索引。
 * @param obj 传入：控件对象指针。
 * @return 选中项索引；无选项时返回 -1。
 */
int16_t we_roller_get_selected(const we_roller_obj_t *obj)
{
    return (obj != NULL) ? obj->sel_idx : -1;
}

/**
 * @brief 设置选中项改变回调（吸附完成且索引变化时触发）。
 * @param obj 传入：控件对象指针。
 * @param cb 传入：回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_roller_set_changed_cb(we_roller_obj_t *obj, we_roller_changed_cb_t cb)
{
    if (obj == NULL || obj->changed_cb == cb)
        return;
    obj->changed_cb = cb;
}

/**
 * @brief 设置字体资源，按新字体行高重推导行高与控件高度。
 * @param obj 传入：控件对象指针。
 * @param font 传入：字体资源指针（NULL 或与当前相同直接返回）。
 * @return 无。
 * @note 流程：标脏旧区 → 换字体重算几何 → scroll 按行高比例换算 →
 *       刷新测量缓存 → 标脏新区。scroll 换算拆成"整行数 * 新行高 +
 *       行内余数按比例折算"，行对齐位置精确映射（选中行不变），
 *       中间量最大 (row_h-1)*row_h 远小于 int32 上限，无溢出。
 */
void we_roller_set_font(we_roller_obj_t *obj, const unsigned char *font)
{
    uint16_t line_h;
    uint16_t old_row_h;
    int32_t max_scroll;

    if (obj == NULL || font == NULL || obj->font == font)
        return;

    /* 打断进行中的触摸序列与吸附动画（几何即将改变，旧坐标全部失效） */
    _roller_stop_anim(obj);
    obj->tracking = 0U;
    obj->dragging = 0U;
    obj->tap_armed = 0U;

    /* 1. 标脏旧区（旧高度下的外接矩形） */
    we_obj_invalidate((we_obj_t *)obj);

    /* 2. 换字体并重推导行高 / 控件高 */
    old_row_h = obj->row_h;
    obj->font = font;
    line_h = we_font_get_line_height(font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->row_h = (uint16_t)(line_h + 2U * WE_ROLLER_ROW_PAD);
    obj->base.h = (int16_t)((int16_t)obj->visible_rows * (int16_t)obj->row_h);

    /* 3. scroll 按行高比例换算保持选中行：拆成整行 + 余数避免大数乘法。
     *    行对齐位置 idx*old_row_h → idx*new_row_h 精确映射（余数为 0）；
     *    行间余量（此前正处拖拽/动画中）按 rem*new/old 比例折算。 */
    if (old_row_h != 0U)
    {
        int32_t idx = obj->scroll_px / (int32_t)old_row_h;
        int32_t rem = obj->scroll_px % (int32_t)old_row_h;

        obj->scroll_px = idx * (int32_t)obj->row_h
                         + (rem * (int32_t)obj->row_h) / (int32_t)old_row_h;
    }
    max_scroll = _roller_max_scroll(obj);
    if (obj->scroll_px > max_scroll)
        obj->scroll_px = max_scroll;

    /* 4. 刷新文字测量缓存（bbox 常量依赖字体与 row_h，行宽缓存全清） */
    _roller_refresh_text_metrics(obj);

    /* 5. 标脏新区（新高度下的外接矩形），屏上内容整体重绘 */
    obj->disp_band_w = (uint16_t)obj->base.w;
    we_obj_invalidate((we_obj_t *)obj);

    /* 6. 换算后若停在行间（换字体前正处拖拽/动画中被打断），就近吸附归位 */
    if (obj->row_h != 0U && (obj->scroll_px % (int32_t)obj->row_h) != 0)
        _roller_begin_snap(obj);
}

/**
 * @brief 删除滚轮控件：先摘除吸附动画节点（we_anim_stop）再摘链。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_roller_obj_delete(we_roller_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    _roller_stop_anim(obj); /* 节点归控件所有，删除前必须摘链 */
    we_obj_delete((we_obj_t *)obj);
}
