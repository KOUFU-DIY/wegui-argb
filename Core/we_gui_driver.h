#ifndef WE_GUI_DRIVER_H
#define WE_GUI_DRIVER_H

#include "we_font_runtime_types.h"
#include "stdint.h"
#include "string.h"
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
#if (LCD_DEEP == DEEP_RGB565)
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
#define RGB888TOC(c, cr, cg, cb)                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        c.rgb.r = cr;                                                                                                  \
        c.rgb.g = cg;                                                                                                  \
        c.rgb.b = cb;                                                                                                  \
    } while (0)
#endif

    typedef struct we_area
    {
        uint16_t x0;
        uint16_t y0;
        uint16_t x1;
        uint16_t y1;
    } we_area_t;

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
        uint16_t screen_w;   // 所属屏幕宽度 (由 we_lcd_init_with_port 写入)
        uint16_t screen_h;   // 所属屏幕高度
    } we_dirty_mgr_t;

    // 控件事件枚举
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
    } we_event_t;

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
     * 语义键值：端口负责把物理按键（独立按键/五向摇杆/EC11 编码器等）
     * 消抖后翻译成语义键注入。双沿端口用 we_gui_key_press/release 上报
     * 按下/松开（OK 按住期间控件保持按压态）；简单端口用 we_gui_key_inject
     * 注入一次完整 tap。方向键长按连发由端口重复注入按下沿实现。
     * WE_KEY_EVT_* 为内核→控件的焦点通知，端口禁止注入。 */
    typedef enum
    {
        WE_KEY_NONE = 0,
        WE_KEY_UP,            /* 方向：上（导航态等效 PREV） */
        WE_KEY_DOWN,          /* 方向：下（导航态等效 NEXT） */
        WE_KEY_LEFT,          /* 方向：左（导航态等效 PREV） */
        WE_KEY_RIGHT,         /* 方向：右（导航态等效 NEXT） */
        WE_KEY_PREV,          /* 前一个（Shift+Tab 语义） */
        WE_KEY_NEXT,          /* 后一个（Tab 语义） */
        WE_KEY_OK,            /* 进入/确认：容器下钻、控件触发 */
        WE_KEY_BACK,          /* 返回/取消：退出容器、清除焦点 */
        WE_KEY_EVT_FOCUS,      /* 通知：获得焦点查询（返回 0 = 本实例拒绝聚焦） */
        WE_KEY_EVT_DEFOCUS,    /* 通知：失去焦点 */
        WE_KEY_EVT_FLASH_END,   /* 通知：OK 按压取消（焦点切走/清除时仅回弹，不触发点击） */
        WE_KEY_EVT_OK_RELEASE,  /* 通知：OK 松开沿（回弹按压态并触发点击动作） */
        WE_KEY_EVT_CHILD_FOCUS, /* 通知：子树内有对象获得焦点（发给祖先容器链，
                                 * scroll_panel 据此滚动跟随让焦点子控件可见） */
    } we_key_evt_t;

/* we_class_t.class_flags 位定义 */
#define WE_CLASS_FLAG_CHILD_OWNER (0x01U) /* 复合容器：前缀为 we_child_owner_t，焦点可进入 */

/* 键队列编码：松开沿 = 键值 | 本标志（经 we_gui_key_release 注入） */
#define WE_KEY_RELEASE_FLAG (0x80U)

/* we_lcd_t.focus_flags 位定义（内核内部状态，端口/应用只读勿写） */
#define WE_FOCUS_F_EDIT (0x01U)     /* 编辑态：方向键调值，光标换编辑色 */
#define WE_FOCUS_F_OK_HELD (0x02U)  /* OK 物理按住中（吞掉系统连发的重复按下沿） */
#define WE_FOCUS_F_OK_ARMED (0x04U) /* OK 按下沿已被焦点控件消费，等待松开沿回发 */
#define WE_FOCUS_F_REL_PEND (0x08U) /* 松开沿已到但仍在最短按压窗口内，窗口到期补发 */
#endif

    typedef void (*we_gui_timer_cb_t)(struct we_lcd_t *lcd, uint16_t elapsed_ms);

    typedef struct
    {
        we_gui_timer_cb_t cb;
        uint16_t period_ms;
        uint16_t acc_ms;
        uint8_t repeat;
        uint8_t active;
    } we_gui_timer_node_t;

    typedef void (*we_lcd_set_addr_cb_t)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
#if (LCD_DEEP == DEEP_RGB565)
    typedef void (*we_lcd_flush_cb_t)(uint16_t *gram, uint32_t pix_size);
#elif (LCD_DEEP == DEEP_RGB888)
typedef void (*we_lcd_flush_cb_t)(uint8_t *gram, uint32_t pix_size);
#endif
    typedef void (*we_input_read_cb_t)(we_indev_data_t *data);
    typedef void (*we_storage_read_cb_t)(uint32_t addr, uint8_t buf[], uint32_t len);

    typedef enum
    {
        WE_POPUP_TYPE_NONE = 0,
        WE_POPUP_TYPE_DROPDOWN,
        WE_POPUP_TYPE_MENU,
        WE_POPUP_TYPE_DIALOG,
        WE_POPUP_TYPE_KEYBOARD,
        WE_POPUP_TYPE_TOOLTIP,
    } we_popup_type_t;

    typedef struct
    {
        uint8_t active;
        uint8_t type; /* stores we_popup_type_t */
        void *owner;
        we_area_t area;
        void (*draw_cb)(void *owner);
        uint8_t (*event_cb)(void *owner, we_event_t event, we_indev_data_t *data);
        void (*close_cb)(void *owner);
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
        /* 弹层键通道：弹层激活期间语义键改送本回调（owner 透传）——
         * 按下沿传裸键值，松开沿传 键值|WE_KEY_RELEASE_FLAG（弹层内控件
         * 可据此做双沿按压手感，忽略松开沿即为按下沿触发语义）；
         * 为 NULL 时维持"吞掉全部按键"的模态语义。open 时自动清空，
         * 弹层拥有者在 open 后经 we_popup_layer_set_key_cb 挂接。 */
        uint8_t (*key_cb)(void *owner, uint8_t key);
#endif
    } we_popup_layer_t;

    // 控件类描述符 (存放于 Flash，节省 RAM)
    typedef struct
    {
        void (*draw_cb)(void *obj);
        uint8_t (*event_cb)(void *obj, we_event_t event, we_indev_data_t *data);
        void (*set_pos_cb)(void *obj, int16_t x, int16_t y);
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
        /* 按键/焦点回调：非 NULL 即视为可聚焦；返回非 0 = 已消费，
         * 普通键未消费时交焦点管理器执行默认导航。
         * 全部类描述符均为指定初始化器，旧控件不写该成员即为 NULL。 */
        uint8_t (*key_cb)(void *obj, uint8_t key_evt);
        uint8_t class_flags; /* WE_CLASS_FLAG_* 位组合 */
#endif
    } we_class_t;

    // 所有控件的绝对基类 (Base Object)
    typedef struct we_obj_s
    {
        struct we_obj_s *next;     // 链表指针 (Z轴层级)
        struct we_obj_s *parent;   // [新增] 父节点指针 (用于脏矩形自动裁剪)
        const we_class_t *class_p; // [重构] 指向 Flash 中的类描述符
        struct we_lcd_t *lcd;      // 绑定的屏幕上下文
        int16_t x;                 // 绝对 X 坐标
        int16_t y;                 // 绝对 Y 坐标
        int16_t w;                 // 控件物理宽度
        int16_t h;                 // 控件物理高度
    } we_obj_t;

    /* 可挂子控件对象的最小公共前缀：
     * base + children_head。
     * 目前 group/slideshow/scroll_panel 用它与 driver 共享父子摘链语义。
     * （历史上前缀里还有 int8_t task_id；控件动画迁入中央动画引擎后已移除，
     *  若新增复合容器，结构体前两个成员必须保持 base、children_head 顺序。） */
    typedef struct
    {
        we_obj_t base;
        we_obj_t *children_head;
    } we_child_owner_t;

    /* ---------------- 中央动画引擎 ----------------
     * 动画节点内嵌在控件结构体里：零堆、不占用任何槽位、数量无上限。
     * we_gui_task_handler 每个调度周期遍历该链表并调用 step_cb；
     * 控件动画到达目标态后在 step_cb 内自行 we_anim_stop 摘链。
     * 约定：
     * 1. step_cb 内只允许摘除自身节点，不得摘除其他节点；
     * 2. 删除带动画的控件前必须先 we_anim_stop（节点归控件所有，
     *    内核无法代摘，否则链上留悬空指针——同 pressed_obj 教训）。 */
    typedef struct we_anim_s
    {
        struct we_anim_s *next;                            /* 侵入式链表指针 */
        void (*step_cb)(void *owner, uint16_t elapsed_ms); /* 每周期推进回调 */
        void *owner;                                       /* 回指控件实例 */
    } we_anim_t;

    typedef struct we_lcd_t
    {
        // 1. PFB 显存管理
        colour_t *pfb_gram;      // 当前活动的显存指针 (可用于DMA切换)
        colour_t *pfb_gram_base; // 显存基地址
        uint16_t pfb_size;
        we_lcd_set_addr_cb_t set_addr_cb; // 当前 LCD 绑定的设窗接口
        we_lcd_flush_cb_t flush_cb;       // 当前 LCD 绑定的刷屏接口
        we_area_t pfb_area;
        uint16_t pfb_width, pfb_y_start, pfb_y_end;

        // 2. 屏幕属性
        uint16_t width;
        uint16_t height;
        colour_t bg_color;

        // 3. 脏矩形管理器 (彻底干掉全局变量，完美内嵌！)
        we_dirty_mgr_t dirty_mgr;
        we_obj_t *obj_list_head;
        we_gui_timer_node_t timer_list[WE_CFG_GUI_TIMER_MAX_NUM]; // 面向用户的 GUI 定时器表
        we_anim_t *anim_head;                                     // 中央动画链表头（控件动画不占用任何槽位）
        uint16_t tick_elapsed_ms;                                 // GUI 内核累计的未消费时间
        we_indev_data_t indev_data;                               // 当前 LCD 实例绑定的输入状态缓存
        int16_t gesture_press_x;                                  // 手势识别：按下时 X 坐标
        int16_t gesture_press_y;                                  // 手势识别：按下时 Y 坐标
        we_obj_t *pressed_obj;                                    // 当前按压中的对象（we_obj_delete 会同步清空，防悬空派发）
        we_input_read_cb_t input_read_cb;                         // 当前 LCD 绑定的输入读取接口
        we_storage_read_cb_t storage_read_cb;                     // 当前 LCD 绑定的外部存储读取接口
        we_popup_layer_t popup_layer;                             // LCD 级单 overlay popup
        uint8_t opa_scale;                                        // 容器透明度级联乘子，255=无衰减（group 画子时设置，原语入口消费）
        uint8_t gesture_had_stay;                                 // 本次触摸序列是否经历过 STAY（拖拽）；与相邻字节成员共享对齐槽
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
        we_obj_t *focus_obj;                     // 当前焦点对象（NULL=无；we_obj_delete 沿祖先链防悬空）
        uint8_t focus_flags;                     // WE_FOCUS_F_* 位组合（编辑态/OK按住/OK武装/松开挂起）
        uint8_t key_flash_left_ms;               // OK 最短按压窗口剩余毫秒（task_handler 直接倒计时，不占动画节点）
        uint8_t key_queue[WE_CFG_KEY_QUEUE_LEN]; // 语义键环形队列（SPSC：注入侧只写 tail，消费侧只写 head，中断注入安全）
        volatile uint8_t key_q_head;             // 队头下标（仅消费侧写；head==tail 为空）
        volatile uint8_t key_q_tail;             // 队尾下标（仅注入侧写；容量 = 队列深度-1）
#endif

        /* 渲染统计信息
         *
         * 统计口径：
         * 1. stat_render_frames：真正完成一次脏区提交就记 1 帧
         * 2. stat_pfb_pushes：这一帧里实际推了多少个 PFB 小块
         * 3. stat_pushed_pixels：累计真正送到底层 LCD 的像素数
         *
         * 这样在 STM32 真机上看性能时，不会只剩一个 FPS 数字，
         * 还能一起判断是否是“脏矩形太碎”导致效率下降。
         */
#if (WE_CFG_ENABLE_RENDER_STATS == 1)
        uint32_t stat_render_frames;
        uint32_t stat_pfb_pushes;
        uint32_t stat_pushed_pixels;
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

    typedef struct
    {
        uint16_t adv_w;   // 步进宽度 (用于光标前进)
        uint16_t box_w;   // 有效墨迹裁剪宽度
        uint16_t box_h;   // 有效墨迹裁剪高度
        int16_t x_ofs;    // 有效墨迹 X 偏移
        int16_t y_ofs;    // 有效墨迹 Y 偏移
        uint32_t offset;  // font2c internal: 相对 bitmap_data 的偏移
    } we_glyph_info_t;

    // API 声明

    /**
     * @brief 将动画节点挂入中央动画链表（已在链上则仅更新回调与 owner）
     * @param lcd 传入，当前 GUI 屏幕上下文指针
     * @param anim 传入，内嵌于控件的动画节点
     * @param step_cb 传入，每调度周期推进回调（elapsed_ms 为本周期毫秒数）
     * @param owner 传入，回调透传的控件实例指针
     * @return 无
     * @note 不占用任何槽位、不会失败；动画完成后请在 step_cb 内调用
     *       we_anim_stop 摘链，删除控件前必须先 we_anim_stop。
     */
    void we_anim_start(we_lcd_t *lcd, we_anim_t *anim,
                       void (*step_cb)(void *owner, uint16_t elapsed_ms), void *owner);

    /**
     * @brief 将动画节点从中央动画链表摘除（不在链上则为空操作）
     * @param lcd 传入，当前 GUI 屏幕上下文指针
     * @param anim 传入，待摘除的动画节点
     * @return 无
     */
    void we_anim_stop(we_lcd_t *lcd, we_anim_t *anim);
    /**
     * @brief 初始化脏矩形管理器
     * @param mgr 传入：待初始化的脏矩形管理器指针
     * @return 无
     */
    void we_dirty_init(we_dirty_mgr_t *mgr);
    /**
     * @brief 登记一个脏矩形区域
     * @param mgr 传入：脏矩形管理器指针
     * @param x 传入：区域左上角 X 坐标
     * @param y 传入：区域左上角 Y 坐标
     * @param w 传入：区域宽度
     * @param h 传入：区域高度
     * @return 无
     */
    void we_dirty_invalidate(we_dirty_mgr_t *mgr, int16_t x, int16_t y, int16_t w, int16_t h);
    /**
     * @brief 登记一个矩形脏区，但排除其中一个安全空洞矩形
     * @param mgr 传入：脏矩形管理器指针
     * @param x 传入：整体区域左上角 X 坐标
     * @param y 传入：整体区域左上角 Y 坐标
     * @param w 传入：整体区域宽度
     * @param h 传入：整体区域高度
     * @param ex 传入：排除区域左上角 X 坐标
     * @param ey 传入：排除区域左上角 Y 坐标
     * @param ew 传入：排除区域宽度
     * @param eh 传入：排除区域高度
     * @return 无
     * @note 该函数只在新增脏区时把 bbox-hole 拆成最多 4 个矩形提交，
     *       不会从已有 dirty list 中删除任何区域。
     */
    void we_dirty_invalidate_exclude(we_dirty_mgr_t *mgr, int16_t x, int16_t y, int16_t w, int16_t h,
                                     int16_t ex, int16_t ey, int16_t ew, int16_t eh);
    /**
     * @brief 取出下一个待刷新的脏矩形
     * @param mgr 传入：脏矩形管理器指针
     * @param out_rect 传出：取出的脏矩形结果
     * @param iterator 传入传出：遍历迭代器
     * @return 1 表示成功取到脏矩形，0 表示已经遍历结束
     */
    uint8_t we_dirty_get_next(we_dirty_mgr_t *mgr, we_rect_t *out_rect, uint8_t *iterator);
    /**
     * @brief 清空所有脏矩形
     * @param mgr 传入：脏矩形管理器指针
     * @return 无
     */
    void we_dirty_clear(we_dirty_mgr_t *mgr);

    /* ================== WE-GUI 核心 API 声明 ================== */
    /**
     * @brief 向 GUI 内核累计经过的时间
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param ms 传入：本次经过的毫秒数
     * @return 无
     */
    void we_gui_tick_inc(we_lcd_t *p_lcd, uint16_t ms);
    /* GUI timer 使用说明
     *
     * 1. timer 面向业务层使用，适合“按时间触发”的逻辑：
     *    页面自动切换、消息自动关闭、数值轮询、demo tick 等；
     * 2. create 成功后会立即进入激活状态，通常不需要再额外 start；
     * 3. repeat = 1 表示周期定时器，repeat = 0 表示单次定时器；
     * 4. 回调参数 elapsed_ms 固定等于创建时的 period_ms。
     *    当主循环偶尔抖动时，内核会按周期补偿多次回调，
     *    而不是把一个很大的时间片一次性塞给业务层；
     * 5. 常见用法示例：
     *    int8_t anim_timer = we_gui_timer_create(lcd, we_btn_simple_demo_tick, 16U, 1U);
     *    int8_t hide_timer = we_gui_timer_create(lcd, hide_cb, 1200U, 0U);
     *    we_gui_timer_restart(lcd, hide_timer);
     *    we_gui_timer_stop(lcd, anim_timer);
     */
    /**
     * @brief 创建并启动一个 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param cb 传入，定时器回调函数，回调参数中的 elapsed_ms 为定时器周期值。
     * @param period_ms 传入，定时器周期，单位毫秒，必须大于 0。
     * @param repeat 传入，1 表示周期定时器，0 表示单次定时器。
     * @return 定时器编号，成功时返回 0 ~ WE_CFG_GUI_TIMER_MAX_NUM-1，失败返回 -1。
     * @note 这是面向业务层的公开时间调度接口，适合延时触发、周期刷新和 demo 动画。
     */
    int8_t we_gui_timer_create(we_lcd_t *p_lcd, we_gui_timer_cb_t cb, uint16_t period_ms, uint8_t repeat);
    /**
     * @brief 启动一个已创建的 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param timer_id 传入，定时器编号。
     * @return 无。
     * @note 这个接口主要用于“先 stop 后恢复”的场景。
     *       定时器在 create 成功后默认就是激活状态。
     */
    void we_gui_timer_start(we_lcd_t *p_lcd, int8_t timer_id);
    /**
     * @brief 停止一个 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param timer_id 传入，定时器编号。
     * @return 无。
     * @note 停止后会清空当前累计时间，再次启动时重新开始计时。
     */
    void we_gui_timer_stop(we_lcd_t *p_lcd, int8_t timer_id);
    /**
     * @brief 重启一个 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param timer_id 传入，定时器编号。
     * @return 无。
     * @note 重启会清空累计时间并重新进入激活状态。
     */
    void we_gui_timer_restart(we_lcd_t *p_lcd, int8_t timer_id);
    /**
     * @brief 删除一个 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param timer_id 传入，定时器编号。
     * @return 无。
     * @note 删除后槽位会被完全清空，可供后续重新创建。
     */
    void we_gui_timer_delete(we_lcd_t *p_lcd, int8_t timer_id);
    /**
     * @brief 执行一次 GUI 主任务处理
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @return 无
     */
    void we_gui_task_handler(we_lcd_t *p_lcd);
    /**
     * @brief 向 GUI 提交一次输入事件
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param data 传入：输入数据
     * @return 无
     */
    void we_gui_indev_handler(we_lcd_t *lcd, we_indev_data_t *data);

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /**
     * @brief 注入语义按键的按下沿（端口在消抖后的按下沿调用）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param key 传入：WE_KEY_UP..WE_KEY_BACK 语义键值（WE_KEY_EVT_* 通知类禁止注入）
     * @return 无
     * @note 方向/前后/BACK 键按下沿即触发，长按连发由端口重复注入本函数实现；
     *       OK 键按下沿进入按压态，需配合 we_gui_key_release 在松开时触发动作。
     */
    void we_gui_key_press(we_lcd_t *lcd, uint8_t key);
    /**
     * @brief 注入语义按键的松开沿（端口在按键释放时调用）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param key 传入：WE_KEY_UP..WE_KEY_BACK 语义键值
     * @return 无
     * @note 目前只有 OK 键消费松开沿（回弹按压态并触发点击），
     *       其余键的松开沿会被静默忽略，端口可只对 OK 上报松开。
     */
    void we_gui_key_release(we_lcd_t *lcd, uint8_t key);
    /**
     * @brief 注入一次完整按键（按下沿 + 松开沿，tap 语义）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param key 传入：WE_KEY_UP..WE_KEY_BACK 语义键值
     * @return 无
     * @note 面向只上报"按了一下"的简单端口（三键板/编码器等）；OK 键
     *       经最短按压窗口（WE_CFG_FOCUS_FLASH_MS）保证按压视觉可见。
     */
    void we_gui_key_inject(we_lcd_t *lcd, uint8_t key);
    /**
     * @brief 程序化设置/清除焦点
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param obj 传入：目标对象；NULL 表示清除焦点
     * @return 无
     * @note 目标须可聚焦（类带 key_cb，或为子树含可聚焦控件的复合容器）；
     *       交互控件可在 WE_KEY_EVT_FOCUS 查询中按实例拒绝（如 DISABLED）。
     */
    void we_focus_set(we_lcd_t *lcd, we_obj_t *obj);
    /**
     * @brief 查询当前焦点对象
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 焦点对象指针，无焦点返回 NULL
     */
    we_obj_t *we_focus_get(we_lcd_t *lcd);
    /**
     * @brief 结构性判定对象是否可成为焦点候选（不触发 FOCUS 实例查询）
     * @param obj 传入：待判定对象
     * @return 1 表示候选（自带 key_cb 的交互控件，或子树含候选的复合容器）
     * @note 供容器控件在 WE_KEY_EVT_FOCUS 查询里判断"子树里有没有可聚焦
     *       内容"（如 scroll_panel：空面板拒绝聚焦避免死停靠点）。
     */
    uint8_t we_focus_candidate(we_obj_t *obj);
#if (WE_CFG_FOCUS_EDIT == 1)
    /**
     * @brief 进入编辑态（焦点光标换编辑色，方向键交焦点控件调值）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 无
     * @note 由值类控件在 key_cb 的 WE_KEY_OK 分支调用；无焦点时空操作。
     *       编辑态下未被控件消费的 OK/BACK 由管理器退出编辑，其余导航键吞掉。
     */
    void we_focus_edit_enter(we_lcd_t *lcd);
    /**
     * @brief 退出编辑态（焦点光标恢复导航色）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 无
     */
    void we_focus_edit_exit(we_lcd_t *lcd);
    /**
     * @brief 查询当前是否处于编辑态
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 1 表示编辑态，0 表示导航态
     */
    uint8_t we_focus_edit_active(we_lcd_t *lcd);
#else
    /* WE_CFG_FOCUS_EDIT=0：编辑态整体编译剔除，API 退化为空操作 stub，
     * 应用层代码无需条件编译即可两态通编。 */
    static inline void we_focus_edit_enter(we_lcd_t *lcd) { (void)lcd; }
    static inline void we_focus_edit_exit(we_lcd_t *lcd) { (void)lcd; }
    static inline uint8_t we_focus_edit_active(we_lcd_t *lcd)
    {
        (void)lcd;
        return 0U;
    }
#endif
#endif

    /**
     * @brief 为指定 LCD 实例注册输入读取接口
     * @param p_lcd 传入：待绑定输入接口的 GUI 屏幕上下文指针
     * @param read_cb 传入：输入读取回调，传 NULL 表示当前 LCD 不启用输入采集
     * @return 无
     * @note 该接口只负责登记“如何读取输入”，真正的事件分发仍由主循环决定何时调用
     *       we_gui_indev_handler()。
     */
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
    void we_input_init_with_port(we_lcd_t *p_lcd, we_input_read_cb_t read_cb);
#endif
    /**
     * @brief 为指定 LCD 实例注册外部存储读取接口
     * @param p_lcd 传入：待绑定存储接口的 GUI 屏幕上下文指针
     * @param read_cb 传入：存储读取回调，参数顺序为(存储地址 addr, 数组指针 buf, 读取数量 len)，与 typedef 一致
     * @return 无
     * @note 传 NULL 表示当前 LCD 不启用外部存储读取能力。
     */
#if (WE_CFG_ENABLE_STORAGE_PORT_BIND == 1)
    void we_storage_init_with_port(we_lcd_t *p_lcd, we_storage_read_cb_t read_cb);
#endif
    /**
     * @brief 使用指定的 PFB 缓冲区和底层端口接口初始化 GUI 屏幕上下文
     * @param p_lcd 传入：待初始化的 GUI 屏幕上下文指针
     * @param bg 传入：默认背景色
     * @param gram_base 传入：用户指定的 PFB 缓冲区基址，传 NULL 时使用库内默认缓冲区
     * @param gram_size 传入：用户指定的 PFB 缓冲总大小
     * @param set_addr_cb 传入：用户指定的设窗接口，传 NULL 时使用库内默认设窗接口
     * @param flush_cb 传入：用户指定的刷屏接口，传 NULL 时使用库内默认刷屏接口
     * @return 无
     * @note 该接口适合：
     *       1. 手动指定 GRAM 所在内存区；
     *       2. 运行时切换不同刷屏端口；
     *       3. 在保留默认初始化接口的前提下，扩展特殊平台用法。
     */
    void we_lcd_init_with_port(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
                               we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb);
    /**
     * @brief 一次性完成 LCD / 输入 / 存储三路端口绑定
     * @param p_lcd       传入：GUI 屏幕上下文指针
     * @param bg          传入：默认背景色
     * @param gram_base   传入：PFB 缓冲区基址，传 NULL 使用库内默认缓冲区
     * @param gram_size   传入：PFB 缓冲总大小
     * @param set_addr_cb 传入：设窗接口
     * @param flush_cb    传入：刷屏接口
     * @param input_cb    传入：输入读取回调；WE_CFG_ENABLE_INPUT_PORT_BIND==0 时忽略，可传 NULL
     * @param storage_cb  传入：存储读取回调；WE_CFG_ENABLE_STORAGE_PORT_BIND==0 时忽略，可传 NULL
     * @return 无
     */
    void we_gui_init(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
                     we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb,
                     we_input_read_cb_t input_cb, we_storage_read_cb_t storage_cb);
    /**
     * @brief 将当前 PFB 的一块区域推送到底层显示端口
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x 传入：目标区域左上角 X 坐标
     * @param y 传入：目标区域左上角 Y 坐标
     * @param w 传入：区域宽度
     * @param h 传入：区域高度
     * @return 无
     */
    void we_push_pfb(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h);
    /**
     * @brief 清空当前 PFB 显存内容
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @return 无
     */
    void we_clear_gram(we_lcd_t *p_lcd);
    /**
     * @brief 用纯色填充整个 PFB
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param c 传入：填充颜色
     * @return 无
     */
    void we_fill_gram(we_lcd_t *p_lcd, colour_t c);

    /* === 通用控件操作 API ===*/
    /**
     * @brief 修改对象绝对坐标
     * @param obj 传入：目标对象指针
     * @param new_x 传入：新的 X 坐标
     * @param new_y 传入：新的 Y 坐标
     * @return 无
     */
    void we_obj_set_pos(we_obj_t *obj, int16_t new_x, int16_t new_y);
    /**
     * @brief 将对象追加到指定对象链表尾部
     * @param head_p 传入：链表头指针地址
     * @param obj 传入：待追加对象
     * @return 无
     */
    void we_obj_append_to_list(we_obj_t **head_p, we_obj_t *obj);
    /**
     * @brief 将对象追加到 LCD 顶层对象链表尾部
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param obj 传入：待追加对象
     * @return 无
     */
    void we_obj_attach_to_lcd(we_lcd_t *lcd, we_obj_t *obj);
    /**
     * @brief 删除一个对象
     * @param obj 传入：待删除对象指针
     * @return 无
     */
    void we_obj_delete(we_obj_t *obj);
    /**
     * @brief 将对象提升到同级链表最前端（最后绘制）
     * @param obj 传入：目标对象指针
     * @return 无
     * @note 若对象属于父容器，则在父容器 children_head 内重排；
     *       否则在 lcd 顶层对象链表中重排。
     */
    void we_obj_bring_to_front(we_obj_t *obj);
    /**
     * @brief 按对象当前区域执行智能标脏
     * @param obj 传入：目标对象指针
     * @return 无
     * @note 标脏区域会沿 parent 链逐层裁剪，避免越界刷新。
     */
    void we_obj_invalidate(we_obj_t *obj);
    /**
     * @brief 按对象局部区域执行智能标脏
     * @param obj 传入：目标对象指针
     * @param x 传入：局部区域左上角 X 坐标（相对对象坐标系）
     * @param y 传入：局部区域左上角 Y 坐标（相对对象坐标系）
     * @param w 传入：局部区域宽度
     * @param h 传入：局部区域高度
     * @return 无
     * @note 标脏区域会沿 parent 链逐层裁剪，适合局部内容更新。
     */
    void we_obj_invalidate_area(we_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h);
    /**
     * @brief 按对象区域标脏，但排除其中一个安全空洞矩形
     * @param obj 传入：目标对象指针
     * @param x 传入：整体区域左上角 X 坐标（屏幕绝对坐标）
     * @param y 传入：整体区域左上角 Y 坐标（屏幕绝对坐标）
     * @param w 传入：整体区域宽度
     * @param h 传入：整体区域高度
     * @param ex 传入：排除区域左上角 X 坐标（屏幕绝对坐标）
     * @param ey 传入：排除区域左上角 Y 坐标（屏幕绝对坐标）
     * @param ew 传入：排除区域宽度
     * @param eh 传入：排除区域高度
     * @return 无
     * @note 标脏区域会沿 parent 链逐层裁剪；该接口只拆分本次新增脏区，
     *       不会从已有 dirty list 中删除任何区域。
     */
    void we_obj_invalidate_area_exclude(we_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h,
                                        int16_t ex, int16_t ey, int16_t ew, int16_t eh);

    /* === LCD 级 overlay popup API === */
    void we_popup_layer_open(we_lcd_t *lcd, uint8_t type, void *owner,
                             const we_area_t *area,
                             void (*draw_cb)(void *owner),
                             uint8_t (*event_cb)(void *owner, we_event_t event, we_indev_data_t *data),
                             void (*close_cb)(void *owner));
    void we_popup_layer_close(we_lcd_t *lcd, void *owner);
    void we_popup_layer_close_any(we_lcd_t *lcd);
    uint8_t we_popup_layer_is_owner(we_lcd_t *lcd, void *owner);
    void we_popup_layer_set_area(we_lcd_t *lcd, void *owner, const we_area_t *area);
    void we_popup_layer_invalidate(we_lcd_t *lcd);
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /**
     * @brief 为当前 overlay popup 挂接语义键回调（弹层键通道）
     * @param lcd 传入，GUI 屏幕上下文指针
     * @param owner 传入，popup 拥有者（不匹配则忽略）
     * @param key_cb 传入，键回调；NULL 恢复"吞掉全部按键"模态语义
     * @return 无
     * @note 在 we_popup_layer_open 之后调用；弹层关闭时自动清空。
     */
    void we_popup_layer_set_key_cb(we_lcd_t *lcd, void *owner,
                                   uint8_t (*key_cb)(void *owner, uint8_t key));
#endif

    /* === 图形绘制 API === */
    /**
     * @brief 绘制一块带 alpha 的遮罩图形
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x 传入：绘制起点 X 坐标
     * @param y 传入：绘制起点 Y 坐标
     * @param w 传入：遮罩宽度
     * @param h 传入：遮罩高度
     * @param src_data 传入：遮罩数据指针
     * @param bpp 传入：遮罩位深
     * @param fg_color 传入：前景颜色
     * @param opacity 传入：透明度
     * @return 无
     */
    void we_draw_alpha_mask(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h,
                            const unsigned char *src_data, uint8_t bpp, colour_t fg_color, uint8_t opacity);
    /**
     * @brief 绘制纯色矩形
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x 传入：左上角 X 坐标
     * @param y 传入：左上角 Y 坐标
     * @param w 传入：宽度
     * @param h 传入：高度
     * @param color 传入：填充颜色
     * @param opacity 传入：透明度
     * @return 无
     */
    void we_fill_rect(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h, colour_t color, uint8_t opacity);
    /**
     * @brief 在 PFB 缓冲区内写入单个像素
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param px 传入：像素 X 坐标 (屏幕绝对坐标)
     * @param py 传入：像素 Y 坐标
     * @param color 传入：像素颜色
     * @param opacity 传入：透明度 (0~255)
     * @return 无
     */
    void we_draw_pixel(we_lcd_t *p_lcd, int16_t px, int16_t py, colour_t color, uint8_t opacity);

    /**
     * @brief 绘制粗线段 (Bresenham + 厚度扩展)
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x0 传入：起点 X
     * @param y0 传入：起点 Y
     * @param x1 传入：终点 X
     * @param y1 传入：终点 Y
     * @param thickness 传入：线宽 (像素)
     * @param color 传入：线段颜色
     * @param opacity 传入：透明度 (0~255)
     * @return 无
     */
    void we_draw_line(we_lcd_t *p_lcd, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, uint8_t thickness,
                      colour_t color, uint8_t opacity);

    /**
     * @brief 绘制圆头抗锯齿线段（单遍胶囊覆盖，半透明下圆头与线身不叠色）
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x0 传入：起点 X
     * @param y0 传入：起点 Y
     * @param x1 传入：终点 X
     * @param y1 传入：终点 Y
     * @param width 传入：线宽 (像素)
     * @param color 传入：线段颜色
     * @param opacity 传入：透明度 (0~255)
     * @return 无
     */
    void we_draw_line_round(we_lcd_t *p_lcd, int16_t x0, int16_t y0,
                            int16_t x1, int16_t y1, uint8_t width,
                            colour_t color, uint8_t opacity);

    /* === 文本排版 API === */
    /**
     * @brief 计算一段文本的显示宽度
     * @param font_array 传入：字库数组指针
     * @param str 传入：UTF-8 字符串
     * @return 文本宽度，单位为像素
     * @note 遇到换行符会停止累加，仅统计第一行宽度（与 we_font_text.h 保持一致）。
     */
    uint16_t we_get_text_width(const unsigned char *font_array, const char *str);
    /**
     * @brief 测量字符串实际可见的垂直范围（相对于 we_draw_string 的 y 参数）
     * @param font_array  传入：字库数组指针
     * @param str         传入：UTF-8 字符串，只测量第一行
     * @param out_y_top   传出：可见内容最顶端偏移（相对 cursor_y，通常为正值）
     * @param out_y_bottom 传出：可见内容最底端偏移（相对 cursor_y，= y_ofs + box_h）
     * @return 无
     * @note 居中公式：cursor_y = center_y - (*out_y_top + *out_y_bottom) / 2
     */
    void we_get_text_bbox(const unsigned char *font_array, const char *str, int8_t *out_y_top, int8_t *out_y_bottom);
    /**
     * @brief 读取单个字形的排版信息
     * @param font_array 传入：字库数组指针
     * @param code 传入：Unicode 码点
     * @param out_info 传出：字形信息结构
     * @return 1 表示找到字形，0 表示未找到
     */
    uint8_t we_font_get_glyph_info(const unsigned char *font_array, uint16_t code, we_glyph_info_t *out_info);
    /**
     * @brief 获取字库的标准行高
     * @param font_array 传入：字库数组指针
     * @return 行高（像素），失败返回 0
     */
    uint16_t we_font_get_line_height(const unsigned char *font_array);
    /**
     * @brief 获取字形点阵地址、位深和行跨度信息
     * @param font_array 传入：字库数组指针
     * @param info 传入：已有的字形信息结构体
     * @param out_bitmap 传出：字形点阵起始地址
     * @param out_bpp 传出：字形位深（1/2/4/8 bpp）
     * @param out_row_stride 传出：点阵每行字节数
     * @return 1 表示成功，0 表示失败或参数非法
     */
    uint8_t we_font_get_bitmap_info(const unsigned char *font_array, const we_glyph_info_t *info,
                                    const uint8_t **out_bitmap, uint8_t *out_bpp, uint32_t *out_row_stride);
    /**
     * @brief 在指定位置绘制一段字符串
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x 传入：起始绘制 X 坐标
     * @param y 传入：起始绘制 Y 坐标
     * @param font_array 传入：字库数组指针
     * @param str 传入：UTF-8 字符串
     * @param fg_color 传入：前景颜色
     * @param opacity 传入：透明度
     * @return 无
     */
    void we_draw_string(we_lcd_t *p_lcd, int16_t x, int16_t y, const unsigned char *font_array, const char *str,
                        colour_t fg_color, uint8_t opacity);

#ifdef __cplusplus
}
#endif
#endif
