#ifndef WE_TYPES_H
#define WE_TYPES_H

/* --------------------------------------------------------------------------
 * WeGui 基础类型：数学工具、颜色、矩形、事件码、对象与类描述符、
 * 定时器与动画节点、屏幕实例结构体。其余内核头文件都建立在它之上。
 * -------------------------------------------------------------------------- */

#include <stdint.h>
#include <string.h>
#include "we_user_config.h"
#include "we_gui_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define WE_ABS(x) ((x) < 0 ? -(x) : (x))
#define WE_MIN(a, b) ((a) < (b) ? (a) : (b))
#define WE_MAX(a, b) ((a) > (b) ? (a) : (b))
    struct we_lcd_t;
    /**
     * @brief 将浮点角度转换为 512 步制单位（四舍五入）
     * @param float_deg 传入：浮点角度（单位：度）
     * @return 512 步制角度值
     * @note 系统统一使用 512 步/圈，0~511 对应 0°~<360°
     *       好处：归一化只需 & 0x1FF，无除法；90° = 128，对齐 2 的幂次
     *       使用 static inline 保证零调用开销，且避免宏多重求值副作用
     */
    static inline int16_t WE_ANGLE(float float_deg)
    {
        float v = float_deg * (512.0f / 360.0f);
        return (int16_t)(v >= 0.0f ? (v + 0.5f) : (v - 0.5f));
    }
    /**
     * @brief  将整数度数（编译期常量）转换为 512 步制单位（截断）
     * @note   d 为整数度数，可为负数或超过 360 的值
     *         推荐用于控件初始化角度参数，避免在代码中出现魔法数字
     */
#define WE_DEG(d) ((int16_t)((int32_t)(d) * 512 / 360))
    /**
     * @brief 计算 512 步制角度对应的余弦值
     * @param angle 传入：512 步制角度（0~511 对应 0°~<360°）
     * @return Q15 定点余弦值（范围约 -32767~32767）
     */
    extern int16_t we_cos(int16_t angle);
    /**
     * @brief 计算 512 步制角度对应的正弦值
     * @param angle 传入：512 步制角度（0~511 对应 0°~<360°）
     * @return Q15 定点正弦值（范围约 -32767~32767）
     */
    extern int16_t we_sin(int16_t angle);

/**
 * @brief 计算 8 位分数线性插值结果
 * @param from 传入：起始值
 * @param to 传入：目标值
 * @param t 传入：插值系数（0~256，超过 256 时允许过冲）
 * @return 插值结果，计算公式为 from + (to - from) * t / 256
 */
static inline int32_t we_lerp(int32_t from, int32_t to, uint16_t t)
{
    return from + (((to - from) * (int32_t)t) >> 8);
}

/* --- 色深跨平台共用体 --- */
#if (LCD_DEEP == DEEP_GRAY8)
    /* 8 位灰度：单字节亮度。单色/灰度 OLED 由端口在 flush 时量化打包，
     * 内核渲染管线（AA 字体/mask/混色）按灰度照常工作。 */
    typedef union colour
    {
        uint8_t v; /* 亮度 0~255 */
        uint8_t dat[1];
        uint8_t dat8;
    } colour_t;
    static __inline colour_t we_rgb888_to_dev(uint8_t cr, uint8_t cg, uint8_t cb)
    {
        colour_t c;
        /* 整数亮度公式 (r*77 + g*150 + b*29) >> 8，无除法无浮点 */
        c.dat8 = (uint8_t)(((uint16_t)cr * 77U + (uint16_t)cg * 150U + (uint16_t)cb * 29U) >> 8);
        return c;
    }
#define RGB888TODEV(cr, cg, cb) we_rgb888_to_dev((uint8_t)(cr), (uint8_t)(cg), (uint8_t)(cb))
/* 编译期常量版本, 可用于 static const 初始化 */
#define RGB888_CONST(cr, cg, cb)                                                                                       \
    {                                                                                                                  \
        .dat8 = (uint8_t)((((cr) * 77U) + ((cg) * 150U) + ((cb) * 29U)) >> 8)                                          \
    }
#elif (LCD_DEEP == DEEP_RGB565)
    typedef union colour
    {
        struct
        {
            uint16_t b : 5;
            uint16_t g : 6;
            uint16_t r : 5;
        } rgb;
        uint8_t dat[2];
        uint16_t dat16;
    } colour_t;
    static __inline colour_t we_rgb888_to_dev(uint8_t cr, uint8_t cg, uint8_t cb)
    {
        colour_t c;
        c.rgb.r = (uint16_t)(cr >> 3);
        c.rgb.g = (uint16_t)(cg >> 2);
        c.rgb.b = (uint16_t)(cb >> 3);
        return c;
    }
#define RGB888TODEV(cr, cg, cb) we_rgb888_to_dev((uint8_t)(cr), (uint8_t)(cg), (uint8_t)(cb))
/* 编译期常量版本, 可用于 static const 初始化 */
#define RGB888_CONST(cr, cg, cb) { .dat16 = (uint16_t)((((cr) >> 3) << 11) | (((cg) >> 2) << 5) | ((cb) >> 3)) }
#elif (LCD_DEEP == DEEP_RGB888)
typedef union colour
{
    struct
    {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } rgb;
    uint8_t dat[3];
} colour_t;
static __inline colour_t we_rgb888_to_dev(uint8_t cr, uint8_t cg, uint8_t cb)
{
    colour_t c;
    c.rgb.r = cr;
    c.rgb.g = cg;
    c.rgb.b = cb;
    return c;
}
#define RGB888TODEV(cr, cg, cb) we_rgb888_to_dev((uint8_t)(cr), (uint8_t)(cg), (uint8_t)(cb))
/* 编译期常量版本, 可用于 static const 初始化 */
#define RGB888_CONST(cr, cg, cb) { .rgb = { (cr), (cg), (cb) } }
#endif

    // 矩形坐标结构体
    typedef struct
    {
        int16_t x0, y0, x1, y1;
    } we_rect_t;

    // 脏矩形管理器 (独立模块)
    typedef struct
    {
        we_rect_t rects[WE_CFG_DIRTY_MAX_NUM];
        uint8_t count;       // 当前脏矩形的数量
        uint16_t screen_w;   // 所属屏幕宽度 (由 we_gui_init 写入)
        uint16_t screen_h;   // 所属屏幕高度
    } we_dirty_mgr_t;

    /* 控件事件枚举——统一事件码空间，分段编码（同一 event_cb 接收全部段）：
     *   0x00..0x0F  触摸/手势/内核查询与通知（HIT_TEST/DRAG_BEGIN/MODAL_CLOSE 等）
     *   0x10..0x17  语义键（端口注入；内核只发给 FOCUSABLE 类与模态对象）
     *   0x20..0x2F  焦点通知（内核→控件；端口禁止注入）
     *   0x40..0x7F  控件/应用自定义事件保留段（从 WE_EVENT_USER_BASE 起自行分配）
     *   0x80        松开沿修饰位（WE_KEY_RELEASE_FLAG）——模态对象的 event_cb
     *               会实际收到 0x90..0x97（键值|0x80，软键盘双沿击键手感依赖），
     *               控件按 (uint8_t)event >= WE_KEY_UP 分流即可一并覆盖 */
    typedef enum
    {
        WE_EVENT_PRESSED,   // 按下
        WE_EVENT_RELEASED,  // 释放
        WE_EVENT_CLICKED,   // 点击 (按下并在原位释放)
        WE_EVENT_STAY,      // 按住不放
        WE_EVENT_VALUE_CHG,  // 数值改变
        WE_EVENT_SCROLLED,   // 外部强制滚动位移
        WE_EVENT_SWIPE_LEFT, // 向左滑动
        WE_EVENT_SWIPE_RIGHT,// 向右滑动
        WE_EVENT_SWIPE_UP,   // 向上滑动
        WE_EVENT_SWIPE_DOWN, // 向下滑动
        /* --- 内核统一输入派发用（仅发给复合容器，叶子控件收不到） --- */
        WE_EVENT_HIT_TEST,   // 命中查询：返回 0 = 跳过本容器及其整棵子树
        WE_EVENT_DRAG_BEGIN, // 拖拽接管询问：位移越阈值时沿祖先链发起，返回非 0 = 接管手势
        WE_EVENT_MODAL_CLOSE, // 模态被替换/关闭通知：收到后自行收起（新模态打开时发给旧模态）

        /* ---- 语义键段 0x10..0x17（端口注入；内核只发给 FOCUSABLE 类）----
         * 0x80 位保留作键队列松开沿标志 WE_KEY_RELEASE_FLAG，
         * 故本段必须落在 0x10..0x7F。 */
        WE_KEY_UP = 0x10,     /* 方向：上（导航态等效 PREV） */
        WE_KEY_DOWN,          /* 方向：下（导航态等效 NEXT） */
        WE_KEY_LEFT,          /* 方向：左（导航态等效 PREV） */
        WE_KEY_RIGHT,         /* 方向：右（导航态等效 NEXT） */
        WE_KEY_PREV,          /* 前一个（Shift+Tab 语义） */
        WE_KEY_NEXT,          /* 后一个（Tab 语义） */
        WE_KEY_OK,            /* 进入/确认：容器下钻、控件触发 */
        WE_KEY_BACK,          /* 返回/取消：退出容器、清除焦点 */

        /* ---- 焦点通知段 0x20+（内核→控件；端口禁止注入）---- */
        WE_KEY_EVT_FOCUS = 0x20, /* 焦点查询：返回 0 = 本实例拒绝聚焦 */
        WE_KEY_EVT_DEFOCUS,      /* 失去焦点 */
        WE_KEY_EVT_FLASH_END,    /* OK 按压取消：仅回弹，不触发点击 */
        WE_KEY_EVT_OK_RELEASE,   /* OK 松开沿：回弹并触发点击动作 */
        WE_KEY_EVT_CHILD_FOCUS,  /* 子树内有对象获得焦点（发给祖先容器链，
                                  * scroll_panel 据此滚动跟随） */

        /* ---- 控件/应用自定义事件保留段 0x40..0x7F ----
         * 复合控件的自定义通知（如页签切换广播）从此处起分配，
         * 不会与内核任何段（含 0x80 修饰位组合）冲突。 */
        WE_EVENT_USER_BASE = 0x40,
    } we_event_t;

/* 端口侧"无键"哨值（仅作注入前的判空哨，不是合法键码） */
#define WE_KEY_NONE (0x00U)

    typedef enum
    {
        WE_TOUCH_STATE_NONE = 0,
        WE_TOUCH_STATE_PRESSED,
        WE_TOUCH_STATE_RELEASED,
        WE_TOUCH_STATE_STAY // 按住不放
    } we_touch_state_t;

    typedef struct
    {
        int16_t x;
        int16_t y;
        uint8_t state; /* stores we_touch_state_t */
    } we_indev_data_t;

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* ---------------- 全局聚焦 / 按键导航 ----------------
     * 语义键值（并入 we_event_t 的 0x10 段）：端口负责把物理按键（独立
     * 按键/五向摇杆/EC11 编码器等）消抖后翻译成语义键注入。双沿端口用
     * we_gui_key_press/release 上报按下/松开（OK 按住期间控件保持按压态）；
     * 简单端口用 we_gui_key_inject 注入一次完整 tap。方向键长按连发由
     * 端口重复注入按下沿实现。WE_KEY_EVT_*（0x20 段）为内核→控件的焦点
     * 通知，端口禁止注入。 */

/* 键队列编码：松开沿 = 键值 | 本标志（经 we_gui_key_release 注入） */
#define WE_KEY_RELEASE_FLAG (0x80U)

/* we_lcd_t.focus_flags 位定义（内核内部状态，端口/应用只读勿写） */
#define WE_FOCUS_F_EDIT (0x01U)     /* 编辑态：方向键调值，光标换编辑色 */
#define WE_FOCUS_F_OK_HELD (0x02U)  /* OK 物理按住中（吞掉系统连发的重复按下沿） */
#define WE_FOCUS_F_OK_ARMED (0x04U) /* OK 按下沿已被焦点控件消费，等待松开沿回发 */
#define WE_FOCUS_F_CURSOR_VIS (0x10U) /* 光标可见：按键活动/we_focus_set 亮出，触摸按下收起 */
#endif

    typedef void (*we_gui_timer_cb_t)(struct we_lcd_t *lcd, uint16_t elapsed_ms);

    /* 用户定时器：与中央动画引擎同构的侵入式节点——调用方持有节点、
     * 零槽位、数量无上限、create 不会失败。 */
    typedef struct we_gui_timer_s
    {
        struct we_gui_timer_s *next; /* 侵入式链表指针（内核维护） */
        we_gui_timer_cb_t cb;
        uint16_t period_ms;
        uint16_t acc_ms;
        uint8_t repeat : 1;
        uint8_t active : 1;
    } we_gui_timer_t;

    /* ---------------- 端口接口 ---------------- */
    /* 设刷新窗口：每个脏矩形调用一次，随后按 PFB 切片连续推送像素 */
    typedef void (*we_lcd_set_addr_cb_t)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    /* 推送一块像素：pix_size 恒为像素个数（与色深无关） */
#if (LCD_DEEP == DEEP_RGB565)
    typedef void (*we_lcd_flush_cb_t)(uint16_t *gram, uint32_t pix_size);
#elif (LCD_DEEP == DEEP_RGB888)
typedef void (*we_lcd_flush_cb_t)(uint8_t *gram, uint32_t pix_size);
#elif (LCD_DEEP == DEEP_GRAY8)
typedef void (*we_lcd_flush_cb_t)(uint8_t *gram, uint32_t pix_size);
#endif

    /* 触摸采样：端口只报"当前是否按下 + 坐标"，按下/按住/松开的边沿由内核判定。
     * 调用前内核把 x/y 预填为上一次的坐标，未按下时端口可以不填坐标。 */
    typedef struct
    {
        int16_t x;
        int16_t y;
        uint8_t pressed; /* 1 = 按下中，0 = 未按下 */
    } we_touch_data_t;
    typedef void (*we_touch_read_cb_t)(we_touch_data_t *data);

    /* 外挂存储读取：从 addr 起读 len 字节到 buf */
    typedef void (*we_storage_read_cb_t)(uint32_t addr, uint8_t buf[], uint32_t len);

    /* 端口描述：各平台定义成常量（放 Flash），we_gui_init 时绑定到屏幕实例。
     * flush_async = 1 表示 flush 发起 DMA 后立即返回：内核随即开始改写另一半
     * PFB，因此端口在开始下一次传输（下一次 flush 或 set_addr）之前，必须先等
     * 上一次传输结束。flush 返回时已发完的端口填 0。 */
    typedef struct
    {
        we_lcd_set_addr_cb_t set_addr;     /* 必填：设刷新窗口 */
        we_lcd_flush_cb_t flush;           /* 必填：推送像素 */
        we_touch_read_cb_t touch_read;     /* 选填：触摸采样，NULL = 无触摸 */
        we_storage_read_cb_t storage_read; /* 选填：外挂存储读取，NULL = 无 */
        uint8_t flush_async;               /* 1 = flush 为异步 DMA 发送 */
    } we_port_t;



/* we_class_t.class_flags 位定义
 *
 * 结构位与行为位分开：容器“结构上能不能取 children_head”和“焦点能不能
 * 下钻进去”是两件正交的事。slideshow / mask_group 结构上是 we_child_owner_t
 * （删除、改挂父子关系都要走 children_head），但在分页作用域做出来之前
 * 不该让焦点钻进非当前页的子控件，因此只带结构位。 */
#define WE_CLASS_FLAG_CHILD_OWNER (0x01U) /* 结构位：前缀为 we_child_owner_t，可安全取 children_head */
#define WE_CLASS_FLAG_FOCUS_ENTER (0x02U) /* 行为位：焦点可 OK 下钻 / BACK 上退 */
#define WE_CLASS_FLAG_FOCUSABLE (0x04U)   /* 行为位：可聚焦——内核把语义键段/通知段事件经
                                           * event_cb 发给它；返回非 0 = 已消费，普通键未
                                           * 消费时交焦点管理器执行默认导航 */
/* 输入派发契约：带 CHILD_OWNER 的容器由内核递归下钻到最深命中子控件并
 * 沿父链冒泡按压；容器自身只需应答 WE_EVENT_HIT_TEST（返回 0 = 本容器连
 * 同整棵子树跳过命中）与 WE_EVENT_DRAG_BEGIN（拖拽接管询问），不手工转发。 */

    // 控件类描述符 (存放于 Flash，节省 RAM)
    typedef struct
    {
        void (*draw_cb)(void *obj);
        uint8_t (*event_cb)(void *obj, we_event_t event, we_indev_data_t *data);
        void (*set_pos_cb)(void *obj, int16_t x, int16_t y);
        /* 析构回调：we_obj_delete 在摘链前调用，控件在这里做自有收尾
         * （通知业务层、释放自管资源等）。调用时 obj->lcd 与 class_p 仍然
         * 有效，可安全标脏、可读写自身字段。
         * 为 NULL 表示无需自清理：内核随后会统一回收 pressed / focus /
         * modal / 动画节点四类引用，漏写不会留下悬空指针。
         * 禁止在 delete_cb 内再次调用 we_obj_delete(自身)——会无限递归。 */
        void (*delete_cb)(void *obj);
        /* 子控件区（CHILD_OWNER 类可选）：area 传入时为包围盒，只能在包围盒以内
         * 收窄（屏幕绝对坐标，含端点），返回子控件的级联透明度，返回 0 = 子树
         * 本帧不画、也不参与命中。为 NULL 时可见区 = 包围盒、透明度 255。 */
        uint8_t (*content_cb)(void *obj, we_rect_t *area);
        /* 子控件画完之后的补画（滚动条、蒙版合成等），窗口为本对象包围盒；可为 NULL */
        void (*draw_post_cb)(void *obj);
        /* WE_CLASS_FLAG_* 位组合。不随按键功能裁剪：结构位 CHILD_OWNER 被
         * 对象链表与删除路径使用，纯触摸工程同样需要它来递归删除子控件。
         * 键/焦点事件与触摸事件共用回调：FOCUSABLE 类经统一 event_cb 接收
         * 0x10/0x20 两段事件码（全部类描述符均为指定初始化器）。 */
        uint8_t class_flags;
    } we_class_t;

    // 所有控件的绝对基类 (Base Object)
    typedef struct we_obj_s
    {
        struct we_obj_s *next;     // 链表指针 (Z轴层级)
        struct we_obj_s *parent;   // 父节点指针 (用于脏矩形自动裁剪)
        const we_class_t *class_p; // 指向 Flash 中的类描述符
        struct we_lcd_t *lcd;      // 绑定的屏幕上下文
        int16_t x;                 // 绝对 X 坐标
        int16_t y;                 // 绝对 Y 坐标
        int16_t w;                 // 控件物理宽度
        int16_t h;                 // 控件物理高度
    } we_obj_t;

    /* 可挂子控件对象的最小公共前缀：
     * base + children_head。
     * 目前 group/slideshow/scroll_panel 用它与 driver 共享父子摘链语义。
     * 新增复合容器时，结构体前两个成员必须保持 base、children_head 顺序。 */
    typedef struct
    {
        we_obj_t base;
        we_obj_t *children_head;
    } we_child_owner_t;

    /* ---------------- 中央动画引擎 ----------------
     * 动画节点内嵌在控件结构体里：零堆、不占用任何槽位、数量无上限。
     * we_gui_run 每一帧遍历该链表并调用 step_cb；
     * 控件动画到达目标态后在 step_cb 内自行 we_anim_stop 摘链。
     * 约定：
     * 1. step_cb 内只允许摘除自身节点，不得摘除其他节点，也不得在
     *    step_cb 内删除“其他”控件（we_obj_delete 会清空被摘节点的
     *    next，截断本帧的动画遍历）；
     * 2. 控件删除时的摘链由内核负责：we_obj_delete 扫描动画链摘掉
     *    全部 owner 指向被删对象的节点（line 这类多节点控件一次全清），
     *    因此“容器基类删子”不会留下僵尸节点，控件的 we_xxx_obj_delete
     *    也不必再调 we_anim_stop（那只会多扫一遍动画链）。 */
    typedef struct we_anim_s
    {
        struct we_anim_s *next;                            /* 侵入式链表指针 */
        void (*step_cb)(void *owner, uint16_t elapsed_ms); /* 每周期推进回调 */
        void *owner;                                       /* 回指控件实例 */
    } we_anim_t;

    typedef struct we_lcd_t
    {
        // 1. PFB 显存管理（指针成员集中在前，半字成员集中在后，消除对齐填充）
        colour_t *pfb_gram;      // 当前绘制窗口左上角 (clip.x0, clip.y0) 对应的显存地址
        colour_t *pfb_gram_base; // 显存基地址
        const we_port_t *port;   // 端口描述（设窗/刷屏/触摸/外挂存储）
        we_rect_t clip;          // 当前绘制窗口（屏幕绝对坐标，含端点）：切片 ∩ 逐级收窄的对象区域
        uint16_t pfb_size;       // 一次可绘制的显存像素数（双缓冲时为一半）
        uint16_t pfb_width;      // 显存行跨度（像素，= 当前脏矩形宽度）

        // 2. 屏幕属性
        uint16_t width;
        uint16_t height;
        colour_t bg_color;

        // 3. 脏矩形管理器 (彻底干掉全局变量，完美内嵌！)
        we_dirty_mgr_t dirty_mgr;
        we_obj_t *obj_list_head;
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
        we_obj_t *top_list_head;                                  // 顶层链表：toast/msgbox 等"保证置顶"对象，普通层之后、焦点光标之后绘制
        we_obj_t *modal_obj;                                      // 当前模态对象（顶层链成员；NULL=无模态。模态期间命中限顶层、未命中交它、语义键直送它）
#endif
        we_gui_timer_t *timer_head;                               // 用户定时器侵入式链表头（节点归调用方所有）
        we_anim_t *anim_head;                                     // 中央动画链表头（控件动画不占用任何槽位）
        uint32_t time_last_ms;                                    // 上一次 we_gui_run 的时间戳（毫秒）
#if (WE_CFG_REFR_PERIOD_MS > 0)
        uint32_t frame_last_ms;                                   // 上一次渲染帧的时间戳
        uint16_t anim_pending_ms;                                 // 两帧之间攒下、尚未交给动画的毫秒数
#endif
#if (WE_CFG_INDEV_PERIOD_MS > 0)
        uint32_t indev_last_ms;                                   // 上一次触摸采样的时间戳
#endif
        we_indev_data_t indev_data;                               // 当前 LCD 实例绑定的输入状态缓存
        int16_t gesture_press_x;                                  // 手势识别：按下时 X 坐标
        int16_t gesture_press_y;                                  // 手势识别：按下时 Y 坐标
        we_obj_t *pressed_obj;                                    // 当前按压中的对象（we_obj_delete 会同步清空，防悬空派发）
        uint8_t opa_scale;                                        // 容器透明度级联乘子，255=无衰减（group 画子时设置，原语入口消费）
        uint8_t gesture_had_stay;                                 // 本次触摸序列是否经历过 STAY（拖拽）；与相邻字节成员共享对齐槽
        uint8_t gesture_drag_done;                                // 本次触摸序列是否已完成拖拽接管询问（DRAG_BEGIN 只发一轮）
        uint8_t touch_down;                                       // 端口触摸上一次采样是否按下（内核据此判定边沿）
        uint8_t pfb_double;                                       // 1 = PFB 分两半交替使用（开启双缓冲且端口为异步发送）
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
        we_obj_t *focus_obj;                     // 当前焦点对象（NULL=无；we_obj_delete 沿祖先链防悬空）
        uint8_t focus_flags;                     // WE_FOCUS_F_* 位组合（编辑态/OK按住/OK待回发/光标可见）
        uint8_t key_queue[WE_CFG_KEY_QUEUE_LEN]; // 语义键环形队列（SPSC：注入侧只写 tail/key_ok_down，消费侧只写 head，中断注入安全）
        volatile uint8_t key_q_head;             // 队头下标（仅消费侧写；head==tail 为空）
        volatile uint8_t key_q_tail;             // 队尾下标（仅注入侧写；容量 = 队列深度-1）
        volatile uint8_t key_ok_down;            // OK 物理电平（注入侧独写：按下沿置 1、松开沿清 0；队列是顺序信使，电平是兜底事实源）
#endif

        /* 渲染统计：真正完成一次脏区提交就记 1 帧（FPS 显示的数据源）。 */
#if (WE_CFG_ENABLE_RENDER_STATS == 1)
        uint32_t stat_render_frames;
#endif
    } we_lcd_t;

    /* ---------------- 容器透明度级联 ----------------
     * group/slideshow/scroll_panel 绘制子控件前把自身 opacity 乘进
     * lcd->opa_scale，所有绘图原语在入口消费一次（不进内环）。
     * 常态 opa_scale==255 时仅一次比较，零额外开销。 */
    static __inline uint8_t we_opa_apply(const we_lcd_t *p_lcd, uint8_t opacity)
    {
        uint32_t v;
        if (p_lcd->opa_scale == 255U)
            return opacity;
        v = (uint32_t)opacity * p_lcd->opa_scale;
        return (uint8_t)((v + (v >> 8) + 1U) >> 8); /* 同 we_div255 的 /255 近似 */
    }
#ifdef __cplusplus
}
#endif

#endif /* WE_TYPES_H */
