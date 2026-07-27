/**
 * @file  demo_canvas.c
 * @brief 自绘壳（canvas，preview）功能 demo —— 李萨如曲线 + 十字网格
 *
 * 用户自绘回调里混用三种原语：
 *   1. we_fill_rect     —— 十字网格背景（细网格 + 中心十字加亮）
 *   2. we_draw_line_round —— 李萨如曲线（3:2 参数方程取 48 点，相邻点连线）
 *   3. we_draw_string   —— 画布角落的比率标注
 *
 * 相位由 demo tick 推进（cv_phase），推进后调 we_canvas_invalidate()
 * 请求整框重绘，形成持续旋转变形动画；user_data 演示业务上下文透传。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/canvas/we_widget_canvas.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_label_obj_t  cv_title;
static we_label_obj_t  cv_note;
static we_label_obj_t  cv_fps;
static we_canvas_obj_t cv;

static uint16_t cv_phase;       /* 李萨如 X 相位（512 步制角度） */
static uint32_t cv_ms;          /* 累计毫秒（相位推进时基） */
static uint32_t cv_fps_timer;
static uint32_t cv_last_frames;
static char     cv_fps_buf[16];

/* 画布布局与曲线参数（280x240 基准） */
#define CV_X     40
#define CV_Y     62
#define CV_W     200
#define CV_H     150
#define CV_GRID  20            /* 网格间距（像素） */
#define CV_PTS   48            /* 曲线采样点数（40~60 区间取 48） */
#define CV_FREQ_X 3            /* X 频率（李萨如比率 3:2） */
#define CV_FREQ_Y 2            /* Y 频率 */

/**
 * @brief canvas 用户自绘回调：十字网格 + 李萨如曲线 + 文字标注
 * @param lcd 传入：GUI 屏幕上下文指针（窗口已收窄到画布矩形）
 * @param canvas 传入：控件实例指针（cast 回 we_canvas_obj_t* 取几何）
 * @param user_data 传入：业务上下文（此处 = cv_phase 的地址）
 * @return 无
 * @note 纯绘制、无状态推进：同一帧可能按 PFB 条带被多次调用。
 */
static void _cv_user_draw(we_lcd_t *lcd, void *canvas, void *user_data)
{
    const we_canvas_obj_t *c = (const we_canvas_obj_t *)canvas;
    uint16_t phase = *(const uint16_t *)user_data;
    int16_t x = c->base.x;
    int16_t y = c->base.y;
    int16_t w = c->base.w;
    int16_t h = c->base.h;
    int16_t ccx = (int16_t)(x + w / 2);
    int16_t ccy = (int16_t)(y + h / 2);
    int16_t amp_x = (int16_t)(w / 2 - 12); /* 内收 12px：线宽+AA 不越画布 */
    int16_t amp_y = (int16_t)(h / 2 - 12);
    colour_t grid_c = RGB888TODEV(52, 62, 80);
    colour_t cross_c = RGB888TODEV(84, 100, 128);
    colour_t line_c = RGB888TODEV(120, 214, 255);
    int16_t g;
    int16_t i;
    int16_t last_x = 0;
    int16_t last_y = 0;

    /* 1. 画布底色 + 细网格（we_fill_rect 原语） */
    we_fill_rect(lcd, x, y, (uint16_t)w, (uint16_t)h, RGB888TODEV(24, 30, 42), 255U);
    for (g = (int16_t)(x + CV_GRID); g < (int16_t)(x + w); g = (int16_t)(g + CV_GRID))
        we_fill_rect(lcd, g, y, 1U, (uint16_t)h, grid_c, 255U);
    for (g = (int16_t)(y + CV_GRID); g < (int16_t)(y + h); g = (int16_t)(g + CV_GRID))
        we_fill_rect(lcd, x, g, (uint16_t)w, 1U, grid_c, 255U);

    /* 2. 中心十字加亮，展示网格上的层叠 */
    we_fill_rect(lcd, ccx, y, 1U, (uint16_t)h, cross_c, 255U);
    we_fill_rect(lcd, x, ccy, (uint16_t)w, 1U, cross_c, 255U);

    /* 3. 李萨如曲线：x = A*cos(3θ + phase)，y = B*sin(2θ)，θ 走满一圈闭合
     *    we_cos/we_sin 返回 Q15，乘幅值后 >>15 还原像素（全整数） */
    for (i = 0; i <= CV_PTS; i++)
    {
        uint16_t theta = (uint16_t)(((uint32_t)(i % CV_PTS) * 512U) / CV_PTS);
        int16_t ang_x = (int16_t)((((uint32_t)theta * CV_FREQ_X) + phase) & 511U);
        int16_t ang_y = (int16_t)(((uint32_t)theta * CV_FREQ_Y) & 511U);
        int16_t pt_x = (int16_t)(ccx + (((int32_t)amp_x * we_cos(ang_x)) >> 15));
        int16_t pt_y = (int16_t)(ccy + (((int32_t)amp_y * we_sin(ang_y)) >> 15));

        if (i > 0)
            we_draw_line_round(lcd, last_x, last_y, pt_x, pt_y, 2U, line_c, 255U);
        last_x = pt_x;
        last_y = pt_y;
    }

    /* 4. 角落标注（we_draw_string 原语混用） */
    we_draw_string(lcd, (int16_t)(x + 6), (int16_t)(y + h - 24),
                   we_font_consolas_18, "3:2", RGB888TODEV(138, 152, 170), 255U);
}

/**
 * @brief 初始化 canvas demo：标题/说明/FPS + 一块自绘画布
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_canvas_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    cv_phase = 0U;
    cv_ms = 0U;
    cv_fps_timer = 0U;
    cv_last_frames = 0U;
    memset(cv_fps_buf, 0, sizeof(cv_fps_buf));

    we_label_obj_init(&cv_title, lcd, 14, 8,
                      "CANVAS", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&cv_note, lcd, 14, 34,
                      "user draw: grid + lissajous", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&cv_fps, lcd, fps_x, 8,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* user_data 透传相位变量地址：绘制回调只读，推进在 tick 里做 */
    we_canvas_obj_init(&cv, lcd, CV_X, CV_Y, CV_W, CV_H,
                       _cv_user_draw, &cv_phase);
}

/**
 * @brief canvas demo 周期更新：推进相位并请求重绘 + 刷 FPS
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_canvas_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint16_t new_phase;

    if (lcd == NULL || ms_tick == 0U)
        return;

    /* 相位 = 累计毫秒 / 8（512 步一圈 → 约 4.1 秒转满一圈） */
    cv_ms += ms_tick;
    new_phase = (uint16_t)((cv_ms >> 3) & 511U);
    if (new_phase != cv_phase)
    {
        cv_phase = new_phase;
        we_canvas_invalidate(&cv); /* 数据变了 → 请求整框重绘 */
    }

    we_demo_update_fps(lcd, &cv_fps, &cv_fps_timer,
                       &cv_last_frames, cv_fps_buf, ms_tick);
}
