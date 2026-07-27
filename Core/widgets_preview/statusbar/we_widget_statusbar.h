#ifndef __WE_WIDGET_STATUSBAR_H
#define __WE_WIDGET_STATUSBAR_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 状态栏控件（statusbar）—— preview 孵化区实验控件
 *
 * 一条高度固定（WE_STATUSBAR_HEIGHT ≈ 22px）的深色横条：
 *   - 左侧：时间文本（"HH:MM"，调用方持有字符串，仅存指针）；
 *   - 右侧：右对齐依次排列 信号 → WiFi → 电池 三个矢量图标
 *     （电池贴最右，图标高约 12px，全部 we_fill_rect /
 *      we_draw_round_rect_analytic_fill 矢量拼装，零图片资产）。
 *
 * 图标细节：
 *   - 电池：圆角外壳（1px 轮廓）+ 右侧电极凸块 + 内部按百分比填充，
 *     电量 < 20% 时填充用低电色；charging 时叠加三段矩形拼的小闪电；
 *   - WiFi：3 层逐级加宽的横向圆角短条叠成扇形近似（底部一点 +
 *     两层弧条），level 决定自下而上点亮层数，未点亮层低透明度；
 *     level = -1 整个图标隐藏（右侧布局自动收拢）；
 *   - 信号：4 根递增高度小柱，自左点亮 level 根，未点亮柱低透明度；
 *     level = -1 整个图标隐藏。
 *
 * 装饰性控件：event_cb 恒返回 0（事件穿透）。无动画节点，删除无需摘链。
 * 零 malloc、渲染零浮点（全部整数矩形/圆角矩形原语）。
 *
 * preview 限制（毕业前需优化项见 widget.md）：
 *   - 任何值变化按整条状态栏标脏；
 *   - set_time 每次调用都重绘（调用方常在原缓冲上覆写，指针/内容
 *     等值判断都不可靠，preview 阶段直接从简）。
 * -------------------------------------------------------------------------- */

/* 状态栏固定高度（像素，包含本头文件前可用宏覆盖） */
#ifndef WE_STATUSBAR_HEIGHT
#define WE_STATUSBAR_HEIGHT 22
#endif

/* 低电阈值（百分比，电量低于该值时填充改用低电色） */
#ifndef WE_STATUSBAR_LOW_PCT
#define WE_STATUSBAR_LOW_PCT 20U
#endif

/* 未点亮图层（WiFi 层 / 信号柱）的透明度（0~255） */
#ifndef WE_STATUSBAR_DIM_OPA
#define WE_STATUSBAR_DIM_OPA 64U
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_statusbar_obj_t
{
    we_obj_t base;          /* 基类，必须在首位：base.h = WE_STATUSBAR_HEIGHT */

    const char *time_str;   /* 左侧时间文本（"HH:MM"，调用方持有，可为 NULL） */
    uint8_t  battery_pct;   /* 电量百分比（0~100） */
    uint8_t  charging;      /* 充电标志（0/1，叠加小闪电） */
    int8_t   wifi_level;    /* WiFi 层数（-1 = 隐藏，0~3 = 点亮层数） */
    int8_t   signal_level;  /* 信号柱数（-1 = 隐藏，0~4 = 点亮柱数） */

    colour_t bg_color;      /* 底条颜色（深色） */
    colour_t fg_color;      /* 前景色（文本 / 图标主体） */
    colour_t low_color;     /* 低电填充色 */
    uint8_t  opacity;       /* 整体不透明度（0~255，默认 255） */
    const unsigned char *font;  /* 字体资源（init 必传） */
} we_statusbar_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化状态栏并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标，置顶通常传 0）。
 * @param y 左上角 Y（置顶通常传 0）。
 * @param w 底条宽度（像素，最小 64，通常 = 屏幕宽度）。
 * @return 无。
 * @note 高度固定 WE_STATUSBAR_HEIGHT；默认：时间 NULL（不显示）、
 *       电量 100%、不充电、WiFi 3 格、信号 4 柱、深蓝灰底 / 近白前景 /
 *       橙红低电色、不透明。
 */
void we_statusbar_obj_init(we_statusbar_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, uint16_t w,
                        const unsigned char *font);

/**
 * @brief 设置左侧时间文本。
 * @param obj 控件对象指针。
 * @param hhmm 时间字符串（UTF-8，如 "12:34"；调用方持有，仅存指针；
 *             NULL 表示不显示时间）。
 * @return 无。
 * @note 调用方常在原缓冲上覆写内容后再调用本接口，指针等值不代表
 *       内容未变，故每次调用都会重绘（preview 从简，见 widget.md）。
 */
void we_statusbar_set_time(we_statusbar_obj_t *obj, const char *hhmm);

/**
 * @brief 设置电量百分比。
 * @param obj 控件对象指针。
 * @param pct 电量（0~100，超出自动钳制）；值未变直接返回。
 * @return 无。
 * @note 低于 WE_STATUSBAR_LOW_PCT（默认 20%）时填充改用低电色。
 */
void we_statusbar_set_battery(we_statusbar_obj_t *obj, uint8_t pct);

/**
 * @brief 设置充电状态（电池图标叠加小闪电）。
 * @param obj 控件对象指针。
 * @param charging 0 = 未充电，非 0 = 充电中；状态未变直接返回。
 * @return 无。
 */
void we_statusbar_set_charging(we_statusbar_obj_t *obj, uint8_t charging);

/**
 * @brief 设置 WiFi 信号层数。
 * @param obj 控件对象指针。
 * @param level -1 = 隐藏图标，0~3 = 自下而上点亮层数（超出钳到 3）；
 *              值未变直接返回。
 * @return 无。
 * @note 隐藏后右侧图标布局自动收拢（信号图标右移补位）。
 */
void we_statusbar_set_wifi(we_statusbar_obj_t *obj, int8_t level);

/**
 * @brief 设置蜂窝信号柱数。
 * @param obj 控件对象指针。
 * @param level -1 = 隐藏图标，0~4 = 自左点亮柱数（超出钳到 4）；
 *              值未变直接返回。
 * @return 无。
 */
void we_statusbar_set_signal(we_statusbar_obj_t *obj, int8_t level);

/**
 * @brief 设置底色 / 前景色 / 低电色。
 * @param obj 控件对象指针。
 * @param bg 底条颜色。
 * @param fg 前景色（文本与图标主体）。
 * @param low 低电填充色。
 * @return 无。
 * @note 三色均未变化时直接返回，不触发重绘。
 */
void we_statusbar_set_colors(we_statusbar_obj_t *obj, colour_t bg,
                             colour_t fg, colour_t low);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_statusbar_set_opacity(we_statusbar_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件并从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 * @note statusbar 无动画节点，无需 we_anim_stop。
 */
void we_statusbar_obj_delete(we_statusbar_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_STATUSBAR_H */
