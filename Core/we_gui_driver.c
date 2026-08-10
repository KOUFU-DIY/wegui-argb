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
#include "we_gui_driver.h"
#include "image_res.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * GUI 时间调度模型（两层）
 *
 * 1. 用户定时器（timer_head 侵入式链表，节点归调用方所有）：面向业务层的
 *    we_gui_timer_create 一族接口，适合"按时间触发"的逻辑；
 * 2. 中央动画引擎（we_anim_t 侵入式链表，lcd->anim_head）：控件/容器
 *    动画统一走这里，节点内嵌在控件结构体里，零堆、不占槽位、数量无上限。
 * 两者都由 we_gui_task_handler() 每个调度周期统一推进。
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * PFB 双缓冲辅助
 *
 * 设计目标：
 * 1. 当 GRAM_DMA_BUFF_EN = 1 时，把用户传入的整块 GRAM 一分为二：
 *    - 一半给当前 CPU 绘制
 *    - 一半给底层 DMA/SPI 发送
 * 2. pfb_gram_base 始终保存整块缓冲的起始地址；
 * 3. pfb_gram 始终指向“当前正在被 GUI 绘制”的那一半。
 * -------------------------------------------------------------------------- */
/* PFB 真双缓冲条件：用户开启双缓冲 且 端口确为异步发送（WE_PORT_FLUSH_ASYNC）。
 * 阻塞端口（如 F103 默认 _SOFT_4SPI、SDL 模拟器）自动退回整块 PFB。 */
#define WE_PFB_DOUBLE_BUF ((GRAM_DMA_BUFF_EN == 1) && (WE_PORT_FLUSH_ASYNC == 1))

/**
 * @brief 在 DMA 双缓冲模式下切换当前 PFB 工作区
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 仅在真双缓冲（WE_PFB_DOUBLE_BUF）时生效，否则为空操作。
 */
static void _we_lcd_swap_pfb(we_lcd_t *p_lcd)
{
#if WE_PFB_DOUBLE_BUF
    /* pfb_gram 是 colour_t *，sizeof(colour_t) 随色深变化，
     * 指针步进天然覆盖 RGB565 和 RGB888，无需按色深分支。 */
    if (p_lcd->pfb_gram == p_lcd->pfb_gram_base)
    {
        p_lcd->pfb_gram = p_lcd->pfb_gram_base + p_lcd->pfb_size;
    }
    else
    {
        p_lcd->pfb_gram = p_lcd->pfb_gram_base;
    }
#else
    (void)p_lcd;
#endif
}

/* --------------------------------------------------------------------------
 * GUI 用户定时器运行器
 *
 * 设计目标：
 * 1. 对业务层提供“按周期触发”的时间接口，而不是暴露底层 task 队列；
 * 2. 支持单次定时器和周期定时器；
 * 3. 当主循环偶尔抖动时，允许按周期补偿回调次数，避免动画或逻辑明显变慢。
 * -------------------------------------------------------------------------- */
/**
 * @brief 推进并触发 GUI 用户定时器
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param elapsed_ms 传入，本轮累计的已流逝时间，单位毫秒
 * @return 无
 * @note 当 elapsed_ms 大于周期时会按周期补偿触发，避免时间驱动逻辑整体变慢。
 */
static void _we_gui_run_timers(we_lcd_t *p_lcd, uint16_t elapsed_ms)
{
    we_gui_timer_t *t;
    we_gui_timer_t *t_next;

    if (p_lcd == NULL || elapsed_ms == 0U)
        return;

    /* 先存 next 再回调：允许回调内 delete（摘除）自身节点 */
    for (t = p_lcd->timer_head; t != NULL; t = t_next)
    {
        uint32_t total_ms;
        uint8_t fired = 0U;

        t_next = t->next;
        if (t->cb == NULL || t->active == 0U || t->period_ms == 0U)
            continue;

        total_ms = (uint32_t)t->acc_ms + elapsed_ms;

        while (t->cb != NULL && t->active != 0U && t->period_ms != 0U && total_ms >= t->period_ms)
        {
            /* 补偿封顶：长阻塞恢复帧最多补发 CATCHUP_MAX 次，剩余节拍丢弃
             * （从当前重新计时），防止回调风暴把恢复帧再次拖死。 */
            if (fired >= WE_CFG_TIMER_CATCHUP_MAX)
            {
                total_ms = 0U;
                break;
            }
            fired++;
            total_ms -= t->period_ms;
            t->cb(p_lcd, t->period_ms);

            if (t->cb == NULL || t->active == 0U)
                break;

            if (t->repeat == 0U)
            {
                t->active = 0U;
                total_ms = 0U;
                break;
            }
        }

        if (t->cb != NULL && t->active != 0U)
        {
            t->acc_ms = (uint16_t)total_ms;
        }
    }
}

/* --------------------------------------------------------------------------
 * GUI 脏矩形刷新任务
 *
 * 设计目标：
 * 1. 把 we_gui_task_handler() 中“消费脏矩形并推屏”的逻辑单独收口；
 * 2. 保持主任务函数只负责调度顺序，降低主函数体积和阅读负担；
 * 3. 不把脏矩形刷新塞进任务表，避免打乱核心渲染阶段顺序。
 * -------------------------------------------------------------------------- */
/**
 * @brief 消费脏矩形并按块推送刷新
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
static void _we_gui_flush_dirty(we_lcd_t *p_lcd)
{
    uint8_t iter = 0;
    we_rect_t rect;

    if (p_lcd == NULL || p_lcd->dirty_mgr.count == 0)
    {
        return;
    }

    /* 遍历合并后的脏矩形，逐块推动底层刷新。 */
    while (we_dirty_get_next(&p_lcd->dirty_mgr, &rect, &iter))
    {
        uint16_t w = rect.x1 - rect.x0 + 1;
        uint16_t h = rect.y1 - rect.y0 + 1;

        we_push_pfb(p_lcd, rect.x0, rect.y0, w, h);
    }

    /* 只要这一轮真正消费了脏区并完成提交，就记作 1 帧渲染。 */
#if (WE_CFG_ENABLE_RENDER_STATS == 1)
    p_lcd->stat_render_frames++;
#endif

    /* 所有脏区处理完成后，清空管理器，等待业务层下一轮标脏。 */
    we_dirty_clear(&p_lcd->dirty_mgr);
}

#if (WE_CFG_DEBUG_PERF_STRESS == 1)
/* --------------------------------------------------------------------------
 * 控件性能压测：每帧强制标脏所有顶层控件
 *
 * 仅在 WE_CFG_DEBUG_PERF_STRESS == 1 时编译进来。开启后，主任务每轮都会把
 * 当前对象链表里的每个控件按其自身区域重新标脏，使 GUI 持续全量重绘，
 * 从而暴露当前页面控件的最坏情况渲染吞吐，配合 FPS / stat 计数器观察性能。
 * -------------------------------------------------------------------------- */
/**
 * @brief 强制把所有顶层控件按自身区域标脏（性能压测专用）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 只遍历顶层 obj_list_head；子容器内的子控件会随父容器整块重绘，
 *       因此无需递归进入 children_head。
 */
static void _we_gui_perf_stress_invalidate(we_lcd_t *p_lcd)
{
    we_obj_t *curr;

    if (p_lcd == NULL)
        return;

    for (curr = p_lcd->obj_list_head; curr != NULL; curr = curr->next)
    {
        we_obj_invalidate(curr);
    }
}
#endif


/**
 * @brief 用当前屏幕背景色清空整个 PFB。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @return 无。
 * @note 本函数只是对 we_fill_gram() 的轻量封装，
 *       目的是让“按背景色清屏”这个语义更直观。
 */
void we_clear_gram(we_lcd_t *p_lcd) { we_fill_gram(p_lcd, p_lcd->bg_color); }

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
/* --------------------------------------------------------------------------
 * 全局聚焦 / 按键导航管理器
 *
 * 模型（分层作用域，"OK 向下钻 / BACK 向上退"）：
 * 1. 语义键由端口消抖后注入 SPSC 环形队列（press/release 双沿或 inject
 *    tap），we_gui_task_handler() 每周期消费并分发；
 * 2. 可聚焦判定：类描述符带 key_cb 的交互控件，或子树内含可聚焦控件的
 *    复合容器（class_flags 含 WE_CLASS_FLAG_FOCUS_ENTER，
 *    WE_CFG_FOCUS_NESTED=0 时不递归容器）；
 * 3. 当前作用域由 focus_obj->parent 推导（父指针即栈，无需焦点栈）：
 *    方向键在同层兄弟间按包围盒中心做空间四向就近移动（方向上无候选
 *    则环绕到对侧最远者），NEXT/PREV 走线性环序；OK 进入容器或触发
 *    控件，BACK 退回父容器本体，顶层再按则清除焦点；
 * 4. 视觉为驱动级矩形光标：普通对象之后、顶层对象之前补画，
 *    悬浮在普通层控件之上且不压顶层对象；顶层对象内部的焦点由其自绘；
 *    光标只在按键活动或 we_focus_set 程序设焦时显示，触摸按下即收起
 *    （触摸的焦点跟随仍生效，只是不画环——手指本身就是光标）；
 * 5. 模态激活期间语义键直送模态对象的 event_cb，不再经过焦点管理器。
 * 全部瞬态标志压缩在 focus_flags 一个字节里（WE_FOCUS_F_*），
 * OK 最短按压窗口由 task_handler 直接倒计时，不占中央动画节点。
 * -------------------------------------------------------------------------- */

/* 焦点光标外扩总量（控件包围盒边缘 → 光标框外缘的距离） */
#define _WE_FOCUS_EXPAND (WE_CFG_FOCUS_CURSOR_GAP + WE_CFG_FOCUS_CURSOR_THICKNESS)

/**
 * @brief 结构性判定对象是否为焦点候选（不触发 FOCUS 实例查询）
 * @param obj 传入，待判定对象
 * @return 1 表示候选（交互控件或子树含候选的容器），0 表示不可聚焦
 */
static uint8_t _we_focus_is_candidate(const we_obj_t *obj)
{
    if (obj == NULL || obj->class_p == NULL)
        return 0U;
    if ((obj->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U)
        return 1U;
#if (WE_CFG_FOCUS_NESTED == 1)
    /* 行为位：只有声明“焦点可下钻”的容器才作为停靠点向内探测。
     * slideshow / mask_group 只带结构位，故焦点不会进入其子树。 */
    if ((obj->class_p->class_flags & WE_CLASS_FLAG_FOCUS_ENTER) != 0U)
    {
        const we_obj_t *child = ((const we_child_owner_t *)obj)->children_head;
        while (child != NULL)
        {
            if (_we_focus_is_candidate(child))
                return 1U;
            child = child->next;
        }
    }
#endif
    return 0U;
}

/**
 * @brief 标脏对象的焦点光标环形区域
 * @param obj 传入，光标所属对象
 * @return 无
 * @note 直接提交与绘制端逐一对应的 4 条线宽条带（上/下贯通全宽，
 *       左/右扣除与上下带的重叠），确定性 4 个脏矩形：控件本体与间隙
 *       区域零重绘，且不经过 exclude 打洞路径的收益门限（小控件也
 *       不会退化成整框）。
 */
static void _we_focus_cursor_invalidate(we_obj_t *obj)
{
    int16_t x0;
    int16_t y0;
    int16_t out_w;
    int16_t side_h;

    if (obj == NULL || obj->lcd == NULL)
        return;

    x0 = (int16_t)(obj->x - _WE_FOCUS_EXPAND);
    y0 = (int16_t)(obj->y - _WE_FOCUS_EXPAND);
    out_w = (int16_t)(obj->w + 2 * _WE_FOCUS_EXPAND);
    side_h = (int16_t)(obj->h + 2 * WE_CFG_FOCUS_CURSOR_GAP);

    /* 上、下条带（含四角） */
    we_obj_invalidate_area(obj, x0, y0, out_w, WE_CFG_FOCUS_CURSOR_THICKNESS);
    we_obj_invalidate_area(obj, x0, (int16_t)(obj->y + obj->h + WE_CFG_FOCUS_CURSOR_GAP),
                           out_w, WE_CFG_FOCUS_CURSOR_THICKNESS);
    /* 左、右条带（夹在上下带之间） */
    we_obj_invalidate_area(obj, x0, (int16_t)(obj->y - WE_CFG_FOCUS_CURSOR_GAP),
                           WE_CFG_FOCUS_CURSOR_THICKNESS, side_h);
    we_obj_invalidate_area(obj, (int16_t)(obj->x + obj->w + WE_CFG_FOCUS_CURSOR_GAP),
                           (int16_t)(obj->y - WE_CFG_FOCUS_CURSOR_GAP),
                           WE_CFG_FOCUS_CURSOR_THICKNESS, side_h);
}

/**
 * @brief 亮出焦点光标（按键活动或 we_focus_set 程序设焦时调用）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 已可见则无动作；新亮出时标脏当前焦点的环区让光标画出来。
 */
static void _we_focus_cursor_show(we_lcd_t *lcd)
{
    if ((lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
        return;
    lcd->focus_flags |= WE_FOCUS_F_CURSOR_VIS;
    if (lcd->focus_obj != NULL)
        _we_focus_cursor_invalidate(lcd->focus_obj);
}

/* ---------------- OK 键按下/松开双沿机件 ----------------
 * OK 按下沿：焦点控件进入按压态（btn 显示 PRESSED 且按住期间常驻），
 * 管理器吞掉按住期间的重复按下沿（防系统连发误触发）；
 * OK 松开沿：回发 WE_KEY_EVT_OK_RELEASE（控件回弹并触发点击）。
 * tap 式注入（一按一松同周期到达）经"最短按压窗口"（WE_CFG_FOCUS_FLASH_MS）
 * 延后回弹，保证按压视觉可见；焦点切走/清除时改发 WE_KEY_EVT_FLASH_END
 * （仅回弹不点击）。方向/前后/BACK 键仍为按下沿触发，松开沿忽略。
 * 不变式：OK_ARMED 置位期间按压目标恒为 focus_obj（arm 只发生在焦点上，
 * 焦点切换/清除/删除必先取消），因此无需单独的目标指针。 */

/**
 * @brief 交付 OK 松开：清按压状态并回发 WE_KEY_EVT_OK_RELEASE
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
static void _we_key_ok_deliver_release(we_lcd_t *lcd)
{
    we_obj_t *obj = ((lcd->focus_flags & WE_FOCUS_F_OK_ARMED) != 0U) ? lcd->focus_obj : NULL;

    lcd->focus_flags &= (uint8_t)~(WE_FOCUS_F_OK_HELD | WE_FOCUS_F_OK_ARMED | WE_FOCUS_F_REL_PEND);
    lcd->key_flash_left_ms = 0U;
    if (obj != NULL && obj->class_p != NULL && obj->class_p->event_cb != NULL &&
        (obj->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U)
        (void)obj->class_p->event_cb(obj, WE_KEY_EVT_OK_RELEASE, &lcd->indev_data);
}

/**
 * @brief 取消 OK 按压（焦点切换/清除时）：仅回弹按压视觉，不触发点击
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
static void _we_key_ok_cancel(we_lcd_t *lcd)
{
    we_obj_t *obj = ((lcd->focus_flags & WE_FOCUS_F_OK_ARMED) != 0U) ? lcd->focus_obj : NULL;

    if ((lcd->focus_flags & (WE_FOCUS_F_OK_HELD | WE_FOCUS_F_OK_ARMED)) == 0U)
        return;
    lcd->focus_flags &= (uint8_t)~(WE_FOCUS_F_OK_HELD | WE_FOCUS_F_OK_ARMED | WE_FOCUS_F_REL_PEND);
    lcd->key_flash_left_ms = 0U;
    if (obj != NULL && obj->class_p != NULL && obj->class_p->event_cb != NULL &&
        (obj->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U)
        (void)obj->class_p->event_cb(obj, WE_KEY_EVT_FLASH_END, &lcd->indev_data); /* 仅回弹 */
}

/**
 * @brief OK 按下沿被焦点控件消费后进入按压待回发状态与最短按压窗口
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 窗口倒计时由 we_gui_task_handler 直接消费 elapsed_ms 推进，
 *       不占用中央动画节点。
 */
static void _we_key_ok_arm(we_lcd_t *lcd)
{
    lcd->focus_flags |= WE_FOCUS_F_OK_ARMED;
    lcd->key_flash_left_ms = WE_CFG_FOCUS_FLASH_MS;
}

/**
 * @brief 尝试把焦点移到目标对象（含 FOCUS 实例查询）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param obj 传入，目标对象
 * @return 1 表示焦点已落到目标（或目标本就是焦点），0 表示目标拒绝/不可聚焦
 */
static uint8_t _we_focus_try_set(we_lcd_t *lcd, we_obj_t *obj)
{
    we_obj_t *old = lcd->focus_obj;

    if (obj == old)
        return 1U;
    if (!_we_focus_is_candidate(obj))
        return 0U;
    /* 交互控件按实例查询（DISABLED/全透明可拒绝）；容器无查询直接接受 */
    if ((obj->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U &&
        obj->class_p->event_cb(obj, WE_KEY_EVT_FOCUS, &lcd->indev_data) == 0U)
        return 0U;

    lcd->focus_flags &= (uint8_t)~WE_FOCUS_F_EDIT; /* 焦点切换自动退出编辑态 */
    _we_key_ok_cancel(lcd); /* 按住 OK 期间焦点切换：旧控件仅回弹不触发点击 */
    if (old != NULL)
    {
        if (old->class_p != NULL && old->class_p->event_cb != NULL &&
            (old->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U)
            (void)old->class_p->event_cb(old, WE_KEY_EVT_DEFOCUS, &lcd->indev_data);
        if ((lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
            _we_focus_cursor_invalidate(old);
    }
    lcd->focus_obj = obj;
    if ((lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
        _we_focus_cursor_invalidate(obj);

    /* 通知祖先容器链"子树内有对象获得焦点"：scroll_panel 等据此滚动
     * 跟随，保证焦点子控件（含光标环）滚入可视区。 */
    {
        we_obj_t *anc = obj->parent;
        while (anc != NULL)
        {
            if (anc->class_p != NULL && anc->class_p->event_cb != NULL &&
                (anc->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U)
                (void)anc->class_p->event_cb(anc, WE_KEY_EVT_CHILD_FOCUS, &lcd->indev_data);
            anc = anc->parent;
        }
    }
    return 1U;
}

/**
 * @brief 取当前焦点所在作用域的兄弟链表头
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param ref 传入，作用域内任一对象（通常为当前焦点）
 * @return 兄弟链表头指针（顶层链表或父容器子链表）
 */
static we_obj_t *_we_focus_scope_head(we_lcd_t *lcd, const we_obj_t *ref)
{
    if (ref->parent != NULL)
        return ((we_child_owner_t *)ref->parent)->children_head;
    return lcd->obj_list_head;
}

/**
 * @brief 无焦点时聚焦顶层第一个接受的候选对象
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
static void _we_focus_init_first(we_lcd_t *lcd)
{
    we_obj_t *it = lcd->obj_list_head;
    while (it != NULL)
    {
        if (_we_focus_try_set(lcd, it))
            return;
        it = it->next;
    }
}

/**
 * @brief 判定对象当前是否可接受聚焦（结构候选 + FOCUS 实例查询，无副作用）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param obj 传入，待判定对象
 * @return 1 可聚焦，0 不可
 */
static uint8_t _we_focus_acceptable(we_lcd_t *lcd, we_obj_t *obj)
{
    (void)lcd;
    if (!_we_focus_is_candidate(obj))
        return 0U;
    if ((obj->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U &&
        obj->class_p->event_cb(obj, WE_KEY_EVT_FOCUS, &lcd->indev_data) == 0U)
        return 0U;
    return 1U;
}

/**
 * @brief 空间四向移动焦点：按包围盒中心在当前作用域内做方向就近搜索
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param dx 传入，水平方向（-1/0/+1）
 * @param dy 传入，垂直方向（-1/0/+1）
 * @return 无
 * @note 评分 = 主轴投影距离 + 2×侧偏（主轴投影须为正，即真的在该方向上）；
 *       方向上没有任何候选时环绕到对侧最远者（主轴反向投影最大）。
 *       候选判定含 FOCUS 实例查询（无副作用），选中后经 _we_focus_try_set
 *       正式落焦。
 */
static void _we_focus_move_dir(we_lcd_t *lcd, int16_t dx, int16_t dy)
{
    we_obj_t *cur = lcd->focus_obj;
    we_obj_t *it = _we_focus_scope_head(lcd, cur);
    int32_t ccx = (int32_t)cur->x + cur->w / 2;
    int32_t ccy = (int32_t)cur->y + cur->h / 2;
    we_obj_t *best = NULL;
    int32_t best_score = 0x7FFFFFFF;
    we_obj_t *wrap = NULL;
    int32_t wrap_far = -1;

    for (; it != NULL; it = it->next)
    {
        int32_t pdx;
        int32_t pdy;
        int32_t primary;
        int32_t second;

        if (it == cur || !_we_focus_acceptable(lcd, it))
            continue;

        pdx = ((int32_t)it->x + it->w / 2) - ccx;
        pdy = ((int32_t)it->y + it->h / 2) - ccy;
        primary = (dx != 0) ? pdx * dx : pdy * dy; /* 主轴投影（正 = 在目标方向） */
        second = (dx != 0) ? pdy : pdx;            /* 侧偏 */
        if (second < 0)
            second = -second;

        if (primary > 0)
        {
            int32_t score = primary + 2 * second;

            if (score < best_score)
            {
                best_score = score;
                best = it;
            }
        }
        else if (-primary > wrap_far)
        {
            wrap_far = -primary; /* 对侧最远者：方向上无候选时的环绕落点 */
            wrap = it;
        }
    }

    if (best != NULL)
        (void)_we_focus_try_set(lcd, best);
    else if (wrap != NULL)
        (void)_we_focus_try_set(lcd, wrap);
}

/**
 * @brief 在当前作用域兄弟间线性移动焦点（NEXT/PREV，到边界回绕）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param dir_next 传入，1 = 向后（NEXT），0 = 向前（PREV）
 * @return 无
 * @note 逐个尝试候选直到有对象接受聚焦；转满一圈无人接受则原地不动。
 */
static void _we_focus_move(we_lcd_t *lcd, uint8_t dir_next)
{
    we_obj_t *cur = lcd->focus_obj;
    we_obj_t *head = _we_focus_scope_head(lcd, cur);
    we_obj_t *pick = cur;

    for (;;)
    {
        if (dir_next)
        {
            pick = (pick->next != NULL) ? pick->next : head;
        }
        else
        {
            /* 单向链表取前驱：从头扫到 pick 的前一个；pick 已是头则回绕取尾 */
            we_obj_t *it = head;
            we_obj_t *prev = NULL;
            while (it != NULL && it != pick)
            {
                prev = it;
                it = it->next;
            }
            if (prev == NULL)
            {
                it = head;
                while (it != NULL && it->next != NULL)
                    it = it->next;
                prev = it;
            }
            pick = prev;
        }
        if (pick == NULL || pick == cur)
            return; /* 作用域内没有其他可聚焦对象 */
        if (_we_focus_try_set(lcd, pick))
            return;
    }
}

#if (WE_CFG_FOCUS_NESTED == 1)
/**
 * @brief OK 下钻：焦点进入容器，落到第一个接受的子候选上
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
static void _we_focus_enter(we_lcd_t *lcd)
{
    we_obj_t *cur = lcd->focus_obj;
    we_obj_t *child;

    if (cur->class_p == NULL || (cur->class_p->class_flags & WE_CLASS_FLAG_FOCUS_ENTER) == 0U)
        return;
    child = ((we_child_owner_t *)cur)->children_head;
    while (child != NULL)
    {
        if (_we_focus_try_set(lcd, child))
            return;
        child = child->next;
    }
}
#endif

/**
 * @brief BACK 上退：焦点退回父容器本体；已在顶层则清除焦点
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note WE_CFG_FOCUS_NESTED=0 时不存在"退回容器"语义，BACK 直接清焦点。
 */
static void _we_focus_back(we_lcd_t *lcd)
{
#if (WE_CFG_FOCUS_NESTED == 1)
    we_obj_t *cur = lcd->focus_obj;

    if (cur->parent != NULL)
        (void)_we_focus_try_set(lcd, cur->parent);
    else
        we_focus_set(lcd, NULL);
#else
    we_focus_set(lcd, NULL);
#endif
}

/**
 * @brief 分发单个语义键队列编码（键值 | 可选的松开沿标志）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param code 传入，键值，松开沿带 WE_KEY_RELEASE_FLAG
 * @return 无
 */
static void _we_focus_dispatch(we_lcd_t *lcd, uint8_t code)
{
    we_obj_t *cur;
    uint8_t key = (uint8_t)(code & (uint8_t)~WE_KEY_RELEASE_FLAG);

    /* 松开沿最优先处理（按压状态同步不受弹层/焦点门控影响）：
     * 焦点侧只有 OK 消费松开沿；模态键通道额外收到原始编码
     * （键值 | WE_KEY_RELEASE_FLAG），供弹层内控件做按下/松开双沿
     * 手感（软键盘击键等），不消费的弹层忽略即可。 */
    if ((code & WE_KEY_RELEASE_FLAG) != 0U)
    {
        if (key == WE_KEY_OK && (lcd->focus_flags & WE_FOCUS_F_OK_HELD) != 0U)
        {
            if (lcd->key_flash_left_ms > 0U)
                lcd->focus_flags |= WE_FOCUS_F_REL_PEND; /* 仍在最短按压窗口内：挂起补发 */
            else
                _we_key_ok_deliver_release(lcd);
        }
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
        if (lcd->modal_obj != NULL && lcd->modal_obj->class_p != NULL &&
            lcd->modal_obj->class_p->event_cb != NULL)
        {
            /* 模态键通道：松开沿以原始编码（键值|RELEASE_FLAG）直送，
             * 供弹层内控件做双沿按压手感（软键盘击键等）。 */
            (void)lcd->modal_obj->class_p->event_cb(lcd->modal_obj, (we_event_t)code,
                                                    &lcd->indev_data);
            return;
        }
#endif
        return;
    }

#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    if (lcd->modal_obj != NULL)
    {
        /* 模态键通道：按下沿以裸键值直送模态对象的统一 event_cb，
         * 未消费也不穿透（模态吞键语义）。 */
        if (lcd->modal_obj->class_p != NULL && lcd->modal_obj->class_p->event_cb != NULL)
            (void)lcd->modal_obj->class_p->event_cb(lcd->modal_obj, (we_event_t)key,
                                                    &lcd->indev_data);
        return;
    }
#endif


    /* OK 按下沿去重：按住期间的系统连发直接丢弃（防连续触发） */
    if (key == WE_KEY_OK)
    {
        if ((lcd->focus_flags & WE_FOCUS_F_OK_HELD) != 0U)
            return;
        lcd->focus_flags = (uint8_t)((lcd->focus_flags | WE_FOCUS_F_OK_HELD) &
                                     (uint8_t)~(WE_FOCUS_F_OK_ARMED | WE_FOCUS_F_REL_PEND));
        lcd->key_flash_left_ms = 0U;
    }

    cur = lcd->focus_obj;
    if (cur != NULL && cur->class_p == NULL)
    {
        /* 防御：焦点对象已被外部置失效，立即丢弃引用（同 pressed_obj 口径） */
        lcd->focus_obj = NULL;
        cur = NULL;
    }

    if (cur == NULL)
    {
        /* 无焦点：任意导航键唤出焦点；OK/BACK 忽略 */
        if (key != WE_KEY_OK && key != WE_KEY_BACK)
            _we_focus_init_first(lcd);
        return;
    }

#if (WE_CFG_FOCUS_EDIT == 1)
    /* 编辑态：全部按键先交焦点控件调值；未消费的 OK/BACK 退出编辑，
     * 其余导航键吞掉（编辑期间不移动焦点）。 */
    if ((lcd->focus_flags & WE_FOCUS_F_EDIT) != 0U)
    {
        if ((cur->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U &&
            cur->class_p->event_cb(cur, (we_event_t)key, &lcd->indev_data) != 0U)
        {
            if (key == WE_KEY_OK)
                _we_key_ok_arm(lcd); /* 进入按压待回发状态，等待松开沿 */
            return;
        }
        if (key == WE_KEY_OK || key == WE_KEY_BACK)
            we_focus_edit_exit(lcd);
        return;
    }
#endif

    /* 焦点控件优先消费（btn 吃 OK；值类控件在编辑态吃方向键） */
    if ((cur->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U &&
        cur->class_p->event_cb(cur, (we_event_t)key, &lcd->indev_data) != 0U)
    {
        if (key == WE_KEY_OK)
            _we_key_ok_arm(lcd); /* 进入按压待回发状态，等待松开沿 */
        return;
    }

    /* 管理器默认导航：方向键 = 空间四向就近，Tab/前后 = 线性环序 */
    switch (key)
    {
    case WE_KEY_UP:
        _we_focus_move_dir(lcd, 0, -1);
        break;
    case WE_KEY_DOWN:
        _we_focus_move_dir(lcd, 0, 1);
        break;
    case WE_KEY_LEFT:
        _we_focus_move_dir(lcd, -1, 0);
        break;
    case WE_KEY_RIGHT:
        _we_focus_move_dir(lcd, 1, 0);
        break;
    case WE_KEY_NEXT:
        _we_focus_move(lcd, 1U);
        break;
    case WE_KEY_PREV:
        _we_focus_move(lcd, 0U);
        break;
#if (WE_CFG_FOCUS_NESTED == 1)
    case WE_KEY_OK:
        _we_focus_enter(lcd);
        break;
#endif
    case WE_KEY_BACK:
        _we_focus_back(lcd);
        break;
    default:
        break;
    }
}

/**
 * @brief 键队列入队（编码值 = 键值 | 可选松开沿标志）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param code 传入，队列编码值
 * @return 无
 * @note SPSC 环形队列：本函数只写 tail（可在中断里调用），消费侧只写
 *       head，无读改写竞态；先写数据槽再推进 tail，容量 = 队列深度-1，
 *       队满丢弃本次注入。下标回绕用位与（深度限定 2 的幂，省除法）。
 */
static void _we_key_enqueue(we_lcd_t *lcd, uint8_t code)
{
    uint8_t tail = lcd->key_q_tail;
    uint8_t next = (uint8_t)((tail + 1U) & (WE_CFG_KEY_QUEUE_LEN - 1U));

    if (next == lcd->key_q_head)
    {
        /* 队满丢弃本次注入。OK 松开沿丢失会让 HELD 清不掉（控件卡在
         * 按压态、后续 OK 按下沿全被连发去重吞掉），单独计数（仅注入
         * 侧写这一个字节，维持 SPSC 无竞态），交消费侧对账补投。 */
        if (code == (uint8_t)(WE_KEY_OK | WE_KEY_RELEASE_FLAG))
            lcd->key_ok_drop_seq++;
        return;
    }
    lcd->key_queue[tail] = code;
    lcd->key_q_tail = next;
}

void we_gui_key_press(we_lcd_t *lcd, uint8_t key)
{
    if (lcd == NULL || key < WE_KEY_UP || key > WE_KEY_BACK)
        return; /* WE_KEY_EVT_* 通知类键值禁止注入 */
    _we_key_enqueue(lcd, key);
}

void we_gui_key_release(we_lcd_t *lcd, uint8_t key)
{
    if (lcd == NULL || key < WE_KEY_UP || key > WE_KEY_BACK)
        return;
    _we_key_enqueue(lcd, (uint8_t)(key | WE_KEY_RELEASE_FLAG));
}

void we_gui_key_inject(we_lcd_t *lcd, uint8_t key)
{
    /* tap 语义 = 一按一松；OK 键经最短按压窗口保证按压视觉可见 */
    we_gui_key_press(lcd, key);
    we_gui_key_release(lcd, key);
}

void we_focus_set(we_lcd_t *lcd, we_obj_t *obj)
{
    WE_ASSERT(lcd != NULL);
    if (lcd == NULL)
        return;
    if (obj == NULL)
    {
        we_obj_t *old = lcd->focus_obj;
        if (old == NULL)
            return;
        _we_key_ok_cancel(lcd); /* 清焦点前先取消 OK 按压（仅回弹不点击） */
        if (old->class_p != NULL && old->class_p->event_cb != NULL &&
            (old->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U)
            (void)old->class_p->event_cb(old, WE_KEY_EVT_DEFOCUS, &lcd->indev_data);
        lcd->focus_obj = NULL;
        lcd->focus_flags &= (uint8_t)~WE_FOCUS_F_EDIT;
        if ((lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
            _we_focus_cursor_invalidate(old);
        return;
    }
    if (obj->lcd != lcd)
        return;
    _we_focus_cursor_show(lcd); /* 程序设焦点视为要引导用户视线：光标亮出 */
    (void)_we_focus_try_set(lcd, obj);
}

we_obj_t *we_focus_get(we_lcd_t *lcd) { return (lcd != NULL) ? lcd->focus_obj : NULL; }

uint8_t we_focus_candidate(we_obj_t *obj) { return _we_focus_is_candidate(obj); }

#if (WE_CFG_FOCUS_EDIT == 1)
void we_focus_edit_enter(we_lcd_t *lcd)
{
    if (lcd == NULL || lcd->focus_obj == NULL || (lcd->focus_flags & WE_FOCUS_F_EDIT) != 0U)
        return;
    _we_focus_cursor_show(lcd); /* 程序直接进编辑态时保证光标可见 */
    lcd->focus_flags |= WE_FOCUS_F_EDIT;
    _we_focus_cursor_invalidate(lcd->focus_obj); /* 光标换编辑色 */
}

void we_focus_edit_exit(we_lcd_t *lcd)
{
    if (lcd == NULL || (lcd->focus_flags & WE_FOCUS_F_EDIT) == 0U)
        return;
    lcd->focus_flags &= (uint8_t)~WE_FOCUS_F_EDIT;
    if (lcd->focus_obj != NULL)
        _we_focus_cursor_invalidate(lcd->focus_obj); /* 光标恢复导航色 */
}

uint8_t we_focus_edit_active(we_lcd_t *lcd)
{
    return (lcd != NULL && (lcd->focus_flags & WE_FOCUS_F_EDIT) != 0U) ? 1U : 0U;
}
#endif /* WE_CFG_FOCUS_EDIT */

/**
 * @brief 在钳制矩形内填充一条光标边带
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param x0 传入，边带左上角 X；y0 传入，边带左上角 Y
 * @param x1 传入，边带右下角 X；y1 传入，边带右下角 Y
 * @param cx0/cy0/cx1/cy1 传入，祖先链裁剪矩形（含端点）
 * @param c 传入，光标颜色
 * @return 无
 */
static void _we_focus_fill_clipped(we_lcd_t *lcd, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                   int16_t cx0, int16_t cy0, int16_t cx1, int16_t cy1, colour_t c)
{
    if (x0 < cx0)
        x0 = cx0;
    if (y0 < cy0)
        y0 = cy0;
    if (x1 > cx1)
        x1 = cx1;
    if (y1 > cy1)
        y1 = cy1;
    if (x0 > x1 || y0 > y1)
        return;
    we_fill_rect(lcd, x0, y0, (uint16_t)(x1 - x0 + 1), (uint16_t)(y1 - y0 + 1), c, 255U);
}

/**
 * @brief 绘制焦点矩形光标（渲染循环内：普通对象之后、顶层对象之前调用）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 光标沿焦点对象祖先链裁剪，容器（scroll_panel 等）视口外不越界；
 *       与当前 PFB 切片无交集时一次 AABB 判定即返回。
 */
static void _we_focus_draw_cursor(we_lcd_t *p_lcd)
{
    we_obj_t *obj = p_lcd->focus_obj;
    int16_t x0, y0, x1, y1;     /* 光标框外缘（含端点） */
    int16_t cx0, cy0, cx1, cy1; /* 祖先链裁剪矩形 */
    const we_obj_t *anc;
    colour_t c;

    /* 光标收起期间（触摸操作中）不绘制；焦点跟随本身不受影响 */
    if (obj == NULL || obj->class_p == NULL ||
        (p_lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) == 0U)
        return;

    x0 = (int16_t)(obj->x - _WE_FOCUS_EXPAND);
    y0 = (int16_t)(obj->y - _WE_FOCUS_EXPAND);
    x1 = (int16_t)(obj->x + obj->w - 1 + _WE_FOCUS_EXPAND);
    y1 = (int16_t)(obj->y + obj->h - 1 + _WE_FOCUS_EXPAND);

    /* 快速剔除：光标区与当前 PFB 切片无交集直接返回 */
    if (x1 < p_lcd->pfb_area.x0 || x0 > p_lcd->pfb_area.x1 ||
        y1 < (int16_t)p_lcd->pfb_y_start || y0 > (int16_t)p_lcd->pfb_y_end)
        return;

    cx0 = x0;
    cy0 = y0;
    cx1 = x1;
    cy1 = y1;
    for (anc = obj->parent; anc != NULL; anc = anc->parent)
    {
        if (cx0 < anc->x)
            cx0 = anc->x;
        if (cy0 < anc->y)
            cy0 = anc->y;
        if (cx1 > (int16_t)(anc->x + anc->w - 1))
            cx1 = (int16_t)(anc->x + anc->w - 1);
        if (cy1 > (int16_t)(anc->y + anc->h - 1))
            cy1 = (int16_t)(anc->y + anc->h - 1);
    }
    if (cx0 > cx1 || cy0 > cy1)
        return;

#if (WE_CFG_FOCUS_EDIT == 1)
    c = ((p_lcd->focus_flags & WE_FOCUS_F_EDIT) != 0U)
            ? RGB888TODEV(WE_CFG_FOCUS_EDIT_R, WE_CFG_FOCUS_EDIT_G, WE_CFG_FOCUS_EDIT_B)
            : RGB888TODEV(WE_CFG_FOCUS_CURSOR_R, WE_CFG_FOCUS_CURSOR_G, WE_CFG_FOCUS_CURSOR_B);
#else
    c = RGB888TODEV(WE_CFG_FOCUS_CURSOR_R, WE_CFG_FOCUS_CURSOR_G, WE_CFG_FOCUS_CURSOR_B);
#endif

    /* 四条边带：上、下贯通全宽，左、右扣除与上下带的重叠 */
    _we_focus_fill_clipped(p_lcd, x0, y0, x1,
                           (int16_t)(y0 + WE_CFG_FOCUS_CURSOR_THICKNESS - 1),
                           cx0, cy0, cx1, cy1, c);
    _we_focus_fill_clipped(p_lcd, x0, (int16_t)(y1 - WE_CFG_FOCUS_CURSOR_THICKNESS + 1), x1, y1,
                           cx0, cy0, cx1, cy1, c);
    _we_focus_fill_clipped(p_lcd, x0, (int16_t)(y0 + WE_CFG_FOCUS_CURSOR_THICKNESS),
                           (int16_t)(x0 + WE_CFG_FOCUS_CURSOR_THICKNESS - 1),
                           (int16_t)(y1 - WE_CFG_FOCUS_CURSOR_THICKNESS),
                           cx0, cy0, cx1, cy1, c);
    _we_focus_fill_clipped(p_lcd, (int16_t)(x1 - WE_CFG_FOCUS_CURSOR_THICKNESS + 1),
                           (int16_t)(y0 + WE_CFG_FOCUS_CURSOR_THICKNESS), x1,
                           (int16_t)(y1 - WE_CFG_FOCUS_CURSOR_THICKNESS),
                           cx0, cy0, cx1, cy1, c);
}
#endif /* WE_CFG_ENABLE_KEY_INPUT */

/**
 * @brief 立即执行一次整帧重绘。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @return 无。
 * @note 实现步骤：
 *       1. 先把当前 PFB 用背景色清空；
 *       2. 再遍历对象链表；
 *       3. 依次调用每个对象自己的 draw_cb 完成重绘。
 */
static void _we_engine_refresh(we_lcd_t *p_lcd)
{
    /* 1. 先清背景，保证本轮重绘从干净画布开始。 */
    we_fill_gram(p_lcd, p_lcd->bg_color);

    /* 2. 顺序遍历对象链表，让每个控件自己绘制自己。 */
    we_obj_t *curr = p_lcd->obj_list_head;
    while (curr != NULL)
    {
        if (curr->class_p != NULL && curr->class_p->draw_cb != NULL)
        {
            /* 3. 引擎层包围盒剔除（Culling）：
             * 只有控件的包围盒与当前 PFB 切片存在交集时，才调用 draw_cb。
             * 控件层自己也会做裁剪，但这里的剔除能消除函数调用本身的开销。
             *
             * 交集条件（AABB 相交判定）：
             * - X 方向：控件右边 > 切片左边 且 控件左边 <= 切片右边
             * - Y 方向：控件下边 > 切片上边 且 控件上边 <= 切片下边
             */
            if ((curr->x + curr->w > p_lcd->pfb_area.x0) && (curr->x <= p_lcd->pfb_area.x1) &&
                (curr->y + curr->h > p_lcd->pfb_y_start) && (curr->y <= p_lcd->pfb_y_end))
            {
                curr->class_p->draw_cb(curr);
            }
        }
        curr = curr->next;
    }

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 2.5 焦点矩形光标：普通对象之后补画，悬浮在普通层之上，
     *     但不压住顶层（toast/msgbox）与弹窗。 */
    _we_focus_draw_cursor(p_lcd);
#endif

#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    /* 2.7 顶层对象（toast/msgbox 等"保证置顶"层）：普通层与光标之后绘制。 */
    {
        we_obj_t *top = p_lcd->top_list_head;

        while (top != NULL)
        {
            if (top->class_p != NULL && top->class_p->draw_cb != NULL &&
                (top->x + top->w > p_lcd->pfb_area.x0) && (top->x <= p_lcd->pfb_area.x1) &&
                (top->y + top->h > p_lcd->pfb_y_start) && (top->y <= p_lcd->pfb_y_end))
            {
                top->class_p->draw_cb(top);
            }
            top = top->next;
        }
    }
#endif /* WE_CFG_ENABLE_TOP_LAYER */

}

/**
 * @brief 将指定矩形区域按 PFB 分块推送到底层显示端口。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @param x 传入，目标区域左上角 X 坐标，允许为负。
 * @param y 传入，目标区域左上角 Y 坐标，允许为负。
 * @param w 传入，目标区域原始宽度。
 * @param h 传入，目标区域原始高度。
 * @return 无。
 * @note 实现步骤：
 *       1. 先做整块区域的屏幕边界裁剪；
 *       2. 统计本次真正下发的块数和像素数；
 *       3. 根据 pfb_size（当前 PFB 像素容量，真双缓冲时为整块的一半）计算单次最多能刷多少行；
 *       4. 分块重绘 PFB；
 *       5. 调用底层端口把当前块送到 LCD。
 */
void we_push_pfb(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    /* 1. 先做最基本的尺寸检查。 */
    if (w == 0 || h == 0)
        return;

    /* 2. 如果整块区域已经完全在屏幕外，直接跳过。 */
    if (x >= p_lcd->width || y >= p_lcd->height)
        return;
    if (x + (int16_t)w <= 0 || y + (int16_t)h <= 0)
        return;

    /* 3. 裁掉左边和上边越界的部分。 */
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }

    /* 4. 裁掉右边和下边越界的部分。 */
    if (x + w > p_lcd->width)
        w = p_lcd->width - x;
    if (y + h > p_lcd->height)
        h = p_lcd->height - y;

    /* 5. 核心端口回调缺失时直接跳过，避免异常初始化下的空指针调用。 */
    if (p_lcd->set_addr_cb == NULL || p_lcd->flush_cb == NULL)
        return;

    /* 6. 预计算本次刷新的最终区域信息。 */
    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;

    /* 7. 根据当前宽度和 pfb_size（当前 PFB 像素容量，真双缓冲时为整块的一半），算出单次最多能刷多少行。 */
    uint16_t max_lines = p_lcd->pfb_size / w;

    if (max_lines == 0)
        return;

    p_lcd->pfb_area.x0 = x;
    p_lcd->pfb_area.y0 = y;
    p_lcd->pfb_area.x1 = x1;
    p_lcd->pfb_area.y1 = y1;
    p_lcd->pfb_width = w;

    /* 8. 先告诉底层 LCD 本轮整块刷新的目标窗口。 */
    p_lcd->set_addr_cb((uint16_t)x, (uint16_t)y, x1, y1);

    uint16_t current_y = y;
    uint16_t lines_left = h;

    /* 9. 按块循环：
     *    每次只生成当前块的 PFB 内容，再立即推到底层端口。 */
    while (lines_left > 0)
    {
        uint16_t lines_this_chunk = (lines_left > max_lines) ? max_lines : lines_left;
        uint32_t pixels_to_push = (uint32_t)lines_this_chunk * w;

        p_lcd->pfb_y_start = current_y;
        p_lcd->pfb_y_end = current_y + lines_this_chunk - 1;

        /* 10. 先重绘当前块对应的 PFB 内容。 */
        _we_engine_refresh(p_lcd);

#if (WE_CFG_DEBUG_DIRTY_RECT == 1)
        colour_t debug_color;
#if (LCD_DEEP == DEEP_RGB565)
        debug_color.dat16 = 0xF800;
#elif (LCD_DEEP == DEEP_RGB888)
        debug_color.rgb.r = 255;
        debug_color.rgb.g = 0;
        debug_color.rgb.b = 0;
#endif

        colour_t *gram = (colour_t *)p_lcd->pfb_gram;

        for (uint16_t row = 0; row < lines_this_chunk; row++)
        {
            gram[row * w + 0] = debug_color;
            gram[row * w + (w - 1)] = debug_color;
        }

        if (current_y == y)
        {
            for (uint16_t col = 0; col < w; col++)
                gram[col] = debug_color;
        }

        if (current_y + lines_this_chunk - 1 == y + h - 1)
        {
            uint16_t last_row_idx = lines_this_chunk - 1;
            for (uint16_t col = 0; col < w; col++)
                gram[last_row_idx * w + col] = debug_color;
        }
#endif

#if (LCD_DEEP == DEEP_RGB565)
        p_lcd->flush_cb((uint16_t *)p_lcd->pfb_gram, pixels_to_push);
        _we_lcd_swap_pfb(p_lcd);
#elif (LCD_DEEP == DEEP_RGB888)
        p_lcd->flush_cb((uint8_t *)p_lcd->pfb_gram, pixels_to_push * 3U);
        _we_lcd_swap_pfb(p_lcd); /* 真双缓冲下与 RGB565 路径同样乒乓，否则为空操作 */
#endif
        current_y += lines_this_chunk;
        lines_left -= lines_this_chunk;
    }
}

/**
 * @brief 累计 GUI 主循环流逝时间
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param ms 传入，本次新增的流逝时间，单位毫秒
 * @return 无
 * @note 内部会对累计值做上限保护，避免卡顿后一次性补偿过大。
 */
void we_gui_tick_inc(we_lcd_t *p_lcd, uint16_t ms)
{
    uint32_t sum;

    if (p_lcd == NULL || ms == 0U)
        return;

    /* 单次累计时间需要封顶，避免主循环卡顿后一次性补偿过大，
     * 导致容器吸附或其他时间驱动动画突然跳变。 */
    sum = (uint32_t)p_lcd->tick_elapsed_ms + ms;
    if (sum > 200U)
        sum = 200U;

    p_lcd->tick_elapsed_ms = (uint16_t)sum;
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

/**
 * @brief 执行一次 GUI 主任务处理
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 实现步骤：
 *       1. 先消费 tick 累计时间。
 *       2. 再推进用户定时器和中央动画链表。
 *       3. 遍历当前脏矩形并逐块推送到底层显示端口。
 *       4. 本轮真正发生刷新后，统计一帧渲染并清空脏区。
 */
void we_gui_task_handler(we_lcd_t *p_lcd)
{
    uint16_t elapsed_ms = 0U;

    if (p_lcd == NULL)
        return;

    /* 1. 读取并分发输入事件。 */
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
    if (p_lcd->input_read_cb != NULL)
    {
        p_lcd->input_read_cb(&p_lcd->indev_data);
        if (p_lcd->indev_data.state != WE_TOUCH_STATE_NONE)
            we_gui_indev_handler(p_lcd, &p_lcd->indev_data);
    }
#else
    {
        extern void we_input_port_read(we_indev_data_t * data);
        we_input_port_read(&p_lcd->indev_data);
        if (p_lcd->indev_data.state != WE_TOUCH_STATE_NONE)
            we_gui_indev_handler(p_lcd, &p_lcd->indev_data);
    }
#endif

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 1.5 语义键处理：先对账补投，再抽干环形队列。
     * 对账补投：OK 松开沿曾因队满被丢弃时，按标准松开路径补投——否则
     * HELD 永远清不掉，控件卡按压态、后续 OK 按下沿全被连发去重吞掉。
     * 放在抽干之前：旧按压先正常收尾，队列里的新按下沿再从干净状态起步。 */
    if (p_lcd->key_ok_drop_ack != p_lcd->key_ok_drop_seq)
    {
        p_lcd->key_ok_drop_ack = p_lcd->key_ok_drop_seq;
        if ((p_lcd->focus_flags & WE_FOCUS_F_OK_HELD) != 0U)
        {
            if (p_lcd->key_flash_left_ms > 0U)
                p_lcd->focus_flags |= WE_FOCUS_F_REL_PEND; /* 仍在最短按压窗口内：挂起补发 */
            else
                _we_key_ok_deliver_release(p_lcd);
        }
    }

    /* 消费队列（SPSC：本侧只写 head，注入侧只写 tail）。 */
    if (p_lcd->key_q_head != p_lcd->key_q_tail)
        _we_focus_cursor_show(p_lcd); /* 任何按键活动都把光标亮出来（触摸按下时收起） */
    while (p_lcd->key_q_head != p_lcd->key_q_tail)
    {
        uint8_t key = p_lcd->key_queue[p_lcd->key_q_head];
        p_lcd->key_q_head = (uint8_t)((p_lcd->key_q_head + 1U) & (WE_CFG_KEY_QUEUE_LEN - 1U));
        _we_focus_dispatch(p_lcd, key);
    }
#endif

    /* 2. 消费累计时间，推进用户定时器。 */
    elapsed_ms = p_lcd->tick_elapsed_ms;
    p_lcd->tick_elapsed_ms = 0U;
    _we_gui_run_timers(p_lcd, elapsed_ms);

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 2.4 OK 最短按压窗口倒计时（直接消费 elapsed_ms，不占动画节点）：
     *     到期时若松开沿已挂起则补发回弹+点击；仍按住则仅停表，
     *     等真实松开沿到达时即时交付。 */
    if (p_lcd->key_flash_left_ms != 0U && elapsed_ms != 0U)
    {
        if (elapsed_ms >= (uint16_t)p_lcd->key_flash_left_ms)
        {
            p_lcd->key_flash_left_ms = 0U;
            if ((p_lcd->focus_flags & WE_FOCUS_F_REL_PEND) != 0U)
                _we_key_ok_deliver_release(p_lcd);
        }
        else
        {
            p_lcd->key_flash_left_ms = (uint8_t)(p_lcd->key_flash_left_ms - elapsed_ms);
        }
    }
#endif

    /* 2.5 推进中央动画链表（控件动画）。
     *     先存 next 再调 step_cb，允许回调内摘除自身节点。 */
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

#if (WE_CFG_DEBUG_PERF_STRESS == 1)
    /* 性能压测：在刷新前强制标脏所有控件，制造每帧全量重绘负载。 */
    _we_gui_perf_stress_invalidate(p_lcd);
#endif

    /* 3. 消费脏矩形并推屏。 */
    _we_gui_flush_dirty(p_lcd);
}

/**
 * @brief 通用对象移动接口，适用于所有继承 we_obj_t 的控件
 * @param obj 传入，目标对象指针
 * @param new_x 传入，新的绝对 X 坐标
 * @param new_y 传入，新的绝对 Y 坐标
 * @return 无
 * @note 实现步骤：
 *       1. 先把旧区域标脏，保证旧位置会被擦除。
 *       2. 再更新对象坐标。
 *       3. 最后把新区域标脏，保证新位置会被重新绘制。
 */
void we_obj_set_pos(we_obj_t *obj, int16_t new_x, int16_t new_y)
{
    if (obj == NULL || obj->lcd == NULL)
        return;
    if (obj->x == new_x && obj->y == new_y)
        return;

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 机制而非约定：移动焦点对象时自动标脏光标环（环悬在包围盒外侧
     * GAP+THICKNESS 像素，控件自身的旧/新 footprint 标脏盖不住它）。
     * 移动前标旧环、完成后标新环，任何移动路径（含容器 relayout 逐子
     * set_pos）都免疫残影，滚动跟随等调用方无需再手工补标。
     * 光标收起期间无环可擦，跳过。 */
    if (obj == obj->lcd->focus_obj && (obj->lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
        _we_focus_cursor_invalidate(obj);
#endif

    if (obj->class_p != NULL && obj->class_p->set_pos_cb != NULL)
    {
        obj->class_p->set_pos_cb(obj, new_x, new_y);
    }
    else
    {
        // 1. 先把旧位置标脏，保证移动后旧区域会被重绘擦除。
        we_obj_invalidate(obj);

        // 2. 更新对象坐标；移动不改变所属链表和绘制顺序。
        obj->x = new_x;
        obj->y = new_y;

        // 3. 再把新位置标脏，保证新区域会被绘制出来。
        we_obj_invalidate(obj);
    }

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    if (obj == obj->lcd->focus_obj && (obj->lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
        _we_focus_cursor_invalidate(obj);
#endif
}

/**
 * @brief 将对象追加到指定对象链表尾部
 * @param head_p 传入，链表头指针地址
 * @param obj 传入，待追加对象
 * @return 无
 */
void we_obj_append_to_list(we_obj_t **head_p, we_obj_t *obj)
{
    WE_ASSERT(head_p != NULL && obj != NULL);
    we_obj_t *tail;

    if (head_p == NULL || obj == NULL)
        return;

    obj->next = NULL;
    if (*head_p == NULL)
    {
        *head_p = obj;
        return;
    }

    tail = *head_p;
    while (tail->next != NULL)
        tail = tail->next;
    tail->next = obj;
}

/**
 * @brief 将对象追加到 LCD 顶层对象链表尾部
 * @param lcd 传入，GUI 屏幕上下文指针
 * @param obj 传入，待追加对象
 * @return 无
 */
void we_obj_attach_to_lcd(we_lcd_t *lcd, we_obj_t *obj)
{
    WE_ASSERT(lcd != NULL && obj != NULL);
    if (lcd == NULL)
        return;
    we_obj_append_to_list(&lcd->obj_list_head, obj);
}

/**
 * @brief 从指定链表头中摘除目标对象（公用 helper）
 * @param head_p 传入：链表头指针的地址（顶层链表为 &lcd->obj_list_head，
 *                                       子容器为 &parent->children_head）
 * @param obj 传入：待摘除对象指针
 * @return 无
 * @note 仅做摘除，不清状态。we_obj_delete 与 we_obj_bring_to_front 共用。
 */
static void _we_obj_unlink_from(we_obj_t **head_p, we_obj_t *obj)
{
    we_obj_t *curr = *head_p;
    we_obj_t *prev = NULL;

    while (curr != NULL)
    {
        if (curr == obj)
        {
            if (prev == NULL)
                *head_p = curr->next;
            else
                prev->next = curr->next;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * @brief 返回对象所在链表的头指针地址
 * @param obj 传入：目标对象
 * @return 链表头指针的地址（顶层或子容器）
 * @note 上层调用方保证 obj 与 obj->lcd 非空。
 */
static we_obj_t **_we_obj_owner_head(we_obj_t *obj)
{
    /* 取 children_head 依赖“父对象前缀是 we_child_owner_t”，只有带结构位
     * WE_CLASS_FLAG_CHILD_OWNER 的类才成立。父指针指向非容器时按顶层处理，
     * 避免把任意对象强转成 we_child_owner_t 写坏其后续成员。 */
    if (obj->parent != NULL && obj->parent->class_p != NULL &&
        (obj->parent->class_p->class_flags & WE_CLASS_FLAG_CHILD_OWNER) != 0U)
    {
        we_child_owner_t *parent = (we_child_owner_t *)obj->parent;
        return &parent->children_head;
    }
    return &obj->lcd->obj_list_head;
}

/**
 * @brief 把对象从其所属的任意一条根链上摘除（根链清单的唯一维护点）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param obj 传入：目标对象
 * @return 无
 * @note 对象可能挂在普通层/父容器子链（经 owner_head 定位）或顶层链上；
 *       逐条尝试摘除，空摘无害。将来新增根链（如调试覆盖层）在此补摘，
 *       detach / delete 两个调用方自动同步，不再各自维护清单。
 */
static void _we_obj_unlink_any_root(we_lcd_t *lcd, we_obj_t *obj)
{
    _we_obj_unlink_from(_we_obj_owner_head(obj), obj);
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    _we_obj_unlink_from(&lcd->top_list_head, obj);
#else
    (void)lcd; /* 裁剪档：只有普通层/父容器一条根链 */
#endif
}

void we_obj_detach(we_obj_t *obj)
{
    WE_ASSERT(obj != NULL);
    if (obj == NULL || obj->lcd == NULL)
        return;

    _we_obj_unlink_any_root(obj->lcd, obj);
    obj->next = NULL;
    obj->parent = NULL;
}

void we_obj_set_parent(we_obj_t *obj, we_obj_t *parent)
{
    WE_ASSERT(obj != NULL && obj != parent);
    if (obj == NULL || obj->lcd == NULL || obj == parent)
        return;

    we_obj_detach(obj);

    if (parent != NULL && parent->class_p != NULL &&
        (parent->class_p->class_flags & WE_CLASS_FLAG_CHILD_OWNER) != 0U)
    {
        we_child_owner_t *owner = (we_child_owner_t *)parent;

        obj->parent = parent;
        we_obj_append_to_list(&owner->children_head, obj);
    }
    else
    {
        we_obj_append_to_list(&obj->lcd->obj_list_head, obj);
    }
}

void we_obj_attach_to_top(we_lcd_t *lcd, we_obj_t *obj)
{
    WE_ASSERT(lcd != NULL && obj != NULL);
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    if (lcd == NULL || obj == NULL)
        return;

    /* 先从所属根链摘出（重复调用即移至顶层链尾；保留 lcd/class_p，
     * 对象仍可用），再挂到顶层链尾。 */
    _we_obj_unlink_any_root(lcd, obj);
    obj->parent = NULL;
    obj->next = NULL;
    we_obj_append_to_list(&lcd->top_list_head, obj);
    we_obj_invalidate(obj);
#else
    /* 裁剪档：无顶层链，退化为普通层内置顶——仍可显示，但可能被
     * 后来的 bring_to_front 盖住，失去"永远压住普通层"的保证。 */
    (void)lcd;
    if (obj == NULL)
        return;
    we_obj_bring_to_front(obj);
    we_obj_invalidate(obj);
#endif
}

#if (WE_CFG_ENABLE_TOP_LAYER == 1)
void we_modal_open(we_lcd_t *lcd, we_obj_t *obj)
{
    WE_ASSERT(lcd != NULL && obj != NULL);
    if (lcd == NULL || obj == NULL || lcd->modal_obj == obj)
        return;

    /* 模态互斥：先通知旧模态自行收起（收起过程通常回调 we_modal_close，
     * 此处随后无条件改指即可）。 */
    if (lcd->modal_obj != NULL && lcd->modal_obj->class_p != NULL &&
        lcd->modal_obj->class_p->event_cb != NULL)
        (void)lcd->modal_obj->class_p->event_cb(lcd->modal_obj, WE_EVENT_MODAL_CLOSE,
                                                &lcd->indev_data);
    lcd->modal_obj = obj;
}

void we_modal_close(we_lcd_t *lcd, we_obj_t *obj)
{
    WE_ASSERT(lcd != NULL);
    if (lcd == NULL || lcd->modal_obj != obj)
        return;
    lcd->modal_obj = NULL;
}

we_obj_t *we_modal_get(we_lcd_t *lcd)
{
    return (lcd != NULL) ? lcd->modal_obj : NULL;
}
#else
/* 裁剪档空 stub：无模态语义，弹层退化为普通对象（吞键/全屏命中失效），
 * 调用方无需改代码即可链接。 */
void we_modal_open(we_lcd_t *lcd, we_obj_t *obj)
{
    (void)lcd;
    (void)obj;
}

void we_modal_close(we_lcd_t *lcd, we_obj_t *obj)
{
    (void)lcd;
    (void)obj;
}

we_obj_t *we_modal_get(we_lcd_t *lcd)
{
    (void)lcd;
    return NULL;
}
#endif /* WE_CFG_ENABLE_TOP_LAYER */

/**
 * @brief 通用对象删除接口，把任意控件从渲染树中摘除
 * @param obj 传入，待删除对象指针
 * @return 无
 * @note 同时支持顶层链表和带 children_head 的复合容器（如 slideshow）。
 * @note 本函数是**唯一的删除入口**：控件的 we_xxx_obj_delete 只是薄封装。
 *       内核在这里统一回收对该对象的全部引用（按压 / 焦点 / 弹层 / 动画
 *       节点），所以“容器用基类接口删子控件”也不会留下悬空引用——这条
 *       兜底是机制而非约定，控件漏写自清理最多损失一次自定义收尾。
 */
void we_obj_delete(we_obj_t *obj)
{
    WE_ASSERT(obj != NULL);
    we_lcd_t *lcd;

    if (obj == NULL || obj->lcd == NULL)
        return;

    /* lcd 在末步会被清空，先取本地副本供后续回收使用。 */
    lcd = obj->lcd;

    // 1. 控件自清理：此刻 lcd / class_p 仍有效，控件可标脏、可访问自身字段。
    //    （禁止在 delete_cb 内再次调用 we_obj_delete(自身)，会无限递归。）
    if (obj->class_p != NULL && obj->class_p->delete_cb != NULL)
        obj->class_p->delete_cb(obj);

    // 2. 复合容器：后序递归删除子控件，让每个子控件都走完整的删除流程
    //    （自身 delete_cb + 内核四类引用回收），而不是仅仅被遗弃在树外。
    //    先存 next 再删，子控件在自己的 we_obj_delete 里会摘出本链表。
    if (obj->class_p != NULL &&
        (obj->class_p->class_flags & WE_CLASS_FLAG_CHILD_OWNER) != 0U)
    {
        we_child_owner_t *owner = (we_child_owner_t *)obj;
        we_obj_t *child = owner->children_head;

        while (child != NULL)
        {
            we_obj_t *child_next = child->next;

            we_obj_delete(child);
            child = child_next;
        }
        owner->children_head = NULL;
    }

    // 3. 先把当前区域标脏，用于删除后的残影擦除。
    we_obj_invalidate(obj);

    // 4. 从所在的任意根链上摘掉当前对象（根链清单见 _we_obj_unlink_any_root）。
    _we_obj_unlink_any_root(lcd, obj);

    // 5.1 若该对象正处于按压状态，同步清空输入派发缓存，
    //     防止 RELEASED/STAY/SWIPE 阶段经悬空指针回调。
    if (lcd->pressed_obj == obj)
        lcd->pressed_obj = NULL;

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    // 5.2 焦点引用防悬空：被删对象是焦点本身或焦点的祖先容器时一并清理
    //     （删除路径不发 DEFOCUS 通知）。OK 按压目标恒为焦点，故编辑态与
    //     待回发/挂起标志一起清零即可；HELD 保留，待物理松开沿自然清零。
    if (lcd->focus_obj != NULL)
    {
        const we_obj_t *it = lcd->focus_obj;
        while (it != NULL && it != obj)
            it = it->parent;
        if (it == obj)
        {
            if ((lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
                _we_focus_cursor_invalidate(lcd->focus_obj);
            lcd->focus_obj = NULL;
            lcd->focus_flags &= (uint8_t)~(WE_FOCUS_F_EDIT | WE_FOCUS_F_OK_ARMED |
                                           WE_FOCUS_F_REL_PEND);
            lcd->key_flash_left_ms = 0U;
        }
    }
#endif

#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    // 5.3 模态引用防悬空：被删对象正是当前模态时直接清指针
    //     （不发 WE_EVENT_MODAL_CLOSE——对象即将消亡，无收尾可做）。
    if (lcd->modal_obj == obj)
        lcd->modal_obj = NULL;
#endif

    // 5.4 动画节点防悬空：摘掉全部 owner 指向本对象的节点。
    //     全库 we_anim_start 一律以控件实例指针作 owner，而控件首成员是
    //     we_obj_t base（复合控件为 we_group_obj_t group），地址与 obj 相同，
    //     故此处逐一比对即可命中；line/box 这类多节点控件一次全部摘净。
    {
        we_anim_t **pp = &lcd->anim_head;

        while (*pp != NULL)
        {
            if ((*pp)->owner == (void *)obj)
            {
                we_anim_t *dead = *pp;

                *pp = dead->next;
                dead->next = NULL; /* 与 we_anim_stop 同口径 */
            }
            else
            {
                pp = &(*pp)->next;
            }
        }
    }

    // 6. 清空对象状态，避免后续误用。
    obj->next = NULL;
    obj->parent = NULL;
    obj->class_p = NULL;
    obj->lcd = NULL;
}

/* --------------------------------------------------------------------------
 * 中央动画引擎
 *
 * 控件动画不占用任何槽位：节点内嵌在控件结构体里，挂到
 * lcd->anim_head 侵入式链表上，由 we_gui_task_handler 每周期统一推进。
 * 空链时开销仅一次判空；start 不会失败（彻底消除"槽满→动画静默消失"）。
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


void we_obj_bring_to_front(we_obj_t *obj)
{
    WE_ASSERT(obj != NULL);
    we_obj_t **head_p;
    we_obj_t *tail;

    if (obj == NULL || obj->lcd == NULL || obj->next == NULL)
        return;

    head_p = _we_obj_owner_head(obj);

    // 1. 先把自己从原链表中摘下来。
    _we_obj_unlink_from(head_p, obj);

    // 2. 再追加到链表尾部，使其位于绘制顺序最上层。
    tail = *head_p;
    if (tail == NULL)
    {
        *head_p = obj;
    }
    else
    {
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = obj;
    }
    obj->next = NULL;
}

/**
 * @brief 基于父节点层层裁剪后的局部区域标脏函数
 * @param obj 传入，目标对象指针
 * @param x 传入，自定义脏区左上角 X 坐标
 * @param y 传入，自定义脏区左上角 Y 坐标
 * @param w 传入，自定义脏区宽度
 * @param h 传入，自定义脏区高度
 * @return 无
 * @note 这里的坐标使用屏幕绝对坐标。
 *       内部会沿 parent 链逐层求交，最终只把容器可视区内的部分提交给脏矩形管理器。
 */
void we_obj_invalidate_area(we_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (obj == NULL || obj->lcd == NULL)
        return;

    int16_t x0 = x;
    int16_t y0 = y;
    int16_t x1 = x + w - 1;
    int16_t y1 = y + h - 1;

    // 向上遍历所有父容器，逐层求交，保证标脏不会溢出容器可视区。
    we_obj_t *p = obj->parent;
    while (p)
    {
        x0 = WE_MAX(x0, p->x);
        y0 = WE_MAX(y0, p->y);
        x1 = WE_MIN(x1, p->x + p->w - 1);
        y1 = WE_MIN(y1, p->y + p->h - 1);
        p = p->parent;
    }

    // 只有最终仍有有效交集时，才真正提交给脏矩形管理器。
    if (x0 <= x1 && y0 <= y1)
    {
        we_dirty_invalidate(&obj->lcd->dirty_mgr, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
}

/**
 * @brief 基于父节点裁剪后的局部区域标脏函数，并排除一个安全空洞矩形
 * @param obj 传入，目标对象指针
 * @param x 传入，整体脏区左上角 X 坐标（屏幕绝对坐标）
 * @param y 传入，整体脏区左上角 Y 坐标（屏幕绝对坐标）
 * @param w 传入，整体脏区宽度
 * @param h 传入，整体脏区高度
 * @param ex 传入，排除区域左上角 X 坐标（屏幕绝对坐标）
 * @param ey 传入，排除区域左上角 Y 坐标（屏幕绝对坐标）
 * @param ew 传入，排除区域宽度
 * @param eh 传入，排除区域高度
 * @return 无
 * @note 该接口只在新增 dirty 时拆分 bbox - hole；不会从已有 dirty list 中删除区域。
 */
void we_obj_invalidate_area_exclude(we_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h,
                                    int16_t ex, int16_t ey, int16_t ew, int16_t eh)
{
    int16_t x0;
    int16_t y0;
    int16_t x1;
    int16_t y1;
    we_obj_t *p;

    if (obj == NULL || obj->lcd == NULL || w <= 0 || h <= 0)
        return;

    x0 = x;
    y0 = y;
    x1 = (int16_t)(x + w - 1);
    y1 = (int16_t)(y + h - 1);

    p = obj->parent;
    while (p)
    {
        x0 = WE_MAX(x0, p->x);
        y0 = WE_MAX(y0, p->y);
        x1 = WE_MIN(x1, p->x + p->w - 1);
        y1 = WE_MIN(y1, p->y + p->h - 1);
        p = p->parent;
    }

    if (x0 <= x1 && y0 <= y1)
    {
        we_dirty_invalidate_exclude(&obj->lcd->dirty_mgr, x0, y0, x1 - x0 + 1, y1 - y0 + 1,
                                    ex, ey, ew, eh);
    }
}

/**
 * @brief 基于父节点层层裁剪的智能标脏函数，默认标脏整个控件
 * @param obj 传入，目标对象指针
 * @return 无
 * @note 这样可以避免子控件标脏区域越过父容器边界，造成无意义重绘。
 */
void we_obj_invalidate(we_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_invalidate_area(obj, obj->x, obj->y, obj->w, obj->h);
}

/**
 * @brief 使用指定的 PFB 缓冲区和底层端口接口初始化一套 GUI 屏幕上下文
 * @param p_lcd 传入，待初始化的 GUI 屏幕上下文指针
 * @param bg 传入，默认背景色
 * @param gram_base 传入，用户指定的 PFB 缓冲区基址
 * @param gram_size 传入，用户提供的 PFB 总像素容量
 * @param set_addr_cb 传入，底层设窗回调
 * @param flush_cb 传入，底层刷屏回调
 * @return 无
 * @note 实现步骤：
 *       1. 选择本次初始化要绑定的 PFB 缓冲区和底层端口接口；
 *       2. 初始化背景色、对象链表、时间累计、定时器表和统计字段；
 *       3. 初始化脏矩形管理器；
 *       4. 把整屏标记为脏区，确保首帧一定完整刷新。
 */
void we_lcd_init_with_port(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
                           we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb)
{
    WE_ASSERT(p_lcd != NULL && gram_base != NULL && gram_size != 0U && set_addr_cb != NULL && flush_cb != NULL);
    if (p_lcd == NULL)
        while (1)
            ;
    if (gram_base == NULL)
        while (1)
            ;
    if (set_addr_cb == NULL)
        while (1)
            ;
    if (flush_cb == NULL)
        while (1)
            ;

    p_lcd->width = SCREEN_WIDTH;
    p_lcd->height = SCREEN_HEIGHT;
    p_lcd->bg_color = bg;

    p_lcd->obj_list_head = NULL; // 对象链表初始为空
    p_lcd->tick_elapsed_ms = 0U; // GUI 时间累计从 0 开始
    p_lcd->timer_head = NULL; /* 用户定时器链从空开始（节点归调用方所有） */
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    p_lcd->top_list_head = NULL; /* 顶层对象链从空开始（toast/msgbox 等置顶层） */
    p_lcd->modal_obj = NULL;     /* 无模态 */
#endif
    p_lcd->anim_head = NULL; // 中央动画链表初始为空
    p_lcd->opa_scale = 255U; // 容器透明度级联乘子，默认无衰减
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
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    p_lcd->focus_obj = NULL; // 焦点从无开始，首个导航键或 we_focus_set 唤出
    p_lcd->focus_flags = 0U;
    p_lcd->key_flash_left_ms = 0U;
    p_lcd->key_q_head = 0U;
    p_lcd->key_q_tail = 0U;
    p_lcd->key_ok_drop_seq = 0U;
    p_lcd->key_ok_drop_ack = 0U;
#endif
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
    p_lcd->input_read_cb = NULL;
#endif
#if (WE_CFG_ENABLE_STORAGE_PORT_BIND == 1)
    p_lcd->storage_read_cb = NULL;
#endif
    /*
    p_lcd->pfb_gram_base = gram; // 记录 PFB 基址，后续双缓冲切换会用到
p_lcd->pfb_gram = gram;
    */
    p_lcd->pfb_gram_base = gram_base; // 记录 PFB 基址，后续双缓冲切换会用到
    p_lcd->pfb_gram = gram_base;
    /* PFB 长度约定：
     * 1. 非真双缓冲（含阻塞端口）时，pfb_size = 用户注册的总长度；
     * 2. 真双缓冲（GRAM_DMA_BUFF_EN=1 且端口异步）时，用户仍注册整块
     *    USER_GRAM_NUM，内核自动按一半作为"当前绘制 PFB"长度使用，
     *    另一半留给底层 DMA 异步发送。 */
#if WE_PFB_DOUBLE_BUF
    p_lcd->pfb_size = gram_size / 2U;
#else
    p_lcd->pfb_size = gram_size;
#endif
    p_lcd->set_addr_cb = set_addr_cb;
    p_lcd->flush_cb = flush_cb;

    we_dirty_init(&p_lcd->dirty_mgr);
    p_lcd->dirty_mgr.screen_w = p_lcd->width;
    p_lcd->dirty_mgr.screen_h = p_lcd->height;
    we_dirty_invalidate(&p_lcd->dirty_mgr, 0, 0, p_lcd->width, p_lcd->height);
}

/**
 * @brief 递归命中测试：返回该点上最深、最靠上的可交互对象
 * @param head 传入：本层链表头
 * @param data 传入：输入状态（HIT_TEST 查询要透传给容器）
 * @param cx0/cy0/cx1/cy1 传入：本层可视裁剪矩形（屏幕绝对坐标，闭区间）
 * @return 命中对象；无命中返回 NULL
 * @note 1. 逐层与父容器可视区求交，滚出视口的子控件不会被命中；
 *       2. 对带 WE_CLASS_FLAG_CHILD_OWNER 的复合容器无条件下钻；
 *       3. 下钻前先发 WE_EVENT_HIT_TEST 给容器，返回 0 则整棵子树跳过
 *          （如完全透明的 group、点在 scroll_panel 内容区之外）；
 *       4. 同层后加入者绘制在上，故用"持续覆盖"实现 Z 序优先。
 *       5. WE_CFG_ENABLE_NESTED_INPUT=0 时第 2 条的下钻整体剔除（容器
 *          整体作为一个控件命中，子控件收不到触摸）；第 3 条的 HIT_TEST
 *          查询仍尊重，完全透明的容器让位给其下方的同层控件。
 */
static we_obj_t *_we_hit_test(we_obj_t *head, we_indev_data_t *data,
                              int16_t cx0, int16_t cy0, int16_t cx1, int16_t cy1)
{
    we_obj_t *hit = NULL;
    we_obj_t *curr = head;

    while (curr != NULL)
    {
        if (curr->class_p != NULL && curr->class_p->event_cb != NULL)
        {
            int16_t x0 = WE_MAX(cx0, curr->x);
            int16_t y0 = WE_MAX(cy0, curr->y);
            int16_t x1 = WE_MIN(cx1, (int16_t)(curr->x + curr->w - 1));
            int16_t y1 = WE_MIN(cy1, (int16_t)(curr->y + curr->h - 1));

            if (data->x >= x0 && data->x <= x1 && data->y >= y0 && data->y <= y1)
            {
#if (WE_CFG_ENABLE_NESTED_INPUT == 1)
                uint8_t descend = (uint8_t)((curr->class_p->class_flags &
                                             WE_CLASS_FLAG_CHILD_OWNER) != 0U);

                if (!descend || curr->class_p->event_cb(curr, WE_EVENT_HIT_TEST, data) != 0U)
                {
                    hit = curr;
                    if (descend)
                    {
                        we_obj_t *deeper = _we_hit_test(((we_child_owner_t *)curr)->children_head,
                                                        data, x0, y0, x1, y1);
                        if (deeper != NULL)
                            hit = deeper;
                    }
                }
#else
                /* 平铺输入裁剪档：不下钻子树；HIT_TEST 查询仍尊重。 */
                if ((curr->class_p->class_flags & WE_CLASS_FLAG_CHILD_OWNER) == 0U ||
                    curr->class_p->event_cb(curr, WE_EVENT_HIT_TEST, data) != 0U)
                    hit = curr;
#endif
            }
        }
        curr = curr->next;
    }

    return hit;
}

void we_indev_grab(we_lcd_t *lcd, we_obj_t *obj)
{
    WE_ASSERT(lcd != NULL && obj != NULL);
    we_obj_t *old;

    if (lcd == NULL || obj == NULL || lcd->pressed_obj == obj)
        return;

    old = lcd->pressed_obj;
    lcd->pressed_obj = obj;                /* 先更换按压对象，防止 RELEASED 回调里再次触发接管 */
    lcd->gesture_drag_done = 1U;

    /* 被接管的子控件收一次 RELEASED 回弹按压视觉。不补发 CLICKED——
     * 内核只在 pressed_obj == 松手命中对象时才派发点击，更换后自然不成立。 */
    if (old != NULL && old->class_p != NULL && old->class_p->event_cb != NULL)
        (void)old->class_p->event_cb(old, WE_EVENT_RELEASED, &lcd->indev_data);
}

/**
 * @brief 输入设备事件分发函数
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param data 传入，底层驱动采集到的触摸数据
 * @return 无
 * @note 释放阶段会按位移和阈值判定点击或方向滑动事件。
 */
void we_gui_indev_handler(we_lcd_t *lcd, we_indev_data_t *data)
{
    if (lcd == NULL || data == NULL)
        return;

    /* 按压/拖拽状态保存在 we_lcd_t 内（按实例隔离，多屏不串台）。
     * 防御：若按压对象已被外部置为失效（class_p 为空），立即丢弃引用，
     * 避免 RELEASED/STAY/SWIPE 分支经空 class_p 调用直接 HardFault。 */
    if (lcd->pressed_obj != NULL && lcd->pressed_obj->class_p == NULL)
        lcd->pressed_obj = NULL;

    uint8_t hit_from_top = 0U;
    we_obj_t *target = _we_hit_test(lcd->obj_list_head, data,
                                    0, 0, (int16_t)(lcd->width - 1), (int16_t)(lcd->height - 1));

    (void)hit_from_top; /* 纯触摸或无顶层裁剪档组合下不被读取 */
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    {
        /* 顶层对象绘制在普通层之上，命中优先级同样更高（链序后者更优）；
         * 模态激活时命中限定顶层链，未命中即视为命中模态对象本身
         * （"模态命中区=全屏"，点外收起的判断留在控件事件回调里）。 */
        we_obj_t *top_hit = _we_hit_test(lcd->top_list_head, data,
                                         0, 0, (int16_t)(lcd->width - 1), (int16_t)(lcd->height - 1));

        if (lcd->modal_obj != NULL)
        {
            target = (top_hit != NULL) ? top_hit : lcd->modal_obj;
            hit_from_top = 1U;
        }
        else if (top_hit != NULL)
        {
            target = top_hit;
            hit_from_top = 1U;
        }
    }
#endif /* WE_CFG_ENABLE_TOP_LAYER */

    // 按触摸状态机分发事件。
    if (data->state == WE_TOUCH_STATE_PRESSED)
    {
        /* 记录按下坐标，用于释放时判定滑动方向 */
        lcd->gesture_press_x = data->x;
        lcd->gesture_press_y = data->y;
        lcd->gesture_had_stay = 0U;
        lcd->gesture_drag_done = 0U;

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
        /* 手指本身就是"光标"：触摸按下即收起焦点光标（下方的焦点跟随
         * 继续生效，只是不画环）；下一次按键活动或 we_focus_set 再亮出。 */
        if ((lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
        {
            lcd->focus_flags &= (uint8_t)~WE_FOCUS_F_CURSOR_VIS;
            if (lcd->focus_obj != NULL)
                _we_focus_cursor_invalidate(lcd->focus_obj);
        }

        /* 触摸焦点跟随（切换时自动退出编辑态）：按到可聚焦控件时焦点
         * 移过去；目标本身不可聚焦时沿父链向上退，落到最近的可聚焦
         * 祖先（如点中容器内的装饰 label → 焦点落到容器本体）；
         * 父链走到顶仍无可聚焦归属、或点在空白处（无目标）时，
         * 视为"点到焦点体系之外"——取消当前聚焦。 */
        if (hit_from_top)
        {
            /* 顶层/模态对象（toast/msgbox/弹层 overlay）不参与触摸焦点
             * 跟随：它们不在焦点环里，点击也不应清掉普通层的当前焦点
             * （与旧弹层通道"弹层期间按压不动焦点"的语义一致）。 */
        }
        else if (target != NULL)
        {
            we_obj_t *cand = target;

            while (cand != NULL && !_we_focus_try_set(lcd, cand))
                cand = cand->parent;
            if (cand == NULL)
                we_focus_set(lcd, NULL);
        }
        else
        {
            we_focus_set(lcd, NULL);
        }
#endif

        /* 自最深命中对象起沿父链冒泡，首个返回"已消费"的成为按压对象。
         * 装饰性控件（返回 0）不再吞掉手势，容器空白区可交给外层做拖拽。 */
        lcd->pressed_obj = NULL;
        {
            we_obj_t *cand = target;

            while (cand != NULL)
            {
                if (cand->class_p != NULL && cand->class_p->event_cb != NULL &&
                    cand->class_p->event_cb(cand, WE_EVENT_PRESSED, data) != 0U)
                {
                    lcd->pressed_obj = cand;
                    break;
                }
                cand = cand->parent;
            }
        }
    }
    else if (data->state == WE_TOUCH_STATE_RELEASED)
    {
        if (lcd->pressed_obj != NULL)
        {
            lcd->pressed_obj->class_p->event_cb(lcd->pressed_obj, WE_EVENT_RELEASED, data);

            /* 滑动手势识别：仅在"快速划过"（无 STAY）时才生成 SWIPE。
             * 如果经历过 STAY（手指拖拽），控件已经通过 STAY 实时跟随了，
             * 松手后只需要吸附即可，不再额外派发 SWIPE 避免冲突。 */
            int16_t dx = data->x - lcd->gesture_press_x;
            int16_t dy = data->y - lcd->gesture_press_y;
            int16_t abs_dx = (dx >= 0) ? dx : -dx;
            int16_t abs_dy = (dy >= 0) ? dy : -dy;

            if (!lcd->gesture_had_stay && abs_dx > abs_dy && abs_dx >= WE_CFG_SWIPE_THRESHOLD)
            {
                /* 水平滑动 */
                we_event_t swipe = (dx < 0) ? WE_EVENT_SWIPE_LEFT : WE_EVENT_SWIPE_RIGHT;
                lcd->pressed_obj->class_p->event_cb(lcd->pressed_obj, swipe, data);
            }
            else if (!lcd->gesture_had_stay && abs_dy >= WE_CFG_SWIPE_THRESHOLD)
            {
                /* 垂直滑动 */
                we_event_t swipe = (dy < 0) ? WE_EVENT_SWIPE_UP : WE_EVENT_SWIPE_DOWN;
                lcd->pressed_obj->class_p->event_cb(lcd->pressed_obj, swipe, data);
            }
            else if (lcd->pressed_obj == target)
            {
                /* 位移不足或拖拽后松手，视为点击（拖拽后不在原控件上则不触发） */
                lcd->pressed_obj->class_p->event_cb(lcd->pressed_obj, WE_EVENT_CLICKED, data);
            }
        }
        lcd->pressed_obj = NULL;
    }
    else if (data->state == WE_TOUCH_STATE_STAY)
    {
        lcd->gesture_had_stay = 1U;

#if (WE_CFG_ENABLE_NESTED_INPUT == 1)
        /* 拖拽接管询问：位移一旦越过阈值，沿祖先链自内向外询问一次
         * WE_EVENT_DRAG_BEGIN，首个应答的容器接管手势（子控件回弹）。
         * 这是"可滚动容器里放交互控件"的关键——否则子控件恒消费 STAY，
         * 容器永远看不到拖拽。每个触摸序列只询问一轮。 */
        if (lcd->gesture_drag_done == 0U && lcd->pressed_obj != NULL)
        {
            int16_t dx = (int16_t)(data->x - lcd->gesture_press_x);
            int16_t dy = (int16_t)(data->y - lcd->gesture_press_y);

            if (WE_ABS(dx) >= WE_CFG_DRAG_THRESHOLD || WE_ABS(dy) >= WE_CFG_DRAG_THRESHOLD)
            {
                we_obj_t *anc = lcd->pressed_obj->parent;

                lcd->gesture_drag_done = 1U;
                while (anc != NULL)
                {
                    if (anc->class_p != NULL && anc->class_p->event_cb != NULL &&
                        anc->class_p->event_cb(anc, WE_EVENT_DRAG_BEGIN, data) != 0U)
                    {
                        we_indev_grab(lcd, anc);
                        break;
                    }
                    anc = anc->parent;
                }
            }
        }
#endif /* WE_CFG_ENABLE_NESTED_INPUT */

        // 长按/拖拽期间持续分发 STAY 事件。
        if (lcd->pressed_obj != NULL && lcd->pressed_obj->class_p->event_cb != NULL)
        {
            lcd->pressed_obj->class_p->event_cb(lcd->pressed_obj, WE_EVENT_STAY, data);
        }
    }
}

/**
 * @brief 为指定 LCD 实例注册输入读取接口
 * @param p_lcd 传入，待绑定输入接口的 GUI 屏幕上下文指针
 * @param read_cb 传入，输入读取回调，传 NULL 表示当前 LCD 不启用输入采集
 * @return 无
 * @note 该接口只负责登记“如何读取输入”，主循环仍然可以自行决定轮询时机
 *       和是否把读取结果送入 we_gui_indev_handler()。
 */
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
void we_input_init_with_port(we_lcd_t *p_lcd, we_input_read_cb_t read_cb)
{
    WE_ASSERT(p_lcd != NULL);
    if (p_lcd == NULL)
        return;

    p_lcd->input_read_cb = read_cb;
    p_lcd->indev_data.x = 0;
    p_lcd->indev_data.y = 0;
    p_lcd->indev_data.state = WE_TOUCH_STATE_NONE;
    p_lcd->pressed_obj = NULL;       /* 重绑输入时丢弃旧按压引用 */
    p_lcd->gesture_had_stay = 0U;
    p_lcd->gesture_drag_done = 0U;
}
#endif

/**
 * @brief 为指定 LCD 实例注册外部存储读取接口
 * @param p_lcd 传入，待绑定存储接口的 GUI 屏幕上下文指针
 * @param read_cb 传入，存储读取回调，参数顺序为(存储地址 addr, 数组指针 buf, 读取数量 len)
 * @return 无
 */
#if (WE_CFG_ENABLE_STORAGE_PORT_BIND == 1)
void we_storage_init_with_port(we_lcd_t *p_lcd, we_storage_read_cb_t read_cb)
{
    WE_ASSERT(p_lcd != NULL);
    if (p_lcd == NULL)
        return;

    p_lcd->storage_read_cb = read_cb;
}
#endif

/**
 * @brief 一次性完成 LCD / 输入 / 存储三路端口绑定
 * @param p_lcd 传入，待初始化的 GUI 屏幕上下文指针
 * @param bg 传入，默认背景色
 * @param gram_base 传入，用户指定的 PFB 缓冲区基址
 * @param gram_size 传入，用户提供的 PFB 总像素容量
 * @param set_addr_cb 传入，底层设窗回调
 * @param flush_cb 传入，底层刷屏回调
 * @param input_cb 传入，输入读取回调
 * @param storage_cb 传入，外部存储读取回调
 * @return 无
 * @note input_cb 和 storage_cb 在对应 WE_CFG_ENABLE_xxx_PORT_BIND == 0 时被忽略，可安全传 NULL。
 */
void we_gui_init(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
                 we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb, we_input_read_cb_t input_cb,
                 we_storage_read_cb_t storage_cb)
{
    we_lcd_init_with_port(p_lcd, bg, gram_base, gram_size, set_addr_cb, flush_cb);
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
    we_input_init_with_port(p_lcd, input_cb);
#else
    (void)input_cb;
#endif
#if (WE_CFG_ENABLE_STORAGE_PORT_BIND == 1)
    we_storage_init_with_port(p_lcd, storage_cb);
#else
    (void)storage_cb;
#endif
}
