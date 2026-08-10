/**
 * @file  we_scroll.c
 * @brief 可嵌入单轴滚动物理组件实现
 *
 * 物理口径与 list/dropdown 等控件既有实现逐条一致（行为不变的收敛）：
 * - 拖拽跟手：pos = press_pos - (c - press_c)，软夹紧允许 ±overscroll 越界；
 * - 惯性：位移 = vel × step/16（至少 1px 防停滞），vel 每步 ×num/den 整数
 *   截断向零收敛；越界段 vel 额外减半尽快交棒回弹；
 * - 回弹：每步拉回"过冲/pull_div"（钳到 [1, max_step]），整数缓动先快后慢；
 * - 快扫：初速度 = -总位移×16/时间片（内容运动方向与手指一致）；
 * - 滚动条：活动即拉满 alpha 清空闲计时，空闲 idle_ms 后按 fade_step 淡出到
 *   常驻透明度。
 *
 * 零浮点、无除法出现在热路径以外不做限制（衰减/回弹的小整数除法与原
 * 各控件实现相同）。
 */

#include "we_scroll.h"

void we_scroll_reset(we_scroll_t *sc)
{
    sc->pos = 0;
    sc->press_pos = 0;
    sc->press_c = 0;
    sc->last_c = 0;
    sc->vel = 0;
    sc->tracking = 0U;
    sc->dragging = 0U;
    sc->animating = 0U;
}

void we_scroll_press(we_scroll_t *sc, int16_t c)
{
    sc->animating = 0U; /* 按住即停惯性/回弹（越界时定格，松手再回弹） */
    sc->vel = 0;
    sc->tracking = 1U;
    sc->dragging = 0U;
    sc->press_c = c;
    sc->last_c = c;
    sc->press_pos = sc->pos;
}

/**
 * @brief 软夹紧：允许越界 overscroll 像素（拖拽/惯性期间的橡皮筋）
 */
static int32_t _scroll_clamp_over(const we_scroll_cfg_t *cfg, int32_t pos, int32_t max_scroll)
{
    if (pos < -(int32_t)cfg->overscroll)
        pos = -(int32_t)cfg->overscroll;
    if (pos > max_scroll + (int32_t)cfg->overscroll)
        pos = max_scroll + (int32_t)cfg->overscroll;
    return pos;
}

uint8_t we_scroll_stay(we_scroll_t *sc, const we_scroll_cfg_t *cfg,
                       int16_t c, int32_t max_scroll)
{
    int16_t d_total;
    int16_t ad;
    uint8_t crossed = 0U;

    if (!sc->tracking)
        return 0U;

    d_total = (int16_t)(c - sc->press_c);
    ad = (d_total >= 0) ? d_total : (int16_t)(-d_total);

    if (!sc->dragging)
    {
        if (ad < cfg->drag_threshold)
            return 0U;
        sc->dragging = 1U;
        crossed = 1U;
    }

    /* 内容跟手（手指移动方向与内容一致：pos 增量 = -d）+ 步进测速 */
    sc->pos = _scroll_clamp_over(cfg, sc->press_pos - (int32_t)d_total, max_scroll);
    if (c != sc->last_c)
        sc->vel = (int16_t)(-(int16_t)(c - sc->last_c));
    sc->last_c = c;

    return crossed ? 1U : 2U;
}

uint8_t we_scroll_release(we_scroll_t *sc, int32_t max_scroll)
{
    uint8_t was_drag = sc->dragging;
    uint8_t out = (sc->pos < 0 || sc->pos > max_scroll) ? 1U : 0U;

    sc->tracking = 0U;
    sc->dragging = 0U;

    if ((was_drag && sc->vel != 0) || out)
    {
        sc->animating = 1U;
        return 1U;
    }
    sc->vel = 0;
    return 0U;
}

uint8_t we_scroll_swipe(we_scroll_t *sc, const we_scroll_cfg_t *cfg,
                        int16_t c_now, int32_t max_scroll)
{
    int32_t d_total = (int32_t)c_now - (int32_t)sc->press_c;
    int16_t v = (int16_t)((-d_total * 16) / (int32_t)cfg->swipe_slice_ms);

    if (v != 0 && max_scroll > 0)
    {
        sc->vel = v;
        sc->animating = 1U;
        return 1U;
    }
    return 0U;
}

uint8_t we_scroll_anim_step(we_scroll_t *sc, const we_scroll_cfg_t *cfg,
                            uint16_t elapsed_ms, int32_t max_scroll)
{
    int32_t pos;
    int32_t old;
    uint16_t step_ms;

    if (!sc->animating || elapsed_ms == 0U)
        return 0U;

    step_ms = (elapsed_ms > 64U) ? 64U : elapsed_ms;
    old = sc->pos;
    pos = sc->pos;

    /* 1. 惯性段 */
    if (sc->vel != 0)
    {
        int32_t move = ((int32_t)sc->vel * (int32_t)step_ms) / 16;

        if (move == 0)
            move = (sc->vel > 0) ? 1 : -1;
        pos = _scroll_clamp_over(cfg, pos + move, max_scroll);

        sc->vel = (int16_t)(((int32_t)sc->vel * cfg->inertia_num) / cfg->inertia_den);
        if (pos < 0 || pos > max_scroll)
            sc->vel = (int16_t)(sc->vel / 2);
    }

    /* 2. 回弹段 */
    if (pos < 0)
    {
        int32_t pull = (-pos) / cfg->rebound_pull_div;

        if (pull < 1)
            pull = 1;
        if (pull > cfg->rebound_max_step)
            pull = cfg->rebound_max_step;
        pos += pull;
        if (pos > 0)
            pos = 0;
    }
    else if (pos > max_scroll)
    {
        int32_t pull = (pos - max_scroll) / cfg->rebound_pull_div;

        if (pull < 1)
            pull = 1;
        if (pull > cfg->rebound_max_step)
            pull = cfg->rebound_max_step;
        pos -= pull;
        if (pos < max_scroll)
            pos = max_scroll;
    }

    sc->pos = pos;

    /* 3. 收敛：速度归零且在边界内 → 结束（调用方摘自己的动画节点） */
    if (sc->vel == 0 && pos >= 0 && pos <= max_scroll)
        sc->animating = 0U;

    return (pos != old) ? 1U : 0U;
}

uint8_t we_scroll_set(we_scroll_t *sc, int32_t pos, int32_t max_scroll)
{
    if (pos < 0)
        pos = 0;
    if (pos > max_scroll)
        pos = max_scroll;
    if (pos == sc->pos)
        return 0U;
    sc->pos = pos;
    return 1U;
}

void we_scroll_bar_wake(we_scroll_t *sc, uint8_t active_alpha)
{
    sc->sb_idle_ms = 0U;
    sc->sb_alpha = active_alpha;
}

uint8_t we_scroll_bar_step(we_scroll_t *sc, uint16_t elapsed_ms, uint16_t idle_ms,
                           uint8_t fade_step, uint8_t resident_alpha)
{
    if (sc->sb_alpha <= resident_alpha)
        return 0U;

    if (sc->sb_idle_ms < idle_ms)
    {
        uint16_t left = (uint16_t)(idle_ms - sc->sb_idle_ms);

        if (elapsed_ms < left)
        {
            sc->sb_idle_ms = (uint16_t)(sc->sb_idle_ms + elapsed_ms);
            return 0U;
        }
        sc->sb_idle_ms = idle_ms;
        elapsed_ms = (uint16_t)(elapsed_ms - left);
        if (elapsed_ms == 0U)
            return 0U;
    }

    /* 淡出：按 16ms 折算步数（至少 1 步），递减到常驻透明度为止 */
    {
        uint16_t steps = (uint16_t)(elapsed_ms / 16U);
        uint16_t dec;

        if (steps == 0U)
            steps = 1U;
        dec = (uint16_t)steps * (uint16_t)fade_step;
        if ((uint16_t)sc->sb_alpha > (uint16_t)resident_alpha + dec)
            sc->sb_alpha = (uint8_t)(sc->sb_alpha - dec);
        else
            sc->sb_alpha = resident_alpha;
    }
    return 1U;
}
