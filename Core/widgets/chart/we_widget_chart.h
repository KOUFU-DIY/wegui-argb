#ifndef WE_WIDGET_CHART_H
#define WE_WIDGET_CHART_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * chart 使用说明
 *
 * chart 是单曲线滚动波形图控件，采用前瞻式 vspan + 可配置柔边绘制波形带。
 * 数据采用像素坐标系：值 0 = 控件垂直中心，正值向上，负值向下，
 * 超出控件范围的值会被截断，无需设置 y_min/y_max。
 *
 * 算法说明：
 *   当前波形主体 + 柔边绘制思路参考自 Arm-2D 的波形图实现，
 *   但已按本工程的脏矩形、PFB 裁剪和整数像素坐标体系做本地化调整。
 *
 * 使用步骤：
 * 1. 分配 int16_t 数据缓冲区，容量建议 = 控件宽度（每列一个采样点）。
 * 2. 调用 we_chart_obj_init() 完成初始化。
 * 3. 定期调用 we_chart_push() 推入像素单位的采样值（±bh/2 为满幅显示）。
 *
 * 背景网格配置（在平台 config 或工程预定义中覆盖，默认 10x10）：
 *   WE_CHART_GRID_COLS — 竖向网格分段数，设为 0 可关闭
 *   WE_CHART_GRID_ROWS — 横向网格分段数，设为 0 可关闭
 *   WE_CHART_GRID_R/G/B — 网格线 RGB 分量，默认暗灰色 (50, 50, 50)
 *
 * 波形标脏配置：
 *   WE_CFG_CHART_DIRTY_MODE — 0: 整个控件包围盒标脏
 *                             1: push 时按列块联合包络标脏（默认）
 *   WE_CHART_DIRTY_BLOCK_W  — 列块宽度（像素），默认 8
 * -------------------------------------------------------------------------- */

#ifndef WE_CHART_GRID_COLS
#define WE_CHART_GRID_COLS 10
#endif

#ifndef WE_CHART_GRID_ROWS
#define WE_CHART_GRID_ROWS 10
#endif

#ifndef WE_CHART_GRID_R
#define WE_CHART_GRID_R 50
#endif
#ifndef WE_CHART_GRID_G
#define WE_CHART_GRID_G 50
#endif
#ifndef WE_CHART_GRID_B
#define WE_CHART_GRID_B 50
#endif

/*   柔边高度配置：
 *   0 = 关闭柔边
 *   N = 顶部/底部各 N 像素柔边，并按距离主体递减透明度 */
#ifndef WE_CHART_AA_MAX
#define WE_CHART_AA_MAX 2
#endif

/* push 标脏策略：
 *   0 = 每次 push 整个控件包围盒标脏
 *   1 = 每次 push 按列块联合包络标脏 */
#ifndef WE_CFG_CHART_DIRTY_MODE
#define WE_CFG_CHART_DIRTY_MODE 1
#endif

/* 联合包络标脏时的列块宽度（像素）
 *   1 = 最精细逐列标脏
 *   N = 每 N 列合成一个局部 dirty block */
#ifndef WE_CHART_DIRTY_BLOCK_W
#define WE_CHART_DIRTY_BLOCK_W 16
#endif

/* 柔边衰减曲线：
 *   0 = 线性衰减
 *   1 = 二次衰减（内侧更饱满，外侧衰减更快） */
#ifndef WE_CFG_CHART_AA_CURVE
#define WE_CFG_CHART_AA_CURVE 0
#endif

typedef struct
{
    we_obj_t base; /* 基类，必须在首位 */

    int16_t *data;       /* 环形数据缓冲区指针（调用方分配） */
    uint16_t data_cap;   /* 缓冲区容量（建议 = 控件宽度） */
    uint16_t data_head;  /* 写入头：下一个样本写入位置 */
    uint16_t data_count; /* 当前有效样本数 */

    colour_t line_color;
    uint8_t stroke; /* 波形主体厚度（像素）；顶部/底部额外固定附加 1px 柔边 */
    uint8_t opacity;
} we_chart_obj_t;

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param data_buf 采样环形缓冲区指针。
 * @param data_cap 缓冲区容量（采样点个数）。
 * @param line_color 线条颜色值。
 * @param stroke 文本字符串。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_chart_obj_init(we_chart_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, uint16_t w, uint16_t h,
                       int16_t *data_buf, uint16_t data_cap, colour_t line_color, uint8_t stroke, uint8_t opacity);

/**
 * @brief 向控件数据队列推入一个新采样点。
 * @param obj 目标控件对象指针。
 * @param value 输入采样值。
 * @return 无。
 */
void we_chart_push(we_chart_obj_t *obj, int16_t value);

/**
 * @brief 清空控件内部数据并重置游标。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_chart_clear(we_chart_obj_t *obj);

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param color 目标颜色值。
 * @return 无。
 */
void we_chart_set_color(we_chart_obj_t *obj, colour_t color);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_chart_set_opacity(we_chart_obj_t *obj, uint8_t opacity);

/**
 * @brief 执行we_chart_obj_delete的核心处理流程。
 * @param obj 控件对象指针
 * @return 无
 */
static inline void we_chart_obj_delete(we_chart_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

#endif
