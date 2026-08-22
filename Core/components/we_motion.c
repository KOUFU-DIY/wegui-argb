#include "we_motion.h"
#include "we_gui_driver.h"

/**
 * @brief 匀速缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_linear(uint16_t t)
{
    if (t >= 256U)
        return 256U;
    return t;
}

/**
 * @brief 二次缓入缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_in_quad(uint16_t t)
{
    if (t >= 256U)
        return 256U;
    return (uint16_t)(((uint32_t)t * t) >> 8);
}

/**
 * @brief 二次缓出缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_out_quad(uint16_t t)
{
    uint16_t inv;
    if (t >= 256U)
        return 256U;
    inv = (uint16_t)(256U - t);
    return (uint16_t)(256U - (((uint32_t)inv * inv) >> 8));
}

/**
 * @brief 二次缓入缓出缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_in_out_quad(uint16_t t)
{
    uint16_t inv;
    if (t >= 256U)
        return 256U;
    if (t < 128U)
        return (uint16_t)(((uint32_t)t * t) >> 7);
    inv = (uint16_t)(256U - t);
    return (uint16_t)(256U - (((uint32_t)inv * inv) >> 7));
}

/**
 * @brief 三次缓出缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 */
uint16_t we_ease_out_cubic(uint16_t t)
{
    uint32_t inv;
    uint32_t inv2;
    if (t >= 256U)
        return 256U;
    inv  = 256U - t;
    inv2 = (inv * inv) >> 8;
    return (uint16_t)(256U - ((inv2 * inv) >> 8));
}

/**
 * @brief 正弦缓入缓出缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 *
 * 入参 t ∈ [0,256] 刚好等于 512 步制下的半圆弧度（π = 256 步）。
 * we_cos(t) 返回 Q15 值：cos(0)=32767，cos(128)=0，cos(256)=-32767。
 * 公式：(32768 - we_cos(t)) >> 8，在 t=0 得 0，t=128 得 128，t≥256 由守卫返回 256。
 */
uint16_t we_ease_in_out_sine(uint16_t t)
{
    int32_t cos_val;
    if (t == 0U)
        return 0U;
    if (t >= 256U)
        return 256U;
    cos_val = (int32_t)we_cos((int16_t)t);
    return (uint16_t)((uint32_t)(32768 - cos_val) >> 8);
}

/**
 * @brief 弹跳缓出缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8）
 *
 * 参数来源：n1 = 7.5625 = 121/16，d1 = 2.75 = 11/4。
 * 各段起止（Q8 = [0,256]）：
 *   段 0: [  0,  93) — adj = t
 *   段 1: [ 93, 186) — adj = t - 140（≈ 1.5/d1·256）
 *   段 2: [186, 233) — adj = t - 209（≈ 2.25/d1·256）
 *   段 3: [233, 256] — adj = t - 244（≈ 2.625/d1·256）
 * 每段值 = n1·(adj/256)^2 + offset = (121·adj^2)>>12 + offset。
 */
uint16_t we_ease_out_bounce(uint16_t t)
{
    int32_t  adj;
    uint16_t offset;
    int32_t  val;

    if (t >= 256U)
        return 256U;

    if (t < 93U)
    {
        adj    = (int32_t)t;
        offset = 0U;
    }
    else if (t < 186U)
    {
        adj    = (int32_t)t - 140;
        offset = 192U;
    }
    else if (t < 233U)
    {
        adj    = (int32_t)t - 209;
        offset = 240U;
    }
    else
    {
        adj    = (int32_t)t - 244;
        offset = 252U;
    }

    val = (121 * adj * adj) >> 12;
    val += offset;

    if (val > 256)
        val = 256;
    return (uint16_t)val;
}

/**
 * @brief 回弹缓出缓动
 * @param t 传入：当前进度（Q8，范围 0~256）
 * @return 缓动后进度（Q8，动画中途可能超过 256）
 *
 * 标准公式：1 + c3·(t-1)^3 + c1·(t-1)^2
 *   c1 = 1.70158 ≈ 435/256，c3 = 2.70158 ≈ 691/256
 *
 * 令 v = t - 256（Q8，范围 [-256, 0]）：
 *   out_q8 = 256 + c3·v^3/256^2 + c1·v^2/256
 *
 * 返回值在动画中途可能超过 256，搭配 we_lerp 可实现过冲效果。
 */
uint16_t we_ease_out_back(uint16_t t)
{
    int32_t v;
    int32_t v2;
    int32_t v3;
    int32_t out;

    if (t >= 256U)
        return 256U;

    v  = (int32_t)t - 256;
    v2 = (v * v) >> 8;
    v3 = (v2 * v) / 256;

    out = 256 + ((691 * v3) >> 8) + ((435 * v2) >> 8);

    if (out < 0)
        out = 0;
    return (uint16_t)out;
}
