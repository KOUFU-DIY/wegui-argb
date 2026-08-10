#ifndef __WE_WIDGET_LIST_H
#define __WE_WIDGET_LIST_H

#include "we_gui_driver.h"
#include "we_scroll.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪列表的按键回调与可聚焦性，其余控件不受影响。
 * 键控选行依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_LIST_USE_KEY
#define WE_LIST_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 数据驱动列表（list）
 *
 * 垂直菜单列表：背景面板（圆角可选）+ 逐行左对齐文字 + 行底 1px 低透明度
 * 分隔线 + 按压行高亮背景；内容超出控件高度时右缘细滚动条
 * （胶囊滑块，位置按滚动比例，dropdown 同款空闲淡出：活动期全显，
 * 空闲 WE_LIST_SB_HOLD_MS 后经中央动画渐隐到常驻最低透明度）。
 *
 * 数据驱动：条目字符串数组由调用方持有（通常是 static const 数组），
 * 控件只保存 const 指针，绝不拷贝文本。
 *
 * 滚动模型：scroll_px 为像素级累计偏移（int32），常态范围
 * [0, 内容总高 - 控件高]；拖拽/惯性允许越界过冲至多
 * WE_LIST_OVERSCROLL_LIMIT 像素，松手后经回弹动画（过冲/3 每步拉回，
 * 参数对齐 scroll_panel 的 REBOUND 风格）收敛到边界。
 * 行高默认 = 字体行高 + 2 * WE_LIST_ROW_PAD，可用 we_list_set_row_height
 * 调整；we_list_set_font 换字体时按新字体行高重推导。
 *
 * 交互：
 *   - PRESSED 记录命中行 + 起点 Y，命中行进入按压高亮（只标脏该行条带）；
 *   - STAY 位移超 WE_LIST_DRAG_THRESHOLD 进入拖拽滚动（取消行按压态）；
 *   - RELEASED 未拖拽且释放点仍在同一行 → 触发 clicked 回调；
 *   - 拖拽松手带惯性：速度每步衰减 7/8，经中央动画引擎推进；
 *   - 快速轻扫（无 STAY，内核 SWIPE_UP/DOWN）按"总位移 / 固定时间片"
 *     估算初速度注入同一惯性动画，快扫与拖拽手感统一。
 *
 * 标脏粒度：
 *   - 行按压/释放高亮只标脏该行条带（与面板矩形求交，半露行不外扩）；
 *   - 滚动位移标脏内容裁剪矩形（= 面板矩形，不越过面板边界）；
 *   - 滚动条淡出只标脏右缘滚动条条带。
 *
 * 行内容经 PFB 窗口收窄裁剪在控件矩形内（scroll_panel 同款
 * save/restore 套路），半露行不会渗出控件边界。首/末行按压高亮在
 * 面板圆角带内改用同心半径（面板半径 - 内缩），不溢出直角。
 *
 * 零 malloc、渲染内环零浮点。删除前必须 we_list_obj_delete
 * （内部先摘除惯性与滚动条淡出两个动画节点再 we_obj_delete）。
 * -------------------------------------------------------------------------- */

/* 行内上下边距（像素）：默认行高 = 字体行高 + 2 * PAD */
#ifndef WE_LIST_ROW_PAD
#define WE_LIST_ROW_PAD 7U
#endif

/* 行文字左内边距（像素），分隔线两端同步内缩 */
#ifndef WE_LIST_TEXT_PAD
#define WE_LIST_TEXT_PAD 10
#endif

/* 面板背景默认圆角半径（像素，0 = 直角） */
#ifndef WE_LIST_DEF_RADIUS
#define WE_LIST_DEF_RADIUS 10U
#endif

/* 判定为拖拽滚动（而非行点击）的位移阈值（像素） */
#ifndef WE_LIST_DRAG_THRESHOLD
#define WE_LIST_DRAG_THRESHOLD 6
#endif

/* 惯性速度衰减：每步 v = v * NUM / DEN（默认 7/8） */
#ifndef WE_LIST_INERTIA_NUM
#define WE_LIST_INERTIA_NUM 7
#endif
#ifndef WE_LIST_INERTIA_DEN
#define WE_LIST_INERTIA_DEN 8
#endif

/* 快速轻扫（无 STAY 的 SWIPE）测速时间片（毫秒）：
 * 初速度(像素/16ms) = 总位移 * 16 / 时间片，即假定整段快扫约耗时该时长 */
#ifndef WE_LIST_SWIPE_SLICE_MS
#define WE_LIST_SWIPE_SLICE_MS 128
#endif

/* 越界过冲上限（像素）：拖拽/惯性最多超出边界的距离 */
#ifndef WE_LIST_OVERSCROLL_LIMIT
#define WE_LIST_OVERSCROLL_LIMIT 24
#endif

/* 回弹拉力：每步回弹 = 过冲 / PULL_DIV（下限 1px），对齐 scroll_panel */
#ifndef WE_LIST_REBOUND_PULL_DIV
#define WE_LIST_REBOUND_PULL_DIV 3
#endif

/* 回弹单步上限（像素），对齐 scroll_panel */
#ifndef WE_LIST_REBOUND_MAX_STEP
#define WE_LIST_REBOUND_MAX_STEP 24
#endif

/* 分隔线透明度（0~255，低透明度淡线） */
#ifndef WE_LIST_SEP_OPA
#define WE_LIST_SEP_OPA 46U
#endif

/* 按压高亮条相对面板的内缩量（像素）与默认圆角半径 */
#ifndef WE_LIST_PRESS_INSET
#define WE_LIST_PRESS_INSET 2
#endif
#ifndef WE_LIST_PRESS_RADIUS
#define WE_LIST_PRESS_RADIUS 6U
#endif

/* 滚动条几何：滑块宽 / 距右缘边距 / 活动期峰值透明度 */
#ifndef WE_LIST_SB_WIDTH
#define WE_LIST_SB_WIDTH 4
#endif
#ifndef WE_LIST_SB_MARGIN
#define WE_LIST_SB_MARGIN 3
#endif
#ifndef WE_LIST_SB_OPA
#define WE_LIST_SB_OPA 255U
#endif

/* 滚动条空闲淡出（参数命名风格对齐 dropdown 的 WE_DROPDOWN_SB_*）：
 * 停止滚动后保持峰值 HOLD_MS，再按 FADE_MS 时长线性渐隐，
 * 收敛到常驻最低透明度 IDLE_ALPHA（>0 = 常驻可见，不完全消失）。 */
#ifndef WE_LIST_SB_HOLD_MS
#define WE_LIST_SB_HOLD_MS 600U
#endif
#ifndef WE_LIST_SB_FADE_MS
#define WE_LIST_SB_FADE_MS 400U
#endif
#ifndef WE_LIST_SB_IDLE_ALPHA
#define WE_LIST_SB_IDLE_ALPHA 80U
#endif

/**
 * @brief 行点击回调（未拖拽、按压与释放落在同一行时触发）。
 * @param list 列表控件对象指针（we_list_obj_t *，以 void * 透传）。
 * @param idx 被点击行的条目索引。
 * @return 无。
 */
typedef void (*we_list_clicked_cb_t)(void *list, uint16_t idx);

typedef struct we_list_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h 为控件外接矩形 */

    /* 4 字节对齐成员（指针/int32/动画节点）在前，消 padding */
    const char *const *items;   /* 条目字符串数组（调用方持有，只存指针） */
    const unsigned char *font;  /* 字体资源（init 传入，可 set_font） */
    we_list_clicked_cb_t clicked_cb; /* 行点击回调（可为 NULL） */
    we_scroll_t sc;             /* 滚动物理状态机（拖拽/惯性/回弹；sc.pos 即滚动偏移） */
    we_anim_t anim;             /* 惯性/回弹动画节点（归控件所有，删除前必须摘链） */
    we_anim_t sb_anim;          /* 滚动条淡出动画节点（归控件所有，删除前必须摘链） */

    /* 2 字节成员 */
    uint16_t item_cnt;          /* 条目个数 */
    uint16_t row_h;             /* 行高（像素） */
    uint16_t radius;            /* 面板圆角半径（0 = 直角） */
    int16_t pressed_row;        /* 当前按压高亮行索引，-1 = 无 */
    uint16_t sb_idle_ms;        /* 自上次滚动以来累计的空闲毫秒 */
    colour_t bg_color;          /* 面板背景色 */
    colour_t text_color;        /* 行文字色 */
    colour_t sep_color;         /* 分隔线颜色（以低透明度绘制） */
    colour_t press_color;       /* 按压行高亮背景色 */
    colour_t sb_color;          /* 滚动条滑块色 */

    /* 1 字节成员与状态位域 */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */
    uint8_t sb_alpha;           /* 滚动条当前透明度（0~255），0 = 完全隐藏 */
} we_list_obj_t;

/**
 * @brief 初始化列表控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param h 控件高度（像素）。
 * @param font 字体资源指针（必传；NULL 时不执行初始化）。
 * @return 无。
 * @note 行高 = 字体行高 + 2*WE_LIST_ROW_PAD、圆角 WE_LIST_DEF_RADIUS、
 *       深色主题配色、初始无条目（需再调 we_list_set_options）。
 */
void we_list_obj_init(we_list_obj_t *obj, we_lcd_t *lcd,
                      int16_t x, int16_t y, int16_t w, int16_t h,
                      const unsigned char *font);

/**
 * @brief 绑定条目字符串数组（控件只保存指针，不复制内容）。
 * @param obj 控件对象指针。
 * @param items 字符串指针数组，需在控件生命周期内保持有效。
 * @param count 条目个数。
 * @return 无。
 * @note 绑定后滚动复位到顶部并清除按压态；内容溢出时滚动条会短暂
 *       全显提示"此处可滚动"，随后自动渐隐；数组指针与个数均未变时直接返回。
 */
void we_list_set_options(we_list_obj_t *obj,
                         const char *const *items, uint16_t count);

/**
 * @brief 设置行点击回调（未拖拽、按压与释放同行时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_list_set_clicked_cb(we_list_obj_t *obj, we_list_clicked_cb_t cb);

/**
 * @brief 设置行高（像素）。
 * @param obj 控件对象指针。
 * @param row_h 新行高（0 时忽略；值未变直接返回）。
 * @return 无。
 * @note 修改后滚动偏移会重新夹紧到新内容高度范围内。
 */
void we_list_set_row_height(we_list_obj_t *obj, uint16_t row_h);

/**
 * @brief 设置字体资源，并按新字体行高重推导默认行高。
 * @param obj 控件对象指针。
 * @param font 字体资源指针（NULL 或与当前相同直接返回）。
 * @return 无。
 * @note 行高恢复为"新字体行高 + 2*WE_LIST_ROW_PAD"（覆盖之前的
 *       we_list_set_row_height 结果），内容总高随之重算，滚动偏移
 *       夹紧到新范围，整控件标脏重绘。
 */
void we_list_set_font(we_list_obj_t *obj, const unsigned char *font);

/**
 * @brief 设置面板圆角半径（0 = 直角）。
 * @param obj 控件对象指针。
 * @param radius 圆角半径（像素）。
 * @return 无。
 */
void we_list_set_radius(we_list_obj_t *obj, uint16_t radius);

/**
 * @brief 设置滚动偏移（像素，硬夹紧到有效范围，无动画）。
 * @param obj 控件对象指针。
 * @param scroll_px 目标滚动偏移。
 * @return 无。
 */
void we_list_set_scroll(we_list_obj_t *obj, int32_t scroll_px);

/**
 * @brief 删除列表控件：先摘除惯性与滚动条淡出动画节点（we_anim_stop）再摘链。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_list_obj_delete(we_list_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_LIST_H */
