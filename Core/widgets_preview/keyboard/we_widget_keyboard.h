#ifndef __WE_WIDGET_KEYBOARD_H
#define __WE_WIDGET_KEYBOARD_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 软键盘控件（keyboard）—— preview 孵化区实验控件
 *
 * 内置 3 个页面布局（小写字母 / 大写字母 / 数字符号），键位表以 static const
 * 数组存放在 .c 中，控件零拷贝引用。采用自绘网格实现（方案 B）：
 * 每行按"宽度份数"划分（份数表与键名表一一对应），支持通长空格键、
 * 加宽 Shift/退格/切换键——这是 btnmatrix 等分网格无法表达的。
 * 绘制/命中代码结构借鉴 btnmatrix（圆角键底 + 居中键名 + 行优先命中）。
 *
 * 交互状态机（与 btnmatrix 同源）：
 *   PRESSED  命中键 -> 记录键序号并高亮；
 *   STAY     拖出原键 -> 取消按压态（本次触摸不再产生点击）；
 *   CLICKED  在原键释放 -> 功能键内部消化 / 普通键触发 key_cb。
 *
 * 键值回调约定：
 *   普通键     -> key_cb(kb, 键面字符串)（如 "q"、"A"、"1"、","）
 *   空格       -> key_cb(kb, " ")
 *   退格 "<-"  -> key_cb(kb, "\b")
 *   SH / 123 / abc（页面切换）在控件内部消化，不回调。
 *   回调传出的字符串均指向 static const 存储，生命周期贯穿运行期。
 *
 * shift 语义：点 SH 切大写页，敲出一个字母后自动回小写页（非 sticky）。
 *
 * 弹层模式（隐藏/收回软键盘）：
 *   we_keyboard_popup_init 创建的键盘不挂普通对象链表，经 LCD 弹层
 *   （LCD 顶层链 + 模态）承载：we_keyboard_popup_show 从屏底滑入
 *   （toast 同款 Q8 状态机，一个中央动画节点），点击键盘外部区域或
 *   BACK 键滑出收回；show 可绑定目标输入框（we_textarea_obj_t*），
 *   普通键/退格直接 we_textarea_input 注入，"OK" 确定键触发 done_cb
 *   并收回。滑入/滑出中途重复调用 show/hide 从当前位置平滑反向。
 *
 *   按键导航（WE_CFG_ENABLE_KEY_INPUT 且 WE_KEYBOARD_USE_KEY）：弹层键
 *   通道接管——方向键在键位网格上移动键光标（换行按 x 中心就近落键、
 *   行内回绕），OK 双沿击键（按下沿按压高亮、松开沿触发），BACK 收回。
 *
 * 零 malloc、零浮点；静态模式无动画节点，弹层模式含一个滑动节点
 * （we_keyboard_obj_delete 内部摘链，调用方无需关心）。
 *
 * preview 限制：
 *   - 页面切换 / 按压反馈按整控件或单键包围盒标脏，未做精细化；
 *   - 每次重绘遍历当前页全部键，依赖 PFB 裁剪丢弃窗口外写入；
 *   - 键名文字不按键格裁剪，超长标签会溢出（内置表无此情况）。
 * -------------------------------------------------------------------------- */

/* 键间距（像素，行距与列距共用），可在包含本头文件前用宏覆盖 */
#ifndef WE_KEYBOARD_GAP
#define WE_KEYBOARD_GAP 4
#endif

/* 面板内边距（像素） */
#ifndef WE_KEYBOARD_PAD
#define WE_KEYBOARD_PAD 3
#endif

/* 按键圆角半径（像素），绘制时按键宽/键高各半自动钳制 */
#ifndef WE_KEYBOARD_RADIUS
#define WE_KEYBOARD_RADIUS 6U
#endif

/* 弹层模式滑入/滑出动画时长（毫秒，≤ 一个 uint16 累计器可表达范围） */
#ifndef WE_KEYBOARD_ANIM_MS
#define WE_KEYBOARD_ANIM_MS 220U
#endif

/* 弹层模式顶部回显条高度（像素）：实时显示目标输入框内容（含末尾光标），
 * 键盘滑入遮住输入框时也能看到正在输入的文本。0 = 关闭回显条。 */
#ifndef WE_KEYBOARD_ECHO_H
#define WE_KEYBOARD_ECHO_H 26
#endif

/* 本控件弹层键导航开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 裁剪键光标与模态键通道回调（触摸弹层模式不受影响）。 */
#ifndef WE_KEYBOARD_USE_KEY
#define WE_KEYBOARD_USE_KEY 1
#endif

/* 页面编号 */
#define WE_KEYBOARD_PAGE_LOWER  0U /* 小写字母页 */
#define WE_KEYBOARD_PAGE_UPPER  1U /* 大写字母页 */
#define WE_KEYBOARD_PAGE_SYMBOL 2U /* 数字符号页 */

/* 键值回调：kb 为 we_keyboard_obj_t*，key 为键面字符串（"\b"=退格，" "=空格） */
typedef void (*we_keyboard_key_cb_t)(void *kb, const char *key);

/* 确定回调："OK" 键触发；kb 为 we_keyboard_obj_t*，target 为 show 时绑定的
 * 目标输入框（we_textarea_obj_t*，未绑定为 NULL）。回调后弹层模式自动收回。 */
typedef void (*we_keyboard_done_cb_t)(void *kb, void *target);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_keyboard_obj_t
{
    we_obj_t base;                 /* 必须在首位：base.x/y/w/h 为键盘面板外接矩形 */

    /* 4 字节对齐成员（指针/动画节点）在前，消 padding */
    const unsigned char *font;     /* 字库（init 必传） */
    we_keyboard_key_cb_t key_cb;   /* 键值回调（可为 NULL） */
    we_keyboard_done_cb_t done_cb; /* "OK" 确定回调（可为 NULL） */
    void *target;                  /* 弹层模式绑定的目标输入框（we_textarea_obj_t*，可 NULL） */
    we_anim_t anim;                /* 弹层滑动动画节点（delete 内部摘链） */

    /* 2 字节成员 */
    int16_t press_idx;             /* 本次触摸按下的键序号（当前页行优先），-1 = 无 */
    uint16_t slide_q8;             /* 弹层滑入进度 Q8：0=全隐藏，256=完全展开 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
    int16_t focus_idx;             /* 键光标序号（当前页行优先），-1 = 未落位 */
#endif
    colour_t bg_color;             /* 面板底色 */
    colour_t key_color;            /* 普通键底色 */
    colour_t key_press_color;      /* 按压键底色 */
    colour_t fn_color;             /* 功能键底色（init/set_colors 时由键色向面板色混合派生） */
    colour_t text_color;           /* 键面文字色 */

    /* 1 字节成员与状态位域 */
    uint8_t opacity;               /* 整体不透明度（0~255） */
    uint8_t page : 2;              /* 当前页面（WE_KEYBOARD_PAGE_xxx，0..2） */
    uint8_t pressed : 1;           /* 1 = 按压高亮显示中 */
    uint8_t popup_mode : 1;        /* 1 = 弹层滑入/收回模式（不挂普通对象链表） */
    uint8_t slide_state : 2;       /* 弹层滑动状态机（内部：隐藏/滑入/停留/滑出） */
} we_keyboard_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化软键盘控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 面板左上角 X（屏幕绝对坐标）。
 * @param y 面板左上角 Y。
 * @param w 面板宽度（像素）。
 * @param h 面板高度（像素）。
 * @return 无。
 * @note 初始页为小写字母页；默认深色面板 / 暗灰键 / 亮蓝按压 / 浅色文字，
 *       字体经 init 传入，不透明。面板矩形内的触摸全部被消费，
 *       不会穿透到下层控件。
 */
void we_keyboard_obj_init(we_keyboard_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                        const unsigned char *font);

/**
 * @brief 注册键值回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针（kb, key），NULL 表示取消。
 * @return 无。
 * @note 普通键传键面字符串，退格传 "\b"，空格传 " "；
 *       SH/123/abc 页面切换键在控件内部消化，不触发回调。
 */
void we_keyboard_set_key_cb(we_keyboard_obj_t *obj, we_keyboard_key_cb_t cb);

/**
 * @brief 切换键盘页面并重绘。
 * @param obj 控件对象指针。
 * @param page 目标页面（WE_KEYBOARD_PAGE_LOWER/UPPER/SYMBOL）。
 * @return 无。
 * @note 值未变或页面编号非法时直接返回；切页会取消进行中的按压态。
 */
void we_keyboard_set_page(we_keyboard_obj_t *obj, uint8_t page);

/**
 * @brief 设置四项配色：面板底色 / 普通键底色 / 按压键底色 / 文字色。
 * @param obj 控件对象指针。
 * @param panel 面板底色。
 * @param key 普通键底色。
 * @param key_press 按压键底色。
 * @param text 键面文字色。
 * @return 无。
 * @note 四项均与当前值相同时直接返回；功能键底色自动由
 *       键色向面板色混合派生，不单独设置。
 */
void we_keyboard_set_colors(we_keyboard_obj_t *obj, colour_t panel,
                            colour_t key, colour_t key_press, colour_t text);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255），值未变时直接返回。
 * @return 无。
 * @note 完全透明（0）时控件不再拦截输入。
 */
void we_keyboard_set_opacity(we_keyboard_obj_t *obj, uint8_t opacity);

/**
 * @brief 初始化弹层模式软键盘（不挂普通对象链表，show 时经 LCD 弹层滑入）。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param h 面板总高（像素，含顶部回显条 WE_KEYBOARD_ECHO_H）；
 *          宽度固定 = 屏宽，停靠屏底。
 * @param font 字体资源指针（必传；NULL 时不执行初始化）。
 * @return 无。
 * @note 初始为完全隐藏（屏外），调用 we_keyboard_popup_show 滑入。
 *       配色/页面等 set API 与静态键盘通用。
 */
void we_keyboard_popup_init(we_keyboard_obj_t *obj, we_lcd_t *lcd,
                            int16_t h, const unsigned char *font);

/**
 * @brief 弹出软键盘：占用 LCD 弹层并从屏底滑入。
 * @param obj 控件对象指针（须为 popup_init 创建）。
 * @param target_textarea 绑定的目标输入框（we_textarea_obj_t*，可 NULL）。
 * @return 无。
 * @note 绑定后普通键/退格直接注入目标（we_textarea_input），无需 key_cb；
 *       滑出中途调用则从当前位置反向滑入；重复 show 仅更新绑定目标。
 *       同屏唯一弹层：会自动替换已打开的其他弹层（dropdown 等）。
 */
void we_keyboard_popup_show(we_keyboard_obj_t *obj, void *target_textarea);

/**
 * @brief 收回软键盘：滑出到屏外后释放 LCD 弹层。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 触发途径：点击键盘外部区域 / BACK 键 / "OK" 确定键 / 本 API。
 */
void we_keyboard_popup_hide(we_keyboard_obj_t *obj);

/**
 * @brief 注册 "OK" 确定键回调（触发后弹层模式自动收回）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针（kb, target），NULL 表示取消。
 * @return 无。
 */
void we_keyboard_set_done_cb(we_keyboard_obj_t *obj, we_keyboard_done_cb_t cb);

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
/**
 * @brief 键光标导航核心：方向键移动键光标、OK 双沿击键。
 * @param obj 控件对象指针。
 * @param code 语义键编码（松开沿带 WE_KEY_RELEASE_FLAG）。
 * @return 1 = 已消费；0 = BACK 键未消费（由宿主决定收回动作）。
 * @note 供 ime_pinyin 等组合宿主在自己的模态键通道里转发；
 *       键盘自身的弹层模式内部已接好，无需调用。
 */
uint8_t we_keyboard_key_nav(we_keyboard_obj_t *obj, uint8_t code);

/**
 * @brief 查询键光标当前所在行号。
 * @param obj 控件对象指针。
 * @return 行号（0 = 顶行 .. WE_KEYBOARD_ROWS-1）；光标未落位返回 -1。
 * @note 供 ime_pinyin 等组合宿主判断"顶行再按上"的上探时机
 *       （把光标交给宿主自己的上方区域，如候选栏）。
 */
int16_t we_keyboard_focus_row(we_keyboard_obj_t *obj);
#endif

/**
 * @brief 删除控件（弹层模式先收弹层摘动画节点；静态模式从对象链表移除）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_keyboard_obj_delete(we_keyboard_obj_t *obj);

#endif /* __WE_WIDGET_KEYBOARD_H */
