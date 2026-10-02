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
 * @file  we_obj.c
 * @brief 对象树：挂接/摘链/改挂、几何修改、标脏、唯一删除入口与引用回收、
 *        顶层链与模态。
 */
#include "we_internal.h"

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
 * @brief 通用对象改尺寸接口，适用于所有继承 we_obj_t 的控件
 * @param obj 传入，目标对象指针
 * @param new_w 传入，新的宽度（像素）
 * @param new_h 传入，新的高度（像素）
 * @return 无
 * @note 与 we_obj_set_pos 同款契约：焦点光标环悬在包围盒外侧，尺寸变化会带动
 *       环的位置，控件自身的旧/新包围盒标脏盖不住它，因此改尺寸前后由内核
 *       各标一次环——控件不必自己算环的几何。
 *       只改尺寸不改坐标（左上角不动）；尺寸未变时直接返回。
 */
void we_obj_set_size(we_obj_t *obj, int16_t new_w, int16_t new_h)
{
    if (obj == NULL || obj->lcd == NULL)
        return;
    if (obj->w == new_w && obj->h == new_h)
        return;

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    if (obj == obj->lcd->focus_obj && (obj->lcd->focus_flags & WE_FOCUS_F_CURSOR_VIS) != 0U)
        _we_focus_cursor_invalidate(obj);
#endif

    // 1. 先把旧尺寸区域标脏，保证缩小后多出来的部分会被重绘擦除。
    we_obj_invalidate(obj);

    // 2. 更新尺寸；左上角与所属链表、绘制顺序都不变。
    obj->w = new_w;
    obj->h = new_h;

    // 3. 再把新尺寸区域标脏，保证放大后新增的部分会被绘制出来。
    we_obj_invalidate(obj);

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
 * @return 链表头指针的地址（父容器子链、LCD 顶层链或普通层链）
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
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    /* 顶层链成员与普通层对象一样父指针为空，只能查链区分（顶层链通常只有几个对象） */
    {
        const we_obj_t *it = obj->lcd->top_list_head;

        while (it != NULL && it != obj)
            it = it->next;
        if (it != NULL)
            return &obj->lcd->top_list_head;
    }
#endif
    return &obj->lcd->obj_list_head;
}

/**
 * @brief 把对象从其所属的任意一条根链上摘除（根链清单的唯一维护点）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param obj 传入：目标对象
 * @return 无
 * @note 先摘 owner_head 定位到的所属链（父容器子链 / 顶层链 / 普通层链），
 *       再对顶层链补摘一次兜底（父指针被外部改乱时定位会偏），空摘无害。
 *       将来新增根链（如调试覆盖层）在 owner_head 与此处同步补上，
 *       detach / delete / attach_to_top / bring_to_front 自动跟随。
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
    if (lcd == NULL || obj == NULL)
        return;

    /* 先从所属根链摘出（重复调用即移至链尾；保留 lcd/class_p，对象仍可用；
     * 从未挂链的弹层对象空摘无害），再挂到链尾。 */
    _we_obj_unlink_any_root(lcd, obj);
    obj->parent = NULL;
    obj->next = NULL;
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    we_obj_append_to_list(&lcd->top_list_head, obj);
#else
    /* 裁剪档：无顶层链，挂到普通层链尾——仍可显示，但可能被后来者
     * 盖住，失去"永远压住普通层"的保证。 */
    we_obj_append_to_list(&lcd->obj_list_head, obj);
#endif
    we_obj_invalidate(obj);
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
    //     待回发标志一起清零即可；HELD 保留，待物理松开沿自然清零。
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
            lcd->focus_flags &= (uint8_t)~(WE_FOCUS_F_EDIT | WE_FOCUS_F_OK_ARMED);
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
