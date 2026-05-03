/**
 * @file  demo_chart.c
 * @brief 波形图控件 (chart) 演示
 *
 * 演示内容：
 * 1. 轮换慢速正弦、快速正弦、三角波、方波
 * 2. 顶部统一标题/说明/FPS/波形名
 * 3. 下方大区域滚动绘制波形
 */

#include "demo_common.h"
#include "simple_widget_demos.h"
#include "widgets/chart/we_widget_chart.h"
#include <string.h>

#define CHART_BUF_CAP 280
static int16_t chart_data[CHART_BUF_CAP];

static we_chart_obj_t chart;
static we_label_obj_t chart_title;
static we_label_obj_t chart_note;
static we_label_obj_t chart_fps;
static we_label_obj_t chart_wave;

#define WAVE_SIN_SLOW 0
#define WAVE_SIN_FAST 1
#define WAVE_TRIANGLE 2
#define WAVE_SQUARE 3
#define WAVE_COUNT 4

static const char *const _wave_names[WAVE_COUNT] = {"SINE LOW", "SINE HIGH", "TRIANGLE", "SQUARE"};

static uint8_t  chart_wave_type;
static int16_t  chart_angle;
static int16_t  chart_amplitude;
static uint32_t chart_fps_timer;
static uint32_t chart_last_frames;
static uint32_t chart_wave_timer;
static char     chart_fps_buf[16];

#define WAVE_SWITCH_MS 8000U
#define STEP_SIN_SLOW 5
#define STEP_SIN_FAST 14
#define STEP_TRI_SLOW 5
#define STEP_SQR_SLOW 5

/**
 * @brief 切换当前波形类型并清空历史数据
 * @param type 波形类型枚举值
 */
static void _switch_wave(uint8_t type)
{
    chart_wave_type = type;
    chart_angle = 0;
    we_chart_clear(&chart);
    we_label_set_text(&chart_wave, _wave_names[type]);
}

/**
 * @brief 生成当前波形的下一个采样点
 * @return 采样值（像素域）
 */
static int16_t _sample_next(void)
{
    int16_t val = 0;
    int16_t amp = chart_amplitude;

    switch (chart_wave_type)
    {
    case WAVE_SIN_SLOW:
        chart_angle = (int16_t)((chart_angle + STEP_SIN_SLOW) & 0x1FF);
        val = (int16_t)((int32_t)we_sin(chart_angle) * amp / 32768);
        break;
    case WAVE_SIN_FAST:
        chart_angle = (int16_t)((chart_angle + STEP_SIN_FAST) & 0x1FF);
        val = (int16_t)((int32_t)we_sin(chart_angle) * amp / 32768);
        break;
    case WAVE_TRIANGLE:
        chart_angle = (int16_t)((chart_angle + STEP_TRI_SLOW) & 0x1FF);
        if (chart_angle < 256)
            val = (int16_t)((int32_t)(chart_angle - 128) * amp / 128);
        else
            val = (int16_t)((int32_t)(384 - chart_angle) * amp / 128);
        break;
    case WAVE_SQUARE:
        chart_angle = (int16_t)((chart_angle + STEP_SQR_SLOW) & 0x1FF);
        val = (chart_angle < 256) ? amp : (int16_t)(-amp);
        break;
    default:
        break;
    }

    return val;
}

/**
 * @brief 初始化波形图控件演示场景
 * @param lcd GUI 运行时上下文
 */
void we_chart_simple_demo_init(we_lcd_t *lcd)
{
    int16_t chart_x = 10;
    int16_t chart_y = 78;
    int16_t chart_w = 260;
    int16_t chart_h = 148;
    uint16_t cap    = (uint16_t)chart_w;

    chart_wave_type   = WAVE_SIN_SLOW;
    chart_angle       = 0;
    chart_amplitude   = (int16_t)(chart_h * 3 / 8);
    chart_fps_timer   = 0U;
    chart_last_frames = 0U;
    chart_wave_timer  = 0U;
    memset(chart_fps_buf, 0, sizeof(chart_fps_buf));

    we_label_obj_init(&chart_title, lcd, 10, 10,
                      "CHART DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255U);
    we_label_obj_init(&chart_note, lcd, 10, 32,
                      "rolling waveform", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255U);
    we_label_obj_init(&chart_fps, lcd, 196, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255U);
    we_label_obj_init(&chart_wave, lcd, 10, 54,
                      _wave_names[WAVE_SIN_SLOW], we_font_consolas_18,
                      RGB888TODEV(200, 200, 80), 255U);

    we_chart_obj_init(&chart, lcd, chart_x, chart_y, (uint16_t)chart_w, (uint16_t)chart_h,
                      chart_data, cap, RGB888TODEV(80, 220, 120), 1U, 255U);
}

/**
 * @brief 波形图演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_chart_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (!lcd || ms_tick == 0U)
        return;

    chart_wave_timer += ms_tick;
    if (chart_wave_timer >= WAVE_SWITCH_MS)
    {
        chart_wave_timer = 0U;
        _switch_wave((uint8_t)((chart_wave_type + 1U) % WAVE_COUNT));
    }

    we_chart_push(&chart, _sample_next());
    we_demo_update_fps(lcd, &chart_fps, &chart_fps_timer, &chart_last_frames, chart_fps_buf, ms_tick);
}
