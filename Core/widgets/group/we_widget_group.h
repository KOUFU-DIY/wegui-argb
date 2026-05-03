#ifndef WE_WIDGET_GROUP_H
#define WE_WIDGET_GROUP_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 控件组最多挂载的子控件数量。 */
#ifndef WE_GROUP_CHILD_MAX
#define WE_GROUP_CHILD_MAX 24
#endif

typedef struct
{
    we_obj_t *child;
    int16_t local_x;
    int16_t local_y;
    uint8_t used;
} we_group_child_slot_t;

typedef struct
{
    we_obj_t base;
    int8_t task_id;
    we_obj_t *children_head;
    colour_t bg_color;
    uint8_t opacity;
    we_group_child_slot_t child_slots[WE_GROUP_CHILD_MAX];
} we_group_obj_t;

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
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
                       colour_t bg_color, uint8_t opacity);

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_group_obj_delete(we_group_obj_t *obj);

/**
 * @brief 将子控件加入组容器并建立父子关系。
 * @param obj 目标控件对象指针。
 * @param child 需要加入分组管理的子控件对象。
 * @return 无。
 */
void we_group_add_child(we_group_obj_t *obj, we_obj_t *child);

/**
 * @brief 将子控件从组容器中移除。
 * @param obj 目标控件对象指针。
 * @param child 需要从分组中移除的子控件对象。
 * @return 无。
 */
void we_group_remove_child(we_group_obj_t *obj, we_obj_t *child);

/**
 * @brief 设置子控件在组内的局部坐标并更新绝对位置。
 * @param obj 目标控件对象指针。
 * @param child 需要加入分组管理的子控件对象。
 * @param local_x 子控件相对分组左上角的局部 X 坐标（像素）。
 * @param local_y 子控件相对分组左上角的局部 Y 坐标（像素）。
 * @return 无。
 */
void we_group_set_child_pos(we_group_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y);

/**
 * @brief 按当前局部坐标重新布局全部子控件。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_group_relayout(we_group_obj_t *obj);

/**
 * @brief 按给定位移整体平移子控件，并可派发滚动事件。
 * @param obj 目标控件对象指针。
 * @param dx 滚动增量 X（像素）。
 * @param dy 滚动增量 Y（像素）。
 * @param send_scrolled_event 是否向子控件派发 WE_EVENT_SCROLLED 事件（0 否，非 0 是）。
 * @return 无。
 */
void we_group_shift_children(we_group_obj_t *obj, int16_t dx, int16_t dy, uint8_t send_scrolled_event);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_group_set_opacity(we_group_obj_t *obj, uint8_t opacity);

#ifdef __cplusplus
}
#endif

#endif
