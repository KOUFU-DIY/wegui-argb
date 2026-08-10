#ifndef WE_WIDGET_SCROLL_PANEL_H
#define WE_WIDGET_SCROLL_PANEL_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 开启后面板成为焦点停靠点：OK 下钻进入子控件、BACK 退回面板本体，
 * 子控件获得焦点时面板自动滚动跟随（连光标环一起滚进可视区）。
 * 依赖容器下钻语义，WE_CFG_FOCUS_NESTED=0 时整体关闭；
 * 置 0 单独裁剪本控件的按键支持，其余控件不受影响。 */
#ifndef WE_SCROLL_PANEL_USE_KEY
#define WE_SCROLL_PANEL_USE_KEY 1
#endif

#ifndef WE_SCROLL_PANEL_SCROLLBAR_W
#define WE_SCROLL_PANEL_SCROLLBAR_W 4U
#endif

#ifndef WE_SCROLL_PANEL_DRAG_THRESHOLD
#define WE_SCROLL_PANEL_DRAG_THRESHOLD 8
#endif

#ifndef WE_SCROLL_PANEL_USE_INERTIA
#define WE_SCROLL_PANEL_USE_INERTIA 1
#endif

#ifndef WE_SCROLL_PANEL_USE_REBOUND
#define WE_SCROLL_PANEL_USE_REBOUND 1
#endif

#ifndef WE_SCROLL_PANEL_INERTIA_FRICTION_NUM
#define WE_SCROLL_PANEL_INERTIA_FRICTION_NUM 7
#endif

#ifndef WE_SCROLL_PANEL_INERTIA_FRICTION_DEN
#define WE_SCROLL_PANEL_INERTIA_FRICTION_DEN 8
#endif

#ifndef WE_SCROLL_PANEL_REBOUND_PULL_DIV
#define WE_SCROLL_PANEL_REBOUND_PULL_DIV 3
#endif

#ifndef WE_SCROLL_PANEL_REBOUND_MAX_STEP
#define WE_SCROLL_PANEL_REBOUND_MAX_STEP 24
#endif

#ifndef WE_SCROLL_PANEL_OVERSCROLL_LIMIT
#define WE_SCROLL_PANEL_OVERSCROLL_LIMIT 32
#endif

#ifndef WE_SCROLL_PANEL_VELOCITY_GAIN_NUM
#define WE_SCROLL_PANEL_VELOCITY_GAIN_NUM 1
#endif

#ifndef WE_SCROLL_PANEL_VELOCITY_GAIN_DEN
#define WE_SCROLL_PANEL_VELOCITY_GAIN_DEN 1
#endif

typedef struct
{
    we_obj_t base;  /* 前缀契约：与 we_child_owner_t 保持 base、children_head 顺序 */
    we_obj_t *children_head;
    we_anim_t anim; /* 中央动画引擎节点（惯性/回弹动画，不占 GUI task 槽） */
    int16_t scroll_y;
    int16_t content_h;
    uint16_t radius;
    int16_t inner_x;
    int16_t inner_y;
    int16_t inner_w;
    int16_t inner_h;
    int16_t last_touch_y;
    int16_t drag_vy;
    int16_t anim_scroll_y;
    colour_t bg_color;
    colour_t border_color;
    colour_t scrollbar_color;
    uint8_t opacity;
    uint8_t scrollbar_w;
    uint8_t scrollbar_opacity;
    uint8_t show_scrollbar : 1;
    uint8_t dragging : 1;
    uint8_t animating : 1;
} we_scroll_panel_obj_t;

void we_scroll_panel_obj_init(we_scroll_panel_obj_t *obj, we_lcd_t *lcd,
                              int16_t x, int16_t y, int16_t w, int16_t h,
                              colour_t bg_color, colour_t border_color,
                              uint16_t radius, uint8_t opacity);

void we_scroll_panel_obj_delete(we_scroll_panel_obj_t *obj);

void we_scroll_panel_add_child(we_scroll_panel_obj_t *obj, we_obj_t *child);
void we_scroll_panel_remove_child(we_scroll_panel_obj_t *obj, we_obj_t *child);
void we_scroll_panel_set_child_pos(we_scroll_panel_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y);
void we_scroll_panel_relayout(we_scroll_panel_obj_t *obj);
void we_scroll_panel_set_scroll_y(we_scroll_panel_obj_t *obj, int16_t scroll_y);
void we_scroll_panel_set_content_h(we_scroll_panel_obj_t *obj, int16_t content_h);
void we_scroll_panel_set_radius(we_scroll_panel_obj_t *obj, uint16_t radius);
void we_scroll_panel_set_colors(we_scroll_panel_obj_t *obj,
                                colour_t bg_color,
                                colour_t border_color,
                                colour_t scrollbar_color);
void we_scroll_panel_set_scrollbar(we_scroll_panel_obj_t *obj, uint8_t show_scrollbar, uint8_t scrollbar_w);
void we_scroll_panel_set_scrollbar_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity);
void we_scroll_panel_draw_only(we_scroll_panel_obj_t *obj);
void we_scroll_panel_draw_scrollbar_overlay(we_scroll_panel_obj_t *obj);
void we_scroll_panel_get_inner_rect(const we_scroll_panel_obj_t *obj,
                                    int16_t *x, int16_t *y,
                                    int16_t *w, int16_t *h);

#ifdef __cplusplus
}
#endif

#endif
