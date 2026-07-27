#ifndef __WE_WIDGET_TEXTAREA_H
#define __WE_WIDGET_TEXTAREA_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 单行输入框控件（textarea）—— preview 孵化区实验控件
 *
 * 圆角底框 + 左对齐单行文本 + 末尾 2px 闪烁光标（500ms 亮灭，走中央动画
 * 引擎 we_anim_t，不占 GUI task 槽）。文本缓冲由调用方提供（含结尾 0），
 * 控件零 malloc。文本宽超出可视宽时左移显示尾部（PFB 窗口收窄裁剪，
 * 头部裁掉），光标始终贴在文本末尾。
 *
 * 输入通过 we_textarea_input(key) 键值注入：追加键面字符串，"\b" 退格
 * （按 UTF-8 字符回退，多字节字符整体删除）。与 keyboard 控件的键值
 * 回调约定一致，可直接对接。
 *
 * 无全局焦点系统：控件视为常聚焦，光标常闪；点击控件本身仅作按压视觉
 * 反馈（底色微亮），event_cb 返回 1 消费事件。
 *
 * preview 限制：
 *   - 光标闪烁 / 内容变化均按整控件包围盒标脏（只标脏光标小区域为毕业项）；
 *   - 每次输入全量重测文本宽（未做增量宽度缓存）。
 * -------------------------------------------------------------------------- */

/* 文本区左右内边距（像素），可在包含本头文件前用宏覆盖 */
#ifndef WE_TEXTAREA_PAD_X
#define WE_TEXTAREA_PAD_X 8
#endif

/* 文本区上下内边距（像素），控件高度 = 字体行高 + 2 * PAD_Y */
#ifndef WE_TEXTAREA_PAD_Y
#define WE_TEXTAREA_PAD_Y 6
#endif

/* 底框圆角半径（像素） */
#ifndef WE_TEXTAREA_RADIUS
#define WE_TEXTAREA_RADIUS 8U
#endif

/* 光标宽度（像素） */
#ifndef WE_TEXTAREA_CURSOR_W
#define WE_TEXTAREA_CURSOR_W 2
#endif

/* 光标闪烁半周期（毫秒，亮/灭各占一段） */
#ifndef WE_TEXTAREA_BLINK_MS
#define WE_TEXTAREA_BLINK_MS 500U
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 开启后输入框可聚焦：OK 按下沿底框按压、松开沿呼出绑定的弹层软键盘
 * （we_textarea_bind_keyboard）。置 0 单独裁剪本控件按键支持。 */
#ifndef WE_TEXTAREA_USE_KEY
#define WE_TEXTAREA_USE_KEY 1
#endif

typedef struct we_textarea_obj_t
{
    we_obj_t base;                 /* 必须在首位：base.x/y/w/h 为底框外接矩形 */

    /* 4 字节对齐成员（指针/动画节点）在前，消 padding */
    char *buf;                     /* 文本缓冲（调用方提供，含结尾 0） */
    const char *placeholder;       /* 空内容占位提示（可为 NULL，调用方持有） */
    const unsigned char *font;     /* 字库（init 必传） */
    we_anim_t blink_anim;          /* 中央动画节点：光标闪烁（delete 前必须摘链） */
    /* 绑定的弹层编辑器（软键盘/输入法，可 NULL）：点击输入框或聚焦后按 OK
     * 时经 summon_cb(editor, ta) 呼出，editor 侧负责 show 并绑定注入目标 */
    void *editor;
    void (*summon_cb)(void *editor, void *ta);

    /* 2 字节成员 */
    uint16_t buf_size;             /* 缓冲总容量（含结尾 0 的字节数） */
    uint16_t len;                  /* 当前文本字节长度（不含结尾 0） */
    uint16_t blink_acc;            /* 闪烁累计毫秒 */
    colour_t bg_color;             /* 底框填充色 */
    colour_t text_color;           /* 文本前景色 */
    colour_t cursor_color;         /* 光标颜色 */
    colour_t placeholder_color;    /* 占位提示灰色（内部默认） */

    /* 1 字节成员与状态位域 */
    uint8_t opacity;               /* 整体不透明度（0~255） */
    uint8_t editing : 1;           /* 1 = 编辑中（弹层编辑器正注入本框）：才显示/闪烁光标 */
    uint8_t cursor_on : 1;         /* 1 = 光标当前处于亮相位（仅编辑中有效） */
    uint8_t pressed : 1;           /* 1 = 按压视觉反馈中（底色微亮） */
} we_textarea_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化单行输入框并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 底框左上角 X（屏幕绝对坐标）。
 * @param y 底框左上角 Y。
 * @param w 底框宽度（像素）。
 * @param buf 文本缓冲（调用方提供并持有，须含结尾 0；已有内容会被保留显示）。
 * @param buf_size 缓冲总容量（含结尾 0 的字节数，须 >= 1）。
 * @return 无。
 * @note 控件高度自动取 字体行高 + 2 * WE_TEXTAREA_PAD_Y；
 *       初始化即启动光标闪烁动画（视为常聚焦）。
 */
void we_textarea_obj_init(we_textarea_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w,
                          char *buf, uint16_t buf_size,
                        const unsigned char *font);

/**
 * @brief 注入一个键值：追加键面字符串，"\b" 为退格。
 * @param obj 控件对象指针。
 * @param key 键面字符串（UTF-8）；"\b" 删除末尾一个 UTF-8 字符。
 * @return 无。
 * @note 缓冲余量不足时本次追加整体忽略；任何内容变化都会把光标拉回
 *       亮相位并复位闪烁计时（输入期间光标保持常亮的通用手感）。
 *       与 keyboard 控件 key_cb 的键值约定一致，可直接透传。
 */
void we_textarea_input(we_textarea_obj_t *obj, const char *key);

/**
 * @brief 清空文本内容（缓冲写入结尾 0），空内容时显示占位提示。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 内容本就为空时直接返回，不触发重绘。
 */
void we_textarea_clear(we_textarea_obj_t *obj);

/**
 * @brief 获取当前文本内容指针（即调用方提供的缓冲区）。
 * @param obj 控件对象指针。
 * @return 文本字符串指针（始终以 0 结尾）；obj 为 NULL 时返回 NULL。
 */
const char *we_textarea_get_text(const we_textarea_obj_t *obj);

/**
 * @brief 设置空内容时的占位提示文本（灰色显示）。
 * @param obj 控件对象指针。
 * @param placeholder 占位字符串（调用方持有，可为 NULL 表示无提示）。
 * @return 无。
 * @note 指针未变时直接返回；仅在当前内容为空（占位可见）时触发重绘。
 */
void we_textarea_set_placeholder(we_textarea_obj_t *obj, const char *placeholder);

/**
 * @brief 绑定弹层软键盘：点击输入框（或聚焦后按 OK）呼出，普通键直接注入本框。
 * @param obj 控件对象指针。
 * @param kb 弹层键盘对象指针（we_keyboard_obj_t*，须为 popup_init 创建；NULL 解绑）。
 * @return 无。
 * @note 单例键盘可同时绑定多个输入框，呼出时以最后交互的输入框为注入目标。
 *       绑定拼音输入法请用 we_ime_pinyin_bind_textarea（ime 侧提供）。
 */
void we_textarea_bind_keyboard(we_textarea_obj_t *obj, void *kb);

/**
 * @brief 设置编辑中状态：进入时光标常亮并开始闪烁，退出时光标熄灭停表。
 * @param obj 控件对象指针。
 * @param on 1 = 编辑中，0 = 空闲（值未变时直接返回）。
 * @return 无。
 * @note 弹层键盘/输入法在 show/hide/被顶掉时自动调用（目标切换也会
 *       同步交接），应用层一般无需手动管理；静态常显键盘的回显框
 *       可手动置 1 保持旧"常聚焦"观感。
 */
void we_textarea_set_editing(we_textarea_obj_t *obj, uint8_t on);

/**
 * @brief 通用弹层编辑器绑定：点击/聚焦 OK 时经 summon_cb(editor, ta) 呼出。
 * @param obj 控件对象指针。
 * @param editor 编辑器对象指针（键盘/输入法等，随回调透传；NULL 解绑）。
 * @param summon_cb 呼出回调；NULL 解绑。
 * @return 无。
 */
void we_textarea_bind_editor(we_textarea_obj_t *obj, void *editor,
                             void (*summon_cb)(void *editor, void *ta));

/**
 * @brief 设置三项配色：底色 / 文字色 / 光标色。
 * @param obj 控件对象指针。
 * @param bg 底框填充色。
 * @param text 文本前景色。
 * @param cursor 光标颜色。
 * @return 无。
 * @note 三项均与当前值相同时直接返回，不触发重绘。
 */
void we_textarea_set_colors(we_textarea_obj_t *obj, colour_t bg,
                            colour_t text, colour_t cursor);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255），值未变时直接返回。
 * @return 无。
 */
void we_textarea_set_opacity(we_textarea_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件：先摘除光标闪烁动画节点，再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 动画节点归控件所有，删除前必须 we_anim_stop 摘链。
 */
void we_textarea_obj_delete(we_textarea_obj_t *obj);

#endif /* __WE_WIDGET_TEXTAREA_H */
