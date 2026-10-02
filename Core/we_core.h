#ifndef WE_CORE_H
#define WE_CORE_H

/* --------------------------------------------------------------------------
 * 屏幕实例初始化与主循环调度。
 * -------------------------------------------------------------------------- */

#include "we_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* we_gui_run 返回值：没有任何待办（无动画、无定时器、无触摸轮询），
 * 应用可以一直休眠到外部事件（按键中断、业务改了控件状态）为止 */
#define WE_GUI_RUN_IDLE (0xFFFFFFFFUL)

    /**
     * @brief 初始化屏幕实例并绑定端口
     * @param p_lcd 传入：待初始化的屏幕实例
     * @param port 传入：端口描述（set_addr 与 flush 必填；需在整个运行期有效，通常定义为 static const）
     * @param bg 传入：屏幕背景色
     * @param gram 传入：PFB 缓冲区基址
     * @param gram_pixels 传入：PFB 缓冲区容量（像素个数）
     * @return 无
     * @note GRAM_DMA_BUFF_EN=1 且 port->flush_async=1 时，PFB 一分为二交替使用
     *       （一半绘制、一半由 DMA 发送），此时每半至少一整行且行数是
     *       WE_LCD_FLUSH_ALIGN_Y 的整数倍，否则断言失败；阻塞端口始终整块使用。
     *       初始化后整屏标脏，第一次 we_gui_run 会完整刷新一帧。
     */
    void we_gui_init(we_lcd_t *p_lcd, const we_port_t *port, colour_t bg, colour_t *gram, uint16_t gram_pixels);

    /**
     * @brief 运行一次 GUI 主循环
     * @param p_lcd 传入：屏幕实例
     * @param now_ms 传入：从 0 起算的毫秒时间戳（回绕安全），通常为
     *               系统毫秒计数减去进入主循环时的起点
     * @return 距离下一次需要调用本函数的毫秒数：0 = 尽快再调；
     *         WE_GUI_RUN_IDLE = 没有待办，可一直休眠到外部事件
     * @note 顺序固定：采样触摸并派发 → 消费语义键 → 推进用户定时器 →
     *       推进动画并渲染。两次调用之间的时间差就是本次流逝的时间，
     *       单次超过 65535 毫秒按 65535 计；第一次调用的流逝时间就是 now_ms 本身。
     */
    uint32_t we_gui_run(we_lcd_t *p_lcd, uint32_t now_ms);

    /**
     * @brief 取屏幕实例绑定的外挂存储读取接口
     * @param p_lcd 传入：屏幕实例
     * @return 读取接口；未初始化或端口未提供时返回 NULL
     */
    static __inline we_storage_read_cb_t we_gui_storage(const we_lcd_t *p_lcd)
    {
        return (p_lcd != NULL && p_lcd->port != NULL) ? p_lcd->port->storage_read : NULL;
    }

#ifdef __cplusplus
}
#endif

#endif /* WE_CORE_H */
