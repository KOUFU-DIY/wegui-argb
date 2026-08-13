#ifndef WE_GUI_CONFIG_H
#define WE_GUI_CONFIG_H

#include "stdint.h"

#define DEEP_RGB565 (4) /* RGB565 */
#define DEEP_RGB888 (5) /* RGB888 */

/* 平台端口头必须显式给出以下配置。
 * Core 层不再提供默认值，避免默认值和平台值混用后产生重复定义或歧义。 */

#ifndef LCD_DEEP
#error "LCD_DEEP must be defined by platform port config.Like DEEP_RGB565"
#endif

#ifndef SCREEN_WIDTH
#error "SCREEN_WIDTH must be defined by platform port config."
#endif

#ifndef SCREEN_HEIGHT
#error "SCREEN_HEIGHT must be defined by platform port config."
#endif

#ifndef GRAM_DMA_BUFF_EN
#error "GRAM_DMA_BUFF_EN must be defined by platform port config."
#endif

/* 端口异步能力契约：
 * 平台端口配置声明 WE_PORT_FLUSH_ASYNC（1 = flush 为 DMA 异步发送，立即返回）。
 * GRAM 双缓冲只有在"用户开启 GRAM_DMA_BUFF_EN 且端口真异步"时才生效，
 * 阻塞端口下自动退回整块 PFB，避免砍半 PFB + 翻倍 flush 次数的负优化。
 * 未声明的旧平台保持历史行为（跟随 GRAM_DMA_BUFF_EN）。 */
#ifndef WE_PORT_FLUSH_ASYNC
#define WE_PORT_FLUSH_ASYNC (GRAM_DMA_BUFF_EN)
#endif

/* 刷新区域像素对齐粒度（QSPI 彩屏 / SSD1306 页式 OLED 等硬件需求）。
 * 部分屏幕对刷新窗口坐标有硬件粒度要求：
 *   - QSPI 接口彩屏常要求 set_addr 的 x/y 起止坐标为 2 或 4 的倍数；
 *   - SSD1306 等页式单色 OLED 以 8 行为一页，y 向必须按 8 对齐。
 * 语义：脏矩形入库时（Core/dirty_driver.c 的 we_dirty_invalidate）把矩形
 * 扩张到对齐边界——x0/y0 向下取整到对齐倍数，x1/y1（包含端点）向上取整到
 * 对齐倍数-1。扩出的边缘随本矩形整块走正常渲染路径重绘，因此推屏时
 * set_addr 收到的窗口天然对齐，且像素流与窗口严格一致。
 * 取值必须为 2 的幂；平台端口不定义时默认 1（不对齐），
 * 现有平台零行为、零开销变化。 */
#ifndef WE_LCD_FLUSH_ALIGN_X
#define WE_LCD_FLUSH_ALIGN_X (1)
#endif

#ifndef WE_LCD_FLUSH_ALIGN_Y
#define WE_LCD_FLUSH_ALIGN_Y (1)
#endif

#if (WE_LCD_FLUSH_ALIGN_X < 1) || ((WE_LCD_FLUSH_ALIGN_X & (WE_LCD_FLUSH_ALIGN_X - 1)) != 0)
#error "WE_LCD_FLUSH_ALIGN_X must be a power of two (1/2/4/8...): dirty_driver.c expands rects with x0 &= ~(A-1) / x1 |= (A-1), which is only valid for power-of-two A."
#endif

#if (WE_LCD_FLUSH_ALIGN_Y < 1) || ((WE_LCD_FLUSH_ALIGN_Y & (WE_LCD_FLUSH_ALIGN_Y - 1)) != 0)
#error "WE_LCD_FLUSH_ALIGN_Y must be a power of two (1/2/4/8...): dirty_driver.c expands rects with y0 &= ~(A-1) / y1 |= (A-1), which is only valid for power-of-two A."
#endif

#if ((SCREEN_WIDTH % WE_LCD_FLUSH_ALIGN_X) != 0)
#error "SCREEN_WIDTH must be a multiple of WE_LCD_FLUSH_ALIGN_X: otherwise a rect touching the right screen edge cannot be both clamped on-screen and X-aligned when expanded."
#endif

#if ((SCREEN_HEIGHT % WE_LCD_FLUSH_ALIGN_Y) != 0)
#error "SCREEN_HEIGHT must be a multiple of WE_LCD_FLUSH_ALIGN_Y: otherwise a rect touching the bottom screen edge cannot be both clamped on-screen and Y-aligned when expanded."
#endif

#if (((USER_GRAM_NUM / SCREEN_WIDTH) % WE_LCD_FLUSH_ALIGN_Y) != 0)
#error "PFB row count (USER_GRAM_NUM / SCREEN_WIDTH) must be a multiple of WE_LCD_FLUSH_ALIGN_Y: we_push_pfb splits each dirty rect into PFB-row chunks after one set_addr, and chunk boundaries inherit the Y alignment only if the PFB row capacity is itself a multiple of it (page-packing flush ports rely on whole pages per chunk)."
#endif

#ifndef WE_CFG_DIRTY_STRATEGY
#error "WE_CFG_DIRTY_STRATEGY must be defined by platform port config."
#endif

#ifndef WE_CFG_DIRTY_MAX_NUM
#error "WE_CFG_DIRTY_MAX_NUM must be defined by platform port config."
#endif

#ifndef WE_CFG_DEBUG_DIRTY_RECT
#error "WE_CFG_DEBUG_DIRTY_RECT must be defined by platform port config."
#endif

#ifndef WE_CFG_ENABLE_INDEXED_QOI
#error "WE_CFG_ENABLE_INDEXED_QOI must be defined by platform port config."
#endif

/* 索引QOI_MASK（A8 透明蒙版压缩）解码裁剪开关：老工程配置无此宏时默认保留 */
#ifndef WE_CFG_ENABLE_INDEXQOI_MASK
#define WE_CFG_ENABLE_INDEXQOI_MASK (1)
#endif

#ifndef WE_CFG_ENABLE_INPUT_PORT_BIND
#error "WE_CFG_ENABLE_INPUT_PORT_BIND must be defined by platform port config."
#endif

#ifndef WE_CFG_ENABLE_STORAGE_PORT_BIND
#error "WE_CFG_ENABLE_STORAGE_PORT_BIND must be defined by platform port config."
#endif

/* 滑动手势识别阈值 (像素)，位移超过此值才判定为滑动。
 * 平台端口可自行 #define 覆盖，不定义则使用默认值。 */
#ifndef WE_CFG_SWIPE_THRESHOLD
#define WE_CFG_SWIPE_THRESHOLD 30
#endif

/* 渲染性能统计开关。
 * 1：保留 stat_render_frames 计帧计数器，供 FPS 显示与真机帧率观测；
 * 0：从 we_lcd_t 中去掉该 uint32 字段并消除计帧自增，
 *    适合对 RAM 敏感的量产固件。
 * 平台端口不定义时默认启用，保证 FPS demo 等开箱即用。 */
#ifndef WE_CFG_ENABLE_RENDER_STATS
#define WE_CFG_ENABLE_RENDER_STATS 1
#endif

/* 控件性能压测开关。
 * 1：每帧强制把所有顶层控件按其脏矩形重新标脏，使 GUI 持续全量重绘，
 *    配合 FPS / stat 计数器即可测出当前页面控件的极限渲染性能；
 * 0：正常按需重绘（仅有内容变化的控件才会刷新）。
 * 仅用于性能调试，量产固件请保持 0。
 * 注意：开启后帧率会明显下降，这是预期现象（衡量的是最坏情况吞吐）。 */
#ifndef WE_CFG_DEBUG_PERF_STRESS
#define WE_CFG_DEBUG_PERF_STRESS 0
#endif

/* 断言钩子：开发期可在用户配置里定义为自己的处理（打印/死循环/断点），
 * 发布档保持默认空实现，零代码零开销。
 * 覆盖范围：对象/定时器/动画/模态的生命周期 API 与各 init 绑定入口
 * （"参数明显非法 = 调用方代码写错"的冷入口）；断言之后紧跟的静默
 * 判空守卫保持不变——发布档行为始终是容错返回，断言只是开发期放大器。
 * 不覆盖：逐帧热路径（tick/task_handler/标脏/命中测试）与可在中断里
 * 调用的键注入三函数（we_gui_key_inject/press/release）。 */
#ifndef WE_ASSERT
#define WE_ASSERT(expr) ((void)0)
#endif

/* 定时器单帧补偿上限：主循环被长时间阻塞（flash 擦写/长中断）后恢复时，
 * 周期定时器按补偿语义会在一帧内连续补发错过的节拍——本宏给补发次数
 * 封顶，超出部分丢弃（从当前时刻重新计时），避免恢复帧被回调风暴卡死。
 * 正常运行（每帧最多触发 1~2 次）不受影响。 */
#ifndef WE_CFG_TIMER_CATCHUP_MAX
#define WE_CFG_TIMER_CATCHUP_MAX 4
#endif

/* 手势接管阈值（像素）：按压后位移超过此值，内核沿祖先链发起
 * WE_EVENT_DRAG_BEGIN 接管询问，让可滚动容器从子控件手里接管手势。
 * 与 WE_CFG_SWIPE_THRESHOLD（松手时判定快扫，默认 30）是两码事。 */
#ifndef WE_CFG_DRAG_THRESHOLD
#define WE_CFG_DRAG_THRESHOLD 8
#endif

/* 层次输入子开关（默认开）。
 * 1：触摸命中测试递归下钻复合容器子树，STAY 位移越阈值时沿祖先链发起
 *    WE_EVENT_DRAG_BEGIN 接管询问——"可滚动容器里放交互控件"依赖这两者；
 * 0：命中测试只扫本层链表（容器整体作为一个控件被命中，子控件收不到
 *    触摸），接管询问编译剔除。控件全部平铺在屏幕上、不用 scroll_panel/
 *    list 类容器交互的产品可省下钻递归与询问逻辑的 ROM 与调用栈。 */
#ifndef WE_CFG_ENABLE_NESTED_INPUT
#define WE_CFG_ENABLE_NESTED_INPUT 1
#endif

/* 顶层链 + 模态子开关（默认开）。
 * 1：we_obj_attach_to_top 把对象挂顶层链（普通层与焦点光标之后绘制、
 *    命中优先），we_modal_open 声明模态（命中限顶层、未命中交模态对象、
 *    语义键双沿直送其 event_cb）；
 * 0：整套编译剔除——we_lcd_t 不含顶层链/模态字段，we_obj_attach_to_top
 *    退化为 we_obj_bring_to_front（仍置顶但可能被普通层后来者盖住），
 *    modal 三函数退化为空操作 stub（we_modal_get 恒返回 NULL）。
 *    msgbox/toast/dropdown 弹层照常编译可用，只是失去"保证置顶"与
 *    模态吞键/全屏命中语义。不用弹层类控件的产品可整段裁掉。 */
#ifndef WE_CFG_ENABLE_TOP_LAYER
#define WE_CFG_ENABLE_TOP_LAYER 1
#endif

/* 全局聚焦 + 按键导航总开关。
 * 1：启用 we_gui_key_inject 键值注入、焦点管理器、分层作用域导航
 *    （OK 进入容器 / BACK 退出容器）与驱动级矩形焦点光标；
 * 0：全部编译剔除——we_lcd_t 不含任何焦点字段、语义键事件不再派发、
 *    各控件按键回调整体不参与编译，纯触摸工程零 ROM/RAM 成本。
 * 平台端口不定义时默认关闭。 */
#ifndef WE_CFG_ENABLE_KEY_INPUT
#define WE_CFG_ENABLE_KEY_INPUT 0
#endif

#if (WE_CFG_ENABLE_KEY_INPUT == 1)

/* 编辑态子开关（默认开）。
 * 1：OK 可进入编辑态，方向键在值类控件（slider/stepper/roller/list）上调值；
 * 0：整个编辑态编译剔除——驱动侧编辑分支/编辑色/we_focus_edit_* 退化为空
 *    操作 stub，值类控件的按键回调整体不参与编译（不可聚焦）。
 *    只有按钮/开关类控件的极简按键面板可再省数百字节 ROM。 */
#ifndef WE_CFG_FOCUS_EDIT
#define WE_CFG_FOCUS_EDIT 1
#endif

/* 层级导航子开关（默认开）。
 * 1：焦点可经 OK 下钻进入复合容器（group 等）、BACK 逐层上退；
 * 0：候选判定不再递归容器子树、下钻代码剔除，焦点环只含顶层可聚焦
 *    控件，BACK 直接清除焦点。扁平界面产品可再省约两百字节 ROM。
 *    （触摸跟随/we_focus_set 仍可把焦点直接放到容器内的控件上。） */
#ifndef WE_CFG_FOCUS_NESTED
#define WE_CFG_FOCUS_NESTED 1
#endif

/* 语义键环形队列深度（须为 2 的幂且 >= 4；SPSC 单生产者单消费者语义，
 * 实际容量 = 深度-1，队满时丢弃新注入的键值；OK 松开沿被丢弃时消费侧
 * 会对账补投，不会卡在按压态）。
 * 按下/松开双沿各占一个槽位，tap 式注入一次占两格——深度 2（容量 1）
 * 连一次 tap 都装不下，故下限 4；默认给到 8。 */
#ifndef WE_CFG_KEY_QUEUE_LEN
#define WE_CFG_KEY_QUEUE_LEN 8
#endif

#if (WE_CFG_KEY_QUEUE_LEN < 4) || ((WE_CFG_KEY_QUEUE_LEN & (WE_CFG_KEY_QUEUE_LEN - 1)) != 0)
#error "WE_CFG_KEY_QUEUE_LEN must be a power of two >= 4 (a tap injects two edges; ring wraps with & (LEN-1) to avoid __aeabi_uidivmod on Cortex-M0)."
#endif

/* 焦点光标框线宽（像素）。 */
#ifndef WE_CFG_FOCUS_CURSOR_THICKNESS
#define WE_CFG_FOCUS_CURSOR_THICKNESS 2
#endif

/* 焦点光标框与控件包围盒之间的间隙（像素）。 */
#ifndef WE_CFG_FOCUS_CURSOR_GAP
#define WE_CFG_FOCUS_CURSOR_GAP 2
#endif

/* 焦点光标颜色（RGB888，内部按目标色深转换）。 */
#ifndef WE_CFG_FOCUS_CURSOR_R
#define WE_CFG_FOCUS_CURSOR_R 92
#endif
#ifndef WE_CFG_FOCUS_CURSOR_G
#define WE_CFG_FOCUS_CURSOR_G 181
#endif
#ifndef WE_CFG_FOCUS_CURSOR_B
#define WE_CFG_FOCUS_CURSOR_B 255
#endif

/* OK 最短按压窗口（毫秒，≤255）：tap 式注入（一按一松同周期到达）时，
 * 松开沿会挂起到窗口结束再交付，保证按压视觉至少可见这么久；
 * 端口双沿上报且真实按住超过窗口时不引入任何额外延迟。 */
#ifndef WE_CFG_FOCUS_FLASH_MS
#define WE_CFG_FOCUS_FLASH_MS 90U
#endif

#if (WE_CFG_FOCUS_FLASH_MS) > 255
#error "WE_CFG_FOCUS_FLASH_MS must fit in uint8 (<= 255): we_lcd_t stores the countdown in one byte."
#endif

/* 编辑态焦点光标颜色（RGB888）：值类控件 OK 进入编辑后光标换此色，
 * 与导航色区分"当前方向键在调值而不是移焦点"。 */
#ifndef WE_CFG_FOCUS_EDIT_R
#define WE_CFG_FOCUS_EDIT_R 255
#endif
#ifndef WE_CFG_FOCUS_EDIT_G
#define WE_CFG_FOCUS_EDIT_G 150
#endif
#ifndef WE_CFG_FOCUS_EDIT_B
#define WE_CFG_FOCUS_EDIT_B 60
#endif

#endif /* WE_CFG_ENABLE_KEY_INPUT */

#endif
