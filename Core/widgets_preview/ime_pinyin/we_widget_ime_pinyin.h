#ifndef __WE_WIDGET_IME_PINYIN_H
#define __WE_WIDGET_IME_PINYIN_H

#include "we_gui_driver.h"
#include "widgets_preview/keyboard/we_widget_keyboard.h"
#include "we_pinyin.h"

/* --------------------------------------------------------------------------
 * 拼音输入法面板控件（ime_pinyin）—— preview 孵化区实验控件
 *
 * 结构自上而下三段：
 *   1. 拼音缓冲条：显示已敲入的拼音字母（上限 7 字符）；
 *   2. 候选栏：一页最多 7 个候选字 + 两端 "<" ">" 翻页键，
 *      填充候选页时逐字调 we_font_get_glyph_info——字库没有的字直接
 *      跳过（缺字过滤）；游标式向后翻页 + 最近页起点栈回翻；
 *   3. 内嵌 we_keyboard_obj_t 软键盘（组合复用，字母页供拼音输入）。
 *
 * 键值路由（键盘 key_cb 与 inject 共用同一入口）：
 *   小写字母（键盘处于小写页）-> 进拼音缓冲并实时刷新候选
 *     （精确命中音节优先排最前，无精确命中时用前缀区间联想）；
 *   "\b" -> 缓冲非空删最后一个字母；缓冲空透传给 commit 回调（宿主删字）；
 *   " "  -> 缓冲非空选中当前页第 1 个候选；缓冲空透传空格；
 *   其余键值（数字/符号/大写字母页的键）-> 缓冲空时原样透传（中英直通），
 *     缓冲非空时忽略（preview 放宽：不做"先上屏再跟标点"）。
 *   点候选字 -> 经 commit 回调输出该字 UTF-8 串并清空缓冲。
 *
 * 弹层按键操作（随 WE_KEYBOARD_USE_KEY 门控，无独立开关）：键盘网格导航
 * 之外，键盘顶行再按"上"且当前有候选时，光标上探进候选栏（键盘键环
 * 暂隐）；候选区左右移动光标（自动跳过禁用的翻页键/空槽）、OK 双沿
 * 激活（翻页或候选上屏，上屏清缓冲后光标自动落回键盘区）、下/BACK
 * 退回键盘区（BACK 在键盘区才收回弹层）。
 *
 * 候选池是 uint16 Unicode 码点（见 we_pinyin.h），commit 上屏时才转
 * UTF-8；回调收到的字符串指向控件内部小缓冲，仅本次回调期间有效，
 * 需要保留请自行拷贝（对接 we_textarea_input 直接可用）。
 *
 * 零 malloc、无动画节点（删除无需摘链）、事件返回 1 消费不穿透。
 *
 * preview 限制：
 *   - 回翻栈深 WE_IME_PINYIN_BACK_MAX 页，翻更深后最早的页起点被挤掉；
 *   - 不支持 we_obj_set_pos 移动（内嵌键盘坐标不跟随）；
 *   - 二级字开关是引擎级全局状态，多实例共享；
 *   - 缓冲非空时的非拼音键直接忽略（不自动上屏首选）。
 * -------------------------------------------------------------------------- */

/* 拼音缓冲条高度（像素），可在包含本头文件前用宏覆盖 */
#ifndef WE_IME_PINYIN_BUF_H
#define WE_IME_PINYIN_BUF_H 22
#endif

/* 候选栏高度（像素） */
#ifndef WE_IME_PINYIN_CAND_H
#define WE_IME_PINYIN_CAND_H 30
#endif

/* 候选页容量（个），布局按 7 设计，改动需同步候选格宽度观感 */
#define WE_IME_PINYIN_PAGE_CAP 7

/* 翻页键宽度（像素） */
#ifndef WE_IME_PINYIN_PAGER_W
#define WE_IME_PINYIN_PAGER_W 26
#endif

/* 回翻栈深度（页），翻更深后最早的页起点被挤掉 */
#define WE_IME_PINYIN_BACK_MAX 16

/* 拼音缓冲上限（字母数，与引擎 WE_PINYIN_MAX_LEN 对齐再放 1 个冗余位） */
#define WE_IME_PINYIN_BUF_MAX 7

/* commit 回调：ime 为 we_ime_pinyin_obj_t*，utf8 为候选字/透传键值字符串。
 * utf8 指向控件内部缓冲或键盘 static 键面，仅回调期间保证有效。 */
typedef void (*we_ime_pinyin_commit_cb_t)(void *ime, const char *utf8);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_ime_pinyin_obj_t
{
    we_obj_t base;                 /* 必须在首位：整个面板（含键盘区）外接矩形 */
    we_keyboard_obj_t kb;          /* 内嵌软键盘（位于面板下段） */

    /* 4 字节对齐成员（指针/动画节点）在前，消 padding */
    const unsigned char *font;     /* 候选/拼音条字库（须含中文字形） */
    we_ime_pinyin_commit_cb_t commit_cb; /* 上屏回调（可为 NULL） */
    void *target;                  /* 弹层模式绑定的目标输入框（we_textarea_obj_t*，可 NULL） */
    we_anim_t anim;                /* 弹层滑动动画节点（delete 内部摘链） */

    /* 2 字节成员 */
    uint16_t range_first;          /* 当前前缀匹配音节区间首索引 */
    uint16_t range_count;          /* 区间音节数（0 = 无候选） */
    uint16_t slide_q8;             /* 弹层滑入进度 Q8：0=全隐藏，256=完全展开 */
    int16_t cand_kb_idx;           /* 候选区键控光标激活期间暂存的键盘键光标序号（-1 = 无） */
    uint16_t page_cp[WE_IME_PINYIN_PAGE_CAP]; /* 当前页候选码点 */
    we_pinyin_iter_t iter;         /* 游标：指向下一页起点 */
    we_pinyin_iter_t page_start;   /* 本页起点（回翻压栈用） */
    we_pinyin_iter_t back_stack[WE_IME_PINYIN_BACK_MAX]; /* 最近页起点栈 */
    colour_t bg_color;             /* 面板底色（候选栏背景） */
    colour_t buf_color;            /* 拼音条底色 */
    colour_t text_color;           /* 候选字/拼音文字色 */
    colour_t hint_color;           /* 提示灰字色（init/set_colors 时由文字色向底色混合派生） */
    colour_t press_color;          /* 按压高亮色 */

    /* 1 字节成员与状态位域 */
    char pybuf[WE_IME_PINYIN_BUF_MAX + 1]; /* 拼音缓冲（NUL 结尾） */
    char commit_utf8[4];           /* 候选字 UTF-8 临时缓冲（回调期间有效） */
    uint8_t pylen;                 /* 缓冲字母数 */
    uint8_t back_depth;            /* 栈内页数 */
    uint8_t page_cnt;              /* 当前页候选数 */
    int8_t press_slot;             /* 按压中的槽位：0 = "<"，1..7 = 候选格，8 = ">"，-1 无 */
    int8_t cand_focus;             /* 候选栏键控光标槽位（编码同 press_slot），-1 = 光标在键盘区 */
    uint8_t opacity;               /* 整体不透明度（0~255），同步透传给内嵌键盘 */
    uint8_t has_more : 1;          /* 1 = 后面还有可显示候选（">" 可用） */
    uint8_t pressed : 1;           /* 1 = 按压高亮显示中 */
    uint8_t popup_mode : 1;        /* 1 = 弹层滑入/收回模式（面板与内嵌键盘均不挂链表） */
    uint8_t slide_state : 2;       /* 弹层滑动状态机（内部） */
    uint8_t popup_press_kb : 1;    /* 本次触摸序列按在键盘区标志（弹层事件按区路由） */
} we_ime_pinyin_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化拼音输入法面板并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 面板左上角 X（屏幕绝对坐标）。
 * @param y 面板左上角 Y。
 * @param w 面板宽度（像素）。
 * @param h 面板总高度（像素，含内嵌键盘；键盘高 = h - 拼音条 - 候选栏）。
 * @param font 字库指针（候选栏用它渲染中文，须含所需字形；NULL 用默认
 *             ASCII 字库，此时所有中文候选都会被缺字过滤掉）。
 * @return 无。
 * @note 内嵌键盘随 init 一并初始化并挂到 LCD 链（晚于面板本体，命中
 *       优先）；面板矩形内触摸全部被消费。默认深色配色与键盘一致。
 */
void we_ime_pinyin_obj_init(we_ime_pinyin_obj_t *obj, we_lcd_t *lcd,
                            int16_t x, int16_t y, int16_t w, int16_t h,
                            const unsigned char *font);

/**
 * @brief 初始化弹层模式拼音输入法（不挂对象链表，show 时经 LCD 弹层滑入）。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param h 面板总高（像素，含顶部回显条 WE_KEYBOARD_ECHO_H + 拼音条 +
 *          候选栏 + 键盘区）；宽度固定 = 屏宽，停靠屏底。
 * @param font 字库指针（须含中文字形；必传，NULL 不执行初始化）。
 * @return 无。
 * @note 结构自上而下：回显条（镜像目标输入框内容）/ 拼音条 / 候选栏 /
 *       内嵌键盘。绑定目标后候选上屏与透传键直接 we_textarea_input 注入。
 */
void we_ime_pinyin_popup_init(we_ime_pinyin_obj_t *obj, we_lcd_t *lcd,
                              int16_t h, const unsigned char *font);

/**
 * @brief 弹出拼音输入法：占用 LCD 弹层并从屏底滑入。
 * @param obj 控件对象指针（须为 popup_init 创建）。
 * @param target_textarea 绑定的目标输入框（we_textarea_obj_t*，可 NULL）。
 * @return 无。
 * @note 收回途径：点面板上方区域 / BACK 键 / 本 API 的 hide；
 *       键盘按键聚焦经模态键通道转发 we_keyboard_key_nav。
 */
void we_ime_pinyin_popup_show(we_ime_pinyin_obj_t *obj, void *target_textarea);

/**
 * @brief 收回拼音输入法：滑出到屏外后释放 LCD 弹层。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_ime_pinyin_popup_hide(we_ime_pinyin_obj_t *obj);

/**
 * @brief 把弹层输入法绑定到输入框（点击/聚焦 OK 呼出，触发框为注入目标）。
 * @param ta 输入框对象指针（we_textarea_obj_t*）。
 * @param ime 弹层输入法对象指针（须为 popup_init 创建；NULL 解绑）。
 * @return 无。
 */
void we_ime_pinyin_bind_textarea(void *ta, we_ime_pinyin_obj_t *ime);

/**
 * @brief 注册 commit 上屏回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针（ime, utf8），NULL 表示取消。
 * @return 无。
 * @note 候选字上屏、退格/空格/符号透传都走这一个回调；utf8 仅回调期间
 *       有效（"\b" 表示退格，与 keyboard/textarea 键值约定一致）。
 */
void we_ime_pinyin_set_commit_cb(we_ime_pinyin_obj_t *obj, we_ime_pinyin_commit_cb_t cb);

/**
 * @brief 注入一个键值（与内嵌键盘 key_cb 完全同一入口）。
 * @param obj 控件对象指针。
 * @param key 键面字符串（"a".."z"、"\b"、" "、其它符号）。
 * @return 无。
 * @note 供自动演示脚本/外接实体键盘使用；真实触摸与注入互不干扰。
 */
void we_ime_pinyin_inject_key(we_ime_pinyin_obj_t *obj, const char *key);

/**
 * @brief 选中当前候选页第 slot 个字并 commit 上屏。
 * @param obj 控件对象指针。
 * @param slot 候选格序号（0 .. page_cnt-1）。
 * @return 1 = 已上屏，0 = 槽位为空/越界。
 * @note 与点击候选格等效；供自动演示脚本使用。
 */
uint8_t we_ime_pinyin_select(we_ime_pinyin_obj_t *obj, uint8_t slot);

/**
 * @brief 候选栏翻页。
 * @param obj 控件对象指针。
 * @param dir 传入：>0 向后翻页，<0 回翻上一页。
 * @return 1 = 翻动成功，0 = 已到边界（无更多候选/回翻栈空）。
 */
uint8_t we_ime_pinyin_page(we_ime_pinyin_obj_t *obj, int8_t dir);

/**
 * @brief 运行期开/关二级字候选并刷新候选栏。
 * @param obj 控件对象指针。
 * @param enable 1 = 候选含二级字，0 = 只出一级字。
 * @return 无。
 * @note 引擎编译期未启用二级段（WE_PINYIN_ENABLE_L2 == 0）时为空操作；
 *       开关是引擎级全局状态，会影响所有 ime_pinyin 实例。值未变时
 *       直接返回。
 */
void we_ime_pinyin_set_l2(we_ime_pinyin_obj_t *obj, uint8_t enable);

/**
 * @brief 设置四项配色：面板底色 / 拼音条底色 / 文字色 / 按压高亮色。
 * @param obj 控件对象指针。
 * @param bg 面板底色（候选栏背景）。
 * @param buf_bg 拼音条底色。
 * @param text 候选字与拼音字母文字色。
 * @param press 按压高亮色。
 * @return 无。
 * @note 四项均与当前值相同时直接返回；提示灰字色自动由文字色向面板底
 *       色混合派生。内嵌键盘配色请直接用 we_keyboard_set_colors(&obj->kb, ...)。
 */
void we_ime_pinyin_set_colors(we_ime_pinyin_obj_t *obj, colour_t bg,
                              colour_t buf_bg, colour_t text, colour_t press);

/**
 * @brief 设置整体不透明度并按需重绘（同步设置内嵌键盘）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255），值未变时直接返回。
 * @return 无。
 * @note 完全透明（0）时面板与键盘均不再拦截输入。
 */
void we_ime_pinyin_set_opacity(we_ime_pinyin_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件：先删内嵌键盘再摘面板本体（均无动画节点）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_ime_pinyin_obj_delete(we_ime_pinyin_obj_t *obj);

#endif /* __WE_WIDGET_IME_PINYIN_H */
