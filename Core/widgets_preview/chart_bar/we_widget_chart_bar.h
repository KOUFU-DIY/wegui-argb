#ifndef __WE_WIDGET_CHART_BAR_H
#define __WE_WIDGET_CHART_BAR_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 柱状图（chart_bar）—— preview 孵化区实验控件
 *
 * N 根竖直柱 + 底部 1px 基线 + 可选横向低透明网格线。
 * 数据为像素高度值（uint8_t，绘制时钳到可用高度），与绘制解耦：
 * 内部环形缓冲（uint8_t 数组 + head 指针，独立实现，思路同 chart 控件）。
 *
 * 两种喂数方式：
 *   1. we_chart_bar_push()   —— 滚动模式：最新值进右端，整体视觉左移一格；
 *   2. we_chart_bar_set_all() —— 整帧覆盖：一次给满 bar_cnt 个值（左->右）。
 *
 * 初始全零（显示为只有基线的空图），push 从第一次调用起即整幅滚动。
 * 柱宽/缝隙由控件宽度等分推导（槽宽 = w/bar_cnt，缝隙 = 槽宽/4，余数居中）。
 *
 * 装饰性控件：event_cb 恒返回 0，输入穿透。
 * 零 malloc（数据数组为控件定长成员）、渲染内环零浮点、
 * 无动画节点（删除直接 we_obj_delete）。
 *
 * preview 限制：标脏按整控件包围盒；网格线 Y 含 /(rows+1) 除法（模拟器无所谓）。
 * -------------------------------------------------------------------------- */

/* 柱数上限（数据数组按此静态分配，每柱 1 字节） */
#ifndef WE_CHART_BAR_MAX
#define WE_CHART_BAR_MAX 32U
#endif

/* 横向网格线数上限（set_grid 超出时钳制） */
#ifndef WE_CHART_BAR_GRID_MAX
#define WE_CHART_BAR_GRID_MAX 16U
#endif

typedef struct
{
    we_obj_t base;      /* 必须在首位：x/y/w/h 为控件外接矩形 */

    uint8_t bar_cnt;    /* 柱数（1..WE_CHART_BAR_MAX） */
    uint8_t head;       /* 环形头：display[i] = values[(head+i) 回绕]，push 写 head 后前进 */
    uint8_t grid_rows;  /* 横向网格线数（0=关闭） */

    colour_t bar_color;  /* 柱体颜色 */
    colour_t grid_color; /* 网格线 + 基线颜色 */

    uint8_t values[WE_CHART_BAR_MAX]; /* 像素高度环形缓冲（绘制时钳到 h-1） */
} we_chart_bar_obj_t;

/**
 * @brief 初始化柱状图控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素），柱宽/缝隙按 w / bar_cnt 等分推导。
 * @param h 控件高度（像素），底部 1px 为基线，柱高可用量程 = h-1。
 * @param bar_cnt 柱数，超过 WE_CHART_BAR_MAX 时钳制，0 时按 1 处理。
 * @return 无。
 * @note 默认：数据全零、网格关闭、青蓝柱色 + 暗灰网格色。
 */
void we_chart_bar_obj_init(we_chart_bar_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           uint8_t bar_cnt);

/**
 * @brief 推入一个新值（像素高度）：最新值进右端，整体视觉左移一格。
 * @param obj 控件对象指针。
 * @param value_px 柱高（像素），绘制时钳到可用高度（h-1）。
 * @return 无。
 * @note 环形覆盖最旧值；每次 push 都会整控件标脏（滚动本就全变）。
 */
void we_chart_bar_push(we_chart_bar_obj_t *obj, uint8_t value_px);

/**
 * @brief 整帧覆盖全部柱值（values[0] 为最左柱，长度须 >= bar_cnt）。
 * @param obj 控件对象指针。
 * @param values 像素高度数组指针（拷入控件内部环形缓冲）。
 * @return 无。
 * @note 新帧与当前显示序列完全一致时直接返回，不触发重绘。
 */
void we_chart_bar_set_all(we_chart_bar_obj_t *obj, const uint8_t *values);

/**
 * @brief 设置柱体颜色与网格线（含基线）颜色。
 * @param obj 控件对象指针。
 * @param bar 柱体颜色。
 * @param grid 网格线 + 基线颜色。
 * @return 无。
 * @note 两色均未变化时直接返回，不触发重绘。
 */
void we_chart_bar_set_colors(we_chart_bar_obj_t *obj, colour_t bar, colour_t grid);

/**
 * @brief 设置横向网格线数（0 = 关闭）。
 * @param obj 控件对象指针。
 * @param rows 网格线数，超过 WE_CHART_BAR_GRID_MAX 时钳制。
 * @return 无。
 * @note 网格线在量程内均匀分布（y = 可用高 * k / (rows+1)），低透明绘制；
 *       值未变化时直接返回。
 */
void we_chart_bar_set_grid(we_chart_bar_obj_t *obj, uint8_t rows);

/**
 * @brief 删除柱状图控件（无动画节点，直接摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
static inline void we_chart_bar_obj_delete(we_chart_bar_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_CHART_BAR_H */
