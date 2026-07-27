/**
 * @file  demo_spinner.c
 * @brief 加载指示器（spinner）preview demo —— DEMO_ID 114
 *
 * 三个不同直径/颜色/速度的 spinner 并排展示拖尾旋转效果；
 * 中间那个每 SP_TOGGLE_MS 自动 stop/start 一次演示启停接口，
 * 下方 label 实时显示其运行状态（RUN/STOP）。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/spinner/we_widget_spinner.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_spinner_obj_t sp_small;
static we_spinner_obj_t sp_mid;      /* 该实例周期性 stop/start */
static we_spinner_obj_t sp_big;
static we_label_obj_t   sp_title;
static we_label_obj_t   sp_fps_label;
static we_label_obj_t   sp_state_label;

static uint32_t sp_fps_timer;
static uint32_t sp_last_frames;
static char     sp_fps_buf[16];
static uint32_t sp_toggle_acc;       /* 启停切换计时器 */

/* 中间 spinner 启停切换周期（毫秒） */
#define SP_TOGGLE_MS 1600U

/**
 * @brief 初始化 spinner preview demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_spinner_preview_demo_init(we_lcd_t *lcd)
{
    sp_fps_timer   = 0U;
    sp_last_frames = 0U;
    sp_toggle_acc  = 0U;
    memset(sp_fps_buf, 0, sizeof(sp_fps_buf));

    we_label_obj_init(&sp_title, lcd, 10, 8,
                      "SPINNER preview", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&sp_fps_label, lcd,
                      we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 三个 spinner 中心同高（y=136）并排：直径 48 / 68 / 88，速度渐慢 */
    we_spinner_obj_init(&sp_small, lcd, 32, 112, 48U);
    we_spinner_set_speed(&sp_small, 70U); /* 默认青蓝主色 */

    we_spinner_obj_init(&sp_mid, lcd, 106, 102, 68U);
    we_spinner_set_colors(&sp_mid, RGB888TODEV(120, 220, 130));
    we_spinner_set_speed(&sp_mid, 80U);

    we_spinner_obj_init(&sp_big, lcd, 180, 92, 88U);
    we_spinner_set_colors(&sp_big, RGB888TODEV(250, 170, 70));
    we_spinner_set_speed(&sp_big, 90U);

    /* 中间 spinner 的运行状态提示 */
    we_label_obj_init(&sp_state_label, lcd, 122, 196,
                      "RUN", we_font_consolas_18,
                      RGB888TODEV(120, 220, 130), 255);
}

/**
 * @brief spinner preview demo 周期更新。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_spinner_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 中间 spinner 定时启停：演示 stop 定格 / start 恢复 */
    sp_toggle_acc += ms_tick;
    if (sp_toggle_acc >= SP_TOGGLE_MS)
    {
        sp_toggle_acc = 0U;
        if (we_spinner_is_running(&sp_mid))
        {
            we_spinner_stop(&sp_mid);
            we_label_set_text(&sp_state_label, "STOP");
            we_label_set_color(&sp_state_label, RGB888TODEV(240, 130, 120));
        }
        else
        {
            we_spinner_start(&sp_mid);
            we_label_set_text(&sp_state_label, "RUN");
            we_label_set_color(&sp_state_label, RGB888TODEV(120, 220, 130));
        }
    }

    we_demo_update_fps(lcd, &sp_fps_label, &sp_fps_timer,
                       &sp_last_frames, sp_fps_buf, ms_tick);
}
