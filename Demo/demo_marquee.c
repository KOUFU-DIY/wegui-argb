/**
 * @file  demo_marquee.c
 * @brief 跑马灯标签（marquee）控件功能 demo —— DEMO_ID 27，三条对比
 *
 * 三条 marquee 纵向排布，每条背后垫一块深色 box 衬托裁剪边界：
 *   1. 长英文句：默认 30 px/s + 默认 800ms 接缝停留（循环滚动）
 *   2. 短文本：文本宽 < 控件宽，静止左对齐（不滚动对照）
 *   3. 快速条：90 px/s + 300ms 停留 + 橙色文字 + 更窄窗口
 *
 * 滚动完全由控件内部的中央动画节点推进，demo tick 只刷 FPS。
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/marquee/we_widget_marquee.h"
#include "widgets/box/we_widget_box.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_label_obj_t   mq_title;
static we_label_obj_t   mq_fps;
static we_label_obj_t   mq_cap[3];
static we_box_obj_t     mq_bg[3];
static we_marquee_obj_t mq[3];

static uint32_t mq_fps_timer;
static uint32_t mq_last_frames;
static char     mq_fps_buf[16];

/* 布局（280x240 基准） */
#define MQ_X      14
#define MQ_W_WIDE 200
#define MQ_W_SLIM 140

/* 三条示例文案（由 demo 静态持有，满足"调用方持有字符串"约定） */
static const char *const mq_text_long =
    "The quick brown fox jumps over the lazy dog -- WeGui marquee preview loop";
static const char *const mq_text_short = "SHORT TEXT";
static const char *const mq_text_fast =
    "Fast lane 90 px/s with a short 300 ms pause at the seam";

/**
 * @brief 初始化 marquee demo：标题/FPS + 三组（垫底 box + 跑马灯 + 说明）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_marquee_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t row_y[3] = {52, 116, 180};
    int16_t cap_y[3] = {32, 96, 160};
    int16_t row_w[3] = {MQ_W_WIDE, MQ_W_WIDE, MQ_W_SLIM};
    int16_t i;

    mq_fps_timer = 0U;
    mq_last_frames = 0U;
    memset(mq_fps_buf, 0, sizeof(mq_fps_buf));

    we_label_obj_init(&mq_title, lcd, MQ_X, 8,
                      "MARQUEE", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&mq_fps, lcd, fps_x, 8,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 三行说明（压在垫底 box 之上、跑马灯之外的空白处） */
    we_label_obj_init(&mq_cap[0], lcd, MQ_X, cap_y[0],
                      "long text @30px/s", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&mq_cap[1], lcd, MQ_X, cap_y[1],
                      "short text: static", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&mq_cap[2], lcd, MQ_X, cap_y[2],
                      "fast @90px/s pause 300", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    /* 垫底 box：与跑马灯同矩形，衬托 PFB 收窄裁剪边界（先建，压在下层） */
    for (i = 0; i < 3; i++)
    {
        /* 跑马灯高度 = 行高 + 2*WE_MARQUEE_PAD_Y，与控件同款公式 */
        int16_t mq_h = (int16_t)(we_font_get_line_height(we_font_consolas_18) +
                                 2 * WE_MARQUEE_PAD_Y);
        we_box_obj_init(&mq_bg[i], lcd, MQ_X, row_y[i], row_w[i], mq_h);
        we_box_set_radius(&mq_bg[i], 4U);
        we_box_set_color(&mq_bg[i], RGB888TODEV(30, 38, 52));
        we_box_set_border(&mq_bg[i], RGB888TODEV(70, 84, 106), 1U);
    }

    /* 1. 长英文句：默认速度 30 px/s、默认停留 800ms */
    we_marquee_obj_init(&mq[0], lcd, MQ_X, row_y[0], row_w[0],
                        mq_text_long, we_font_consolas_18, RGB888TODEV(236, 241, 248));

    /* 2. 静止对照：文本宽 < 控件宽，不滚动、不占动画链 */
    we_marquee_obj_init(&mq[1], lcd, MQ_X, row_y[1], row_w[1],
                        mq_text_short, we_font_consolas_18, RGB888TODEV(120, 230, 205));

    /* 3. 快速条：更窄窗口 + 90 px/s + 300ms 停留 + 橙色 */
    we_marquee_obj_init(&mq[2], lcd, MQ_X, row_y[2], row_w[2],
                        mq_text_fast, we_font_consolas_18, RGB888TODEV(250, 180, 90));
    we_marquee_set_speed(&mq[2], 90U);
    we_marquee_set_pause(&mq[2], 300U);
}

/**
 * @brief marquee demo 周期更新：滚动由控件动画自驱，这里只刷 FPS
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_marquee_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &mq_fps, &mq_fps_timer,
                       &mq_last_frames, mq_fps_buf, ms_tick);
}
