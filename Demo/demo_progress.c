#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/btn/we_widget_btn.h"
#include "widgets/progress/we_widget_progress.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t progress_title;
static we_label_obj_t progress_note;
static we_label_obj_t progress_fps;
static we_label_obj_t progress_stat;
static we_progress_obj_t progress_bar;
static we_btn_obj_t progress_btn_minus;
static we_btn_obj_t progress_btn_plus;
static we_btn_obj_t progress_btn_auto;

static uint32_t progress_fps_timer;
static uint32_t progress_last_frames;
static uint32_t progress_auto_acc_ms;
static uint8_t progress_auto_mode;
static int8_t progress_auto_dir;
static uint8_t progress_demo_value;
static char progress_fps_buf[16];
static char progress_stat_buf[40];

/**
 * @brief 更新进度条 demo 状态文本
 * @return 无
 */
static void _progress_update_stat(void)
{
    sprintf(progress_stat_buf, "VAL %03u DSP %03u AUTO %u", (unsigned)progress_demo_value,
            (unsigned)we_progress_get_display_value(&progress_bar), (unsigned)progress_auto_mode);
    we_label_set_text(&progress_stat, progress_stat_buf);
}

/**
 * @brief 处理减小进度按钮点击
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入数据
 * @return 1 表示事件已消费
 */
static uint8_t _progress_minus_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        progress_auto_mode = 0U;
        we_progress_sub_value(&progress_bar, 16U);
        progress_demo_value = we_progress_get_value(&progress_bar);
        _progress_update_stat();
    }
    return 1U;
}

/**
 * @brief 处理增加进度按钮点击
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入数据
 * @return 1 表示事件已消费
 */
static uint8_t _progress_plus_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        progress_auto_mode = 0U;
        we_progress_add_value(&progress_bar, 16U);
        progress_demo_value = we_progress_get_value(&progress_bar);
        _progress_update_stat();
    }
    return 1U;
}

/**
 * @brief 切换自动模式开关
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入数据
 * @return 1 表示事件已消费
 */
static uint8_t _progress_auto_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    we_btn_obj_t *btn = (we_btn_obj_t *)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        progress_auto_mode ^= 1U;
        progress_auto_acc_ms = 0U;
        we_btn_set_text(btn, progress_auto_mode ? "AUTO ON" : "AUTO OFF");
        _progress_update_stat();
    }
    return 1U;
}

/**
 * @brief 初始化 progress demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_progress_simple_demo_init(we_lcd_t *lcd)
{
    int16_t btn_w = 74;
    int16_t btn_h = 30;

    progress_fps_timer = 0U;
    progress_last_frames = 0U;
    progress_auto_acc_ms = 0U;
    progress_auto_mode = 1U;
    progress_auto_dir = 1;
    progress_demo_value = 90U;
    memset(progress_fps_buf, 0, sizeof(progress_fps_buf));
    memset(progress_stat_buf, 0, sizeof(progress_stat_buf));

    we_label_obj_init(&progress_title, lcd, 10, 10, "PROGRESS DEMO", we_font_consolas_18, RGB888TODEV(236, 241, 248),
                      255);
    we_label_obj_init(&progress_note, lcd, 10, 32, "manual 0~255 animated bar", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&progress_fps, lcd, we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 10, "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&progress_stat, lcd, 10, 54, "VAL 090 DSP 090 AUTO 1", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    we_progress_obj_init(&progress_bar, lcd,
                         20,                        // x
                         92,                        // y
                         240,                       // w
                         26,                        // h
                         progress_demo_value,       // 进度[0:255]
                         RGB888TODEV(40, 52, 70),   // 前景颜色
                         RGB888TODEV(74, 166, 255), // 背景颜色
                         255);
    /*
we_progress_set_colors(&progress_bar,
               RGB888TODEV(40, 52, 70),//前景颜色
               RGB888TODEV(74, 166, 255));//背景颜色
    */

    we_btn_obj_init(&progress_btn_minus, lcd, 20, 146, btn_w, btn_h, "-16", we_font_consolas_18, _progress_minus_cb);
    we_btn_obj_init(&progress_btn_plus, lcd, 103, 146, btn_w, btn_h, "+16", we_font_consolas_18, _progress_plus_cb);
    we_btn_obj_init(&progress_btn_auto, lcd, 186, 146, btn_w, btn_h, "AUTO ON", we_font_consolas_18, _progress_auto_cb);

    we_btn_set_radius(&progress_btn_minus, 10);
    we_btn_set_radius(&progress_btn_plus, 10);
    we_btn_set_radius(&progress_btn_auto, 10);

    _progress_update_stat();
}

/**
 * @brief progress demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_progress_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    if (progress_auto_mode)
    {
        progress_auto_acc_ms += ms_tick;
        while (progress_auto_acc_ms >= 8U)
        {
            progress_auto_acc_ms -= 8U;

            if (progress_auto_dir > 0)
            {
                if (we_progress_get_display_value(&progress_bar) >= 255U)
                {
                    progress_auto_dir = -1;
                }
                else if (progress_demo_value < 255U)
                {
                    progress_demo_value++;
                }
            }
            else
            {
                if (we_progress_get_display_value(&progress_bar) == 0U)
                {
                    progress_auto_dir = 1;
                }
                else if (progress_demo_value > 0U)
                {
                    progress_demo_value--;
                }
            }
        }
        if (progress_demo_value != we_progress_get_value(&progress_bar))
        {
            we_progress_set_value(&progress_bar, progress_demo_value); // 设置进度[0:255]
        }
    }

    _progress_update_stat();
    we_demo_update_fps(lcd, &progress_fps, &progress_fps_timer, &progress_last_frames, progress_fps_buf, ms_tick);
}
