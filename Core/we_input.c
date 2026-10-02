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
 * @file  we_input.c
 * @brief 触摸输入：递归命中测试、按压冒泡、拖拽接管、点击/滑动判定。
 */
#include "we_internal.h"

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
                        /* 子控件只在它们被画出来的区域（content_cb 给出的子控件区）内可命中 */
                        we_rect_t area;
                        uint8_t opa = 255U;

                        area.x0 = x0;
                        area.y0 = y0;
                        area.x1 = x1;
                        area.y1 = y1;
                        if (curr->class_p->content_cb != NULL)
                            opa = curr->class_p->content_cb(curr, &area);
                        if (opa != 0U)
                        {
                            we_obj_t *deeper = _we_hit_test(((we_child_owner_t *)curr)->children_head, data,
                                                            WE_MAX(x0, area.x0), WE_MAX(y0, area.y0),
                                                            WE_MIN(x1, area.x1), WE_MIN(y1, area.y1));
                            if (deeper != NULL)
                                hit = deeper;
                        }
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

/**
 * @brief 向按压对象派发一个触摸事件
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param event 传入：事件
 * @param data 传入：输入状态
 * @return 无
 * @note 按压途中控件可能换成不收事件的类（event_cb 为 NULL，如 indicator
 *       取消可点击），此时本序列余下的事件直接丢弃。
 */
static void _we_send_pressed(we_lcd_t *lcd, we_event_t event, we_indev_data_t *data)
{
    we_obj_t *po = lcd->pressed_obj;

    if (po != NULL && po->class_p != NULL && po->class_p->event_cb != NULL)
        (void)po->class_p->event_cb(po, event, data);
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
 * @brief 解析当前触摸点的命中对象（普通链 → 顶层链 → 模态兜底）
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param data 传入，触摸数据
 * @param hit_from_top 传出（可空），置 1 表示命中来自顶层/模态通道
 * @return 命中对象；空白处为 NULL（模态激活时恒非空）
 * @note 只在需要命中结果的分支调用（PRESSED / RELEASED 点击复核）；
 *       STAY 全部派发给 pressed_obj，不做命中测试。
 */
static we_obj_t *_we_resolve_target(we_lcd_t *lcd, we_indev_data_t *data, uint8_t *hit_from_top)
{
    we_obj_t *target = _we_hit_test(lcd->obj_list_head, data,
                                    0, 0, (int16_t)(lcd->width - 1), (int16_t)(lcd->height - 1));

    if (hit_from_top != NULL)
        *hit_from_top = 0U;
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
            if (hit_from_top != NULL)
                *hit_from_top = 1U;
        }
        else if (top_hit != NULL)
        {
            target = top_hit;
            if (hit_from_top != NULL)
                *hit_from_top = 1U;
        }
    }
#endif /* WE_CFG_ENABLE_TOP_LAYER */
    return target;
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

    // 按触摸状态机分发事件。
    if (data->state == WE_TOUCH_STATE_PRESSED)
    {
        uint8_t hit_from_top = 0U;
        we_obj_t *target = _we_resolve_target(lcd, data, &hit_from_top);

        (void)hit_from_top; /* 纯触摸档（无按键焦点跟随）不读取 */

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
            _we_send_pressed(lcd, WE_EVENT_RELEASED, data);

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
                _we_send_pressed(lcd, (dx < 0) ? WE_EVENT_SWIPE_LEFT : WE_EVENT_SWIPE_RIGHT, data);
            }
            else if (!lcd->gesture_had_stay && abs_dy >= WE_CFG_SWIPE_THRESHOLD)
            {
                /* 垂直滑动 */
                _we_send_pressed(lcd, (dy < 0) ? WE_EVENT_SWIPE_UP : WE_EVENT_SWIPE_DOWN, data);
            }
            else if (lcd->pressed_obj == _we_resolve_target(lcd, data, NULL))
            {
                /* 位移不足或拖拽后松手，视为点击（拖拽后不在原控件上则不触发）。
                 * 命中测试惰性化：只有走到点击复核才需要"松手时手指下是谁"。 */
                _we_send_pressed(lcd, WE_EVENT_CLICKED, data);
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

        /* 长按/拖拽期间持续分发 STAY 事件 */
        _we_send_pressed(lcd, WE_EVENT_STAY, data);
    }
}

/**
 * @brief 输入阶段：采样端口触摸并按电平变化派发边沿事件
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 端口只报"是否按下 + 坐标"：未按下→按下 派发 PRESSED，持续按下
 *       每次采样派发 STAY，按下→未按下 派发 RELEASED（坐标取端口报告的
 *       松手点；端口不填时沿用最后一次按下的位置）。端口不提供 touch_read
 *       时无触摸，直接返回。
 */
void _we_input_poll(we_lcd_t *p_lcd)
{
    we_touch_data_t t;

    if (p_lcd->port->touch_read == NULL)
        return;

    t.x = p_lcd->indev_data.x; /* 预填上一次坐标：松手时不报坐标的触摸芯片无需特殊处理 */
    t.y = p_lcd->indev_data.y;
    t.pressed = 0U;
    p_lcd->port->touch_read(&t);

    if (t.pressed != 0U)
    {
        p_lcd->indev_data.state = (p_lcd->touch_down != 0U) ? WE_TOUCH_STATE_STAY : WE_TOUCH_STATE_PRESSED;
        p_lcd->touch_down = 1U;
    }
    else if (p_lcd->touch_down != 0U)
    {
        p_lcd->indev_data.state = WE_TOUCH_STATE_RELEASED;
        p_lcd->touch_down = 0U;
    }
    else
    {
        return; /* 一直未按下：无事件 */
    }
    p_lcd->indev_data.x = t.x;
    p_lcd->indev_data.y = t.y;
    we_gui_indev_handler(p_lcd, &p_lcd->indev_data);
}
