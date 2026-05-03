#ifndef WE_WIDGET_SLIDESHOW_H
#define WE_WIDGET_SLIDESHOW_H

#include "we_gui_driver.h"
#include "widgets/group/we_widget_group.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 幻灯片吸附动画模式：
 * 0: 松手后立即吸附到目标页
 * 1: 固定步长靠近，代码最简单
 * 2: 纯整数“拉力 + 阻尼”缓动，视觉更自然 */
#define WE_SLIDESHOW_SNAP_ANIM_NONE    0
#define WE_SLIDESHOW_SNAP_ANIM_SIMPLE  1
#define WE_SLIDESHOW_SNAP_ANIM_COMPLEX 2

#ifndef WE_SLIDESHOW_SNAP_ANIM_MODE
#define WE_SLIDESHOW_SNAP_ANIM_MODE WE_SLIDESHOW_SNAP_ANIM_COMPLEX
#endif

#ifndef WE_SLIDESHOW_SNAP_SIMPLE_STEP
#define WE_SLIDESHOW_SNAP_SIMPLE_STEP 8
#endif

#ifndef WE_SLIDESHOW_SNAP_COMPLEX_PULL_DIV
#define WE_SLIDESHOW_SNAP_COMPLEX_PULL_DIV 3
#endif

#ifndef WE_SLIDESHOW_SNAP_COMPLEX_DAMP_NUM
#define WE_SLIDESHOW_SNAP_COMPLEX_DAMP_NUM 3
#endif

#ifndef WE_SLIDESHOW_SNAP_COMPLEX_DAMP_DEN
#define WE_SLIDESHOW_SNAP_COMPLEX_DAMP_DEN 4
#endif

#ifndef WE_SLIDESHOW_SNAP_COMPLEX_MAX_STEP
#define WE_SLIDESHOW_SNAP_COMPLEX_MAX_STEP 24
#endif

/* 幻灯片最大页数。每页宽度固定等于控件宽度。 */
#ifndef WE_SLIDESHOW_PAGE_MAX
#define WE_SLIDESHOW_PAGE_MAX 8
#endif

/* 幻灯片内最多挂载的子控件数量（所有页面合计）。 */
#ifndef WE_SLIDESHOW_CHILD_MAX
#define WE_SLIDESHOW_CHILD_MAX 24
#endif

typedef struct
{
    we_obj_t *child;
    uint16_t page_index;
    int16_t local_x;
    int16_t local_y;
    uint8_t used;
} we_slideshow_child_slot_t;

typedef struct
{
    we_group_obj_t group;
    we_obj_t *last_pressed_child;
    int16_t scroll_x;
    int16_t last_touch_x;
    int16_t last_touch_y;
    int16_t snap_target_x;
#if (WE_SLIDESHOW_SNAP_ANIM_MODE == WE_SLIDESHOW_SNAP_ANIM_COMPLEX)
    int16_t snap_vx;
#endif
    uint16_t page_count;
    uint16_t current_page;
    uint8_t swipe_enabled : 1;
    uint8_t is_dragging : 1;
    uint8_t snap_animating : 1;
    we_slideshow_child_slot_t child_slots[WE_SLIDESHOW_CHILD_MAX];
} we_slideshow_obj_t;

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
void we_slideshow_obj_init(we_slideshow_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, int16_t w, int16_t h,
                           colour_t bg_color, uint8_t opacity);

/**
 * @brief 释放控件运行时状态并从任务系统注销。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_slideshow_obj_delete(we_slideshow_obj_t *obj);

/**
 * @brief 新增一页并返回页索引。
 * @param obj 幻灯片控件对象指针。
 * @return 新页索引；失败时返回 0xFFFF。
 */
uint16_t we_slideshow_add_page(we_slideshow_obj_t *obj);

/**
 * @brief 获取已创建的总页数。
 * @param obj 幻灯片控件对象指针。
 * @return 页数；obj 为 NULL 时返回 0。
 */

/**
 * @brief 获取当前停留页索引。
 * @param obj 幻灯片控件对象指针。
 * @return 当前页索引；obj 为 NULL 时返回 0。
 */
uint16_t we_slideshow_get_current_page(const we_slideshow_obj_t *obj);

/**
 * @brief 将子控件挂到指定页面。
 * @param obj 幻灯片控件对象指针。
 * @param page_index 目标页面索引。
 * @param child 要挂载的子控件对象。
 */
void we_slideshow_add_child(we_slideshow_obj_t *obj, uint16_t page_index, we_obj_t *child);

/**
 * @brief 设置子控件在所属页面内的局部坐标。
 * @param obj 幻灯片控件对象指针。
 * @param child 已挂载的子控件对象。
 * @param local_x 子控件在页面内的 X 偏移。
 * @param local_y 子控件在页面内的 Y 偏移。
 */
void we_slideshow_set_child_pos(we_slideshow_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y);

/**
 * @brief 切换到指定页面并处理吸附动画。
 * @param obj 目标控件对象指针。
 * @param page_index 目标页索引。
 * @param animated 是否启用动画（0 否，非 0 是）。
 * @return 无。
 */
void we_slideshow_set_page(we_slideshow_obj_t *obj, uint16_t page_index, uint8_t animated);

/**
 * @brief 切换到下一页并更新当前页索引。
 * @param obj 目标控件对象指针。
 * @param animated 是否启用动画（0 否，非 0 是）。
 * @return 无。
 */
void we_slideshow_next(we_slideshow_obj_t *obj, uint8_t animated);

/**
 * @brief 切换到上一页并更新当前页索引。
 * @param obj 目标控件对象指针。
 * @param animated 是否启用动画（0 否，非 0 是）。
 * @return 无。
 */
void we_slideshow_prev(we_slideshow_obj_t *obj, uint8_t animated);

/**
 * @brief 开关滑动手势翻页功能。
 * @param obj 幻灯片控件对象指针。
 * @param en 0 关闭，非 0 开启。
 */
void we_slideshow_set_swipe_enabled(we_slideshow_obj_t *obj, uint8_t en);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_slideshow_set_opacity(we_slideshow_obj_t *obj, uint8_t opacity);

#ifdef __cplusplus
}
#endif

#endif
