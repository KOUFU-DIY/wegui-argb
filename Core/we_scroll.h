#ifndef WE_SCROLL_H
#define WE_SCROLL_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 可嵌入单轴滚动物理组件
 *
 * 把 list/menu/table/logview/dropdown 等控件里逐份手写的同一套滚动物理
 * （拖拽跟手 / 橡皮筋越界 / 惯性 7/8 衰减 / 回弹 / 无 STAY 快扫测速 /
 * 滚动条空闲淡出）收敛为一份纯状态机。组件不做任何绘制、不标脏、不挂
 * 动画节点：控件把触摸序列喂进来，读回 pos 变化后自行提交（各控件的
 * 精细标脏策略不变），动画节点仍归控件所有（删除契约不变）。
 *
 * 方向约定：c 为主轴触摸坐标（垂直滚动传 y）；pos 正向 = 内容向坐标负
 * 方向滚出（与各控件现行 scroll_px 语义一致：手指下移 → pos 减小）。
 * -------------------------------------------------------------------------- */

/* 参数集：Flash 常量，每类控件各一份（沿用各控件现行宏值） */
typedef struct
{
    int16_t drag_threshold;   /* 拖拽判定阈值（像素） */
    int16_t overscroll;       /* 橡皮筋越界上限（像素，0 = 不允许越界） */
    int16_t inertia_num;      /* 惯性每步衰减分子（如 7） */
    int16_t inertia_den;      /* 惯性每步衰减分母（如 8） */
    int16_t rebound_pull_div; /* 回弹每步拉回 = 过冲 / 本值 */
    int16_t rebound_max_step; /* 回弹单步上限（像素） */
    int16_t swipe_slice_ms;   /* 快扫测速时间片（毫秒） */
} we_scroll_cfg_t;

/* 运行状态：内嵌进控件结构体 */
typedef struct
{
    int32_t pos;        /* 当前滚动量（像素） */
    int32_t press_pos;  /* 按下时滚动量 */
    int16_t press_c;    /* 按下主轴坐标 */
    int16_t last_c;     /* 最近一次主轴坐标（步进测速用） */
    int16_t vel;        /* 速度（像素 / 16ms，方向与 pos 增量一致） */
    uint16_t sb_idle_ms;   /* 滚动条空闲计时 */
    uint8_t sb_alpha;      /* 滚动条当前透明度 */
    uint8_t tracking : 1;  /* 按压序列跟踪中 */
    uint8_t dragging : 1;  /* 已越过拖拽阈值 */
    uint8_t animating : 1; /* 惯性/回弹动画进行中（控件据此决定动画节点挂/摘） */
} we_scroll_t;

/**
 * @brief 复位滚动状态（init/绑定新数据时用；不触碰 sb_alpha）
 */
void we_scroll_reset(we_scroll_t *sc);

/**
 * @brief 喂入按下事件：停惯性、开始跟踪本次按压序列
 * @return 无。调用方随后自行处理按压高亮等控件语义。
 */
void we_scroll_press(we_scroll_t *sc, int16_t c);

/**
 * @brief 喂入 STAY：拖拽判定 + 跟手滚动 + 步进测速
 * @param max_scroll 传入：当前滚动上限（内容高 - 视口高，负值按 0）
 * @return 0 = 未进入拖拽；1 = 本次刚越过阈值（调用方取消按压高亮）；
 *         2 = 拖拽中（无论 pos 是否变化）。pos 是否变化由调用方对比提交。
 */
uint8_t we_scroll_stay(we_scroll_t *sc, const we_scroll_cfg_t *cfg,
                       int16_t c, int32_t max_scroll);

/**
 * @brief 喂入松开：决定是否需要惯性/回弹动画
 * @return 非 0 = 需要动画（调用方 we_anim_start 自己的节点）；0 = 静止。
 * @note 无论返回值，tracking/dragging 均已清零；返回前 animating 已置位。
 */
uint8_t we_scroll_release(we_scroll_t *sc, int32_t max_scroll);

/**
 * @brief 喂入无 STAY 的快扫（内核 SWIPE 事件）：按总位移/时间片注入初速度
 * @param c_now 传入：松手主轴坐标（与 press_c 差值即总位移）
 * @return 非 0 = 需要启动惯性动画。
 */
uint8_t we_scroll_swipe(we_scroll_t *sc, const we_scroll_cfg_t *cfg,
                        int16_t c_now, int32_t max_scroll);

/**
 * @brief 惯性/回弹步进（控件动画节点的 step_cb 调用）
 * @return 非 0 = pos 有变化（调用方提交/标脏）；收敛时自动清 animating，
 *         调用方发现 animating==0 后摘掉自己的动画节点。
 */
uint8_t we_scroll_anim_step(we_scroll_t *sc, const we_scroll_cfg_t *cfg,
                            uint16_t elapsed_ms, int32_t max_scroll);

/**
 * @brief 程序化硬夹紧定位（set_scroll 类接口用；不产生动画）
 * @return 非 0 = pos 有变化。
 */
uint8_t we_scroll_set(we_scroll_t *sc, int32_t pos, int32_t max_scroll);

/**
 * @brief 唤醒滚动条（滚动值变化/绑定数据时调用：透明度拉满、空闲计时清零）
 */
void we_scroll_bar_wake(we_scroll_t *sc, uint8_t active_alpha);

/**
 * @brief 滚动条淡出步进（控件滚动条动画节点的 step_cb 调用）
 * @param idle_ms 传入：全显停留时长（如 600ms）
 * @param fade_step 传入：每 16ms 递减的 alpha 步长
 * @param resident_alpha 传入：常驻最低透明度（淡出下限）
 * @return 非 0 = alpha 有变化（调用方标脏滚动条条带）；到达常驻值后
 *         返回 0 且不再变化，调用方可摘节点。
 */
uint8_t we_scroll_bar_step(we_scroll_t *sc, uint16_t elapsed_ms, uint16_t idle_ms,
                           uint8_t fade_step, uint8_t resident_alpha);

#ifdef __cplusplus
}
#endif

#endif /* WE_SCROLL_H */
