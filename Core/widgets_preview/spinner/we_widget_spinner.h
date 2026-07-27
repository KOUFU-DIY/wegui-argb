#ifndef __WE_WIDGET_SPINNER_H
#define __WE_WIDGET_SPINNER_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 加载指示器控件（spinner）—— preview 孵化区实验控件
 *
 * WE_SPINNER_DOT_CNT（默认 12）个小圆点沿内切圆环均布，以旋转头索引
 * 为最亮点、沿环逆着旋转方向逐点 opacity 递减，形成拖尾旋转效果。
 *
 * 渲染：每个圆点用 we_draw_round_rect_analytic_fill 退化的实心抗锯齿圆
 * （w = h = 直径，radius = 半径）绘制；点位由 we_cos/we_sin（Q15）算出，
 * 渲染路径零浮点、零 malloc。
 *
 * 旋转推进走单个中央动画引擎节点（we_anim_t，不占 GUI timer 槽）：
 * 节点内累计 elapsed_ms，每满 step_ms（默认 80ms）头索引前进一格并整体
 * 标脏重绘；stop 后节点摘链，画面定格，空闲期零开销。
 *
 * 角度统一 512 步制（0..511 = 一圈，90° = 128）。
 * 所有 setter 值未变时直接返回不重绘。
 * 装饰性控件：event_cb 返回 0，输入事件穿透给背后控件。
 *
 * preview 限制（毕业前需优化项见 widget.md）：
 *   - 每次转动按整控件包围盒标脏（未做逐点差分标脏）。
 * -------------------------------------------------------------------------- */

/* 圆点个数（沿环均布；改动会同步影响拖尾衰减梯度） */
#ifndef WE_SPINNER_DOT_CNT
#define WE_SPINNER_DOT_CNT 12U
#endif
#if (WE_SPINNER_DOT_CNT < 2)
#error "WE_SPINNER_DOT_CNT 必须 >= 2（拖尾衰减按 CNT-1 归一化）"
#endif

/* 默认步进周期（毫秒/格，建议 70~90ms，一圈 = DOT_CNT × step_ms） */
#ifndef WE_SPINNER_DEF_STEP_MS
#define WE_SPINNER_DEF_STEP_MS 80U
#endif

/* 拖尾末端最低亮度（0~255，头点恒为 255，沿环线性衰减到该值） */
#ifndef WE_SPINNER_TAIL_MIN
#define WE_SPINNER_TAIL_MIN 18U
#endif

/* 默认主色（青蓝），包含本头文件前可用宏覆盖 */
#ifndef WE_SPINNER_DEF_R
#define WE_SPINNER_DEF_R  86
#endif
#ifndef WE_SPINNER_DEF_G
#define WE_SPINNER_DEF_G 170
#endif
#ifndef WE_SPINNER_DEF_B
#define WE_SPINNER_DEF_B 255
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_spinner_obj_t
{
    we_obj_t base;        /* 基类，必须在首位：base.w = base.h = 直径 */

    colour_t color;       /* 主色（全部圆点同色，仅 alpha 不同） */
    uint8_t  opacity;     /* 整体不透明度（0~255） */
    uint8_t  running;     /* 1 = 旋转中，0 = 定格 */
    uint8_t  head;        /* 旋转头索引（0..WE_SPINNER_DOT_CNT-1） */
    uint16_t step_ms;     /* 步进周期（毫秒/格） */
    uint16_t acc_ms;      /* 动画节点内累计的毫秒数 */

    we_anim_t anim;       /* 旋转推进动画节点（归控件所有，删除前必须摘链） */
} we_spinner_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化加载指示器并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 外接正方形左上角 X（屏幕绝对坐标）。
 * @param y 外接正方形左上角 Y。
 * @param diameter 控件直径（像素，包围盒 = diameter × diameter）。
 * @return 无。
 * @note 默认：主色青蓝、80ms/格、不透明；init 后立即开始旋转。
 */
void we_spinner_obj_init(we_spinner_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, uint16_t diameter);

/**
 * @brief 设置主色（全部圆点同色，拖尾仅按 alpha 衰减）。
 * @param obj 控件对象指针。
 * @param color 主色。
 * @return 无。
 */
void we_spinner_set_colors(we_spinner_obj_t *obj, colour_t color);

/**
 * @brief 开始旋转（挂入中央动画链表；已在旋转则空操作）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_spinner_start(we_spinner_obj_t *obj);

/**
 * @brief 停止旋转（摘除动画节点，画面定格在当前相位；已停止则空操作）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_spinner_stop(we_spinner_obj_t *obj);

/**
 * @brief 查询是否正在旋转。
 * @param obj 控件对象指针。
 * @return 1 旋转中，0 已停止或 obj 为 NULL。
 */
uint8_t we_spinner_is_running(const we_spinner_obj_t *obj);

/**
 * @brief 设置步进周期（旋转速度，毫秒/格）。
 * @param obj 控件对象指针。
 * @param step_ms 步进周期（最小钳制为 16ms，建议 70~90ms）。
 * @return 无。
 */
void we_spinner_set_speed(we_spinner_obj_t *obj, uint16_t step_ms);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_spinner_set_opacity(we_spinner_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件：先摘除动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_spinner_obj_delete(we_spinner_obj_t *obj);

#endif /* __WE_WIDGET_SPINNER_H */
