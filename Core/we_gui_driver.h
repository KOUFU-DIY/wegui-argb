#ifndef WE_GUI_DRIVER_H
#define WE_GUI_DRIVER_H

#include "we_font_runtime_types.h"
#include "arial_16_4bbp.h"
#define we_font_consolas_18 ((const unsigned char *)&arial_16_4bbp)
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

    typedef void (*we_gui_task_cb_t)(struct we_lcd_t *lcd, void *user_data, uint16_t elapsed_ms);
    typedef void (*we_gui_timer_cb_t)(struct we_lcd_t *lcd, uint16_t elapsed_ms);

    typedef struct
    {
        we_gui_task_cb_t cb;
        void *user_data;
    } we_gui_task_node_t;

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

    // 控件类描述符 (存放于 Flash，节省 RAM)
    typedef struct
    {
        void (*draw_cb)(void *obj);
        uint8_t (*event_cb)(void *obj, we_event_t event, we_indev_data_t *data);
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
     * base + task_id + children_head。
     * 目前 slideshow 等复合控件用它与 driver 共享父子摘链语义。 */
    typedef struct
    {
        we_obj_t base;
        int8_t task_id;
        we_obj_t *children_head;
    } we_child_owner_t;

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
        we_gui_task_node_t task_list[WE_CFG_GUI_TASK_MAX_NUM];    // GUI 内部周期任务表
        we_gui_timer_node_t timer_list[WE_CFG_GUI_TIMER_MAX_NUM]; // 面向用户的 GUI 定时器表
        uint16_t tick_elapsed_ms;                                 // GUI 内核累计的未消费时间
        we_indev_data_t indev_data;                               // 当前 LCD 实例绑定的输入状态缓存
        int16_t gesture_press_x;                                  // 手势识别：按下时 X 坐标
        int16_t gesture_press_y;                                  // 手势识别：按下时 Y 坐标
        we_input_read_cb_t input_read_cb;                         // 当前 LCD 绑定的输入读取接口
        we_storage_read_cb_t storage_read_cb;                     // 当前 LCD 绑定的外部存储读取接口

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
        uint32_t stat_render_frames;
        uint32_t stat_pfb_pushes;
        uint32_t stat_pushed_pixels;
    } we_lcd_t;

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
    /**
     * @brief GUI 内部注册一个带上下文数据的周期任务。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param cb 传入，任务回调函数指针。
     * @param user_data 传入，回调执行时要原样传回的上下文指针。
     * @return 任务编号，成功时返回 0 ~ WE_CFG_GUI_TASK_MAX_NUM-1，失败返回 -1。
     * @note 这是 GUI 内部扩展接口，不建议普通业务层直接使用。
     */
    int8_t _we_gui_task_register_with_data(we_lcd_t *p_lcd, we_gui_task_cb_t cb, void *user_data);
    /**
     * @brief GUI 内部注销一个已注册的周期任务。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param task_id 传入，待注销的任务编号。
     * @return 无。
     * @note 任务注销后，对应槽位会被清空，可供内部模块后续复用。
     */
    void _we_gui_task_unregister(we_lcd_t *p_lcd, int8_t task_id);
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
     * @param read_cb 传入：存储读取回调，参数顺序为(数组指针, 存储地址, 读取数量)
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
     * @brief 删除一个对象
     * @param obj 传入：待删除对象指针
     * @return 无
     */
    void we_obj_delete(we_obj_t *obj);
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

    /* === 文本排版 API === */
    /**
     * @brief 计算一段文本的显示宽度
     * @param font_array 传入：字库数组指针
     * @param str 传入：UTF-8 字符串
     * @return 文本宽度，单位为像素
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
