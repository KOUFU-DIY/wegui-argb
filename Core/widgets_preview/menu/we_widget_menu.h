#ifndef __WE_WIDGET_MENU_H
#define __WE_WIDGET_MENU_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪菜单的按键回调与可聚焦性，其余控件不受影响。
 * 键控行巡航依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_MENU_USE_KEY
#define WE_MENU_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 多级菜单（menu）—— preview 孵化区实验控件
 *
 * 结构：顶部标题栏（当前页 title 居中 + 非根页左侧返回箭头 "<"）+ 下方
 * 可滚动行区。行区渲染借鉴 list 控件：左对齐行文字 + 行底 1px 低透明度
 * 分隔线 + 按压行高亮；带子页的行右侧画 ">" 提示箭头；内容溢出时右缘
 * 常显细滚动条。行区经 PFB 窗口收窄裁剪（scroll_panel/list 同款
 * save/restore 套路），半露行与过渡中的页面不会渗出行区边界。
 *
 * 数据驱动：菜单树（we_menu_page_t / we_menu_item_t）全部由调用方以
 * static const 持有，控件只保存 const 指针，绝不拷贝。
 *
 * 导航模型：固定深度页面栈（WE_MENU_STACK_MAX，默认 6）。
 *   - 点击带 submenu 的行 → 入栈进子页（子页滚动从 0 开始）；
 *   - 点击标题栏返回箭头 / 行区快速右滑（WE_EVENT_SWIPE_RIGHT）/
 *     行区水平右拖松手 → 出栈返回上一级（根页时无操作）；
 *   - 每个栈帧记住本页滚动位置，返回时自动恢复。
 *   - 按键（WE_MENU_USE_KEY，依赖编辑态）：OK 进编辑态并落高亮行
 *     （复用 pressed_row），编辑态上下键巡航行 + 滚动跟随，OK 激活行
 *     （子页入栈 / 叶子 action_cb），BACK 逐级出栈、根页 BACK 退出编辑态。
 * 页面切换带 200ms 水平滑入过渡：旧页与新页行内容整体 X 偏移经中央
 * 动画节点 + we_ease_out_quad + we_lerp 推进（进入子页新页从右滑入，
 * 返回时从左滑入）。过渡期间行区不响应新按压。
 *
 * 滚动模型与 list 一致：像素级 scroll_px（int32），拖拽/惯性允许越界
 * 过冲至多 WE_MENU_OVERSCROLL_LIMIT 像素（橡皮筋），松手后经同一中央
 * 动画节点回弹到边界（每步拉回 过冲/WE_MENU_REBOUND_PULL_DIV，整数缓动
 * 先快后慢）；拖拽跟手，松手带简化惯性（速度每步衰减 7/8，越界段
 * 额外减半、尽快交棒给回弹）。
 *
 * 零 malloc、渲染内环零浮点。删除前必须 we_menu_obj_delete
 * （内部先停惯性/过渡两个动画节点再 we_obj_delete）。
 *
 * preview 限制：标脏按整控件包围盒；标题栏/行高亮不精确贴合面板
 * 大圆角；滚动条常显、过渡期间隐藏。
 * -------------------------------------------------------------------------- */

/* 页面栈最大深度（含根页） */
#ifndef WE_MENU_STACK_MAX
#define WE_MENU_STACK_MAX 6U
#endif

/* 页面切换水平滑入过渡时长（毫秒，0 = 关闭过渡直切） */
#ifndef WE_MENU_TRANS_MS
#define WE_MENU_TRANS_MS 200U
#endif

/* 行内上下边距（像素）：默认行高 = 字体行高 + 2 * PAD */
#ifndef WE_MENU_ROW_PAD
#define WE_MENU_ROW_PAD 7U
#endif

/* 标题栏上下边距（像素）：标题栏高 = 字体行高 + 2 * PAD */
#ifndef WE_MENU_TITLE_PAD
#define WE_MENU_TITLE_PAD 8U
#endif

/* 行文字左内边距（像素），分隔线两端同步内缩 */
#ifndef WE_MENU_TEXT_PAD
#define WE_MENU_TEXT_PAD 10
#endif

/* 面板背景默认圆角半径（像素，0 = 直角） */
#ifndef WE_MENU_DEF_RADIUS
#define WE_MENU_DEF_RADIUS 10U
#endif

/* 标题栏返回箭头点击热区宽度（像素，从控件左缘起算） */
#ifndef WE_MENU_BACK_ZONE_W
#define WE_MENU_BACK_ZONE_W 44
#endif

/* 判定为拖拽滚动（而非行点击）的垂直位移阈值（像素） */
#ifndef WE_MENU_DRAG_THRESHOLD
#define WE_MENU_DRAG_THRESHOLD 6
#endif

/* 惯性速度衰减：每步 v = v * NUM / DEN（默认 7/8） */
#ifndef WE_MENU_INERTIA_NUM
#define WE_MENU_INERTIA_NUM 7
#endif
#ifndef WE_MENU_INERTIA_DEN
#define WE_MENU_INERTIA_DEN 8
#endif

/* 越界过冲上限（像素）：拖拽/惯性最多超出边界的距离（list 同款橡皮筋） */
#ifndef WE_MENU_OVERSCROLL_LIMIT
#define WE_MENU_OVERSCROLL_LIMIT 24
#endif

/* 回弹拉力：每步回弹 = 过冲 / PULL_DIV（下限 1px），对齐 list/scroll_panel */
#ifndef WE_MENU_REBOUND_PULL_DIV
#define WE_MENU_REBOUND_PULL_DIV 3
#endif

/* 回弹单步上限（像素） */
#ifndef WE_MENU_REBOUND_MAX_STEP
#define WE_MENU_REBOUND_MAX_STEP 24
#endif

/* 快速纵向轻扫（SWIPE_UP/DOWN，无 STAY）的惯性初速换算与上限 */
#ifndef WE_MENU_KICK_DIV
#define WE_MENU_KICK_DIV 4
#endif
#ifndef WE_MENU_KICK_MAX
#define WE_MENU_KICK_MAX 40
#endif

/* 分隔线透明度（0~255，低透明度淡线） */
#ifndef WE_MENU_SEP_OPA
#define WE_MENU_SEP_OPA 46U
#endif

/* 滚动条几何：滑块宽 / 距右缘边距 / 透明度 */
#ifndef WE_MENU_SB_WIDTH
#define WE_MENU_SB_WIDTH 4
#endif
#ifndef WE_MENU_SB_MARGIN
#define WE_MENU_SB_MARGIN 3
#endif
#ifndef WE_MENU_SB_OPA
#define WE_MENU_SB_OPA 120U
#endif

/* ------------------------------ 数据模型 ------------------------------ */

typedef struct we_menu_page_s we_menu_page_t;

/**
 * @brief 菜单行条目（调用方以 static const 持有）。
 */
typedef struct
{
    const char *label;             /* 行文本 */
    const we_menu_page_t *submenu; /* 非 NULL = 点击进入该子页 */
    uint16_t action_id;            /* submenu==NULL 时点击回调用的动作 ID */
} we_menu_item_t;

/**
 * @brief 菜单页（调用方以 static const 持有）。
 */
struct we_menu_page_s
{
    const char *title;             /* 页标题（标题栏显示） */
    const we_menu_item_t *items;   /* 行数组（调用方持有） */
    uint16_t count;                /* 行数 */
};

/**
 * @brief 叶子行点击回调（点中 submenu==NULL 的行时触发）。
 * @param menu 菜单控件对象指针（we_menu_obj_t *，以 void * 透传）。
 * @param action_id 被点行的动作 ID。
 * @param item 被点行条目指针（可读取 label 等字段）。
 * @return 无。
 */
typedef void (*we_menu_action_cb_t)(void *menu, uint16_t action_id,
                                    const we_menu_item_t *item);

/**
 * @brief 页面栈帧：页指针 + 该页记住的滚动位置。
 */
typedef struct
{
    const we_menu_page_t *page; /* 该层页面（调用方持有） */
    int32_t scroll_px;          /* 该层滚动位置（像素） */
} we_menu_frame_t;

typedef struct we_menu_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h 为控件外接矩形 */

    /* 4 字节对齐成员（指针/int32/动画节点）在前，消 padding */
    const we_menu_page_t *root; /* 根页（调用方持有，只存指针） */
    we_menu_frame_t stack[WE_MENU_STACK_MAX]; /* 页面栈，stack[0] = 根页 */
    const unsigned char *font;  /* 字体资源（init 必传） */
    we_menu_action_cb_t action_cb; /* 叶子行动作回调（可为 NULL） */
    int32_t press_scroll;       /* 按下时当前页 scroll_px */
    we_anim_t anim;             /* 惯性节点（归控件所有，删除前必须摘链） */
    we_anim_t trans_anim;       /* 过渡节点（归控件所有，删除前必须摘链） */
    const we_menu_page_t *trans_prev_page; /* 过渡期间滑出的旧页 */
    int32_t trans_prev_scroll;  /* 旧页滑出时的滚动位置 */

    /* 2 字节成员 */
    uint16_t row_h;             /* 行高（像素） */
    uint16_t title_h;           /* 标题栏高（像素） */
    uint16_t radius;            /* 面板圆角半径（0 = 直角） */
    int16_t pressed_row;        /* 当前按压高亮行索引，-1 = 无 */
    int16_t press_x;            /* 按下时触摸 X */
    int16_t press_y;            /* 按下时触摸 Y */
    int16_t last_y;             /* 上一次 STAY 的触摸 Y（测速用） */
    int16_t vel;                /* 惯性速度（像素 / 16ms，带符号） */
    uint16_t trans_t;           /* 过渡已累计毫秒 */
    int16_t trans_from;         /* 新页起始 X 偏移（+w 进子页 / -w 返回） */
    colour_t bg_color;          /* 行区面板背景色 */
    colour_t title_bg_color;    /* 标题栏背景色 */
    colour_t title_text_color;  /* 标题文字色 */
    colour_t text_color;        /* 行文字色 */
    colour_t sep_color;         /* 分隔线颜色（以低透明度绘制） */
    colour_t press_color;       /* 按压高亮背景色 */
    colour_t arrow_color;       /* 返回箭头 / 子页提示箭头颜色 */
    colour_t sb_color;          /* 滚动条滑块色 */

    /* 1 字节成员与状态位域 */
    uint8_t depth;              /* 当前栈深（1 = 根页） */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */
    uint8_t pressed_back : 1;   /* 返回箭头按压中标志 */
    uint8_t press_in_rows : 1;  /* 本次按压起点是否落在行区 */
    uint8_t tracking : 1;       /* 本次触摸序列是否有效 */
    uint8_t dragging : 1;       /* 是否已进入拖拽滚动 */
    uint8_t inertia_animating : 1; /* 惯性动画进行中标志 */
    uint8_t transitioning : 1;  /* 过渡进行中标志 */
} we_menu_obj_t;

/* ------------------------------ 公共 API ------------------------------ */

/**
 * @brief 初始化多级菜单控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param h 控件高度（像素）。
 * @param root 根页指针（调用方以 static const 持有，控件只存指针）。
 * @return 无。
 * @note 字体 init 必传、行高 = 字体行高 + 2*WE_MENU_ROW_PAD、
 *       标题栏高 = 字体行高 + 2*WE_MENU_TITLE_PAD、圆角 WE_MENU_DEF_RADIUS、
 *       深色主题配色；初始位于根页。
 */
void we_menu_obj_init(we_menu_obj_t *obj, we_lcd_t *lcd,
                      int16_t x, int16_t y, int16_t w, int16_t h,
                      const we_menu_page_t *root,
                        const unsigned char *font);

/**
 * @brief 设置叶子行动作回调（点中 submenu==NULL 的行时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_menu_set_action_cb(we_menu_obj_t *obj, we_menu_action_cb_t cb);

/**
 * @brief 返回上一级页面（出栈，带水平滑入过渡）。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 已在根页时无操作；返回后恢复上一级记住的滚动位置。
 */
void we_menu_back(we_menu_obj_t *obj);

/**
 * @brief 复位到根页（清空页面栈与根页滚动，无过渡动画）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_menu_reset(we_menu_obj_t *obj);

/**
 * @brief 设置主题配色（六色一组，值全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param bg 行区面板背景色。
 * @param title_bg 标题栏背景色。
 * @param text 行文字色。
 * @param title_text 标题文字色。
 * @param press 按压高亮背景色。
 * @param accent 强调色（返回箭头 / 子页提示箭头 / 滚动条滑块）。
 * @return 无。
 * @note 分隔线颜色保持默认（随 text 观感即可，preview 不单独开放）。
 */
void we_menu_set_colors(we_menu_obj_t *obj, colour_t bg, colour_t title_bg,
                        colour_t text, colour_t title_text,
                        colour_t press, colour_t accent);

/**
 * @brief 删除菜单控件：先摘除惯性与过渡两个动画节点再摘链。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_menu_obj_delete(we_menu_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_MENU_H */
