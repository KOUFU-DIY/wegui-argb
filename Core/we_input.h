#ifndef WE_INPUT_H
#define WE_INPUT_H

/* --------------------------------------------------------------------------
 * 触摸输入：命中测试、按压冒泡、手势接管与点击/滑动判定。
 * -------------------------------------------------------------------------- */

#include "we_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 直接提交一次触摸边沿事件（按下/按住/松开）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param data 传入：事件坐标与边沿状态（WE_TOUCH_STATE_PRESSED/STAY/RELEASED）
     * @return 无
     * @note 端口经 we_port_t.touch_read 报告触摸时由内核自动采样并调用本函数；
     *       只有自行管理触摸边沿（脚本回放、外部手势源）时才需要直接调用。
     */
    void we_gui_indev_handler(we_lcd_t *lcd, we_indev_data_t *data);
    /**
     * @brief 由祖先容器接管当前按压（手势接管）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param obj 传入：接管手势的容器对象
     * @return 无
     * @note 原按压对象会先收到一次 WE_EVENT_RELEASED 以回弹按压视觉——
     *       与内核"点击只在 pressed_obj == 命中对象时才派发"配合，
     *       被接管的子控件不会触发 CLICKED。容器通常不必直接调用本接口：
     *       内核在位移越 WE_CFG_DRAG_THRESHOLD 时会沿祖先链发
     *       WE_EVENT_DRAG_BEGIN 接管询问，返回非 0 的容器由内核代为接管。
     */
    void we_indev_grab(we_lcd_t *lcd, we_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* WE_INPUT_H */
