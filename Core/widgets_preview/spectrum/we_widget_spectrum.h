#ifndef __WE_WIDGET_SPECTRUM_H
#define __WE_WIDGET_SPECTRUM_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 频谱柱（spectrum）—— preview 孵化区实验控件
 *
 * N 根竖直电平柱 + 可选峰值保持帽，用于音频频谱/多通道电平可视化。
 * 调用方周期 we_spectrum_push() 一帧目标电平（0~255 x bar_cnt），控件内部
 * 维护"显示电平"：上升立即取 max（快速上冲），回落由中央动画引擎按比例
 * 衰减（慢速回落）；峰值帽跟随柱顶并以更慢的匀速下坠。
 *
 * 渲染全部由 we_fill_rect 完成：柱体从底往上按高度分段渐变
 *（低色 -> 高色预混色带），柱底一条 1px 基线，峰值帽为 2px 横线。
 *
 * 装饰性控件：event_cb 恒返回 0，输入穿透。
 * 零 malloc（电平数组为控件自身固定成员）、渲染内环零浮点。
 * 删除前必须 we_spectrum_obj_delete（内部先 we_anim_stop 再摘链）。
 *
 * preview 限制：标脏按整控件包围盒；柱高换算含 /255 除法（模拟器无所谓）。
 * -------------------------------------------------------------------------- */

/* 柱数上限（电平数组按此静态分配，每柱 3 字节状态） */
#ifndef WE_SPECTRUM_BAR_MAX
#define WE_SPECTRUM_BAR_MAX 32U
#endif

/* 衰减推进时基（毫秒）：动画回调按该量子步进，主循环抖动自动补偿 */
#ifndef WE_SPECTRUM_STEP_MS
#define WE_SPECTRUM_STEP_MS 16U
#endif

/* 柱体回落速度：每步衰减 (显示值-目标值) >> 该移位，最小 1（比例衰减） */
#ifndef WE_SPECTRUM_FALL_SHIFT
#define WE_SPECTRUM_FALL_SHIFT 3
#endif

/* 峰值帽每步匀速下坠量（电平单位/步，应明显慢于柱体回落） */
#ifndef WE_SPECTRUM_PEAK_FALL
#define WE_SPECTRUM_PEAK_FALL 2U
#endif

/* 柱体垂直渐变分段数（分段预混色，段数越多渐变越细腻） */
#ifndef WE_SPECTRUM_GRAD_STEPS
#define WE_SPECTRUM_GRAD_STEPS 8U
#endif

typedef struct we_spectrum_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h 为控件外接矩形 */

    uint8_t bar_cnt;            /* 柱数（1..WE_SPECTRUM_BAR_MAX） */
    uint8_t peak_hold;          /* 峰值帽开关（0=不画帽） */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */
    uint8_t anim_busy;          /* 衰减动画进行中标志（内部） */

    uint8_t target[WE_SPECTRUM_BAR_MAX]; /* 目标电平（push 写入） */
    uint8_t shown[WE_SPECTRUM_BAR_MAX];  /* 显示电平（动画推进） */
    uint8_t peak[WE_SPECTRUM_BAR_MAX];   /* 峰值帽电平（>= shown） */

    colour_t color_low;         /* 柱体低端颜色（渐变起点） */
    colour_t color_high;        /* 柱体高端颜色（渐变终点） */
    colour_t color_peak;        /* 峰值帽颜色 */

    we_anim_t anim;             /* 中央动画节点（归控件所有，删除前必须摘链） */
    uint16_t fall_acc_ms;       /* 衰减时基累积器（内部） */
} we_spectrum_obj_t;

/**
 * @brief 初始化频谱柱控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素），柱宽/间隙按 w / bar_cnt 等分推导。
 * @param h 控件高度（像素），底部 1px 为基线。
 * @param bar_cnt 柱数，超过 WE_SPECTRUM_BAR_MAX 时钳制，0 时按 1 处理。
 * @return 无。
 * @note 默认：全部电平 0、峰值帽开启、青蓝->品红渐变、近白峰值帽。
 */
void we_spectrum_obj_init(we_spectrum_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                          uint8_t bar_cnt);

/**
 * @brief 推入一帧目标电平（bar_cnt 个 0~255 值，拷入控件内部数组）。
 * @param obj 控件对象指针。
 * @param levels 电平数组指针，长度至少 bar_cnt 字节。
 * @return 无。
 * @note 上升立即取 max（快速上冲），回落交给中央动画节点按比例衰减；
 *       峰值帽同步抬升到新柱顶。
 */
void we_spectrum_push(we_spectrum_obj_t *obj, const uint8_t *levels);

/**
 * @brief 设置柱体渐变双色与峰值帽颜色。
 * @param obj 控件对象指针。
 * @param low 柱体低端颜色（靠近基线）。
 * @param high 柱体高端颜色（靠近顶部）。
 * @param peak 峰值帽颜色。
 * @return 无。
 * @note 三色均未变化时直接返回，不触发重绘。
 */
void we_spectrum_set_colors(we_spectrum_obj_t *obj,
                            colour_t low, colour_t high, colour_t peak);

/**
 * @brief 开关峰值保持帽。
 * @param obj 控件对象指针。
 * @param enable 0=关闭（不画帽），非0=开启。
 * @return 无。
 * @note 切换时峰值帽复位到当前柱顶；值未变直接返回。
 */
void we_spectrum_set_peak_hold(we_spectrum_obj_t *obj, uint8_t enable);

/**
 * @brief 设置控件整体透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_spectrum_set_opacity(we_spectrum_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除频谱柱控件：先摘除衰减动画节点（we_anim_stop）再摘链。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_spectrum_obj_delete(we_spectrum_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_SPECTRUM_H */
