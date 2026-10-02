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
 * @file  we_core.c
 * @brief 屏幕实例初始化、端口绑定与主循环调度。
 */
#include "we_internal.h"

/**
 * @brief 计算距下一次需要运行主循环的毫秒数
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param now_ms 传入，本次运行的时间戳
 * @return 剩余毫秒数；WE_GUI_RUN_IDLE 表示没有待办
 */
static uint32_t _we_core_next_wake(we_lcd_t *p_lcd, uint32_t now_ms)
{
    uint32_t wake = WE_GUI_RUN_IDLE;
    uint32_t t;

    (void)now_ms; /* 帧节奏与触摸采样间隔都为 0 时不用 */

    /* 动画在跑或还有脏区没刷：下一帧就要运行 */
    if (p_lcd->anim_head != NULL || p_lcd->dirty_mgr.count != 0U)
    {
#if (WE_CFG_REFR_PERIOD_MS > 0)
        t = now_ms - p_lcd->frame_last_ms;
        wake = (t >= (uint32_t)WE_CFG_REFR_PERIOD_MS) ? 0U : ((uint32_t)WE_CFG_REFR_PERIOD_MS - t);
#else
        wake = 0U;
#endif
    }

    /* 用户定时器：最近一个到期的剩余时间 */
    t = _we_tick_next_timer(p_lcd);
    if (t < wake)
        wake = t;

    /* 触摸只能轮询：有触摸端口就按采样间隔回来 */
    if (p_lcd->port->touch_read != NULL)
    {
#if (WE_CFG_INDEV_PERIOD_MS > 0)
        t = now_ms - p_lcd->indev_last_ms;
        t = (t >= (uint32_t)WE_CFG_INDEV_PERIOD_MS) ? 0U : ((uint32_t)WE_CFG_INDEV_PERIOD_MS - t);
#else
        t = 0U;
#endif
        if (t < wake)
            wake = t;
    }

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 本次消费之后中断里又注入了按键：尽快再运行 */
    if (p_lcd->key_q_head != p_lcd->key_q_tail)
        wake = 0U;
#endif
    return wake;
}

uint32_t we_gui_run(we_lcd_t *p_lcd, uint32_t now_ms)
{
    uint32_t elapsed;
    uint16_t step;

    if (p_lcd == NULL || p_lcd->port == NULL)
        return WE_GUI_RUN_IDLE;

    /* 无符号相减：时间戳回绕后差值仍正确 */
    elapsed = now_ms - p_lcd->time_last_ms;
    p_lcd->time_last_ms = now_ms;
    step = (elapsed > 65535U) ? 65535U : (uint16_t)elapsed;

    /* 1. 采样触摸并派发 */
#if (WE_CFG_INDEV_PERIOD_MS > 0)
    if ((uint32_t)(now_ms - p_lcd->indev_last_ms) >= (uint32_t)WE_CFG_INDEV_PERIOD_MS)
    {
        p_lcd->indev_last_ms = now_ms;
        _we_input_poll(p_lcd);
    }
#else
    _we_input_poll(p_lcd);
#endif

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 2. 消费语义键队列 */
    _we_focus_process_keys(p_lcd);
#endif

    /* 3. 推进用户定时器 */
    _we_tick_run_timers(p_lcd, step);

    /* 4. 推进动画并渲染 */
#if (WE_CFG_REFR_PERIOD_MS > 0)
    {
        uint32_t acc = (uint32_t)p_lcd->anim_pending_ms + step;

        p_lcd->anim_pending_ms = (acc > 65535U) ? 65535U : (uint16_t)acc;
    }
    if ((uint32_t)(now_ms - p_lcd->frame_last_ms) >= (uint32_t)WE_CFG_REFR_PERIOD_MS)
    {
        p_lcd->frame_last_ms = now_ms;
        _we_tick_run_anims(p_lcd, p_lcd->anim_pending_ms);
        p_lcd->anim_pending_ms = 0U;
        _we_refresh_run(p_lcd);
    }
#else
    _we_tick_run_anims(p_lcd, step);
    _we_refresh_run(p_lcd);
#endif

    return _we_core_next_wake(p_lcd, now_ms);
}

void we_gui_init(we_lcd_t *p_lcd, const we_port_t *port, colour_t bg, colour_t *gram, uint16_t gram_pixels)
{
    WE_ASSERT(p_lcd != NULL && port != NULL && port->set_addr != NULL && port->flush != NULL &&
              gram != NULL && gram_pixels != 0U);
    if (p_lcd == NULL || port == NULL || port->set_addr == NULL || port->flush == NULL ||
        gram == NULL || gram_pixels == 0U)
        return; /* 容错返回：与全库"断言后静默判空守卫"契约一致 */

    p_lcd->port = port;
    p_lcd->width = SCREEN_WIDTH;
    p_lcd->height = SCREEN_HEIGHT;
    p_lcd->bg_color = bg;

    p_lcd->obj_list_head = NULL; /* 对象链表初始为空 */
    p_lcd->timer_head = NULL;    /* 用户定时器链从空开始（节点归调用方所有） */
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    p_lcd->top_list_head = NULL; /* 顶层对象链从空开始（toast/msgbox 等置顶层） */
    p_lcd->modal_obj = NULL;     /* 无模态 */
#endif
    p_lcd->anim_head = NULL; /* 中央动画链表初始为空 */
    p_lcd->opa_scale = 255U; /* 容器透明度级联乘子，默认无衰减 */
    p_lcd->time_last_ms = 0U; /* 时间从 0 起算：第一次 we_gui_run 的流逝 = 传入的时间戳 */
#if (WE_CFG_REFR_PERIOD_MS > 0)
    p_lcd->frame_last_ms = 0U;
    p_lcd->anim_pending_ms = 0U;
#endif
#if (WE_CFG_INDEV_PERIOD_MS > 0)
    p_lcd->indev_last_ms = 0U;
#endif
#if (WE_CFG_ENABLE_RENDER_STATS == 1)
    p_lcd->stat_render_frames = 0U;
#endif
    p_lcd->indev_data.x = 0;
    p_lcd->indev_data.y = 0;
    p_lcd->indev_data.state = WE_TOUCH_STATE_NONE;
    p_lcd->gesture_press_x = 0;
    p_lcd->gesture_press_y = 0;
    p_lcd->pressed_obj = NULL;
    p_lcd->gesture_had_stay = 0U;
    p_lcd->gesture_drag_done = 0U;
    p_lcd->touch_down = 0U;
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    p_lcd->focus_obj = NULL; /* 焦点从无开始，首个导航键或 we_focus_set 唤出 */
    p_lcd->focus_flags = 0U;
    p_lcd->key_q_head = 0U;
    p_lcd->key_q_tail = 0U;
    p_lcd->key_ok_down = 0U;
#endif

    /* PFB：默认整块用于绘制。开启 GRAM_DMA_BUFF_EN 且端口为异步发送时一分为二，
     * 一半绘制、一半由 DMA 发送；砍半后每半至少要装下一整行，且行数仍是
     * Y 对齐粒度的整数倍（页式屏依赖整页切片），否则保持整块。 */
    p_lcd->pfb_gram_base = gram;
    p_lcd->pfb_gram = gram;
    p_lcd->pfb_size = gram_pixels;
    p_lcd->pfb_double = 0U;
#if (GRAM_DMA_BUFF_EN == 1)
    if (port->flush_async != 0U)
    {
        uint16_t half = (uint16_t)(gram_pixels / 2U);
        uint16_t rows = (uint16_t)(half / SCREEN_WIDTH);

        if (rows != 0U && (rows & (uint16_t)(WE_LCD_FLUSH_ALIGN_Y - 1)) == 0U)
        {
            p_lcd->pfb_size = half;
            p_lcd->pfb_double = 1U;
        }
    }
#endif
    /* 异步端口必须拿到两半显存，否则内核会改写 DMA 仍在发送的像素 */
    WE_ASSERT(port->flush_async == 0U || p_lcd->pfb_double != 0U);

    we_dirty_init(&p_lcd->dirty_mgr);
    p_lcd->dirty_mgr.screen_w = p_lcd->width;
    p_lcd->dirty_mgr.screen_h = p_lcd->height;
    we_dirty_invalidate(&p_lcd->dirty_mgr, 0, 0, p_lcd->width, p_lcd->height);
}
