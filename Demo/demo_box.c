/**
 * @file  demo_box.c
 * @brief 矩形面板（box）控件功能 demo —— 2x2 四块面板各演示一种能力
 *
 * 4 块面板，动态项每 BX_PERIOD 切换一次（即时生效，无动画）：
 *   mix    —— 静态展示四角混合：大圆角 / 大切角 / 直角 / 小圆角 + 细边框
 *   border —— 全切角，边框厚度循环 0 → 2 → 5 → 9（0 = 无边框状态）
 *   color  —— 圆角 + 细边框，填充颜色循环切换
 *   fade   —— 对角混合（切角/圆角相对），透明度在明/暗两档间切换
 * 本 demo 只使用即时 set 接口，不依赖控件动画（WE_BOX_USE_ANIM 默认关闭）。
 */

#include "simple_widget_demos.h"
#include "demo_common.h"
#include "widgets/box/we_widget_box.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_label_obj_t bx_title;
static we_label_obj_t bx_fps_label;
static we_label_obj_t bx_lbl[4];
static we_box_obj_t   bx[4];           /* 0=mix 1=border 2=color 3=fade */

static uint32_t bx_fps_timer;
static uint32_t bx_last_frames;
static uint32_t bx_t[3];               /* border/color/fade 三个动态项计时器 */
static char     bx_fps_buf[16];

static uint8_t  bx_border_idx;         /* border 块：当前厚度序号 0..3 */
static uint8_t  bx_color_idx;          /* color 块：当前颜色序号 0..4 */
static uint8_t  bx_fade_dim;           /* fade 块：0=亮 1=暗 */

/* 循环周期（毫秒） */
#define BX_PERIOD 1400U

/* 2x2 网格布局（280x240 基准） */
#define BX_MX   14
#define BX_GAP  12
#define BX_W    120
#define BX_H    84
#define BX_Y0   40
#define BX_Y1   (BX_Y0 + BX_H + 10)

/* fade 块暗态透明度 */
#define BX_DIM  56

/* 面板标签 */
static const char *const bx_name[4] = { "mix", "border", "color", "fade" };

/* border 块厚度循环表（含 0 = 无边框） */
static const uint8_t bx_bw_tab[4] = { 0U, 2U, 5U, 9U };

/* color 块调色板 */
static const uint8_t bx_pal[5][3] = {
    {  52,  96, 168 }, {  46, 140,  90 }, { 170, 104,  40 },
    { 156,  62, 110 }, {  44, 134, 146 }
};

/**
 * @brief 初始化 box demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_box_simple_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t i;

    bx_fps_timer   = 0U;
    bx_last_frames = 0U;
    bx_border_idx  = 0U;
    bx_color_idx   = 0U;
    bx_fade_dim    = 0U;
    memset(bx_fps_buf, 0, sizeof(bx_fps_buf));
    /* 错相位起始：三个动态项各每 BX_PERIOD 循环，画面持续有动作 */
    bx_t[0] = BX_PERIOD;
    bx_t[1] = BX_PERIOD * 2U / 3U;
    bx_t[2] = BX_PERIOD / 3U;

    we_label_obj_init(&bx_title, lcd, BX_MX, 10,
                      "BOX", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&bx_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 0 mix：四角混合样式静态展示（大圆角/大切角/直角/小圆角）+ 细边框 */
    we_box_obj_init(&bx[0], lcd, BX_MX, BX_Y0, BX_W, BX_H);
    we_box_set_color(&bx[0], RGB888TODEV(40, 52, 70));
    we_box_set_border(&bx[0], RGB888TODEV(120, 168, 224), 3U);
    we_box_set_corner(&bx[0], WE_BOX_LT, WE_BOX_CORNER_ROUND, 24U);
    we_box_set_corner(&bx[0], WE_BOX_RT, WE_BOX_CORNER_CHAMFER, 24U);
    we_box_set_corner(&bx[0], WE_BOX_LB, WE_BOX_CORNER_ROUND, 0U);
    we_box_set_corner(&bx[0], WE_BOX_RB, WE_BOX_CORNER_ROUND, 10U);

    /* 1 border：全切角，边框厚度周期循环（从 0 开始逐档变厚） */
    we_box_obj_init(&bx[1], lcd, BX_MX + BX_W + BX_GAP, BX_Y0, BX_W, BX_H);
    we_box_set_color(&bx[1], RGB888TODEV(36, 48, 62));
    we_box_set_corner(&bx[1], WE_BOX_LT, WE_BOX_CORNER_CHAMFER, 16U);
    we_box_set_corner(&bx[1], WE_BOX_RT, WE_BOX_CORNER_CHAMFER, 16U);
    we_box_set_corner(&bx[1], WE_BOX_LB, WE_BOX_CORNER_CHAMFER, 16U);
    we_box_set_corner(&bx[1], WE_BOX_RB, WE_BOX_CORNER_CHAMFER, 16U);
    we_box_set_border(&bx[1], RGB888TODEV(250, 160, 60), bx_bw_tab[0]);

    /* 2 color：统一圆角 + 细边框，填充色动画循环 */
    we_box_obj_init(&bx[2], lcd, BX_MX, BX_Y1, BX_W, BX_H);
    we_box_set_radius(&bx[2], 18U);
    we_box_set_color(&bx[2], RGB888TODEV(bx_pal[0][0], bx_pal[0][1], bx_pal[0][2]));
    we_box_set_border(&bx[2], RGB888TODEV(200, 210, 224), 2U);

    /* 3 fade：对角混合（左上/右下切角，右上/左下圆角），透明度呼吸 */
    we_box_obj_init(&bx[3], lcd, BX_MX + BX_W + BX_GAP, BX_Y1, BX_W, BX_H);
    we_box_set_color(&bx[3], RGB888TODEV(58, 46, 88));
    we_box_set_border(&bx[3], RGB888TODEV(168, 140, 230), 3U);
    we_box_set_corner(&bx[3], WE_BOX_LT, WE_BOX_CORNER_CHAMFER, 20U);
    we_box_set_corner(&bx[3], WE_BOX_RT, WE_BOX_CORNER_ROUND, 20U);
    we_box_set_corner(&bx[3], WE_BOX_LB, WE_BOX_CORNER_ROUND, 20U);
    we_box_set_corner(&bx[3], WE_BOX_RB, WE_BOX_CORNER_CHAMFER, 20U);

    /* 面板标签后创建，压在面板上层 */
    for (i = 0; i < 4; i++)
    {
        int16_t lx = (int16_t)(((i % 2) == 0 ? BX_MX : BX_MX + BX_W + BX_GAP) + 12);
        int16_t ly = (int16_t)(((i / 2) == 0 ? BX_Y0 : BX_Y1) + 10);
        we_label_obj_init(&bx_lbl[i], lcd, lx, ly,
                          bx_name[i], we_font_consolas_18,
                          RGB888TODEV(196, 205, 220), 255);
    }
}

/**
 * @brief box demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_box_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int16_t i;

    if (lcd == NULL || ms_tick == 0U)
        return;

    for (i = 0; i < 3; i++)
        bx_t[i] += ms_tick;

    /* border：每个周期切下一档厚度（立即生效，直观对比 0/2/5/9） */
    if (bx_t[0] >= BX_PERIOD)
    {
        bx_t[0] = 0U;
        bx_border_idx = (uint8_t)((bx_border_idx + 1U) % 4U);
        we_box_set_border(&bx[1], RGB888TODEV(250, 160, 60), bx_bw_tab[bx_border_idx]);
    }

    /* color：每个周期切下一个颜色（即时生效） */
    if (bx_t[1] >= BX_PERIOD)
    {
        bx_t[1] = 0U;
        bx_color_idx = (uint8_t)((bx_color_idx + 1U) % 5U);
        we_box_set_color(&bx[2],
                         RGB888TODEV(bx_pal[bx_color_idx][0], bx_pal[bx_color_idx][1],
                                     bx_pal[bx_color_idx][2]));
    }

    /* fade：每个周期在明/暗两档透明度间切换（即时生效） */
    if (bx_t[2] >= BX_PERIOD)
    {
        bx_t[2] = 0U;
        bx_fade_dim ^= 1U;
        we_box_set_opacity(&bx[3], (uint8_t)(bx_fade_dim ? BX_DIM : 255U));
    }

    we_demo_update_fps(lcd, &bx_fps_label, &bx_fps_timer,
                       &bx_last_frames, bx_fps_buf, ms_tick);
}
