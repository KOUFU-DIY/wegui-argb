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
 * @file  we_focus.c
 * @brief 语义键队列、焦点管理器、编辑态与驱动级矩形焦点光标。
 */
#include "we_internal.h"

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
/* --------------------------------------------------------------------------
 * 全局聚焦 / 按键导航管理器
 *
 * 模型（分层作用域，"OK 向下钻 / BACK 向上退"）：
 * 1. 语义键由端口消抖后注入 SPSC 环形队列（press/release 双沿或 inject
 *    tap），we_gui_run() 每次运行时消费并分发；
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
 * 全部瞬态标志压缩在 focus_flags 一个字节里（WE_FOCUS_F_*）。
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
void _we_focus_cursor_invalidate(we_obj_t *obj)
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
 * 焦点切走/清除时改发 WE_KEY_EVT_FLASH_END（仅回弹不点击）。
 * 方向/前后/BACK 键仍为按下沿触发，松开沿忽略。
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

    lcd->focus_flags &= (uint8_t)~(WE_FOCUS_F_OK_HELD | WE_FOCUS_F_OK_ARMED);
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
    lcd->focus_flags &= (uint8_t)~(WE_FOCUS_F_OK_HELD | WE_FOCUS_F_OK_ARMED);
    if (obj != NULL && obj->class_p != NULL && obj->class_p->event_cb != NULL &&
        (obj->class_p->class_flags & WE_CLASS_FLAG_FOCUSABLE) != 0U)
        (void)obj->class_p->event_cb(obj, WE_KEY_EVT_FLASH_END, &lcd->indev_data); /* 仅回弹 */
}

/**
 * @brief 尝试把焦点移到目标对象（含 FOCUS 实例查询）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param obj 传入，目标对象
 * @return 1 表示焦点已落到目标（或目标本就是焦点），0 表示目标拒绝/不可聚焦
 */
uint8_t _we_focus_try_set(we_lcd_t *lcd, we_obj_t *obj)
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
            _we_key_ok_deliver_release(lcd);
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
                                     (uint8_t)~WE_FOCUS_F_OK_ARMED);
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
                lcd->focus_flags |= WE_FOCUS_F_OK_ARMED; /* 进入按压待回发状态，等待松开沿 */
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
            lcd->focus_flags |= WE_FOCUS_F_OK_ARMED; /* 进入按压待回发状态，等待松开沿 */
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
        return; /* 队满丢弃本次注入（丢一次输入无状态残留；OK 松开由 key_ok_down 电平兜底） */
    lcd->key_queue[tail] = code;
    lcd->key_q_tail = next;
}

void we_gui_key_press(we_lcd_t *lcd, uint8_t key)
{
    if (lcd == NULL || key < WE_KEY_UP || key > WE_KEY_BACK)
        return; /* WE_KEY_EVT_* 通知类键值禁止注入 */
    if (key == WE_KEY_OK)
        lcd->key_ok_down = 1U; /* 电平先于入队：消费侧兜底以电平为事实源 */
    _we_key_enqueue(lcd, key);
}

void we_gui_key_release(we_lcd_t *lcd, uint8_t key)
{
    if (lcd == NULL || key < WE_KEY_UP || key > WE_KEY_BACK)
        return;
    if (key == WE_KEY_OK)
        lcd->key_ok_down = 0U;
    _we_key_enqueue(lcd, (uint8_t)(key | WE_KEY_RELEASE_FLAG));
}

void we_gui_key_inject(we_lcd_t *lcd, uint8_t key)
{
    /* tap 语义 = 一按一松 */
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
void _we_focus_draw_cursor(we_lcd_t *p_lcd)
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
    if (x1 < p_lcd->clip.x0 || x0 > p_lcd->clip.x1 ||
        y1 < (int16_t)p_lcd->clip.y0 || y0 > (int16_t)p_lcd->clip.y1)
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

/**
 * @brief 消费语义键队列（主循环每周期调用一次）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
void _we_focus_process_keys(we_lcd_t *p_lcd)
{
    /* GUI按键消费队列 */
    if (p_lcd->key_q_head != p_lcd->key_q_tail)
        _we_focus_cursor_show(p_lcd); /* 任何gui内部按键活动都把聚焦光标亮出来（触摸按下时收起） */
    while (p_lcd->key_q_head != p_lcd->key_q_tail)
    {
        uint8_t key = p_lcd->key_queue[p_lcd->key_q_head];
        p_lcd->key_q_head = (uint8_t)((p_lcd->key_q_head + 1U) & (WE_CFG_KEY_QUEUE_LEN - 1U));
        _we_focus_dispatch(p_lcd, key);
    }

    /* OK 松开电平兜底：松开沿若因队满丢失，按注入侧电平事实收尾按压
     * （正常送达时上面已按序清掉 HELD，此处恒为空操作）。 */
    if ((p_lcd->focus_flags & WE_FOCUS_F_OK_HELD) != 0U && p_lcd->key_ok_down == 0U)
        _we_key_ok_deliver_release(p_lcd);
}
#endif /* WE_CFG_ENABLE_KEY_INPUT */
