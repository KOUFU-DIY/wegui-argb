/**
 * @file  demo_label.c
 * @brief 标签控件 (Label) 基础演示
 *
 * 演示内容：
 * 1. 创建多个 Label，设置初始文本、字体、颜色、透明度
 * 2. 每帧通过 sin 波动态修改文本内容与颜色 (颜色随波值渐变)
 * 3. 另一个 Label 上下浮动 + 透明度渐变，演示位移与淡入淡出
 * 4. 右上角显示实时帧率 (FPS)
 *
 * 用法：
 *   以 280x240 为设计基准，通过 demo_common 做整数缩放适配。
 *   we_label_simple_demo_init(&mylcd);
 *   we_gui_timer_create(&mylcd, we_label_simple_demo_tick, 16U, 1U);
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include <stdio.h>
#include <string.h>

/* ---- 控件对象 ---- */
static we_label_obj_t label_title;       /* 演示标题，固定不动 */
static we_label_obj_t label_note;        /* 功能说明，固定不动 */
static we_label_obj_t label_fps;         /* 右上角帧率显示 */
static we_label_obj_t label_main;        /* 主演示 Label：文本与颜色随 sin 波变化 */
static we_label_obj_t label_sub;         /* 副演示 Label：上下浮动 + 透明度渐变 */

/* ---- 状态变量 ---- */
static uint32_t label_ticks_ms;          /* 累计运行时间 (ms)，驱动动画 */
static uint32_t label_fps_timer;         /* FPS 计算用计时器 */
static uint32_t label_last_frames;       /* 上次 FPS 统计时的帧计数快照 */
static char     label_fps_buf[16];       /* FPS 文本缓冲区，如 "FPS 60" */
static char     label_main_buf[24];      /* 主 Label 动态文本缓冲区 */

/**
 * @brief 初始化标签控件演示场景
 * @param lcd GUI 运行时上下文
 */
void we_label_simple_demo_init(we_lcd_t *lcd)
{
    /* 设计基准：280x240；布局按当前屏幕做整数缩放。 */
    int16_t margin_x = 10;
    int16_t title_y  = 10;
    int16_t note_y   = 32;
    int16_t fps_x    = lcd->width - 10 - 74;
    int16_t main_x   = 24;
    int16_t main_y   = 92;
    int16_t sub_y    = 128;

    /* 重置状态 */
    label_ticks_ms    = 0U;
    label_fps_timer   = 0U;
    label_last_frames = 0U;
    memset(label_fps_buf,  0, sizeof(label_fps_buf));
    memset(label_main_buf, 0, sizeof(label_main_buf));

    /* 标题行：白色，固定 */
    we_label_obj_init(&label_title, lcd, margin_x, title_y,
                      "LABEL DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);

    /* 功能说明：灰色，固定 */
    we_label_obj_init(&label_note, lcd, margin_x, note_y,
                      "text / color / opacity", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    /* FPS 计数：青绿色，与标题同行靠右 */
    we_label_obj_init(&label_fps, lcd, fps_x, title_y,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 主演示 Label：金黄色，tick 中实时更新文本和颜色 */
    we_label_obj_init(&label_main, lcd, main_x, main_y,
                      "LABEL 000", we_font_consolas_18,
                      RGB888TODEV(255, 214, 120), 255);

    /* 副演示 Label：青绿色，透明度初始 220，tick 中上下浮动 */
    we_label_obj_init(&label_sub, lcd, main_x, sub_y,
                      "fade and move", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 220);
}

/**
 * @brief 标签演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_label_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int16_t  base_x;
    int16_t  base_y;
    int16_t  float_y;
    uint8_t  opacity;
    uint8_t  wave;
    colour_t color;

    if (lcd == NULL || ms_tick == 0U)
    {
        return;
    }

    label_ticks_ms += ms_tick;   /* 累加时间，驱动所有动画 */

    base_x = 24;
    base_y = 128;

    /* sin 波映射到 0~255，用于主 Label 文本数字和颜色分量 */
    wave = (uint8_t)(((uint32_t)(we_sin((int16_t)((label_ticks_ms * 6U) / 100U) & 0x1FF)) + 32768U) >> 8);

    /* 副 Label 透明度在 80~230 之间以较慢频率来回变化 */
    opacity = (uint8_t)(80U + (((uint32_t)(we_sin((int16_t)((label_ticks_ms * 5U) / 100U) & 0x1FF)) + 32768U) * 150U >> 16));

    /* 副 Label Y 坐标以 sin 波上下浮动约 ±8px（经屏幕缩放） */
    float_y = (int16_t)(base_y + ((we_sin((int16_t)((label_ticks_ms * 4U) / 100U) & 0x1FF)
                                   * 8) >> 15));

    /* 主 Label 文本：实时显示当前 wave 值 */
    snprintf(label_main_buf, sizeof(label_main_buf), "LABEL %03u", (unsigned)wave);
    we_label_set_text(&label_main, label_main_buf);

    /* 主 Label 颜色：R/G/B 随 wave 值渐变，形成彩色呼吸效果 */
    color = RGB888TODEV((uint8_t)(120U + wave / 2U),
                        (uint8_t)(120U + wave / 3U),
                        (uint8_t)(255U - wave / 3U));
    we_label_set_color(&label_main, color);

    /* 副 Label：更新透明度和 Y 位置 */
    we_label_set_opacity(&label_sub, opacity);
    we_label_obj_set_pos(&label_sub, base_x, float_y);

    /* 更新右上角 FPS 计数 */
    we_demo_update_fps(lcd, &label_fps, &label_fps_timer,
                       &label_last_frames, label_fps_buf, ms_tick);
}
