#ifndef WE_WIDGET_SCROLL_PANEL_H
#define WE_WIDGET_SCROLL_PANEL_H

#include "wegui.h"

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

/* --------------------------------------------------------------------------
 * 竖向滚动面板（scroll_panel）
 *
 * 1px 边框 + 内容区背景（radius > 0 时外壳为圆角）+ 可选右缘滚动条。
 * 子控件经 we_scroll_panel_add_child 挂入，局部坐标以面板外框左上角为原点
 * （we_scroll_panel_set_child_pos），滚动时整体平移；内容区（边框以内）之外
 * 的部分由内核裁掉，也不参与命中。圆角只作用于外壳，子控件区仍是矩形。
 *
 * 交互：空白处按下后位移越过 WE_SCROLL_PANEL_DRAG_THRESHOLD 开始拖动；
 * 子控件上按下拖动时由内核拖拽接管交给面板。松手带速度进入惯性滑行
 * （每步 ×FRICTION_NUM/DEN），越界至多 OVERSCROLL_LIMIT 后回弹
 * （每步拉回 过冲/PULL_DIV，1..MAX_STEP）；无 STAY 的快速轻扫整体翻
 * WE_CFG_SWIPE_THRESHOLD 像素。惯性/回弹经中央动画引擎单节点推进。
 * -------------------------------------------------------------------------- */

/* 滚动条默认宽度（像素） */
#ifndef WE_SCROLL_PANEL_SCROLLBAR_W
#define WE_SCROLL_PANEL_SCROLLBAR_W 4U
#endif

/* 空白处按下后开始拖动的位移阈值（像素） */
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
    we_anim_t anim; /* 惯性/回弹动画节点（删除控件时由内核摘链） */
    int16_t scroll_y;      /* 当前滚动量（像素，过冲期间可为负或超过最大值） */
    int16_t content_h;     /* 内容总高（不小于内容区高） */
    uint16_t radius;       /* 外壳圆角半径（0 = 直角） */
    int16_t last_touch_y;  /* 上一次触摸 Y（拖动增量用） */
    int16_t drag_vy;       /* 惯性速度（像素 / 16ms） */
    int16_t anim_scroll_y; /* 惯性/回弹动画推进中的滚动量 */
    colour_t bg_color;
    colour_t border_color;
    colour_t scrollbar_color;
    uint8_t opacity;
    uint8_t scrollbar_w;
    uint8_t scrollbar_opacity;
    uint8_t show_scrollbar : 1;
    uint8_t dragging : 1;
} we_scroll_panel_obj_t;

/**
 * @brief 初始化滚动面板并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x/y/w/h 面板外框（屏幕绝对坐标）。
 * @param bg_color 内容区背景色。
 * @param border_color 1px 边框色（滚动条默认同色）。
 * @param radius 外壳圆角半径（0 = 直角；子控件区仍是矩形）。
 * @param opacity 整体不透明度（0~255，级联到子控件）。
 * @return 无。
 * @note 内容高默认等于内容区高（不可滚动），用 we_scroll_panel_set_content_h 设置。
 */
void we_scroll_panel_obj_init(we_scroll_panel_obj_t *obj, we_lcd_t *lcd,
                              int16_t x, int16_t y, int16_t w, int16_t h,
                              colour_t bg_color, colour_t border_color,
                              uint16_t radius, uint8_t opacity);

/**
 * @brief 删除面板：子控件一并删除，动画节点由内核摘链。
 * @param obj 控件对象指针。
 * @return 无。
 */
static inline void we_scroll_panel_obj_delete(we_scroll_panel_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

/**
 * @brief 把控件挂进面板（落在内容局部 (0,0)，随后用 set_child_pos 定位）。
 * @param obj 面板对象指针。
 * @param child 子控件对象指针（自挂载 / 跨 lcd / 重复挂载会被忽略）。
 * @return 无。
 */
void we_scroll_panel_add_child(we_scroll_panel_obj_t *obj, we_obj_t *child);

/**
 * @brief 从面板移除子控件：擦掉它在屏上的像素并摘链（绝对坐标不变）。
 * @param obj 面板对象指针。
 * @param child 子控件对象指针。
 * @return 无。
 * @note 摘下的控件不再绘制，要继续显示需由调用方重新挂回。
 */
void we_scroll_panel_remove_child(we_scroll_panel_obj_t *obj, we_obj_t *child);

/**
 * @brief 设置子控件的内容局部坐标（以面板外框左上角为原点，不含滚动量）。
 * @param obj 面板对象指针。
 * @param child 子控件对象指针。
 * @param local_x 局部 X。
 * @param local_y 局部 Y（内容坐标，滚动时自动平移）。
 * @return 无。
 */
void we_scroll_panel_set_child_pos(we_scroll_panel_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y);

/**
 * @brief 整体标脏重绘（子控件绝对坐标即唯一事实源，无需重新排布）。
 * @param obj 面板对象指针。
 * @return 无。
 */
void we_scroll_panel_relayout(we_scroll_panel_obj_t *obj);

/**
 * @brief 设置滚动量（硬夹紧到 [0, 最大滚动量]，无动画）。
 * @param obj 面板对象指针。
 * @param scroll_y 目标滚动量（像素）。
 * @return 无。
 */
void we_scroll_panel_set_scroll_y(we_scroll_panel_obj_t *obj, int16_t scroll_y);

/**
 * @brief 设置内容总高（不小于内容区高），滚动量随新范围夹紧，子控件同步平移。
 * @param obj 面板对象指针。
 * @param content_h 内容总高（像素）。
 * @return 无。
 */
void we_scroll_panel_set_content_h(we_scroll_panel_obj_t *obj, int16_t content_h);

/**
 * @brief 设置外壳圆角半径（0 = 直角）。
 * @param obj 面板对象指针。
 * @param radius 圆角半径（像素）。
 * @return 无。
 */
void we_scroll_panel_set_radius(we_scroll_panel_obj_t *obj, uint16_t radius);

/**
 * @brief 设置背景 / 边框 / 滚动条颜色。
 * @param obj 面板对象指针。
 * @param bg_color 内容区背景色。
 * @param border_color 边框色。
 * @param scrollbar_color 滚动条色。
 * @return 无。
 */
void we_scroll_panel_set_colors(we_scroll_panel_obj_t *obj,
                                colour_t bg_color,
                                colour_t border_color,
                                colour_t scrollbar_color);

/**
 * @brief 开关右缘滚动条并设置宽度（内容溢出时才显示）。
 * @param obj 面板对象指针。
 * @param show_scrollbar 非 0 显示。
 * @param scrollbar_w 滚动条宽（0 = 默认 WE_SCROLL_PANEL_SCROLLBAR_W）。
 * @return 无。
 */
void we_scroll_panel_set_scrollbar(we_scroll_panel_obj_t *obj, uint8_t show_scrollbar, uint8_t scrollbar_w);

/**
 * @brief 设置滚动条透明度（再乘面板整体透明度）。
 * @param obj 面板对象指针。
 * @param opacity 滚动条透明度（0~255）。
 * @return 无。
 */
void we_scroll_panel_set_scrollbar_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity);

/**
 * @brief 设置整体透明度（级联到子控件与滚动条；0 时整棵子树不画不命中）。
 * @param obj 面板对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_scroll_panel_set_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity);

/**
 * @brief 读取内容区（边框以内）矩形。
 * @param obj 面板对象指针。
 * @param x/y/w/h 传出（可为 NULL）。
 * @return 无。
 */
void we_scroll_panel_get_inner_rect(const we_scroll_panel_obj_t *obj,
                                    int16_t *x, int16_t *y,
                                    int16_t *w, int16_t *h);

#ifdef __cplusplus
}
#endif

#endif
