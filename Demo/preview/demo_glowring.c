/**
 * @file  demo_glowring.c
 * @brief 透明渐变色环（glowring）preview demo —— DEMO_ID 127
 *
 * 演示内容：
 * 1. 左侧 150px 三凸台实心环持续旋转（约 4 s 一圈）：外沿由形状表给出，
 *    三个凸台随角度一起转，内沿仍是正圆；环中心空洞放一行静态文字，
 *    旋转只标脏环带，空洞里的文字从不重绘（可开 WE_CFG_DEBUG_DIRTY_RECT
 *    观察红框只框环带、不框中心）
 * 2. 右侧 64px 渐变细环反向旋转，并用整体透明度做呼吸（96~255）
 * 3. 右上角 FPS
 */

#include "preview_demos.h"

#include "demo_common.h"
#include "widgets_preview/glowring/we_widget_glowring.h"
#include <string.h>

static we_glowring_obj_t gr_big;    /* 大环：三凸台实心环，旋转 */
static we_glowring_obj_t gr_small;  /* 小环：渐变环，反向旋转 + 透明度呼吸 */
static we_label_obj_t    gr_center; /* 大环空洞里的静态文字 */

static uint16_t gr_acc_big;   /* 旋转累加器 Q8（ms × 速度，低 8 位留作小数） */
static uint16_t gr_acc_small;
static uint16_t gr_breath;    /* 呼吸相位（ms，& 2047 循环） */

#if !WE_DEMO_SMALL_SCREEN
static we_label_obj_t gr_title;
static we_label_obj_t gr_note;
static we_label_obj_t gr_fps;

static uint32_t gr_fps_timer;
static uint32_t gr_last_frames;
static char     gr_fps_buf[16];
#endif

/* 旋转速度 Q8（每毫秒的 512 步制角度 × 256）：33 ≈ 4 s/圈，52 ≈ 2.5 s/圈 */
#define GR_SPEED_BIG   (33U)
#define GR_SPEED_SMALL (52U)

/* 三凸台形状表：相对透明端角度 → 外半径（Q4，1/16 px），128 项（每项 2.8125°），
 * 索引 0 = angle 方向，顺时针递增，项间线性插值。由手绘环的 SVG 轮廓栅格化后
 * 沿极坐标采样得到：细环段钳到 >= 68（内半径 66 + 1px 实体 + 1px 渐隐），
 * 三个凸台高低不一、最高到 73。小屏用的表是它按 29/74 等比缩放的结果。 */
#if !WE_DEMO_SMALL_SCREEN
static const uint16_t gr_shape_q4[128] = {
    1088, 1088, 1088, 1088, 1088, 1088, 1088, 1092,
    1092, 1092, 1092, 1096, 1096, 1096, 1096, 1100,
    1100, 1100, 1100, 1104, 1100, 1104, 1104, 1108,
    1112, 1116, 1120, 1124, 1132, 1140, 1140, 1144,
    1148, 1144, 1140, 1140, 1132, 1128, 1120, 1112,
    1108, 1104, 1096, 1096, 1096, 1096, 1096, 1096,
    1096, 1096, 1096, 1096, 1096, 1096, 1096, 1096,
    1100, 1100, 1104, 1100, 1100, 1104, 1100, 1100,
    1104, 1104, 1100, 1104, 1100, 1100, 1100, 1096,
    1096, 1092, 1092, 1092, 1092, 1092, 1092, 1096,
    1104, 1116, 1128, 1144, 1152, 1160, 1160, 1152,
    1144, 1136, 1128, 1116, 1108, 1096, 1092, 1088,
    1088, 1088, 1088, 1088, 1092, 1092, 1092, 1092,
    1092, 1096, 1096, 1096, 1100, 1096, 1100, 1104,
    1100, 1100, 1104, 1108, 1112, 1124, 1136, 1148,
    1156, 1164, 1168, 1160, 1140, 1124, 1104, 1088,
};
#else
static const uint16_t gr_shape_q4[128] = {
     426,  426,  426,  426,  426,  426,  426,  428,
     428,  428,  428,  430,  430,  430,  430,  431,
     431,  431,  431,  433,  431,  433,  433,  434,
     436,  437,  439,  440,  444,  447,  447,  448,
     450,  448,  447,  447,  444,  442,  439,  436,
     434,  433,  430,  430,  430,  430,  430,  430,
     430,  430,  430,  430,  430,  430,  430,  430,
     431,  431,  433,  431,  431,  433,  431,  431,
     433,  433,  431,  433,  431,  431,  431,  430,
     430,  428,  428,  428,  428,  428,  428,  430,
     433,  437,  442,  448,  451,  455,  455,  451,
     448,  445,  442,  437,  434,  430,  428,  426,
     426,  426,  426,  426,  428,  428,  428,  428,
     428,  430,  430,  430,  431,  430,  431,  433,
     431,  431,  433,  434,  436,  440,  445,  450,
     453,  456,  458,  455,  447,  440,  433,  426,
};
#endif

/**
 * @brief 推进两只环的角度与小环的呼吸透明度（大小屏共用）
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
static void _gr_demo_step(uint16_t ms_tick)
{
    uint16_t tri;

    /* 累加器高 8 位是整数步数，取走后保留小数部分，长时间不漂移 */
    gr_acc_big = (uint16_t)(gr_acc_big + ms_tick * GR_SPEED_BIG);
    we_glowring_rotate(&gr_big, (int16_t)(gr_acc_big >> 8));
    gr_acc_big &= 0xFFU;

    gr_acc_small = (uint16_t)(gr_acc_small + ms_tick * GR_SPEED_SMALL);
    we_glowring_rotate(&gr_small, (int16_t)(-(int16_t)(gr_acc_small >> 8)));
    gr_acc_small &= 0xFFU;

    /* 呼吸：2048 ms 三角波 → 透明度 96..255（无除法） */
    gr_breath = (uint16_t)((gr_breath + ms_tick) & 2047U);
    tri = (gr_breath < 1024U) ? gr_breath : (uint16_t)(2047U - gr_breath);
    we_glowring_set_opacity(&gr_small, (uint8_t)(96U + ((tri * 159U) >> 10)));
}

#if !WE_DEMO_SMALL_SCREEN /* ==================== 280x240 大屏版 ==================== */

/**
 * @brief 初始化 glowring preview demo 场景
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_glowring_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t tw;

    gr_acc_big     = 0U;
    gr_acc_small   = 0U;
    gr_breath      = 0U;
    gr_fps_timer   = 0U;
    gr_last_frames = 0U;
    memset(gr_fps_buf, 0, sizeof(gr_fps_buf));

    we_label_obj_init(&gr_title, lcd, 10, 10,
                      "GLOWRING PREVIEW", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&gr_note, lcd, 10, 32,
                      "shape ring + alpha ring", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&gr_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 大环空洞里的静态文字 */
    tw = (int16_t)we_get_text_width(we_font_consolas_18, "RING");
    we_label_obj_init(&gr_center, lcd, (int16_t)(89 - tw / 2), 127,
                      "RING", we_font_consolas_18,
                      RGB888TODEV(200, 210, 224), 255);

    /* 左侧大环：150×150，内半径 66；外沿交给形状表（细环段 68、凸台顶 73），实心、黄绿色 */
    we_glowring_obj_init(&gr_big, lcd, 14, 60, 150U, 8U);
    we_glowring_set_shape(&gr_big, gr_shape_q4, 7U);
    we_glowring_set_solid(&gr_big, 1U);
    we_glowring_set_color(&gr_big, RGB888TODEV(0x94, 0xC3, 0x3A));

    /* 右侧小环：64×64，细环带 4px，浅绿渐变 */
    we_glowring_obj_init(&gr_small, lcd, 172, 104, 64U, 4U);
    we_glowring_set_color(&gr_small, RGB888TODEV(120, 240, 160));
    we_glowring_set_angle(&gr_small, WE_DEG(90));
}

/**
 * @brief glowring preview demo 周期更新
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_glowring_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    _gr_demo_step(ms_tick);
    we_demo_update_fps(lcd, &gr_fps, &gr_fps_timer,
                       &gr_last_frames, gr_fps_buf, ms_tick);
}

#else /* ==================== 128x64 小屏版 ==================== */

/**
 * @brief 初始化 glowring preview demo（128x64 布局：左三凸台环 + 右渐变环）
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_glowring_preview_demo_init(we_lcd_t *lcd)
{
    int16_t tw;

    gr_acc_big   = 0U;
    gr_acc_small = 0U;
    gr_breath    = 0U;

    tw = (int16_t)we_get_text_width(we_font_consolas_18, "RING");
    we_label_obj_init(&gr_center, lcd, (int16_t)(32 - tw / 2), 26,
                      "RING", we_font_consolas_18,
                      RGB888TODEV(200, 210, 224), 255);

    /* 左侧大环 60×60：内半径 25，外沿交给形状表（细环段 26.6、凸台顶 28.6），实心。
     * 灰度 / 单色屏上按亮度呈现，默认绿的灰度只有 153，二值化后大半环会消失，
     * 小屏统一换成近白色 */
    we_glowring_obj_init(&gr_big, lcd, 2, 2, 60U, 4U);
    we_glowring_set_shape(&gr_big, gr_shape_q4, 7U);
    we_glowring_set_solid(&gr_big, 1U);
    we_glowring_set_color(&gr_big, RGB888TODEV(235, 255, 240));

    /* 右侧小环 40×40，细环带，渐变 */
    we_glowring_obj_init(&gr_small, lcd, 80, 12, 40U, 8U);
    we_glowring_set_color(&gr_small, RGB888TODEV(235, 255, 240));
    we_glowring_set_angle(&gr_small, WE_DEG(90));
}

/**
 * @brief glowring preview demo 周期更新（旋转 + 呼吸）
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_glowring_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;
    _gr_demo_step(ms_tick);
}

#endif /* WE_DEMO_SMALL_SCREEN */
