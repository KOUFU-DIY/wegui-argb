#ifndef WE_TICK_H
#define WE_TICK_H

/* --------------------------------------------------------------------------
 * 用户定时器与中央动画引擎：节点都由调用方持有，挂在屏幕实例的侵入式链表上。
 * -------------------------------------------------------------------------- */

#include "we_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

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

    /* GUI timer 使用说明
     *
     * 1. timer 面向业务层使用，适合“按时间触发”的逻辑：
     *    页面自动切换、消息自动关闭、数值轮询、demo tick 等；
     * 2. create 成功后会立即进入激活状态，通常不需要再额外 start；
     * 3. repeat = 1 表示周期定时器，repeat = 0 表示单次定时器；
     * 4. 回调参数 elapsed_ms 为距上次触发的真实流逝毫秒（≥ period_ms，
     *    上限 65535）；每帧至多触发一次，主循环阻塞的积压以一个大时间
     *    片一次上报，如何消化由业务层的累计逻辑决定；
     * 5. 常见用法示例（节点由调用方持有，静态或生命周期覆盖使用期）：
     *    static we_gui_timer_t anim_timer, hide_timer;
     *    we_gui_timer_create(lcd, &anim_timer, we_btn_simple_demo_tick, 16U, 1U);
     *    we_gui_timer_create(lcd, &hide_timer, hide_cb, 1200U, 0U);
     *    we_gui_timer_restart(lcd, &hide_timer);
     *    we_gui_timer_stop(lcd, &anim_timer);
     */
    /**
     * @brief 创建并启动一个 GUI 定时器（不会失败）。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param t 传入，调用方持有的定时器节点。
     * @param cb 传入，定时器回调函数，回调参数 elapsed_ms 为距上次触发的真实毫秒数。
     * @param period_ms 传入，定时器周期，单位毫秒，必须大于 0。
     * @param repeat 传入，1 表示周期定时器，0 表示单次定时器。
     * @return 无。
     * @note 与 we_anim_start 同构：节点已在链上时仅刷新参数（幂等）。
     */
    void we_gui_timer_create(we_lcd_t *p_lcd, we_gui_timer_t *t, we_gui_timer_cb_t cb,
                             uint16_t period_ms, uint8_t repeat);
    /**
     * @brief 启动一个已创建的 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param t 传入，定时器节点。
     * @return 无。
     * @note 这个接口主要用于“先 stop 后恢复”的场景。
     *       定时器在 create 成功后默认就是激活状态。
     */
    void we_gui_timer_start(we_lcd_t *p_lcd, we_gui_timer_t *t);
    /**
     * @brief 停止一个 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param t 传入，定时器节点。
     * @return 无。
     * @note 停止后会清空当前累计时间，再次启动时重新开始计时。
     */
    void we_gui_timer_stop(we_lcd_t *p_lcd, we_gui_timer_t *t);
    /**
     * @brief 重启一个 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param t 传入，定时器节点。
     * @return 无。
     * @note 重启会清空累计时间并重新进入激活状态。
     */
    void we_gui_timer_restart(we_lcd_t *p_lcd, we_gui_timer_t *t);
    /**
     * @brief 删除一个 GUI 定时器。
     * @param p_lcd 传入，GUI 屏幕上下文指针。
     * @param t 传入，定时器节点。
     * @return 无。
     * @note 删除后节点被摘链并清空，可复用于重新创建。
     */
    void we_gui_timer_delete(we_lcd_t *p_lcd, we_gui_timer_t *t);

#ifdef __cplusplus
}
#endif

#endif /* WE_TICK_H */
