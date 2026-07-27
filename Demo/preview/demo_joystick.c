/**
 * @file  demo_joystick.c
 * @brief 虚拟摇杆（joystick）preview demo —— DEMO_ID 111
 *
 * 左侧 112px 摇杆：底盘圆内按住拖动输出矢量，松手弹性回中；
 * 右侧 120×120 围栏内一个小圆点按矢量速度移动（tick 里 Q8 定点积分
 * pos += vector × dt，碰壁夹紧）；顶部 label 实时显示 "vec dx,dy"
 * （由 changed_cb 驱动，含回中衰减过程）。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/joystick/we_widget_joystick.h"
#include "widgets/label/we_widget_label.h"
#include "widgets/box/we_widget_box.h"
#include <stdio.h>
#include <string.h>

/* 布局（280x240 基准）：摇杆与围栏底边对齐（96+112 = 88+120 = 208） */
#define JS_SIZE    112
#define JS_X       14
#define JS_Y       96
#define JS_FENCE_X 146
#define JS_FENCE_Y 88
#define JS_FENCE_W 120

/* 围栏内移动小圆点（box 四角全圆化退化为圆） */
#define JS_DOT_SIZE 14
#define JS_DOT_MIN_X (JS_FENCE_X + 3)                          /* 边框 2px + 1px 间隙 */
#define JS_DOT_MAX_X (JS_FENCE_X + JS_FENCE_W - 3 - JS_DOT_SIZE)
#define JS_DOT_MIN_Y (JS_FENCE_Y + 3)
#define JS_DOT_MAX_Y (JS_FENCE_Y + JS_FENCE_W - 3 - JS_DOT_SIZE)

static we_joystick_obj_t js_stick;
static we_box_obj_t      js_fence;      /* 围栏底板（圆角 + 边框） */
static we_box_obj_t      js_dot;        /* 受控小圆点 */
static we_label_obj_t    js_title;
static we_label_obj_t    js_vec_label;  /* 顶部矢量回显 */
static we_label_obj_t    js_fps_label;

static uint32_t js_fps_timer;
static uint32_t js_last_frames;
static char     js_fps_buf[16];
static char     js_vec_buf[20];

static int32_t js_dot_x_q8; /* 小圆点左上角 X（Q8 定点，亚像素积分） */
static int32_t js_dot_y_q8; /* 小圆点左上角 Y（Q8 定点） */

/**
 * @brief 摇杆矢量变化回调：刷新顶部 "vec dx,dy" 回显。
 * @param js 触发回调的摇杆对象指针（未使用）。
 * @param dx 当前 X 轴矢量（-127..127）。
 * @param dy 当前 Y 轴矢量（-127..127）。
 * @return 无。
 */
static void _js_demo_on_changed(void *js, int8_t dx, int8_t dy)
{
    (void)js;
    sprintf(js_vec_buf, "vec %d,%d", (int)dx, (int)dy);
    we_label_set_text(&js_vec_label, js_vec_buf);
}

/**
 * @brief 初始化 joystick preview demo。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @return 无。
 */
void we_joystick_preview_demo_init(we_lcd_t *lcd)
{
    js_fps_timer   = 0U;
    js_last_frames = 0U;
    memset(js_fps_buf, 0, sizeof(js_fps_buf));
    memset(js_vec_buf, 0, sizeof(js_vec_buf));

    /* 小圆点起始：围栏正中（Q8 定点） */
    js_dot_x_q8 = (int32_t)(JS_FENCE_X + (JS_FENCE_W - JS_DOT_SIZE) / 2) << 8;
    js_dot_y_q8 = (int32_t)(JS_FENCE_Y + (JS_FENCE_W - JS_DOT_SIZE) / 2) << 8;

    we_label_obj_init(&js_title, lcd, 10, 8,
                      "JOYSTICK preview", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&js_fps_label, lcd,
                      we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 8,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    sprintf(js_vec_buf, "vec 0,0");
    we_label_obj_init(&js_vec_label, lcd, 10, 34,
                      js_vec_buf, we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    /* 右侧围栏：深色底板 + 细边框 */
    we_box_obj_init(&js_fence, lcd, JS_FENCE_X, JS_FENCE_Y, JS_FENCE_W, JS_FENCE_W);
    we_box_set_radius(&js_fence, 10U);
    we_box_set_color(&js_fence, RGB888TODEV(26, 32, 44));
    we_box_set_border(&js_fence, RGB888TODEV(96, 112, 138), 2U);

    /* 受控小圆点：box 四角全圆（半径 = 边长一半）退化为实心圆 */
    we_box_obj_init(&js_dot, lcd,
                    (int16_t)(js_dot_x_q8 >> 8), (int16_t)(js_dot_y_q8 >> 8),
                    JS_DOT_SIZE, JS_DOT_SIZE);
    we_box_set_radius(&js_dot, JS_DOT_SIZE / 2U);
    we_box_set_color(&js_dot, RGB888TODEV(255, 176, 60));

    /* 左侧摇杆：默认死区 8%，矢量回显由 changed_cb 驱动 */
    we_joystick_obj_init(&js_stick, lcd, JS_X, JS_Y, JS_SIZE);
    we_joystick_set_changed_cb(&js_stick, _js_demo_on_changed);
}

/**
 * @brief joystick preview demo 周期更新：矢量积分驱动小圆点移动。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param ms_tick 传入：本轮累计毫秒数。
 * @return 无。
 */
void we_joystick_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int8_t vx;
    int8_t vy;

    if (lcd == NULL || ms_tick == 0U)
        return;

    we_joystick_get_vector(&js_stick, &vx, &vy);
    if (vx != 0 || vy != 0)
    {
        /* Q8 积分：pos += vec × dt / 4 → 满偏（127）约 127px/s */
        js_dot_x_q8 += ((int32_t)vx * ms_tick) / 4;
        js_dot_y_q8 += ((int32_t)vy * ms_tick) / 4;

        /* 碰壁夹紧在围栏内侧 */
        if (js_dot_x_q8 < ((int32_t)JS_DOT_MIN_X << 8))
            js_dot_x_q8 = (int32_t)JS_DOT_MIN_X << 8;
        if (js_dot_x_q8 > ((int32_t)JS_DOT_MAX_X << 8))
            js_dot_x_q8 = (int32_t)JS_DOT_MAX_X << 8;
        if (js_dot_y_q8 < ((int32_t)JS_DOT_MIN_Y << 8))
            js_dot_y_q8 = (int32_t)JS_DOT_MIN_Y << 8;
        if (js_dot_y_q8 > ((int32_t)JS_DOT_MAX_Y << 8))
            js_dot_y_q8 = (int32_t)JS_DOT_MAX_Y << 8;

        /* box 的 set_pos 值未变时自动跳过重绘 */
        we_box_set_pos(&js_dot, (int16_t)(js_dot_x_q8 >> 8),
                       (int16_t)(js_dot_y_q8 >> 8));
    }

    we_demo_update_fps(lcd, &js_fps_label, &js_fps_timer,
                       &js_last_frames, js_fps_buf, ms_tick);
}
