/**
 * @file  demo_table.c
 * @brief 简易表格（table）preview demo —— 传感器参数表实时刷新（DEMO_ID 107）
 *
 * 一张 252x170 表格：4 列（CH/NAME/VAL/UNIT）× 14 行（1 表头 + 13 数据，
 * static 数组由调用方持有，控件只存指针）。列宽按权重 2:5:4:2 分配，
 * 数值列做了长短不一的样本（"1013.25" / "48" / "-67"）。内容超出数据区
 * 可拖拽垂直滚动 + 右缘滚动条。Temp 与 Current 两行的 VAL 单元格指向
 * static 缓冲，tick 每 400ms 用 sprintf 原地改写（整数伪随机游走模拟
 * 实时数据）后调 we_table_refresh 重绘。顶部说明 label 提示可拖拽。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/table/we_widget_table.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t tb_title;
static we_label_obj_t tb_fps_label;
static we_label_obj_t tb_hint_label;  /* 拖拽滚动操作提示 */
static we_table_obj_t tb_table;

static uint32_t tb_fps_timer;
static uint32_t tb_last_frames;
static uint32_t tb_live_timer;        /* 实时数据刷新累计计时 */
static uint16_t tb_rand_state;        /* 整数 LCG 伪随机状态 */
static int16_t tb_temp_x10;           /* Temp 值 ×10（一位小数定点） */
static int16_t tb_curr_ma;            /* Current 值（mA 整数） */
static char tb_fps_buf[16];
static char tb_val_temp[12];          /* Temp 行 VAL 实时缓冲（调用方持有） */
static char tb_val_curr[12];          /* Current 行 VAL 实时缓冲 */

/* 布局（280x240 基准） */
#define TB_X 14
#define TB_Y 58
#define TB_W 252
#define TB_H 170

/* 实时数据刷新周期（毫秒） */
#define TB_LIVE_PERIOD 400U

/* 行结构：1 表头 + 13 数据行 */
#define TB_ROWS 14U
#define TB_COLS 4U

/* 单元格文本（行优先一维数组，第 0 行 = 表头；Temp/Current 的 VAL
 * 指向上方 static 缓冲，tick 原地改写后 refresh） */
static const char *const tb_cells[TB_ROWS * TB_COLS] = {
    "CH", "NAME",     "VAL",       "UNIT",
    "01", "Temp",     tb_val_temp, "C",
    "02", "Humidity", "48",        "%",
    "03", "Pressure", "1013.25",   "hPa",
    "04", "Voltage",  "3.300",     "V",
    "05", "Current",  tb_val_curr, "mA",
    "06", "Power",    "12.5",      "W",
    "07", "Speed",    "1420",      "rpm",
    "08", "Flow",     "6.4",       "L/m",
    "09", "Level",    "118",       "mm",
    "10", "Lux",      "5600",      "lx",
    "11", "CO2",      "412",       "ppm",
    "12", "Noise",    "38.2",      "dB",
    "13", "RSSI",     "-67",       "dBm",
};

/* 列宽权重：CH 窄、NAME 最宽、VAL 次宽、UNIT 窄 */
static const uint8_t tb_col_weights[TB_COLS] = { 2U, 5U, 4U, 2U };

/**
 * @brief 整数 LCG 伪随机数（demo 专用，避免引入 rand 依赖）。
 * @return 16 位伪随机值
 */
static uint16_t tb_rand(void)
{
    tb_rand_state = (uint16_t)(tb_rand_state * 25173U + 13849U);
    return tb_rand_state;
}

/**
 * @brief 用伪随机游走更新 Temp/Current 两个实时 VAL 缓冲。
 * @return 无
 * @note 缓冲归 demo（调用方）持有，改写后由调用处 we_table_refresh 重绘。
 */
static void tb_update_live_vals(void)
{
    /* Temp：23.0±jitter，×10 定点游走并夹紧 [18.0, 32.0] */
    tb_temp_x10 = (int16_t)(tb_temp_x10 + (int16_t)(tb_rand() % 15U) - 7);
    if (tb_temp_x10 < 180)
        tb_temp_x10 = 180;
    if (tb_temp_x10 > 320)
        tb_temp_x10 = 320;
    sprintf(tb_val_temp, "%d.%d", tb_temp_x10 / 10, tb_temp_x10 % 10);

    /* Current：mA 整数游走并夹紧 [120, 980]，宽度 3~4 位动态变化 */
    tb_curr_ma = (int16_t)(tb_curr_ma + (int16_t)(tb_rand() % 61U) - 30);
    if (tb_curr_ma < 120)
        tb_curr_ma = 120;
    if (tb_curr_ma > 980)
        tb_curr_ma = 980;
    sprintf(tb_val_curr, "%d", tb_curr_ma);
}

/**
 * @brief 初始化 table demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_table_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    tb_fps_timer = 0U;
    tb_last_frames = 0U;
    tb_live_timer = 0U;
    tb_rand_state = 0x5A5AU;
    tb_temp_x10 = 235; /* 23.5 C 起始 */
    tb_curr_ma = 560;
    memset(tb_fps_buf, 0, sizeof(tb_fps_buf));
    tb_update_live_vals(); /* 先填好实时缓冲再挂表格 */

    we_label_obj_init(&tb_title, lcd, 14, 10,
                      "TABLE", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&tb_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&tb_hint_label, lcd, 14, 34,
                      "drag to scroll, VAL live", we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);

    /* 表格：4 列权重分宽，行高 26，13 条数据超出数据区可滚动 */
    we_table_obj_init(&tb_table, lcd, TB_X, TB_Y, TB_W, TB_H, (uint8_t)TB_COLS, we_font_consolas_18);
    we_table_set_cells(&tb_table, tb_cells, (uint16_t)TB_ROWS);
    we_table_set_col_weights(&tb_table, tb_col_weights);
    we_table_set_row_h(&tb_table, 26U);
}

/**
 * @brief table demo 周期更新：每 400ms 原地改写两行 VAL 后 refresh + FPS
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_table_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    tb_live_timer += ms_tick;
    if (tb_live_timer >= TB_LIVE_PERIOD)
    {
        tb_live_timer = 0U;
        tb_update_live_vals();
        /* 单元格数据归 demo 持有：原地改写缓冲后手动触发重绘 */
        we_table_refresh(&tb_table);
    }

    we_demo_update_fps(lcd, &tb_fps_label, &tb_fps_timer,
                       &tb_last_frames, tb_fps_buf, ms_tick);
}
