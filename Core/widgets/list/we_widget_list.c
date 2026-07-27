#include "we_widget_list.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * list —— 数据驱动列表菜单（preview 孵化区，毕业级打磨版）
 *
 * 结构：面板背景（圆角可选）+ PFB 收窄裁剪的行内容（按压高亮条 /
 * 左对齐文字 / 行底 1px 分隔线）+ 右缘空闲淡出滚动条。
 *
 * 滚动为像素级 scroll_px（int32 累计），拖拽/惯性允许越界过冲至多
 * WE_LIST_OVERSCROLL_LIMIT，松手后经中央动画引擎回弹到边界
 * （过冲/PULL_DIV 整数缓动，对齐 scroll_panel 风格）。惯性有两个来源：
 * 拖拽松手取最近一次 STAY 步进为初速度；快速轻扫（无 STAY）由内核
 * SWIPE_UP/DOWN 事件按"总位移 / 固定时间片"估算初速度注入。
 *
 * 标脏粒度：行按压/释放只标该行条带；滚动位移标内容裁剪矩形
 * （= 面板矩形，不越过面板边界）；滚动条淡出只标滚动条条带。
 * -------------------------------------------------------------------------- */

/**
 * @brief 将透明度按控件整体不透明度缩放。
 * @param a 传入：原始透明度（0~255）。
 * @param opacity 传入：控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _list_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/**
 * @brief 内容总高度（条目数 × 行高）。
 * @param obj 传入：控件对象指针。
 * @return 内容总高（像素）。
 */
static int32_t _list_content_h(const we_list_obj_t *obj)
{
    return (int32_t)obj->item_cnt * (int32_t)obj->row_h;
}

/**
 * @brief 最大可滚动像素（内容不溢出时为 0）。
 * @param obj 传入：控件对象指针。
 * @return 最大 scroll_px（>= 0）。
 */
static int32_t _list_max_scroll(const we_list_obj_t *obj)
{
    int32_t m = _list_content_h(obj) - (int32_t)obj->base.h;
    return (m > 0) ? m : 0;
}

/* --------------------------------------------------------------------------
 * 精细标脏辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 标脏内容裁剪矩形（滚动位移用）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 行内容经 PFB 窗口裁剪在面板矩形内，因此内容裁剪矩形 = 面板矩形，
 *       绝不越过面板边界向外扩。圆角外圈的 4 个角落死区无法被单个矩形
 *       排除（脏区只能是矩形），但这些像素重绘结果与原值等同，代价可忽略。
 */
static void _list_invalidate_content(we_list_obj_t *obj)
{
    we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y,
                           obj->base.w, obj->base.h);
}

/**
 * @brief 只标脏某一行的条带区域（按压/释放高亮用）。
 * @param obj 传入：控件对象指针。
 * @param row 传入：行索引。
 * @return 无。
 * @note 行条带按当前滚动偏移换算成屏幕绝对坐标，并与面板矩形求交：
 *       半露行只标可见部分，不会把面板边界之外的死区标进脏区。
 */
static void _list_invalidate_row(we_list_obj_t *obj, int16_t row)
{
    int32_t ry0 = (int32_t)obj->base.y + (int32_t)row * (int32_t)obj->row_h - obj->scroll_px;
    int32_t ry1 = ry0 + (int32_t)obj->row_h - 1;
    int32_t py0 = obj->base.y;
    int32_t py1 = (int32_t)obj->base.y + obj->base.h - 1;

    if (row < 0)
        return;
    if (ry0 < py0)
        ry0 = py0;
    if (ry1 > py1)
        ry1 = py1;
    if (ry0 > ry1)
        return; /* 行已完全滚出面板 */

    we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, (int16_t)ry0,
                           obj->base.w, (int16_t)(ry1 - ry0 + 1));
}

/**
 * @brief 只标脏右缘滚动条条带（透明度渐变/滑块移动用）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 条带取整个轨道高度（滑块可能在任意位置），宽度仅滑块宽，
 *       淡出动画每步只刷这一窄条，不再整控件重绘。
 */
static void _list_invalidate_sb(we_list_obj_t *obj)
{
    int16_t sx = (int16_t)(obj->base.x + obj->base.w - WE_LIST_SB_MARGIN - WE_LIST_SB_WIDTH);

    we_obj_invalidate_area((we_obj_t *)obj, sx, obj->base.y,
                           (int16_t)WE_LIST_SB_WIDTH, obj->base.h);
}

/* --------------------------------------------------------------------------
 * 滚动条空闲淡出（中央动画引擎，dropdown 同款口径）
 * -------------------------------------------------------------------------- */

static void _list_sb_fade_step_cb(void *owner, uint16_t elapsed_ms);

/**
 * @brief 唤醒滚动条：透明度拉到峰值、空闲计时清零，并挂入淡出动画。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 仅当内容确实可滚动时生效；收敛/停用后淡出节点会自行摘链，
 *       这里再次唤醒重新挂入（已在链上则 we_anim_start 为空操作）。
 */
static void _list_sb_wake(we_list_obj_t *obj)
{
    if (_list_max_scroll(obj) == 0)
        return; /* 不可滚动，无需显示滚动条 */
    obj->sb_idle_ms = 0U;
    if (obj->sb_alpha != WE_LIST_SB_OPA)
    {
        obj->sb_alpha = WE_LIST_SB_OPA;
        _list_invalidate_sb(obj);
    }
    we_anim_start(obj->base.lcd, &obj->sb_anim, _list_sb_fade_step_cb, obj);
}

/**
 * @brief 滚动条淡出动画步进（中央动画引擎驱动）：空闲超过 HOLD 后按
 *        FADE 时长线性渐隐到常驻最低透明度 WE_LIST_SB_IDLE_ALPHA。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次步进经过的毫秒数。
 * @return 无。
 * @note 拖拽中保持峰值；惯性滑行中滚动提交会持续 wake（空闲清零），
 *       无需特判。每步只标脏滚动条条带。
 */
static void _list_sb_fade_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_list_obj_t *obj = (we_list_obj_t *)owner;
    uint32_t step;

    if (obj == NULL || elapsed_ms == 0U)
        return;
    if (obj->sb_alpha <= WE_LIST_SB_IDLE_ALPHA)
    {
        /* 已收敛到常驻最低透明度：摘链停表，等待下次 wake 重新挂入 */
        we_anim_stop(obj->base.lcd, &obj->sb_anim);
        return;
    }

    /* 拖拽中保持完全显示，不累计空闲 */
    if (obj->dragging)
    {
        obj->sb_idle_ms = 0U;
        return;
    }

    /* 累计空闲时间（饱和到 HOLD+FADE，避免 uint16 溢出） */
    if ((uint32_t)obj->sb_idle_ms + elapsed_ms >=
        (uint32_t)(WE_LIST_SB_HOLD_MS + WE_LIST_SB_FADE_MS))
        obj->sb_idle_ms = (uint16_t)(WE_LIST_SB_HOLD_MS + WE_LIST_SB_FADE_MS);
    else
        obj->sb_idle_ms = (uint16_t)(obj->sb_idle_ms + elapsed_ms);

    if (obj->sb_idle_ms <= WE_LIST_SB_HOLD_MS)
        return; /* 仍在保持期，维持峰值 */

    /* 进入淡出期：按"峰值→0"的全程速率线性递减，使手感与 FADE_MS 一致，
     * 收敛到常驻最低值 IDLE_ALPHA（不到 0，保留位置指示） */
    step = (uint32_t)WE_LIST_SB_OPA * (uint32_t)elapsed_ms / (uint32_t)WE_LIST_SB_FADE_MS;
    if (step == 0U)
        step = 1U; /* 保证慢主循环下也能前进 */

    if ((uint32_t)obj->sb_alpha > (uint32_t)WE_LIST_SB_IDLE_ALPHA + step)
        obj->sb_alpha = (uint8_t)((uint32_t)obj->sb_alpha - step);
    else
        obj->sb_alpha = WE_LIST_SB_IDLE_ALPHA;

    _list_invalidate_sb(obj); /* 只标脏滚动条条带 */
}

/* --------------------------------------------------------------------------
 * 滚动提交与夹紧
 * -------------------------------------------------------------------------- */

/**
 * @brief 提交滚动值：值变化时唤醒滚动条并标脏内容裁剪矩形。
 * @param obj 传入：控件对象指针。
 * @param new_scroll 传入：新滚动像素（调用方已按各自策略夹紧）。
 * @return 无。
 */
static void _list_scroll_commit(we_list_obj_t *obj, int32_t new_scroll)
{
    if (new_scroll == obj->scroll_px)
        return;
    obj->scroll_px = new_scroll;
    _list_sb_wake(obj); /* 滚动即唤醒滚动条（空闲计时清零） */
    _list_invalidate_content(obj);
}

/**
 * @brief 将 scroll_px 硬夹紧到 [0, max] 后提交（程序化定位/参数变更用）。
 * @param obj 传入：控件对象指针。
 * @param new_scroll 传入：目标滚动像素。
 * @return 无。
 */
static void _list_apply_scroll(we_list_obj_t *obj, int32_t new_scroll)
{
    int32_t max_scroll = _list_max_scroll(obj);

    if (new_scroll < 0)
        new_scroll = 0;
    if (new_scroll > max_scroll)
        new_scroll = max_scroll;
    _list_scroll_commit(obj, new_scroll);
}

/**
 * @brief 将 scroll_px 软夹紧到 [-过冲上限, max+过冲上限] 后提交
 *        （拖拽跟手 / 惯性动画用，允许橡皮筋越界）。
 * @param obj 传入：控件对象指针。
 * @param new_scroll 传入：目标滚动像素。
 * @return 无。
 */
static void _list_apply_scroll_over(we_list_obj_t *obj, int32_t new_scroll)
{
    int32_t max_scroll = _list_max_scroll(obj);

    if (new_scroll < -WE_LIST_OVERSCROLL_LIMIT)
        new_scroll = -WE_LIST_OVERSCROLL_LIMIT;
    if (new_scroll > max_scroll + WE_LIST_OVERSCROLL_LIMIT)
        new_scroll = max_scroll + WE_LIST_OVERSCROLL_LIMIT;
    _list_scroll_commit(obj, new_scroll);
}

/* --------------------------------------------------------------------------
 * 惯性 + 回弹动画（单个中央动画节点）
 * -------------------------------------------------------------------------- */

/**
 * @brief 停止惯性/回弹动画并清零速度（摘链停表）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _list_stop_anim(we_list_obj_t *obj)
{
    obj->inertia_animating = 0U;
    obj->vel = 0;
    we_anim_stop(obj->base.lcd, &obj->anim);
}

/**
 * @brief 惯性/回弹动画步进回调：速度积分 + 7/8 衰减 + 越界橡皮筋回弹。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次调度经过的毫秒数。
 * @return 无。
 * @note 越界段速度额外减半，让过冲快速交棒给回弹；回弹每步拉回
 *       "过冲/WE_LIST_REBOUND_PULL_DIV"（1..MAX_STEP px），整数缓动
 *       天然先快后慢。速度归零且回到边界内时自行摘链。
 */
static void _list_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_list_obj_t *obj = (we_list_obj_t *)owner;
    int32_t max_scroll;
    int32_t pos;
    uint16_t step_ms;

    if (obj == NULL || elapsed_ms == 0U)
        return;
    if (!obj->inertia_animating)
    {
        _list_stop_anim(obj);
        return;
    }

    step_ms = (elapsed_ms > 64U) ? 64U : elapsed_ms;
    max_scroll = _list_max_scroll(obj);
    pos = obj->scroll_px;

    /* 1. 惯性段：本帧位移 = 速度（像素/16ms）按帧时长折算，至少 1px 防停滞 */
    if (obj->vel != 0)
    {
        int32_t move = ((int32_t)obj->vel * (int32_t)step_ms) / 16;

        if (move == 0)
            move = (obj->vel > 0) ? 1 : -1;
        pos += move;
        if (pos < -WE_LIST_OVERSCROLL_LIMIT)
            pos = -WE_LIST_OVERSCROLL_LIMIT;
        if (pos > max_scroll + WE_LIST_OVERSCROLL_LIMIT)
            pos = max_scroll + WE_LIST_OVERSCROLL_LIMIT;

        /* 速度衰减 7/8（整数截断向零收敛，最终必然归零） */
        obj->vel = (int16_t)(((int32_t)obj->vel * WE_LIST_INERTIA_NUM) / WE_LIST_INERTIA_DEN);
        /* 越界段再减半：小幅过冲后尽快把控制权交给回弹 */
        if (pos < 0 || pos > max_scroll)
            obj->vel = (int16_t)(obj->vel / 2);
    }

    /* 2. 回弹段：越界时按"过冲/PULL_DIV"拉回边界（对齐 scroll_panel 参数风格） */
    if (pos < 0)
    {
        int32_t pull = (-pos) / WE_LIST_REBOUND_PULL_DIV;

        if (pull < 1)
            pull = 1;
        if (pull > WE_LIST_REBOUND_MAX_STEP)
            pull = WE_LIST_REBOUND_MAX_STEP;
        pos += pull;
        if (pos > 0)
            pos = 0;
    }
    else if (pos > max_scroll)
    {
        int32_t pull = (pos - max_scroll) / WE_LIST_REBOUND_PULL_DIV;

        if (pull < 1)
            pull = 1;
        if (pull > WE_LIST_REBOUND_MAX_STEP)
            pull = WE_LIST_REBOUND_MAX_STEP;
        pos -= pull;
        if (pos < max_scroll)
            pos = max_scroll;
    }

    _list_apply_scroll_over(obj, pos);

    /* 3. 收敛判定：速度归零且回到边界内 → 摘链停表 */
    if (obj->vel == 0 && obj->scroll_px >= 0 && obj->scroll_px <= max_scroll)
        _list_stop_anim(obj);
}

/* --------------------------------------------------------------------------
 * 命中测试
 * -------------------------------------------------------------------------- */

/**
 * @brief 命中测试：返回触摸点所在的条目索引。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触摸 X（屏幕绝对坐标）。
 * @param py 传入：触摸 Y。
 * @return 条目索引；未命中任何条目（越界/落在内容之外）返回 -1。
 * @note 过冲期间顶部露出的空隙 content_y < 0，显式判负避免整数
 *       "向零截断"把空隙误判成第 0 行。
 */
static int16_t _list_hit_row(const we_list_obj_t *obj, int16_t px, int16_t py)
{
    int32_t content_y;
    int32_t row;

    if (obj->item_cnt == 0U || obj->row_h == 0U)
        return -1;
    if (px < obj->base.x || px >= (int16_t)(obj->base.x + obj->base.w) ||
        py < obj->base.y || py >= (int16_t)(obj->base.y + obj->base.h))
        return -1;

    content_y = (int32_t)(py - obj->base.y) + obj->scroll_px;
    if (content_y < 0)
        return -1;
    row = content_y / (int32_t)obj->row_h;
    if (row >= (int32_t)obj->item_cnt)
        return -1;
    return (int16_t)row;
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 绘制右缘滚动条（无轨道，仅胶囊滑块，内容溢出且未完全淡出才显示）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 滑块高按"控件高/内容高"比例（下限 8px），位置按滚动进度插值
 *       （过冲期间按夹紧后的滚动值计算，不越出轨道）；轨道上下各留
 *       radius 像素避开面板圆角。透明度取淡出动画当前值 sb_alpha。
 */
static void _list_draw_scrollbar(we_list_obj_t *obj)
{
    we_lcd_t *lcd = obj->base.lcd;
    int32_t content_h = _list_content_h(obj);
    int32_t max_scroll = _list_max_scroll(obj);
    int32_t sc;
    int16_t track_x;
    int16_t track_y0;
    int16_t track_h;
    int16_t thumb_h;
    int16_t thumb_y;

    if (max_scroll == 0 || content_h <= 0)
        return; /* 内容未溢出，无需滚动条 */
    if (obj->sb_alpha == 0U)
        return; /* 完全隐藏（未唤醒过） */

    track_x = (int16_t)(obj->base.x + obj->base.w - WE_LIST_SB_MARGIN - WE_LIST_SB_WIDTH);
    track_y0 = (int16_t)(obj->base.y + (int16_t)obj->radius);
    track_h = (int16_t)(obj->base.h - 2 * (int16_t)obj->radius);
    if (track_h < (int16_t)obj->row_h)
        track_h = obj->base.h; /* 圆角过大时退化为整高轨道 */

    thumb_h = (int16_t)(((int32_t)track_h * (int32_t)obj->base.h) / content_h);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > track_h)
        thumb_h = track_h;

    sc = obj->scroll_px;
    if (sc < 0)
        sc = 0;
    if (sc > max_scroll)
        sc = max_scroll;
    thumb_y = (int16_t)(track_y0 +
              (int32_t)(track_h - thumb_h) * sc / max_scroll);

    we_draw_round_rect_analytic_fill(lcd, track_x, thumb_y,
                                     (uint16_t)WE_LIST_SB_WIDTH, (uint16_t)thumb_h,
                                     (uint16_t)(WE_LIST_SB_WIDTH / 2),
                                     obj->sb_color,
                                     _list_scale_opa(obj->sb_alpha, obj->opacity));
}

/**
 * @brief 绘制回调：面板背景 + PFB 收窄裁剪的行内容 + 滚动条。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 */
static void _list_draw_cb(void *ptr)
{
    we_list_obj_t *obj = (we_list_obj_t *)ptr;
    we_lcd_t *lcd;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    int16_t clip_x0;
    int16_t clip_y0;
    int16_t clip_x1;
    int16_t clip_y1;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    /* 1. 面板背景（radius 为 0 时解析填充自动退化为直角矩形） */
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     obj->radius, obj->bg_color, obj->opacity);

    if (obj->items == NULL || obj->item_cnt == 0U ||
        obj->row_h == 0U || obj->font == NULL)
        return;

    /* 2. PFB 窗口收窄：把行内容裁剪在控件矩形内（save/restore 套路） */
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
        /* 首个（可能半露）可见行：负向过冲时 first 会算成负/零，统一
         * 夹到 0 并由"行号反推屏幕 Y"公式覆盖过冲露出的顶部空隙。 */
        int32_t first = obj->scroll_px / (int32_t)obj->row_h;
        int16_t iy;
        int32_t idx;

        if (first < 0)
            first = 0;
        iy = (int16_t)((int32_t)obj->base.y + first * (int32_t)obj->row_h - obj->scroll_px);

        lcd->pfb_area.x0 = clip_x0;
        lcd->pfb_area.x1 = clip_x1;
        lcd->pfb_y_start = (uint16_t)clip_y0;
        lcd->pfb_y_end = (uint16_t)clip_y1;
        lcd->pfb_gram = old_gram + (clip_y0 - (int16_t)old_y_start) * lcd->pfb_width
                                 + (clip_x0 - old_pfb_area.x0);

        for (idx = first; idx < (int32_t)obj->item_cnt; idx++)
        {
            const char *text = obj->items[idx];
            colour_t tc = obj->text_color;
            int16_t ty;
            int8_t y_top;
            int8_t y_bot;

            if (iy > (int16_t)(obj->base.y + obj->base.h - 1))
                break; /* 已画到控件底部之外 */

            /* 按压行高亮：内缩圆角条，先与"面板内缩矩形"求交（半露行不外溢），
             * 贴到内缩边界（落在面板圆角带）时改用同心半径 radius-内缩，
             * 高亮圆弧与面板圆角同心贴合，首/末行不再溢出直角。 */
            if ((int16_t)idx == obj->pressed_row)
            {
                int16_t hx = (int16_t)(obj->base.x + WE_LIST_PRESS_INSET);
                uint16_t hw = (uint16_t)(obj->base.w - 2 * WE_LIST_PRESS_INSET);
                int16_t hy0 = (int16_t)(iy + 1);
                int16_t hy1 = (int16_t)(iy + (int16_t)obj->row_h - 2);
                int16_t py0 = (int16_t)(obj->base.y + WE_LIST_PRESS_INSET);
                int16_t py1 = (int16_t)(obj->base.y + obj->base.h - 1 - WE_LIST_PRESS_INSET);
                uint16_t hr = WE_LIST_PRESS_RADIUS;

                if (hy0 < py0)
                    hy0 = py0;
                if (hy1 > py1)
                    hy1 = py1;
                if (hy0 <= hy1)
                {
                    if ((hy0 == py0 || hy1 == py1) &&
                        obj->radius > (uint16_t)WE_LIST_PRESS_INSET)
                    {
                        /* 内缩矩形要包在面板圆角内，同心半径需 >= 面板半径-内缩 */
                        uint16_t cr = (uint16_t)(obj->radius - WE_LIST_PRESS_INSET);
                        if (cr > hr)
                            hr = cr;
                    }
                    we_draw_round_rect_analytic_fill(lcd, hx, hy0, hw,
                                                     (uint16_t)(hy1 - hy0 + 1),
                                                     hr, obj->press_color, obj->opacity);
                }
            }

            /* 行文字：左对齐 + 行内垂直居中 */
            if (text != NULL)
            {
                we_get_text_bbox(obj->font, text, &y_top, &y_bot);
                ty = (int16_t)(iy + (int16_t)obj->row_h / 2 - (y_top + y_bot) / 2);
                we_draw_string(lcd, (int16_t)(obj->base.x + WE_LIST_TEXT_PAD), ty,
                               obj->font, text, tc,
                               _list_scale_opa(255U, obj->opacity));
            }

            /* 行底 1px 分隔线（低透明度，最后一行不画） */
            if (idx < (int32_t)(obj->item_cnt - 1U))
            {
                we_fill_rect(lcd,
                             (int16_t)(obj->base.x + WE_LIST_TEXT_PAD),
                             (int16_t)(iy + (int16_t)obj->row_h - 1),
                             (uint16_t)(obj->base.w - 2 * WE_LIST_TEXT_PAD), 1U,
                             obj->sep_color,
                             _list_scale_opa(WE_LIST_SEP_OPA, obj->opacity));
            }

            iy = (int16_t)(iy + (int16_t)obj->row_h);
        }
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;

    /* 3. 内容溢出时叠加右缘滚动条（透明度由淡出动画驱动） */
    _list_draw_scrollbar(obj);
}

/* --------------------------------------------------------------------------
 * 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 事件回调：行按压高亮 / 拖拽滚动 / 同行释放触发点击回调 /
 *        惯性（拖拽测速 + 快扫 SWIPE 注入）/ 越界回弹。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 1 表示消费事件，0 表示穿透。
 */
static uint8_t _list_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_list_obj_t *obj = (we_list_obj_t *)ptr;

    if (obj == NULL || data == NULL)
        return 0U;

    if (event == WE_EVENT_PRESSED)
    {
        _list_stop_anim(obj); /* 按住即停惯性/回弹（越界时定格，松手再回弹） */
        obj->tracking = 1U;
        obj->dragging = 0U;
        obj->press_y = data->y;
        obj->last_y = data->y;
        obj->press_scroll = obj->scroll_px;
        obj->pressed_row = _list_hit_row(obj, data->x, data->y);
        if (obj->pressed_row >= 0)
            _list_invalidate_row(obj, obj->pressed_row); /* 只标脏按压行条带 */
        return 1U;
    }

    if (event == WE_EVENT_SWIPE_UP || event == WE_EVENT_SWIPE_DOWN)
    {
        /* 快速轻扫惯性：无 STAY 的快扫在内核 RELEASED 之后补发 SWIPE
         * （经历过 STAY 的拖拽被内核 gesture_had_stay 抑制不会发 SWIPE，
         * 与 RELEASED 分支的拖拽惯性互斥，不会叠加）。初速度按
         * "总位移 / 固定时间片"估算，方向与内容运动一致（增量 = -dy）。
         * 注意：RELEASED 已清 tracking，本分支必须放在 tracking 闸门之前。 */
        int32_t dy_total = (int32_t)data->y - (int32_t)obj->press_y;
        int16_t v = (int16_t)((-dy_total * 16) / WE_LIST_SWIPE_SLICE_MS);

        if (v != 0 && _list_max_scroll(obj) > 0)
        {
            obj->vel = v;
            obj->inertia_animating = 1U;
            we_anim_start(obj->base.lcd, &obj->anim, _list_anim_step_cb, obj);
        }
        return 1U;
    }

    if (!obj->tracking)
        return 1U; /* 无有效按压序列，仅消费 */

    if (event == WE_EVENT_STAY)
    {
        int16_t dy_total = (int16_t)(data->y - obj->press_y);
        int16_t ady = (dy_total >= 0) ? dy_total : (int16_t)(-dy_total);

        if (!obj->dragging && ady >= WE_LIST_DRAG_THRESHOLD)
        {
            obj->dragging = 1U;
            if (obj->pressed_row >= 0)
            {
                int16_t row = obj->pressed_row;
                obj->pressed_row = -1; /* 进入拖拽即取消行按压态 */
                _list_invalidate_row(obj, row);
            }
        }

        if (obj->dragging)
        {
            int16_t dy_step = (int16_t)(data->y - obj->last_y);

            /* 内容跟手：手指下移 → 内容下移 → scroll_px 减小；
             * 软夹紧允许橡皮筋越界过冲（松手后回弹） */
            _list_apply_scroll_over(obj, obj->press_scroll - (int32_t)dy_total);

            /* 测速：以最近一次 STAY 步进为速度（像素/16ms 量级），
             * 惯性方向与内容运动方向一致（scroll 增量 = -dy） */
            if (dy_step != 0)
                obj->vel = (int16_t)(-dy_step);
            obj->last_y = data->y;
        }
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        uint8_t was_drag = obj->dragging;
        int16_t row = obj->pressed_row;
        int32_t max_scroll = _list_max_scroll(obj);
        uint8_t out_of_bounds =
            (obj->scroll_px < 0 || obj->scroll_px > max_scroll) ? 1U : 0U;

        obj->tracking = 0U;
        obj->dragging = 0U;

        if (!was_drag && row >= 0)
        {
            /* 未拖拽：释放点仍在同一行才算点击 */
            if (_list_hit_row(obj, data->x, data->y) == row)
            {
                if (obj->clicked_cb != NULL)
                    obj->clicked_cb(obj, (uint16_t)row);
            }
            obj->pressed_row = -1;
            _list_invalidate_row(obj, row); /* 只标脏该行清除按压高亮 */
        }

        /* 拖拽松手带速度 → 惯性（回弹并入同一动画）；
         * 越界（含按住定格后轻点松开）→ 纯回弹 */
        if ((was_drag && obj->vel != 0) || out_of_bounds)
        {
            obj->inertia_animating = 1U;
            we_anim_start(obj->base.lcd, &obj->anim, _list_anim_step_cb, obj);
        }
        return 1U;
    }

    /* CLICKED：点击回调已在 RELEASED 处理，这里仅消费防穿透 */
    return 1U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_LIST_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 进编辑态并落高亮行，编辑态上下键移动高亮 +
 *        滚动跟随，OK 触发行选中回调（停留编辑态），BACK 交管理器退出。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 键控高亮直接复用触摸按压行字段 pressed_row 与其行条带精细标脏，
 *       零新增视觉代码；退出编辑后高亮保留，可作"当前选中行"记忆。
 */
static uint8_t _list_key_cb(void *ptr, uint8_t key_evt)
{
    we_list_obj_t *obj = (we_list_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U && obj->item_cnt > 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (!we_focus_edit_active(lcd))
        {
            we_focus_edit_enter(lcd);
            /* 进入编辑态：无有效高亮行时落到当前可见首行 */
            if (obj->pressed_row < 0 || obj->pressed_row >= (int16_t)obj->item_cnt)
            {
                int16_t row = (obj->row_h != 0U) ? (int16_t)(obj->scroll_px / (int32_t)obj->row_h) : 0;
                if (row < 0)
                    row = 0;
                if (row >= (int16_t)obj->item_cnt)
                    row = (int16_t)(obj->item_cnt - 1U);
                obj->pressed_row = row;
                _list_invalidate_row(obj, row);
            }
        }
        else if (obj->pressed_row >= 0 && obj->clicked_cb != NULL)
        {
            /* 编辑态 OK = 选中当前高亮行（停留在编辑态可继续选） */
            obj->clicked_cb(obj, (uint16_t)obj->pressed_row);
        }
        return 1U;
    case WE_KEY_UP:
    case WE_KEY_DOWN:
        if (!we_focus_edit_active(lcd))
            return 0U;
        if (obj->item_cnt == 0U || obj->row_h == 0U)
            return 1U;
        {
            int16_t old_row = obj->pressed_row;
            int16_t row = (old_row < 0) ? 0 : old_row;
            row += (key_evt == WE_KEY_DOWN) ? 1 : -1;
            if (row < 0)
                row = 0;
            if (row >= (int16_t)obj->item_cnt)
                row = (int16_t)(obj->item_cnt - 1U);
            if (row == old_row)
                return 1U;
            obj->pressed_row = row;
            if (old_row >= 0)
                _list_invalidate_row(obj, old_row);
            _list_invalidate_row(obj, row);

            /* 高亮行滚动跟随：目标行不完整可见时硬滚到完整露出 */
            {
                int32_t row_top = (int32_t)row * (int32_t)obj->row_h;
                int32_t view_h = (int32_t)obj->base.h;
                if (row_top < obj->scroll_px)
                    we_list_set_scroll(obj, row_top);
                else if (row_top + (int32_t)obj->row_h > obj->scroll_px + view_h)
                    we_list_set_scroll(obj, row_top + (int32_t)obj->row_h - view_h);
            }
        }
        return 1U;
    default:
        return 0U;
    }
}
#endif

static const we_class_t _list_class = {
    .draw_cb = _list_draw_cb,
    .event_cb = _list_event_cb,
    .set_pos_cb = NULL, /* 通用移动逻辑（旧区标脏 + 新区标脏）已足够 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_LIST_USE_KEY == 1)
    .key_cb = _list_key_cb,
#endif
};

/* --------------------------------------------------------------------------
 * 生命周期与公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化列表控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param h 传入：控件高度（像素）。
 * @return 无。
 */
void we_list_obj_init(we_list_obj_t *obj, we_lcd_t *lcd,
                      int16_t x, int16_t y, int16_t w, int16_t h,
                      const unsigned char *font)
{
    uint16_t line_h;

    if (obj == NULL || lcd == NULL || font == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_list_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->items = NULL;
    obj->item_cnt = 0U;

    obj->font = font;
    line_h = we_font_get_line_height(obj->font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->row_h = (uint16_t)(line_h + 2U * WE_LIST_ROW_PAD);

    obj->radius = WE_LIST_DEF_RADIUS;
    obj->opacity = 255U;
    obj->scroll_px = 0;

    obj->bg_color = RGB888TODEV(32, 38, 50);
    obj->text_color = RGB888TODEV(214, 221, 233);
    obj->sep_color = RGB888TODEV(220, 228, 242);
    obj->press_color = RGB888TODEV(62, 92, 132);
    obj->sb_color = RGB888TODEV(200, 210, 226);
    obj->clicked_cb = NULL;

    obj->pressed_row = -1;
    obj->tracking = 0U;
    obj->dragging = 0U;
    obj->press_y = 0;
    obj->last_y = 0;
    obj->press_scroll = 0;

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    obj->inertia_animating = 0U;
    obj->vel = 0;

    obj->sb_alpha = 0U;   /* 未绑定条目前不显示滚动条 */
    obj->sb_idle_ms = 0U;
    obj->sb_anim.next = NULL;
    obj->sb_anim.step_cb = NULL;
    obj->sb_anim.owner = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 绑定条目字符串数组（控件只保存指针，不复制内容）。
 * @param obj 传入：控件对象指针。
 * @param items 传入：字符串指针数组，需在控件生命周期内保持有效。
 * @param count 传入：条目个数。
 * @return 无。
 */
void we_list_set_options(we_list_obj_t *obj,
                         const char *const *items, uint16_t count)
{
    if (obj == NULL)
        return;
    if (obj->items == items && obj->item_cnt == count)
        return;

    _list_stop_anim(obj);
    obj->items = items;
    obj->item_cnt = (items != NULL) ? count : 0U;
    obj->scroll_px = 0;
    obj->pressed_row = -1;
    obj->tracking = 0U;
    obj->dragging = 0U;
    we_obj_invalidate((we_obj_t *)obj);
    /* 内容溢出时让滚动条短暂全显提示"此处可滚动"，随后自动渐隐；
     * 不溢出时 wake 内部直接返回，保持隐藏 */
    obj->sb_alpha = 0U;
    obj->sb_idle_ms = 0U;
    _list_sb_wake(obj);
}

/**
 * @brief 设置行点击回调。
 * @param obj 传入：控件对象指针。
 * @param cb 传入：回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_list_set_clicked_cb(we_list_obj_t *obj, we_list_clicked_cb_t cb)
{
    if (obj == NULL || obj->clicked_cb == cb)
        return;
    obj->clicked_cb = cb;
}

/**
 * @brief 设置行高（像素），并把滚动偏移夹紧到新范围。
 * @param obj 传入：控件对象指针。
 * @param row_h 传入：新行高（0 时忽略）。
 * @return 无。
 */
void we_list_set_row_height(we_list_obj_t *obj, uint16_t row_h)
{
    int32_t max_scroll;

    if (obj == NULL || row_h == 0U || obj->row_h == row_h)
        return;

    _list_stop_anim(obj);
    obj->row_h = row_h;

    max_scroll = _list_max_scroll(obj);
    if (obj->scroll_px > max_scroll)
        obj->scroll_px = max_scroll;
    if (obj->scroll_px < 0)
        obj->scroll_px = 0;

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置字体资源，并按新字体行高重推导默认行高。
 * @param obj 传入：控件对象指针。
 * @param font 传入：字体资源指针（NULL 或与当前相同直接返回）。
 * @return 无。
 * @note 行高恢复为"新字体行高 + 2*WE_LIST_ROW_PAD"，内容总高随行高
 *       重算，滚动偏移夹紧到新范围（过冲态一并归位），整控件标脏。
 */
void we_list_set_font(we_list_obj_t *obj, const unsigned char *font)
{
    uint16_t line_h;
    int32_t max_scroll;

    if (obj == NULL || font == NULL || obj->font == font)
        return;

    _list_stop_anim(obj);
    obj->font = font;

    line_h = we_font_get_line_height(font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->row_h = (uint16_t)(line_h + 2U * WE_LIST_ROW_PAD);

    max_scroll = _list_max_scroll(obj);
    if (obj->scroll_px > max_scroll)
        obj->scroll_px = max_scroll;
    if (obj->scroll_px < 0)
        obj->scroll_px = 0;

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置面板圆角半径（0 = 直角）。
 * @param obj 传入：控件对象指针。
 * @param radius 传入：圆角半径（像素）。
 * @return 无。
 */
void we_list_set_radius(we_list_obj_t *obj, uint16_t radius)
{
    if (obj == NULL || obj->radius == radius)
        return;
    obj->radius = radius;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置滚动偏移（像素，硬夹紧到有效范围，无动画）。
 * @param obj 传入：控件对象指针。
 * @param scroll_px 传入：目标滚动偏移。
 * @return 无。
 */
void we_list_set_scroll(we_list_obj_t *obj, int32_t scroll_px)
{
    if (obj == NULL)
        return;
    _list_stop_anim(obj);
    _list_apply_scroll(obj, scroll_px);
}

/**
 * @brief 删除列表控件：先摘除惯性与滚动条淡出动画节点再摘链。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_list_obj_delete(we_list_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    _list_stop_anim(obj);                        /* 惯性/回弹节点归控件所有 */
    we_anim_stop(obj->base.lcd, &obj->sb_anim);  /* 滚动条淡出节点同样必须摘链 */
    we_obj_delete((we_obj_t *)obj);
}
