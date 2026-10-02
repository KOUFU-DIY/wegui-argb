/*
        Copyright 2025 Lu Zhihao

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
/**
 * @file  we_tick.c
 * @brief 用户定时器与中央动画引擎。
 */
#include "we_internal.h"

/* --------------------------------------------------------------------------
 * GUI 时间调度模型（两层）
 *
 * 1. 用户定时器（timer_head 侵入式链表，节点归调用方所有）：面向业务层的
 *    we_gui_timer_create 一族接口，适合"按时间触发"的逻辑；
 * 2. 中央动画引擎（we_anim_t 侵入式链表，lcd->anim_head）：控件/容器
 *    动画统一走这里，节点内嵌在控件结构体里，零堆、不占槽位、数量无上限。
 * 两者都由 we_gui_run() 推进：定时器每次运行都推进，动画随渲染帧推进。
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * GUI 用户定时器运行器
 *
 * 设计目标：
 * 1. 对业务层提供“按周期触发”的时间接口，而不是暴露底层 task 队列；
 * 2. 支持单次定时器和周期定时器；
 * 3. 时间全额上报：每帧每定时器至多触发一次，回调收到距上次触发的
 *    真实流逝毫秒，主循环阻塞产生的积压如何消化由回调的累计逻辑决定。
 * -------------------------------------------------------------------------- */
/**
 * @brief 推进并触发 GUI 用户定时器
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param elapsed_ms 传入，本轮累计的已流逝时间，单位毫秒
 * @return 无
 * @note 累计时间到达周期即触发一次并清零重计；回调参数为真实累计毫秒
 *       （≥ 周期，上限 65535），与中央动画引擎同款的一帧一调模型。
 */
void _we_tick_run_timers(we_lcd_t *p_lcd, uint16_t elapsed_ms)
{
    we_gui_timer_t *t;
    we_gui_timer_t *t_next;

    if (p_lcd == NULL || elapsed_ms == 0U)
        return;

    /* 先存 next 再回调：允许回调内 delete（摘除）自身节点 */
    for (t = p_lcd->timer_head; t != NULL; t = t_next)
    {
        uint32_t total_ms;

        t_next = t->next;
        if (t->cb == NULL || t->active == 0U || t->period_ms == 0U)
            continue;

        total_ms = (uint32_t)t->acc_ms + elapsed_ms;
        if (total_ms < t->period_ms)
        {
            t->acc_ms = (uint16_t)total_ms;
            continue;
        }

        /* 到期：先清零重计/单次收尾再回调，回调内 restart/delete 自身的语义不被覆盖 */
        t->acc_ms = 0U;
        if (t->repeat == 0U)
            t->active = 0U;
        if (total_ms > 65535U)
            total_ms = 65535U;
        t->cb(p_lcd, (uint16_t)total_ms);
    }
}

/**
 * @brief 创建并启动一个 GUI 定时器（节点由调用方持有，不会失败）
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @param t 传入，调用方持有的定时器节点（静态或生命周期覆盖使用期）。
 * @param cb 传入，定时器回调函数。
 * @param period_ms 传入，触发周期，单位毫秒，必须大于 0。
 * @param repeat 传入，1 表示周期触发，0 表示单次触发。
 * @return 无
 * @note 与 we_anim_start 同构：已在链上则仅更新参数（幂等，不会重复挂链）。
 */
void we_gui_timer_create(we_lcd_t *p_lcd, we_gui_timer_t *t, we_gui_timer_cb_t cb,
                         uint16_t period_ms, uint8_t repeat)
{
    we_gui_timer_t *it;

    WE_ASSERT(p_lcd != NULL && t != NULL && cb != NULL && period_ms != 0U);
    if (p_lcd == NULL || t == NULL || cb == NULL || period_ms == 0U)
        return;

    t->cb = cb;
    t->period_ms = period_ms;
    t->acc_ms = 0U;
    t->repeat = (repeat != 0U) ? 1U : 0U;
    t->active = 1U;

    for (it = p_lcd->timer_head; it != NULL; it = it->next)
    {
        if (it == t)
            return; /* 已在链上：仅刷新参数 */
    }
    t->next = p_lcd->timer_head;
    p_lcd->timer_head = t;
}

/**
 * @brief 启动指定 GUI 定时器并清零累计时间
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param t 传入，定时器节点
 * @return 无
 */
void we_gui_timer_start(we_lcd_t *p_lcd, we_gui_timer_t *t)
{
    WE_ASSERT(t != NULL);
    (void)p_lcd;
    if (t == NULL || t->cb == NULL)
        return;
    t->active = 1U;
    t->acc_ms = 0U;
}

/**
 * @brief 停止指定 GUI 定时器并清零累计时间（节点保持挂链，可再 start）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param t 传入，定时器节点
 * @return 无
 */
void we_gui_timer_stop(we_lcd_t *p_lcd, we_gui_timer_t *t)
{
    WE_ASSERT(t != NULL);
    (void)p_lcd;
    if (t == NULL || t->cb == NULL)
        return;
    t->active = 0U;
    t->acc_ms = 0U;
}

/**
 * @brief 重启指定 GUI 定时器
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param t 传入，定时器节点
 * @return 无
 * @note 重启会清零累计时间，并立即置为 active 状态。
 */
void we_gui_timer_restart(we_lcd_t *p_lcd, we_gui_timer_t *t)
{
    WE_ASSERT(t != NULL);
    (void)p_lcd;
    if (t == NULL || t->cb == NULL)
        return;
    t->acc_ms = 0U;
    t->active = 1U;
}

/**
 * @brief 删除指定 GUI 定时器（从链上摘除，节点可复用）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param t 传入，定时器节点
 * @return 无
 */
void we_gui_timer_delete(we_lcd_t *p_lcd, we_gui_timer_t *t)
{
    WE_ASSERT(p_lcd != NULL && t != NULL);
    we_gui_timer_t **pp;

    if (p_lcd == NULL || t == NULL)
        return;

    for (pp = &p_lcd->timer_head; *pp != NULL; pp = &(*pp)->next)
    {
        if (*pp == t)
        {
            *pp = t->next;
            break;
        }
    }
    t->next = NULL;
    t->cb = NULL;
    t->period_ms = 0U;
    t->acc_ms = 0U;
    t->repeat = 0U;
    t->active = 0U;
}

/* --------------------------------------------------------------------------
 * 中央动画引擎
 *
 * 控件动画不占用任何槽位：节点内嵌在控件结构体里，挂到
 * lcd->anim_head 侵入式链表上，由 we_gui_run 每帧统一推进。
 * 空链时开销仅一次判空；start 不会失败，数量无上限。
 * -------------------------------------------------------------------------- */

void we_anim_start(we_lcd_t *lcd, we_anim_t *anim,
                   void (*step_cb)(void *owner, uint16_t elapsed_ms), void *owner)
{
    we_anim_t *it;

    WE_ASSERT(lcd != NULL && anim != NULL && step_cb != NULL);
    if (lcd == NULL || anim == NULL || step_cb == NULL)
        return;

    anim->step_cb = step_cb;
    anim->owner = owner;

    /* 已在链上则只更新回调，避免重复挂链成环 */
    for (it = lcd->anim_head; it != NULL; it = it->next)
    {
        if (it == anim)
            return;
    }

    anim->next = lcd->anim_head;
    lcd->anim_head = anim;
}

void we_anim_stop(we_lcd_t *lcd, we_anim_t *anim)
{
    WE_ASSERT(lcd != NULL && anim != NULL);
    we_anim_t **pp;

    if (lcd == NULL || anim == NULL)
        return;

    for (pp = &lcd->anim_head; *pp != NULL; pp = &(*pp)->next)
    {
        if (*pp == anim)
        {
            *pp = anim->next;
            anim->next = NULL;
            return;
        }
    }
}

/**
 * @brief 取最近一个用户定时器到期的剩余毫秒数
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 剩余毫秒数；没有活动定时器时返回 WE_GUI_RUN_IDLE
 */
uint32_t _we_tick_next_timer(const we_lcd_t *p_lcd)
{
    const we_gui_timer_t *t;
    uint32_t wake = WE_GUI_RUN_IDLE;

    for (t = p_lcd->timer_head; t != NULL; t = t->next)
    {
        uint32_t left;

        if (t->cb == NULL || t->active == 0U || t->period_ms == 0U)
            continue;
        left = (t->acc_ms >= t->period_ms) ? 0U : (uint32_t)(t->period_ms - t->acc_ms);
        if (left < wake)
            wake = left;
    }
    return wake;
}

/**
 * @brief 推进中央动画链表（每个渲染帧一次）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param elapsed_ms 传入，距上一帧流逝的毫秒数（为 0 时不推进）
 * @return 无
 */
void _we_tick_run_anims(we_lcd_t *p_lcd, uint16_t elapsed_ms)
{
    /* 先存 next 再调 step_cb，允许回调内摘除自身节点。 */
    if (elapsed_ms != 0U)
    {
        we_anim_t *an = p_lcd->anim_head;
        while (an != NULL)
        {
            we_anim_t *an_next = an->next;
            an->step_cb(an->owner, elapsed_ms);
            an = an_next;
        }
    }
}
