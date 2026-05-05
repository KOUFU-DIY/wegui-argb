#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/label/we_widget_label.h"
#include "widgets/slider/we_widget_slider.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t slider_title;
static we_label_obj_t slider_hint;
static we_label_obj_t slider_fps;
static we_label_obj_t slider_h_value;
static we_label_obj_t slider_v_value;
static we_label_obj_t slider_auto_value;
static we_slider_obj_t slider_h;
static we_slider_obj_t slider_v;
static we_slider_obj_t slider_auto;
static uint32_t slider_fps_timer;
static uint32_t slider_last_frames;
static uint32_t slider_auto_acc_ms;
static char slider_fps_buf[16];
static char slider_h_buf[24];
static char slider_v_buf[24];
static char slider_auto_buf[24];
static uint8_t slider_last_h;
static uint8_t slider_last_v;
static uint8_t slider_last_auto;
static int8_t slider_auto_dir;

/**
 * @brief 刷新滑条当前值显示文本。
 * @return 无。
 */
static void _slider_update_text(void)
{
    uint8_t h_val;
    uint8_t v_val;
    uint8_t auto_val;

    h_val = we_slider_get_value(&slider_h);
    v_val = we_slider_get_value(&slider_v);
    auto_val = we_slider_get_value(&slider_auto);

    if (h_val != slider_last_h)
    {
        slider_last_h = h_val;
        sprintf(slider_h_buf, "H %03u", (unsigned)h_val);
        we_label_set_text(&slider_h_value, slider_h_buf);
    }

    if (v_val != slider_last_v)
    {
        slider_last_v = v_val;
        sprintf(slider_v_buf, "V %03u", (unsigned)v_val);
        we_label_set_text(&slider_v_value, slider_v_buf);
    }

    if (auto_val != slider_last_auto)
    {
        slider_last_auto = auto_val;
        sprintf(slider_auto_buf, "A %03u", (unsigned)auto_val);
        we_label_set_text(&slider_auto_value, slider_auto_buf);
    }
}

/**
 * @brief 初始化 slider simple demo。
 * @param lcd GUI 屏幕上下文指针。
 * @return 无。
 */
void we_slider_simple_demo_init(we_lcd_t *lcd)
{
    slider_fps_timer = 0U;
    slider_last_frames = 0U;
    slider_auto_acc_ms = 0U;
    slider_auto_dir = 1;
    memset(slider_fps_buf, 0, sizeof(slider_fps_buf));
    memset(slider_h_buf, 0, sizeof(slider_h_buf));
    memset(slider_v_buf, 0, sizeof(slider_v_buf));
    memset(slider_auto_buf, 0, sizeof(slider_auto_buf));
    slider_last_h = 0xFFU;
    slider_last_v = 0xFFU;
    slider_last_auto = 0xFFU;

    we_label_obj_init(&slider_title, lcd, 10, 10, "SLIDER", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&slider_hint, lcd, 10, 32, "range 20~220 | drag or tap", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&slider_fps, lcd, 196, 10, "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    we_slider_obj_init(&slider_h, lcd, 20, 80, 188, 28, WE_SLIDER_ORIENT_HOR, 20U, 220U, 96U,
                       RGB888TODEV(58, 66, 82), RGB888TODEV(74, 166, 255), RGB888TODEV(240, 244, 250), 255);
    we_slider_set_thumb_size(&slider_h, 22U);
    we_slider_set_track_thickness(&slider_h, 8U);

    we_slider_obj_init(&slider_v, lcd, 228, 68, 28, 104, WE_SLIDER_ORIENT_VER, 20U, 220U, 148U,
                       RGB888TODEV(58, 66, 82), RGB888TODEV(255, 166, 74), RGB888TODEV(240, 244, 250), 255);
    we_slider_set_thumb_size(&slider_v, 22U);
    we_slider_set_track_thickness(&slider_v, 8U);

    we_slider_obj_init(&slider_auto, lcd, 20, 174, 188, 28, WE_SLIDER_ORIENT_HOR, 20U, 220U, 64U,
                       RGB888TODEV(58, 66, 82), RGB888TODEV(122, 214, 120), RGB888TODEV(240, 244, 250), 255);
    we_slider_set_thumb_size(&slider_auto, 22U);
    we_slider_set_track_thickness(&slider_auto, 8U);

    we_label_obj_init(&slider_h_value, lcd, 20, 116, "H 096", we_font_consolas_18, RGB888TODEV(245, 214, 120), 255);
    we_label_obj_init(&slider_v_value, lcd, 180, 124, "V 148", we_font_consolas_18, RGB888TODEV(255, 191, 122), 255);
    we_label_obj_init(&slider_auto_value, lcd, 20, 208, "A 064", we_font_consolas_18, RGB888TODEV(122, 236, 168), 255);

    _slider_update_text();
}

/**
 * @brief slider simple demo 周期更新。
 * @param lcd GUI 屏幕上下文指针。
 * @param ms_tick 本轮累计毫秒数。
 * @return 无。
 */
void we_slider_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    slider_auto_acc_ms += ms_tick;
    while (slider_auto_acc_ms >= 16U)
    {
        uint8_t auto_val = we_slider_get_value(&slider_auto);
        slider_auto_acc_ms -= 16U;

        if (slider_auto_dir > 0)
        {
            if (auto_val >= 220U)
            {
                slider_auto_dir = -1;
            }
            else
            {
                we_slider_add_value(&slider_auto, 1U);
            }
        }
        else
        {
            if (auto_val <= 20U)
            {
                slider_auto_dir = 1;
            }
            else
            {
                we_slider_sub_value(&slider_auto, 1U);
            }
        }
    }

    _slider_update_text();
    we_demo_update_fps(lcd, &slider_fps, &slider_fps_timer, &slider_last_frames, slider_fps_buf, ms_tick);
}
