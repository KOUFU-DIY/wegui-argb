#ifndef WE_WIDGET_GROUP_H
#define WE_WIDGET_GROUP_H

#include "wegui.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    we_obj_t base;                /* 前缀契约：与 we_child_owner_t 保持 base、children_head 顺序 */
    we_obj_t *children_head;
    colour_t bg_color;
    uint8_t opacity;
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
 * @brief 删除组容器：子控件由 we_obj_delete 的 CHILD_OWNER 后序递归一并删除。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_group_obj_delete(we_group_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

/**
 * @brief 将子控件加入组容器并建立父子关系。
 * @param obj 目标控件对象指针。
 * @param child 需要加入分组管理的子控件对象。
 * @return 无。
 */
void we_group_add_child(we_group_obj_t *obj, we_obj_t *child);

/**
 * @brief 将子控件从组容器中移除（擦掉屏上像素并摘链，绝对坐标不变）。
 * @param obj 目标控件对象指针。
 * @param child 需要从分组中移除的子控件对象。
 * @return 无。
 * @note 摘下的控件不再绘制，要继续显示需由调用方重新挂回。
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

/* 以下三个类回调供内嵌 we_group_obj_t 作首成员的容器控件（如 slideshow）
 * 直接填进自己的类描述符：纯色直角背景、子控件区 = 包围盒、移动时带子控件平移。 */

/**
 * @brief 组容器绘制回调：画纯色直角背景。
 * @param ptr 控件对象指针（前缀为 we_group_obj_t）。
 * @return 无。
 */
void we_group_draw_cb(void *ptr);

/**
 * @brief 组容器子控件区回调：整个包围盒，返回本容器透明度作级联。
 * @param ptr 控件对象指针（前缀为 we_group_obj_t）。
 * @param area 子控件区（不改）。
 * @return 子控件级联透明度（0 = 子树不画不命中）。
 */
uint8_t we_group_content_cb(void *ptr, we_rect_t *area);

/**
 * @brief 组容器移动回调：外框与全部子控件一起平移，只标曝光条带。
 * @param ptr 控件对象指针（前缀为 we_group_obj_t）。
 * @param new_x 新的左上角 X 坐标。
 * @param new_y 新的左上角 Y 坐标。
 * @return 无。
 */
void we_group_set_pos_cb(void *ptr, int16_t new_x, int16_t new_y);

#ifdef __cplusplus
}
#endif

#endif
