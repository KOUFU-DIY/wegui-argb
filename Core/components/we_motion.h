#ifndef WE_MOTION_H
#define WE_MOTION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 缓动函数类型
 *
 * 入参 t ∈ [0, 256]，256 = 动画完成（1.0）。
 * 返回值通常也在 [0, 256] 内，但 we_ease_out_back 会短暂超过 256（过冲）。
 * 过冲场景可配合 we_gui_driver.h 中的 we_lerp 使用。
 * -------------------------------------------------------------------------- */
typedef uint16_t (*we_ease_fn_t)(uint16_t t);

/**
 * @brief 匀速缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_linear(uint16_t t);

/**
 * @brief 二次缓入缓动（先慢后快）
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_in_quad(uint16_t t);

/**
 * @brief 二次缓出缓动（先快后慢）
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_out_quad(uint16_t t);

/**
 * @brief 二次缓入缓出缓动（两端慢、中间快）
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_in_out_quad(uint16_t t);

/**
 * @brief 三次缓出缓动（比二次缓出更急）
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_out_cubic(uint16_t t);

/**
 * @brief 正弦缓入缓出缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_in_out_sine(uint16_t t);

/**
 * @brief 弹跳缓出缓动（到达目标后弹跳）
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_out_bounce(uint16_t t);

/**
 * @brief 回弹缓出缓动（超过目标后回拉）
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8，动画中途可能超过 256）
 */
uint16_t we_ease_out_back(uint16_t t);

#ifdef __cplusplus
}
#endif

#endif /* WE_MOTION_H */
