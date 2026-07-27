/**
 * @file  demo_animimg.c
 * @brief 帧动画（animimg）preview demo —— DEMO_ID 121
 *
 * 演示内容：
 * 1. init 时用纯整数代码生成 4 帧 48×48 图案（静态帧缓冲，零 malloc）：
 *    深色渐变背景 + 一枚亮色圆点在四个角位轮转（每帧点色不同）
 * 2. 左侧实例：120ms 间隔持续循环播放
 * 3. 右侧实例：共享同一组帧，60ms 间隔，每 2s 自动 start/stop 切换，
 *    状态 label 同步显示 RUN / STOP
 * 4. 右上角 FPS
 */

#include "preview_demos.h"

#include "demo_common.h"
#include "widgets_preview/animimg/we_widget_animimg.h"
#include <string.h>

#define AF_W   48
#define AF_H   48
#define AF_CNT 4

static we_label_obj_t   af_title;
static we_label_obj_t   af_note;
static we_label_obj_t   af_fps;
static we_label_obj_t   af_state_lbl;
static we_animimg_obj_t af_loop;      /* 左：持续循环 */
static we_animimg_obj_t af_toggle;    /* 右：start/stop 定时切换 */

static uint32_t af_fps_timer;
static uint32_t af_last_frames;
static char     af_fps_buf[16];

static uint32_t af_toggle_acc;        /* start/stop 切换累计毫秒 */
static uint8_t  af_running;           /* 右侧实例当前是否播放 */

/* 帧像素静态缓冲（4 帧 48×48 RGB565，本机字节序，调用方持有） */
static uint16_t af_buf[AF_CNT][AF_W * AF_H];
static const uint16_t *const af_frames[AF_CNT] = {
    af_buf[0], af_buf[1], af_buf[2], af_buf[3]
};

/**
 * @brief RGB888 三通道打包为 RGB565 像素（纯整数）
 * @param r 红色分量（0~255）
 * @param g 绿色分量（0~255）
 * @param b 蓝色分量（0~255）
 * @return RGB565 像素值
 */
static uint16_t _af_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r >> 3) << 11) |
                      ((uint16_t)(g >> 2) << 5) |
                      (uint16_t)(b >> 3));
}

/**
 * @brief 生成 4 帧演示图案：渐变背景 + 四角轮转亮点（纯整数）
 * @return 无
 */
static void _af_gen_frames(void)
{
    /* 圆点四个角位（LT → RT → RB → LB 轮转）与每帧点色 */
    static const int16_t dot_x[AF_CNT] = { 14, 34, 34, 14 };
    static const int16_t dot_y[AF_CNT] = { 14, 14, 34, 34 };
    static const uint8_t dot_rgb[AF_CNT][3] = {
        { 255, 120,  80 }, { 120, 255, 140 }, {  90, 170, 255 }, { 255, 220,  90 }
    };
    int16_t f, x, y;

    for (f = 0; f < AF_CNT; f++)
    {
        for (y = 0; y < AF_H; y++)
        {
            for (x = 0; x < AF_W; x++)
            {
                /* 背景：深蓝到紫的双向渐变 */
                uint8_t br = (uint8_t)(18 + (y * 40) / AF_H);
                uint8_t bg = (uint8_t)(24 + (x * 26) / AF_W);
                uint8_t bb = (uint8_t)(48 + (y * 64) / AF_H);
                int32_t dx = x - dot_x[f];
                int32_t dy = y - dot_y[f];
                int32_t d2 = dx * dx + dy * dy;

                if (d2 <= 49) /* r=7 实心亮点 */
                {
                    af_buf[f][y * AF_W + x] =
                        _af_rgb565(dot_rgb[f][0], dot_rgb[f][1], dot_rgb[f][2]);
                }
                else if (d2 <= 81) /* r=7..9 与背景取均值的软边 */
                {
                    af_buf[f][y * AF_W + x] =
                        _af_rgb565((uint8_t)((dot_rgb[f][0] + br) / 2),
                                   (uint8_t)((dot_rgb[f][1] + bg) / 2),
                                   (uint8_t)((dot_rgb[f][2] + bb) / 2));
                }
                else
                {
                    af_buf[f][y * AF_W + x] = _af_rgb565(br, bg, bb);
                }
            }
        }
    }
}

/**
 * @brief 初始化 animimg preview demo 场景
 * @param lcd GUI 运行时上下文
 * @return 无
 */
void we_animimg_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    af_fps_timer   = 0U;
    af_last_frames = 0U;
    af_toggle_acc  = 0U;
    af_running     = 1U;
    memset(af_fps_buf, 0, sizeof(af_fps_buf));

    _af_gen_frames();

    we_label_obj_init(&af_title, lcd, 10, 10,
                      "ANIMIMG PREVIEW", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&af_note, lcd, 10, 32,
                      "shared frames / auto toggle", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&af_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 左：120ms 持续循环 */
    we_animimg_obj_init(&af_loop, lcd, 56, 96, AF_W, AF_H);
    we_animimg_set_frames(&af_loop, af_frames, AF_CNT, 120U);
    we_animimg_start(&af_loop);

    /* 右：共享同一组帧，60ms 更快节奏，由 tick 定时 start/stop */
    we_animimg_obj_init(&af_toggle, lcd, 176, 96, AF_W, AF_H);
    we_animimg_set_frames(&af_toggle, af_frames, AF_CNT, 60U);
    we_animimg_start(&af_toggle);

    we_label_obj_init(&af_state_lbl, lcd, 176, 152,
                      "RUN", we_font_consolas_18,
                      RGB888TODEV(126, 231, 150), 255);
}

/**
 * @brief animimg preview demo 周期更新
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 * @return 无
 */
void we_animimg_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 右侧实例每 2s 自动 start/stop 切换（停播时定格当前帧） */
    af_toggle_acc += ms_tick;
    if (af_toggle_acc >= 2000U)
    {
        af_toggle_acc = 0U;
        af_running ^= 1U;
        if (af_running)
        {
            we_animimg_start(&af_toggle);
            we_label_set_text(&af_state_lbl, "RUN");
            we_label_set_color(&af_state_lbl, RGB888TODEV(126, 231, 150));
        }
        else
        {
            we_animimg_stop(&af_toggle);
            we_label_set_text(&af_state_lbl, "STOP");
            we_label_set_color(&af_state_lbl, RGB888TODEV(255, 128, 118));
        }
    }

    we_demo_update_fps(lcd, &af_fps, &af_fps_timer,
                       &af_last_frames, af_fps_buf, ms_tick);
}
