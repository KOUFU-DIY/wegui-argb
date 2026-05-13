/**
 * @file  demo_label_ex.c
 * @brief 高级文字控件 (label_ex) 演示
 *
 * 演示内容：
 * 1. 三个 label_ex 以不同角速度旋转
 * 2. 中间一组附带缩放呼吸
 * 3. 顶部统一标题/说明/FPS
 */

#include "simple_widget_demos.h"
#include "widgets/label_ex/we_widget_label_ex.h"
#include "demo_common.h"
#include <string.h>

static we_label_obj_t    label_ex_title;
static we_label_obj_t    label_ex_note;
static we_label_obj_t    label_ex_fps;
static we_label_ex_obj_t label_ex_a;
static we_label_ex_obj_t label_ex_b;
static we_label_ex_obj_t label_ex_c;

static uint32_t label_ex_ticks_ms;
static uint32_t label_ex_fps_timer;
static uint32_t label_ex_last_frames;
static char     label_ex_fps_buf[16];

/**
 * @brief 初始化高级文字控件演示场景
 * @param lcd GUI 运行时上下文
 */
void we_label_ex_simple_demo_init(we_lcd_t *lcd)
{
    int16_t cx    = (int16_t)(lcd->width / 2);
    int16_t cy    = 138;
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    label_ex_ticks_ms    = 0U;
    label_ex_fps_timer   = 0U;
    label_ex_last_frames = 0U;
    memset(label_ex_fps_buf, 0, sizeof(label_ex_fps_buf));

    we_label_obj_init(&label_ex_title, lcd, 10, 10,
                      "LABEL_EX DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&label_ex_note, lcd, 10, 32,
                      "rotate / scale", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&label_ex_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    we_label_ex_obj_init(&label_ex_a, lcd,
                         cx, (int16_t)(cy - 44),
                         "WeGui", we_font_consolas_18,
                         RGB888TODEV(255, 214, 120), 255);

    we_label_ex_obj_init(&label_ex_b, lcd,
                         cx, cy,
                         "label_ex", we_font_consolas_18,
                         RGB888TODEV(120, 230, 205), 255);

    we_label_ex_obj_init(&label_ex_c, lcd,
                         cx, (int16_t)(cy + 44),
                         "ROTATE", we_font_consolas_18,
                         RGB888TODEV(255, 140, 80), 255);
}

/**
 * @brief 高级文字演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_label_ex_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int16_t  angle_a;
    int16_t  angle_b;
    int16_t  angle_c;
    uint16_t scale_c;
    uint32_t t;

    if (!lcd || ms_tick == 0U)
        return;

    label_ex_ticks_ms += ms_tick;
    t = label_ex_ticks_ms;

    angle_a = (int16_t)((t / 4000U * 512U + (t % 4000U) * 512U / 4000U) & 0x1FFU);
    angle_b = (int16_t)(512U - ((t / 2500U * 512U + (t % 2500U) * 512U / 2500U) & 0x1FFU));
    angle_c = (int16_t)((t / 1500U * 512U + (t % 1500U) * 512U / 1500U) & 0x1FFU);
    scale_c = (uint16_t)(256U + (uint32_t)((we_sin((int16_t)((t * 3U / 100U) & 0x1FFU)) * 77) >> 15));

    we_label_ex_set_transform(&label_ex_a, angle_a, 256U);
    we_label_ex_set_transform(&label_ex_b, angle_b, 256U);
    we_label_ex_set_transform(&label_ex_c, angle_c, scale_c);

    we_demo_update_fps(lcd, &label_ex_fps, &label_ex_fps_timer,
                       &label_ex_last_frames, label_ex_fps_buf, ms_tick);
}
