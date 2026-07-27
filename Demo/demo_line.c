/**
 * @file  demo_line.c
 * @brief 线段（line）控件功能 demo —— 多条线各演示一种动画 + 多种缓动
 *
 * 4 条线，每个效果每 LN_PERIOD 循环一次（动画 LN_DUR，便于看清曲线/过渡）：
 *   ease  —— 端点钟摆扫动，每次循环换一种缓动（标签显示当前缓动名）
 *   move  —— 平移动画（左右滑动带回弹，out_back）
 *   color —— 颜色动画（循环换色，in_out_quad）
 *   fade  —— 透明度动画（呼吸明灭，in_out_sine）
 * 几何 / 颜色 / 透明度是三类独立的中央动画节点，可同时进行、互不干扰。
 * 圆头用单遍胶囊填充，淡入淡出时圆头与线身不叠色。
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/line/we_widget_line.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_label_obj_t ln_title;
static we_label_obj_t ln_fps_label;
static we_label_obj_t ln_lbl[4];
static we_line_obj_t  ln[4];           /* 0=ease 1=move 2=color 3=fade */

static uint32_t ln_fps_timer;
static uint32_t ln_last_frames;
static uint32_t ln_t[4];               /* 各效果计时器 */
static char     ln_fps_buf[16];

/* 各效果各自的状态（含义不同，故分开具名而非数组） */
static uint8_t  ln_ease_idx;           /* ease 行：当前缓动序号 0..5 */
static uint8_t  ln_move_dir;           /* move 行：平移方向 0/1 */
static uint8_t  ln_color_idx;          /* color 行：当前颜色序号 0..4 */
static uint8_t  ln_fade_dim;           /* fade 行：0=亮 1=暗 */

/* 每个效果的循环周期与动画时长（毫秒） */
#define LN_PERIOD 1200U
#define LN_DUR    800U

/* 各行垂直中心 */
static const int16_t ln_cy[4] = { 72, 114, 156, 198 };
/* 行标签（row0 之后会被替换为当前缓动名） */
static const char *const ln_name[4] = { "ease", "move", "color", "fade" };

/* 线段左右 X、sweep 摆幅、move 位移幅度、fade 暗态透明度 */
#define LN_LX    78
#define LN_RX    250
#define LN_SWING 18
#define LN_SHIFT 40
#define LN_DIM   40

/* 颜色调色板（color 行循环用） */
static const uint8_t ln_pal[5][3] = {
    {  88, 166, 240 }, {  90, 210, 130 }, { 250, 160,  60 },
    { 240, 110, 170 }, {  80, 210, 220 }
};

/**
 * @brief 初始化 line demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_line_simple_demo_init(we_lcd_t *lcd)
{
    int16_t mx    = 14;
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t i;

    ln_fps_timer   = 0U;
    ln_last_frames = 0U;
    ln_ease_idx    = 0U;
    ln_move_dir    = 0U;
    ln_color_idx   = 0U;
    ln_fade_dim    = 0U;
    memset(ln_fps_buf, 0, sizeof(ln_fps_buf));
    /* 错相位起始：4 个效果各每 LN_PERIOD 循环，画面持续有动作 */
    ln_t[0] = LN_PERIOD;
    ln_t[1] = LN_PERIOD * 3U / 4U;
    ln_t[2] = LN_PERIOD / 2U;
    ln_t[3] = LN_PERIOD / 4U;

    we_label_obj_init(&ln_title, lcd, mx, 10,
                      "LINE", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ln_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 四行标签（按各行中心垂直居中） */
    for (i = 0; i < 4; i++)
    {
        int8_t  yt, yb;
        int16_t ly;
        we_get_text_bbox(we_font_consolas_18, ln_name[i], &yt, &yb);
        ly = (int16_t)(ln_cy[i] - (yt + yb) / 2);
        we_label_obj_init(&ln_lbl[i], lcd, mx, ly,
                          ln_name[i], we_font_consolas_18,
                          RGB888TODEV(170, 180, 196), 255);
    }

    /* 0 ease：左端固定，右端钟摆扫动（缓动每次换） */
    we_line_obj_init(&ln[0], lcd, LN_LX, ln_cy[0], LN_RX, ln_cy[0]);
    we_line_set_width(&ln[0], 5U);
    we_line_set_color(&ln[0], RGB888TODEV(88, 166, 240));

    /* 1 move：整体左右滑动（带回弹） */
    we_line_obj_init(&ln[1], lcd, 84, ln_cy[1], 204, ln_cy[1]);
    we_line_set_width(&ln[1], 5U);
    we_line_set_color(&ln[1], RGB888TODEV(90, 210, 130));

    /* 2 color：循环换色 */
    we_line_obj_init(&ln[2], lcd, LN_LX, ln_cy[2], LN_RX, ln_cy[2]);
    we_line_set_width(&ln[2], 5U);
    we_line_set_color(&ln[2], RGB888TODEV(ln_pal[0][0], ln_pal[0][1], ln_pal[0][2]));

    /* 3 fade：透明度呼吸 */
    we_line_obj_init(&ln[3], lcd, LN_LX, ln_cy[3], LN_RX, ln_cy[3]);
    we_line_set_width(&ln[3], 5U);
    we_line_set_color(&ln[3], RGB888TODEV(250, 160, 60));
}

/**
 * @brief line demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_line_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int16_t i;

    if (lcd == NULL || ms_tick == 0U)
        return;

    for (i = 0; i < 4; i++)
        ln_t[i] += ms_tick;

    /* ease：每个周期换一种缓动——直接列出，函数与显示名一眼对照 */
    if (ln_t[0] >= LN_PERIOD)
    {
        we_ease_fn_t ease;
        const char  *name;
        int16_t      ty;

        ln_t[0] = 0U;
        switch (ln_ease_idx)
        {
        case 0:  ease = we_ease_linear;      name = "linear"; break;
        case 1:  ease = we_ease_in_out_quad; name = "quad";   break;
        case 2:  ease = we_ease_out_cubic;   name = "cubic";  break;
        case 3:  ease = we_ease_in_out_sine; name = "sine";   break;
        case 4:  ease = we_ease_out_back;    name = "back";   break;
        default: ease = we_ease_out_bounce;  name = "bounce"; break;
        }
        /* 序号奇偶决定摆向（上/下） */
        ty = (int16_t)(ln_cy[0] + ((ln_ease_idx & 1U) ? -LN_SWING : LN_SWING));

        we_label_set_text(&ln_lbl[0], name);
        we_line_anim_points(&ln[0], LN_LX, ln_cy[0], LN_RX, ty, LN_DUR, ease);
        ln_ease_idx = (uint8_t)((ln_ease_idx + 1U) % 6U);
    }

    /* move：每个周期反向平移（平移动画，out_back 回弹） */
    if (ln_t[1] >= LN_PERIOD)
    {
        int16_t dx;
        ln_t[1] = 0U;
        ln_move_dir ^= 1U;
        dx = (int16_t)(ln_move_dir ? LN_SHIFT : -LN_SHIFT);
        we_line_anim_move(&ln[1], dx, 0, LN_DUR, we_ease_out_back);
    }

    /* color：每个周期换下一个颜色（颜色动画，in_out_quad） */
    if (ln_t[2] >= LN_PERIOD)
    {
        ln_t[2] = 0U;
        ln_color_idx = (uint8_t)((ln_color_idx + 1U) % 5U);
        we_line_anim_color(&ln[2],
                           RGB888TODEV(ln_pal[ln_color_idx][0], ln_pal[ln_color_idx][1],
                                       ln_pal[ln_color_idx][2]),
                           LN_DUR, we_ease_in_out_quad);
    }

    /* fade：每个周期在明/暗之间过渡（透明度动画，in_out_sine） */
    if (ln_t[3] >= LN_PERIOD)
    {
        ln_t[3] = 0U;
        ln_fade_dim ^= 1U;
        we_line_anim_opacity(&ln[3], (uint8_t)(ln_fade_dim ? LN_DIM : 255U), LN_DUR,
                             we_ease_in_out_sine);
    }

    we_demo_update_fps(lcd, &ln_fps_label, &ln_fps_timer,
                       &ln_last_frames, ln_fps_buf, ms_tick);
}
