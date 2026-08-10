#ifndef __WE_WIDGET_TOAST_H
#define __WE_WIDGET_TOAST_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 轻提示横幅控件（toast）
 *
 * 顶部滑入的非模态自动消失横幅：
 *   滑入（屏外 → y=停靠位，位移+透明度同步过渡，约 200ms）
 *   → 停留 duration_ms → 滑出（回屏外并淡出）→ 隐藏。
 *
 * 实现要点：
 *   - 单个中央动画节点（we_anim_t，不占 GUI timer 槽）驱动
 *     ENTER/STAY/EXIT 三阶段状态机：Q8 进度 + we_ease_out_quad，
 *     每步同时更新 base.y 与 opacity；滑动步进的旧/新包围盒合并成
 *     一个 union 矩形单次标脏（省脏矩形合并器槽位）；
 *   - 非模态：class 的 event_cb 为 NULL，核心输入分发完全跳过本控件，
 *     横幅覆盖区域的触摸仍落到背后控件（非模态——
 *     那是单槽资源，归 dropdown 等真弹层使用）；
 *   - we_toast_show 时 we_obj_bring_to_front 置顶 Z 序；
 *     显示中再次 show：重置文本与停留计时，从当前位置/透明度平滑重入，
 *     动画进行中重复 show 不跳变；
 *   - 隐藏态 draw_cb 直接 return，包围盒停在屏外，零渲染开销。
 *
 * 文本字符串由调用方持有（控件只存 const char* 指针，不拷贝）。
 * 字体经 init 传入，可 we_toast_set_font 更换（高度随
 * 新字体行高重算）；停靠位与左右边距可 we_toast_set_margin 调整。
 * 宽度 = 屏宽 - 2*边距，高度 = 字体行高 + 2*WE_TOAST_PAD_Y，
 * 停靠顶部水平居中。超宽文本尾部自动截断并追加 "..."（零拷贝实现：
 * 前缀经 PFB 右界收窄绘制，省略号单独补画）。
 * -------------------------------------------------------------------------- */

/* 横幅左右边距默认值（像素），宽度 = 屏宽 - 2*边距；运行期可 set_margin */
#ifndef WE_TOAST_MARGIN_X
#define WE_TOAST_MARGIN_X 10
#endif

/* 停靠 Y 默认值（滑入到位后的横幅顶边）；运行期可 set_margin */
#ifndef WE_TOAST_DOCK_Y
#define WE_TOAST_DOCK_Y 8
#endif

/* 文本上下留白（像素），高度 = 行高 + 2*PAD */
#ifndef WE_TOAST_PAD_Y
#define WE_TOAST_PAD_Y 8
#endif

/* 文本左右内边距（像素）：可用文本宽 = 面板宽 - 2*PAD，超出走省略号 */
#ifndef WE_TOAST_TEXT_PAD
#define WE_TOAST_TEXT_PAD 4
#endif

/* 滑入/滑出动画时长（毫秒） */
#ifndef WE_TOAST_ANIM_MS
#define WE_TOAST_ANIM_MS 200U
#endif

/* 面板圆角半径（像素） */
#ifndef WE_TOAST_RADIUS
#define WE_TOAST_RADIUS 8U
#endif

/* show 传 duration_ms=0 时使用的默认停留时长（毫秒） */
#ifndef WE_TOAST_DEF_DURATION
#define WE_TOAST_DEF_DURATION 1500U
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_toast_obj_t
{
    we_obj_t base;             /* 必须在首位：base.x/y/w/h 为横幅面板矩形 */

    const char *text;          /* UTF-8 文本指针（调用方持有，不拷贝） */
    const unsigned char *font; /* 字库指针（init 传入，可 set_font） */
    colour_t bg_color;         /* 面板底色 */
    colour_t text_color;       /* 文字颜色 */
    uint8_t  opacity;          /* 当前不透明度（随滑入/滑出过渡） */

    int16_t  margin_top;       /* 停靠 Y（默认 WE_TOAST_DOCK_Y，可 set_margin） */
    int16_t  margin_side;      /* 左右边距（默认 WE_TOAST_MARGIN_X，可 set_margin） */

    uint8_t  state;            /* 阶段状态机（内部枚举：隐藏/滑入/停留/滑出） */
    uint16_t duration_ms;      /* 停留时长（毫秒） */
    uint16_t phase_acc;        /* 当前阶段已累计毫秒 */
    int16_t  from_y;           /* 本段位移动画起点 Y（重入时=当前 Y，不跳变） */
    uint8_t  from_opa;         /* 本段透明度动画起点 */

    we_anim_t anim;            /* 中央动画节点（归控件所有，删除前必须摘链） */
} we_toast_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化轻提示横幅并挂载到 LCD 对象链表（初始隐藏、停在屏外）。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param font 字体资源指针（必传；NULL 时不执行初始化）。
 * @return 无。
 * @note 宽度 = 屏宽 - 2*WE_TOAST_MARGIN_X，高度 = 字体行高 + 2*WE_TOAST_PAD_Y，
 *       停靠顶部；默认深灰底 + 亮白字。非模态，不拦截任何输入。
 */
void we_toast_obj_init(we_toast_obj_t *obj, we_lcd_t *lcd, const unsigned char *font);

/**
 * @brief 弹出提示：滑入 → 停留 duration_ms → 滑出自动消失。
 * @param obj 控件对象指针。
 * @param text UTF-8 提示文本（调用方持有，需在显示期间保持有效）。
 * @param duration_ms 停留时长（毫秒），0 使用 WE_TOAST_DEF_DURATION。
 * @return 无。
 * @note 显示中（含滑入/滑出动画中）再次 show 会重置文本与停留计时，
 *       并从当前位置/透明度平滑重入，不跳变；同时 Z 序置顶。
 *       超宽文本绘制时尾部截断并追加 "..."。
 */
void we_toast_show(we_toast_obj_t *obj, const char *text, uint16_t duration_ms);

/**
 * @brief 设置面板底色与文字色；两者均未变时直接返回。
 * @param obj 控件对象指针。
 * @param bg 面板底色。
 * @param text_color 文字颜色。
 * @return 无。
 */
void we_toast_set_colors(we_toast_obj_t *obj, colour_t bg, colour_t text_color);

/**
 * @brief 设置字体资源，并按新字体行高重算横幅高度。
 * @param obj 控件对象指针。
 * @param font 字体资源指针（NULL 或与当前相同直接返回）。
 * @return 无。
 * @note 隐藏态只重算几何并更新屏外停放位（零标脏）；显示中先标脏旧
 *       几何、改完再标脏新几何，立即以新字体重绘。
 */
void we_toast_set_font(we_toast_obj_t *obj, const unsigned char *font);

/**
 * @brief 设置停靠位与左右边距，并重算横幅宽度/位置。
 * @param obj 控件对象指针。
 * @param top 停靠 Y（滑入到位后的横幅顶边）。
 * @param side 左右边距（像素），宽度 = 屏宽 - 2*side。
 * @return 无。
 * @note 两者均未变时直接返回。显示中（滑入/停留）会从当前位置平滑
 *       滑到新停靠位（复用重入机制）；滑出中不干预，让其自然退场。
 */
void we_toast_set_margin(we_toast_obj_t *obj, int16_t top, int16_t side);

/**
 * @brief 删除控件：先摘除动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_toast_obj_delete(we_toast_obj_t *obj);

#endif /* __WE_WIDGET_TOAST_H */
