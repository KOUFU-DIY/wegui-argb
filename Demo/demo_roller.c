/**
 * @file  demo_roller.c
 * @brief 滚轮选值器（roller）控件功能 demo —— 双滚轮时间选择器（DEMO_ID 26）
 *
 * 两个 roller 并排：左侧小时 00~23，右侧分钟 00~55（步进 5），
 * 中间一枚 ":" 装饰 label。changed 回调把顶部 label 实时刷新为
 * "HH:MM"。FPS 照常显示。底部一行操作提示。
 *
 * 交互（毕业优化版）：
 *   - 慢速拖拽松手：就近吸附最近行；
 *   - 快速拖拽甩动松手：惯性继承拖拽速度，滑过多行再减速吸附
 *     （小时轮 24 项适合体验甩动，试试从 00 一把甩到 20+）；
 *   - 轻点中心行上/下方的可见行：吸附动画直达该行；
 *   - 快速轻扫（快到没有跟手过程）：向滑动方向翻 1 行。
 *
 * 选项字符串全部为 static const 数组（调用方持有），控件只存指针。
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/roller/we_widget_roller.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t rl_title;
static we_label_obj_t rl_fps_label;
static we_label_obj_t rl_time_label;   /* 顶部 "HH:MM" 实时显示 */
static we_label_obj_t rl_colon_label;  /* 两滚轮之间的 ":" 装饰 */
static we_label_obj_t rl_hint_label;   /* 底部操作提示（惯性甩动/点击直达） */
static we_roller_obj_t rl_hour;
static we_roller_obj_t rl_min;

static uint32_t rl_fps_timer;
static uint32_t rl_last_frames;
static char rl_fps_buf[16];
static char rl_time_buf[8];

/* 布局（280x240 基准；滚轮上移 8px 给底部提示行让位） */
#define RL_ROLLER_W   70
#define RL_ROLLER_GAP 24
#define RL_ROLLER_Y   64
#define RL_HINT_GAP   4   /* 滚轮底边到提示行的间距 */

/* 小时选项 00~23（调用方持有的静态数组，控件只存指针） */
static const char *const rl_hour_opts[24] = {
    "00", "01", "02", "03", "04", "05", "06", "07",
    "08", "09", "10", "11", "12", "13", "14", "15",
    "16", "17", "18", "19", "20", "21", "22", "23",
};

/* 分钟选项 00~55，步进 5 */
static const char *const rl_min_opts[12] = {
    "00", "05", "10", "15", "20", "25",
    "30", "35", "40", "45", "50", "55",
};

/**
 * @brief 按两个滚轮当前选中项刷新顶部 "HH:MM" 文本。
 * @return 无
 */
static void rl_update_time_label(void)
{
    int16_t h = we_roller_get_selected(&rl_hour);
    int16_t m = we_roller_get_selected(&rl_min);

    if (h < 0)
        h = 0;
    if (m < 0)
        m = 0;

    sprintf(rl_time_buf, "%02d:%02d", (int)h, (int)(m * 5));
    we_label_set_text(&rl_time_label, rl_time_buf);
}

/**
 * @brief 滚轮吸附完成回调：任一滚轮选中项变化即刷新时间显示。
 * @param obj 传入：触发回调的滚轮控件指针
 * @param idx 传入：新选中项索引
 * @return 无
 */
static void rl_on_changed(we_roller_obj_t *obj, uint16_t idx)
{
    (void)obj;
    (void)idx;
    rl_update_time_label();
}

/**
 * @brief 初始化 roller demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_roller_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t x_hour = (int16_t)((lcd->width - (2 * RL_ROLLER_W + RL_ROLLER_GAP)) / 2);
    int16_t x_min = (int16_t)(x_hour + RL_ROLLER_W + RL_ROLLER_GAP);
    int16_t colon_w;
    int16_t colon_x;
    int16_t colon_y;
    int16_t time_x;
    int16_t hint_x;
    int16_t hint_y;
    static const char *const rl_hint_text = "Fling to spin, tap to jump";

    rl_fps_timer = 0U;
    rl_last_frames = 0U;
    memset(rl_fps_buf, 0, sizeof(rl_fps_buf));
    memset(rl_time_buf, 0, sizeof(rl_time_buf));

    we_label_obj_init(&rl_title, lcd, 14, 10,
                      "ROLLER", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&rl_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 左：小时滚轮（5 可见行，高度由控件按字体行高自行推导） */
    we_roller_obj_init(&rl_hour, lcd, x_hour, RL_ROLLER_Y, RL_ROLLER_W, 5U,
                       we_font_consolas_18);
    we_roller_set_options(&rl_hour, rl_hour_opts, 24U);
    we_roller_set_selected(&rl_hour, 8U);
    we_roller_set_changed_cb(&rl_hour, rl_on_changed);

    /* 右：分钟滚轮（00~55 步进 5） */
    we_roller_obj_init(&rl_min, lcd, x_min, RL_ROLLER_Y, RL_ROLLER_W, 5U,
                       we_font_consolas_18);
    we_roller_set_options(&rl_min, rl_min_opts, 12U);
    we_roller_set_selected(&rl_min, 6U); /* 30 分 */
    we_roller_set_changed_cb(&rl_min, rl_on_changed);

    /* 两滚轮之间的 ":" 装饰（对齐滚轮垂直中心） */
    colon_w = (int16_t)we_get_text_width(we_font_consolas_18, ":");
    colon_x = (int16_t)(x_hour + RL_ROLLER_W + (RL_ROLLER_GAP - colon_w) / 2);
    colon_y = (int16_t)(RL_ROLLER_Y + rl_hour.base.h / 2
                        - (int16_t)we_font_get_line_height(we_font_consolas_18) / 2);
    we_label_obj_init(&rl_colon_label, lcd, colon_x, colon_y,
                      ":", we_font_consolas_18, RGB888TODEV(150, 162, 182), 255);

    /* 顶部时间显示：按初始选中项填充，水平居中 */
    sprintf(rl_time_buf, "%02d:%02d",
            (int)we_roller_get_selected(&rl_hour),
            (int)(we_roller_get_selected(&rl_min) * 5));
    time_x = (int16_t)((lcd->width
                        - (int16_t)we_get_text_width(we_font_consolas_18, rl_time_buf)) / 2);
    we_label_obj_init(&rl_time_label, lcd, time_x, 38,
                      rl_time_buf, we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);

    /* 底部操作提示：惯性甩动 + 点击直达（水平居中，贴滚轮下方） */
    hint_x = (int16_t)((lcd->width
                        - (int16_t)we_get_text_width(we_font_consolas_18, rl_hint_text)) / 2);
    hint_y = (int16_t)(RL_ROLLER_Y + rl_hour.base.h + RL_HINT_GAP);
    we_label_obj_init(&rl_hint_label, lcd, hint_x, hint_y,
                      rl_hint_text, we_font_consolas_18,
                      RGB888TODEV(120, 132, 152), 255);
}

/**
 * @brief roller demo 周期更新（仅刷新 FPS，滚轮交互全部事件驱动）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_roller_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    we_demo_update_fps(lcd, &rl_fps_label, &rl_fps_timer,
                       &rl_last_frames, rl_fps_buf, ms_tick);
}
