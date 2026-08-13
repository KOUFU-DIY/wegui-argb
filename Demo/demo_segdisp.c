/**
 * @file  demo_segdisp.c
 * @brief 段码数码管（segdisp）控件功能 demo —— DEMO_ID 33
 *
 * 演示内容：
 * 1. 大号 "12:34" 时钟：45° 斜切段形（默认）+ 自定义段厚 6 + ghost 鬼影，
 *    每秒分钟 +1；冒号用 we_segdisp_set_colon 直控 500ms 闪烁
 *    （按位标脏只重绘冒号格）
 * 2. 中排小号 "NN.N" 计数器：自定义字宽 20 + 间隔 5 + 段厚 2（宽扁细体）
 *    + 直角矩形段（旧风格对比）+ 橙色配色，'.' 自动合并进前一位的 dp
 *    小数点（dp 显示默认关闭，set_dp 打开）
 * 3. 底排 6 位段码直控：we_segdisp_set_segs 整组喂段码，外圈 a→b→c→d→e→f
 *    逐位相移跑马灯
 * 4. 右上角 FPS
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/segdisp/we_widget_segdisp.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t    ss_title;
static we_label_obj_t    ss_note;
static we_label_obj_t    ss_fps;
static we_segdisp_obj_t ss_clock;   /* 大号时钟（5 位含冒号，斜切 + ghost） */
static we_segdisp_obj_t ss_count;   /* 小号计数器（3 位含 dp，矩形段） */
static we_segdisp_obj_t ss_snake;   /* 段码直控跑马灯（6 位） */

static uint32_t ss_fps_timer;
static uint32_t ss_last_frames;
static char     ss_fps_buf[16];

static uint32_t ss_clock_acc;        /* 时钟累计毫秒 */
static uint32_t ss_blink_acc;        /* 冒号闪烁累计毫秒 */
static uint32_t ss_count_acc;        /* 计数器累计毫秒 */
static uint32_t ss_snake_acc;        /* 跑马灯累计毫秒 */
static uint8_t  ss_hour;
static uint8_t  ss_minute;
static uint8_t  ss_colon_on;
static uint16_t ss_counter;          /* 0.1 步进计数（0~999 → 00.0~99.9） */
static uint8_t  ss_snake_step;
static char     ss_clock_buf[8];     /* "HH:MM" 静态缓冲 */
static char     ss_count_buf[8];     /* "NN.N" 静态缓冲 */
static uint8_t  ss_snake_codes[6];   /* 跑马灯段码组 */

/**
 * @brief 按当前相位生成跑马灯段码组并整组直控写入
 * @return 无
 * @note 外圈段序 a(0)→b(1)→c(2)→d(3)→e(4)→f(5) 恰好是段码 bit0~bit5，
 *       每位相移 1 步，1<<((step+i)%6) 即绕外圈顺时针轮转。
 */
static void _ss_snake_apply(void)
{
    uint8_t i;

    for (i = 0U; i < 6U; i++)
        ss_snake_codes[i] = (uint8_t)(1U << ((uint8_t)(ss_snake_step + i) % 6U));
    we_segdisp_set_segs(&ss_snake, ss_snake_codes, 6U);
}

/**
 * @brief 初始化 segdisp demo 场景
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_segdisp_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    ss_fps_timer   = 0U;
    ss_last_frames = 0U;
    ss_clock_acc   = 0U;
    ss_blink_acc   = 0U;
    ss_count_acc   = 0U;
    ss_snake_acc   = 0U;
    ss_hour        = 12U;
    ss_minute      = 34U;
    ss_colon_on    = 1U;
    ss_counter     = 0U;
    ss_snake_step  = 0U;
    memset(ss_fps_buf, 0, sizeof(ss_fps_buf));
    memset(ss_clock_buf, 0, sizeof(ss_clock_buf));
    memset(ss_count_buf, 0, sizeof(ss_count_buf));
    memset(ss_snake_codes, 0, sizeof(ss_snake_codes));

    we_label_obj_init(&ss_title, lcd, 10, 10,
                      "SEGDISP", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ss_note, lcd, 10, 32,
                      "bevel clock / rect+dp / raw", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&ss_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 大号时钟：digit_h=64、自定义段厚 6（比自动值 8 纤细）、字宽/间隔
     * 走自动（→32/6）、总宽 5*32+4*6=184，水平居中；默认斜切段形 +
     * ghost，冒号由 set_colon 直控闪烁 */
    we_segdisp_obj_init(&ss_clock, lcd, 48, 56, 0U, 64U, 0U, 5U, 6U);
    we_segdisp_set_ghost(&ss_clock, 1U);
    snprintf(ss_clock_buf, sizeof(ss_clock_buf), "%02u:%02u",
             (unsigned)ss_hour, (unsigned)ss_minute);
    we_segdisp_set_text(&ss_clock, ss_clock_buf);

    /* 小号计数器："NN.N" 3 位（'.' 合并进前一位 dp），直角矩形段对比旧观感；
     * 自定义字宽 20（自动值 14，宽扁风）+ 间隔 5 + 段厚 2 细体：
     * digit_h=28、总宽 3*20+2*5=70，水平居中 */
    we_segdisp_obj_init(&ss_count, lcd, 105, 148, 20U, 28U, 5U, 3U, 2U);
    we_segdisp_set_style(&ss_count, WE_SEGDISP_STYLE_RECT);
    we_segdisp_set_dp(&ss_count, 1U); /* dp 显示默认关闭，小数点场景要打开 */
    we_segdisp_set_colors(&ss_count,
                           RGB888TODEV(255, 154, 102),
                           RGB888TODEV(70, 62, 58));
    snprintf(ss_count_buf, sizeof(ss_count_buf), "%02u.%u",
             (unsigned)(ss_counter / 10U), (unsigned)(ss_counter % 10U));
    we_segdisp_set_text(&ss_count, ss_count_buf);

    /* 段码直控跑马灯：全自动几何（digit_h=32 → 段厚 4、字宽 16、间隔 4）、
     * 总宽 6*16+5*4=116，水平居中；ghost 开启可见外圈骨架 */
    we_segdisp_obj_init(&ss_snake, lcd, 82, 192, 0U, 32U, 0U, 6U, 0U);
    we_segdisp_set_colors(&ss_snake,
                           RGB888TODEV(126, 178, 255),
                           RGB888TODEV(52, 64, 84));
    we_segdisp_set_ghost(&ss_snake, 1U);
    _ss_snake_apply();
}

/**
 * @brief segdisp demo 周期更新
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_segdisp_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 时钟：每 1000ms 分钟 +1，文本只在时间变化时重新设置 */
    ss_clock_acc += ms_tick;
    if (ss_clock_acc >= 1000U)
    {
        while (ss_clock_acc >= 1000U)
        {
            ss_clock_acc -= 1000U;
            ss_minute++;
            if (ss_minute >= 60U)
            {
                ss_minute = 0U;
                ss_hour   = (uint8_t)((ss_hour + 1U) % 24U);
            }
        }
        snprintf(ss_clock_buf, sizeof(ss_clock_buf), "%02u:%02u",
                 (unsigned)ss_hour, (unsigned)ss_minute);
        we_segdisp_set_text(&ss_clock, ss_clock_buf);
    }

    /* 冒号 500ms 闪烁：set_colon 直控冒号位（覆盖 set_text 置的常亮），
     * 状态未变时是零代价空调用，按位标脏只重绘冒号格 */
    ss_blink_acc += ms_tick;
    while (ss_blink_acc >= 500U)
    {
        ss_blink_acc -= 500U;
        ss_colon_on = (uint8_t)(ss_colon_on ? 0U : 1U);
    }
    we_segdisp_set_colon(&ss_clock, 2U, ss_colon_on);

    /* 计数器：每 100ms +0.1，"NN.N" 展示 dp 小数点 */
    ss_count_acc += ms_tick;
    while (ss_count_acc >= 100U)
    {
        ss_count_acc -= 100U;
        ss_counter = (uint16_t)((ss_counter + 1U) % 1000U);
    }
    snprintf(ss_count_buf, sizeof(ss_count_buf), "%02u.%u",
             (unsigned)(ss_counter / 10U), (unsigned)(ss_counter % 10U));
    we_segdisp_set_text(&ss_count, ss_count_buf);

    /* 跑马灯：每 100ms 相移一步，整组段码直控 */
    ss_snake_acc += ms_tick;
    while (ss_snake_acc >= 100U)
    {
        ss_snake_acc -= 100U;
        ss_snake_step = (uint8_t)((ss_snake_step + 1U) % 6U);
    }
    _ss_snake_apply();

    we_demo_update_fps(lcd, &ss_fps, &ss_fps_timer,
                       &ss_last_frames, ss_fps_buf, ms_tick);
}
