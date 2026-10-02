#include "we_widget_group.h"
#include "wegui.h"
/**
 * @brief 控件绘制回调：画背景（子控件由内核按 content_cb 递归绘制）。
 * @param ptr 回调透传对象指针（前缀为 we_group_obj_t）。
 * @return 无。
 */
void we_group_draw_cb(void *ptr)
{
    we_group_obj_t *obj = (we_group_obj_t *)ptr;

    if (obj->opacity == 0)
        return;

    we_fill_rect(obj->base.lcd, obj->base.x, obj->base.y,
                 (uint16_t)obj->base.w, (uint16_t)obj->base.h, obj->bg_color, obj->opacity);
}

/**
 * @brief 子控件区：整个包围盒，透明度随本容器级联（0 = 子树不画不命中）。
 * @param ptr 回调透传对象指针（前缀为 we_group_obj_t）。
 * @param area 子控件区（默认已是包围盒，这里不改）。
 * @return 子控件级联透明度。
 */
uint8_t we_group_content_cb(void *ptr, we_rect_t *area)
{
    (void)area;
    return ((we_group_obj_t *)ptr)->opacity;
}

/**
 * @brief 组容器事件回调：只应答命中查询（子控件的命中与派发由内核完成）。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 已处理，0 未处理。
 */
static uint8_t _group_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_group_obj_t *obj = (we_group_obj_t *)ptr;

    (void)data;
    /* 命中查询：完全透明（淡出隐藏）的容器连同整棵子树跳过命中。
     * 其余事件一律返回 0——子控件命中/按压锁定/CLICKED 复核全部由内核
     * 统一派发完成，group 空白区不吞手势（留给外层容器做拖拽）。 */
    if (event == WE_EVENT_HIT_TEST)
        return (uint8_t)(obj->opacity != 0U);
    return 0U;
}

/**
 * @brief 容器移动回调：平移外框的同时按局部坐标同步全部子控件。
 * @param ptr 回调透传对象指针（前缀为 we_group_obj_t）。
 * @param new_x 新的左上角 X 坐标。
 * @param new_y 新的左上角 Y 坐标。
 * @return 无。
 * @note 没有它时，外层容器（如 slideshow 翻页）经 we_obj_set_pos 移动
 *       group 只会挪外框，子控件会留在原地。子控件经 we_obj_set_pos
 *       跟随，嵌套 group / 带 set_pos_cb 的控件（img_ex 等）自动级联。
 *
 *       标脏策略（精细模式）：背景是纯色直角填充（radius 恒为 0），小步
 *       平移时新旧框重叠区里背景混色结果逐像素不变（半透明亦然：底层内容
 *       未动），因此只标新旧框各自扣掉重叠区的曝光 L 条带；子控件的旧/新
 *       足迹由逐子 we_obj_set_pos 自行标脏（越出父容器新框的部分恰落在
 *       旧框曝光条带内，逐层归纳无遗漏）。跳越无重叠或完全透明时回退
 *       整框旧+新标脏。若未来 group 背景引入圆角/渐变/贴图，重叠区不再
 *       平移不变，本优化必须退化为整框标脏。
 */
void we_group_set_pos_cb(void *ptr, int16_t new_x, int16_t new_y)
{
    we_group_obj_t *obj = (we_group_obj_t *)ptr;
    int16_t old_x = obj->base.x;
    int16_t old_y = obj->base.y;
    int16_t w = obj->base.w;
    int16_t h = obj->base.h;
    int16_t ov_x = WE_MAX(old_x, new_x);
    int16_t ov_y = WE_MAX(old_y, new_y);
    int16_t ov_w = (int16_t)(WE_MIN(old_x, new_x) + w - ov_x);
    int16_t ov_h = (int16_t)(WE_MIN(old_y, new_y) + h - ov_y);

    if (obj->opacity != 0U && ov_w > 0 && ov_h > 0)
    {
        we_obj_invalidate_area_exclude((we_obj_t *)obj, old_x, old_y, w, h,
                                       ov_x, ov_y, ov_w, ov_h);
        obj->base.x = new_x;
        obj->base.y = new_y;
        we_group_shift_children(obj, (int16_t)(new_x - old_x), (int16_t)(new_y - old_y), 0U);
        we_obj_invalidate_area_exclude((we_obj_t *)obj, new_x, new_y, w, h,
                                       ov_x, ov_y, ov_w, ov_h);
        return;
    }

    we_obj_invalidate((we_obj_t *)obj);
    obj->base.x = new_x;
    obj->base.y = new_y;
    we_group_shift_children(obj, (int16_t)(new_x - old_x), (int16_t)(new_y - old_y), 0U);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 初始化组容器对象（子控件链表置空、挂载到 LCD 链表）。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param bg_color 背景颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_group_obj_init(we_group_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, int16_t w, int16_t h,
                       colour_t bg_color, uint8_t opacity)
{
    static const we_class_t _group_class = {
        .draw_cb = we_group_draw_cb,
        .event_cb = _group_event_cb,
        .set_pos_cb = we_group_set_pos_cb,
        .content_cb = we_group_content_cb,
        /* 结构位：前缀为 we_child_owner_t，删除/改挂走 children_head。
         * 行为位：焦点管理器据此支持 OK 进入 / BACK 退出，
         * 且仅当子树内存在可聚焦控件时本容器才会成为焦点停靠点。 */
        .class_flags = WE_CLASS_FLAG_CHILD_OWNER | WE_CLASS_FLAG_FOCUS_ENTER,
    };

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_group_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;
    obj->children_head = NULL;
    obj->bg_color = bg_color;
    obj->opacity = opacity;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 将子控件从原链表摘出并挂入本组（建立父子关系，落到组左上角）；自挂载/跨 lcd/重复挂载会被忽略。
 * @param obj 目标控件对象指针。
 * @param child 目标子控件对象指针。
 * @return 无。
 */
void we_group_add_child(we_group_obj_t *obj, we_obj_t *child)
{
    if (obj == NULL || child == NULL)
        return;
    if (child == (we_obj_t *)obj)
        return;
    if (child->lcd != obj->base.lcd)
        return;
    if (child->parent == (we_obj_t *)obj)
        return; /* 已挂载 */

    /* 先按旧父容器标脏旧位置：改挂之后再标会被裁到新父容器范围内，旧处留残影 */
    we_obj_invalidate(child);
    we_obj_set_parent(child, (we_obj_t *)obj);
    /* 默认落在组左上角（局部 0,0）；子控件数量无上限 */
    we_obj_set_pos(child, obj->base.x, obj->base.y);
}

/**
 * @brief 从组中移除子控件：擦掉它在屏上的像素并摘链（绝对坐标保持不变）。
 * @param obj 目标控件对象指针。
 * @param child 目标子控件对象指针。
 * @return 无。
 * @note 摘下的控件不再绘制；要继续显示，由调用方用 we_obj_attach_to_lcd /
 *       we_group_add_child 等重新挂回。
 */
void we_group_remove_child(we_group_obj_t *obj, we_obj_t *child)
{
    if (obj == NULL || child == NULL || child->parent != (we_obj_t *)obj)
        return;

    we_obj_invalidate(child);
    we_obj_detach(child);
}

/**
 * @brief 设置子控件在组内的局部坐标并刷新其屏幕绝对位置。
 * @param obj 目标控件对象指针。
 * @param child 目标子控件对象指针。
 * @param local_x 相对组左上角的局部 X 坐标（像素）。
 * @param local_y 相对组左上角的局部 Y 坐标（像素）。
 * @return 无。
 */
void we_group_set_child_pos(we_group_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y)
{
    if (obj == NULL || child == NULL || child->parent != (we_obj_t *)obj)
        return;

    we_obj_set_pos(child, (int16_t)(obj->base.x + local_x),
                   (int16_t)(obj->base.y + local_y));
}

/**
 * @brief 按给定位移整体平移全部子控件，并可选派发 WE_EVENT_SCROLLED。
 * @param obj 目标控件对象指针。
 * @param dx X 方向平移增量（像素）。
 * @param dy Y 方向平移增量（像素）。
 * @param send_scrolled_event 是否向子控件派发 WE_EVENT_SCROLLED 事件。
 * @return 无。
 */
void we_group_shift_children(we_group_obj_t *obj, int16_t dx, int16_t dy, uint8_t send_scrolled_event)
{
    we_obj_t *child;
    we_indev_data_t scroll_data = {0};

    if (obj == NULL || (dx == 0 && dy == 0))
        return;

    scroll_data.x = dx;
    scroll_data.y = dy;

    child = obj->children_head;
    while (child != NULL)
    {
        we_obj_set_pos(child, (int16_t)(child->x + dx), (int16_t)(child->y + dy));
        if (send_scrolled_event && child->class_p && child->class_p->event_cb)
            child->class_p->event_cb(child, WE_EVENT_SCROLLED, &scroll_data);
        child = child->next;
    }
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_group_set_opacity(we_group_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;

    /* 包围盒不变，标一次即可（从 0 变亮或变到 0 都要重画这块） */
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}
