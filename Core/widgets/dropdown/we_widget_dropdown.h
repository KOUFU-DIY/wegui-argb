#ifndef __WE_WIDGET_DROPDOWN_H
#define __WE_WIDGET_DROPDOWN_H

#include "we_gui_driver.h"

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 闭合态：OK 按下沿主框进入按压态，松开沿展开列表；
 * 展开态经弹层键通道导航：上/下（或前/后）移动高亮行并滚动跟随、
 * OK 选中当前行并收起、BACK 直接收起。
 * 置 0 单独裁剪本控件的按键支持，其余控件不受影响。 */
#ifndef WE_DROPDOWN_USE_KEY
#define WE_DROPDOWN_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 下拉选择控件（dropdown）
 *
 * 数据驱动的轻量下拉框，适合 MCU：
 *   - 闭合态只显示当前选中项 + 箭头；
 *   - 展开态借助 LCD 级 overlay popup 绘制选项列表，
 *     不会被 group / scroll_panel / slideshow 等父容器裁剪；
 *   - 同一时刻全屏只允许一个 popup（由 driver 的 popup_layer 保证）。
 *
 * 选项数组由调用者持有（通常是 const 静态数组），控件只保存指针，
 * 不复制文本，省 RAM。
 * -------------------------------------------------------------------------- */

/* 默认可见选项数（超出部分用 scroll_px 无级滚动） */
#ifndef WE_DROPDOWN_DEF_MAX_VISIBLE
#define WE_DROPDOWN_DEF_MAX_VISIBLE 4
#endif

/* 滚动条自动淡出：停止滚动后保持完全显示的时长（毫秒） */
#ifndef WE_DROPDOWN_SB_HOLD_MS
#define WE_DROPDOWN_SB_HOLD_MS 600U
#endif

/* 滚动条自动淡出：从完全显示淡出到完全透明的时长（毫秒） */
#ifndef WE_DROPDOWN_SB_FADE_MS
#define WE_DROPDOWN_SB_FADE_MS 400U
#endif

/* 滚动条空闲时淡出到的最低透明度（0~255），>0 表示常驻可见、不完全消失 */
#ifndef WE_DROPDOWN_SB_IDLE_ALPHA
#define WE_DROPDOWN_SB_IDLE_ALPHA 40U
#endif

/* 单个选项 */
/* 展开列表越界过冲上限（像素）：拖拽最多超出边界的距离（list 同款橡皮筋） */
#ifndef WE_DROPDOWN_OVERSCROLL_LIMIT
#define WE_DROPDOWN_OVERSCROLL_LIMIT 24
#endif

/* 回弹拉力：每步回弹 = 过冲 / PULL_DIV（下限 1px），对齐 list/scroll_panel */
#ifndef WE_DROPDOWN_REBOUND_PULL_DIV
#define WE_DROPDOWN_REBOUND_PULL_DIV 3
#endif

/* 回弹单步上限（像素） */
#ifndef WE_DROPDOWN_REBOUND_MAX_STEP
#define WE_DROPDOWN_REBOUND_MAX_STEP 24
#endif

typedef struct
{
    const char *text;  /* 选项显示文本（UTF-8） */
    int32_t value;     /* 选项关联值，由调用者定义含义 */
    uint8_t disabled;  /* 非 0 表示禁用，不可被选择 */
} we_dropdown_option_t;

struct we_dropdown_obj_t;

/* 选中项改变回调 */
typedef void (*we_dropdown_changed_cb_t)(struct we_dropdown_obj_t *obj,
                                         int16_t selected_idx,
                                         int32_t value);

typedef struct we_dropdown_obj_t
{
    we_obj_t base;

    /* 4 字节对齐成员（指针/int32/动画节点）在前，消 padding */
    const we_dropdown_option_t *options;
    const unsigned char *font;
    we_dropdown_changed_cb_t changed_cb;
    int32_t scroll_px;          /* popup 内容向上滚动的像素偏移（无级，0=顶部对齐） */
    int32_t drag_start_scroll;  /* 按下时的 scroll_px */
    we_anim_t sb_anim;          /* 淡出动画节点（不占 GUI task 槽，收敛即摘链） */
    we_anim_t rb_anim;          /* 回弹动画节点（收敛即摘链，list 同款口径） */

    /* 2 字节成员 */
    uint16_t option_cnt;
    int16_t selected_idx;     /* 当前选中项，-1 表示未选 */
    int16_t hover_idx;        /* popup 中当前按下高亮项，-1 表示无 */
    uint16_t item_h;          /* 单项高度（含主框/列表项） */
    uint16_t radius;
    int16_t drag_start_y;     /* 按下时的 Y 坐标 */
    uint16_t sb_idle_ms;      /* 自上次滚动以来累计的空闲毫秒 */

    /* 1 字节成员与状态位域 */
    uint8_t max_visible_items;
    uint8_t sb_alpha;         /* 滚动条当前透明度（0~255），0=完全隐藏 */
    uint8_t opened : 1;       /* 是否已展开 */
    uint8_t pressed : 1;      /* 主框是否处于按下态 */
    uint8_t enabled : 1;      /* 是否可交互 */
    uint8_t dragging : 1;     /* 本次触摸是否已判定为拖拽滚动 */
    uint8_t rebounding : 1;   /* 回弹动画进行中标志 */
} we_dropdown_obj_t;

/**
 * @brief 初始化下拉控件并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标。
 * @param y 左上角 Y 坐标。
 * @param w 主框宽度（像素）。
 * @param h 主框高度（像素），同时作为列表项默认高度。
 * @param font 字体资源指针。
 * @return 无。
 */
void we_dropdown_obj_init(we_dropdown_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                          const unsigned char *font);

/**
 * @brief 绑定选项数组（控件只保存指针，不复制内容）。
 * @param obj 目标控件对象指针。
 * @param options 选项数组指针，需在控件生命周期内保持有效。
 * @param option_cnt 选项个数。
 * @return 无。
 */
void we_dropdown_set_options(we_dropdown_obj_t *obj,
                             const we_dropdown_option_t *options,
                             uint16_t option_cnt);

/**
 * @brief 设置当前选中项并刷新主框显示。
 * @param obj 目标控件对象指针。
 * @param index 选中项索引，越界或指向禁用项时忽略。
 * @return 无。
 */
void we_dropdown_set_selected(we_dropdown_obj_t *obj, int16_t index);

/**
 * @brief 获取当前选中项索引。
 * @param obj 目标控件对象指针。
 * @return 选中项索引，未选时为 -1。
 */
int16_t we_dropdown_get_selected(const we_dropdown_obj_t *obj);

/**
 * @brief 获取当前选中项关联值。
 * @param obj 目标控件对象指针。
 * @return 选中项 value，未选时为 0。
 */
int32_t we_dropdown_get_value(const we_dropdown_obj_t *obj);

/**
 * @brief 设置选中项改变回调。
 * @param obj 目标控件对象指针。
 * @param cb 回调函数指针。
 * @return 无。
 */
void we_dropdown_set_changed_cb(we_dropdown_obj_t *obj, we_dropdown_changed_cb_t cb);

/**
 * @brief 展开下拉列表（占用唯一 popup slot）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_dropdown_open(we_dropdown_obj_t *obj);

/**
 * @brief 收起下拉列表。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_dropdown_close(we_dropdown_obj_t *obj);

/**
 * @brief 切换下拉列表展开/收起状态。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_dropdown_toggle(we_dropdown_obj_t *obj);

/**
 * @brief 设置 popup 最多同时可见的选项数量。
 * @param obj 目标控件对象指针。
 * @param count 可见项数量（至少 1）。
 * @return 无。
 */
void we_dropdown_set_max_visible_items(we_dropdown_obj_t *obj, uint8_t count);

/**
 * @brief 设置列表项高度。
 * @param obj 目标控件对象指针。
 * @param item_h 单项高度（像素）。
 * @return 无。
 */
void we_dropdown_set_item_height(we_dropdown_obj_t *obj, uint16_t item_h);

/**
 * @brief 删除下拉控件（如展开则先关闭 popup）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_dropdown_obj_delete(we_dropdown_obj_t *obj);

#endif
