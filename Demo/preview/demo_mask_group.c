/**
 * @file  demo_mask_group.c
 * @brief 蒙版容器（mask_group）preview demo —— DEMO_ID 125，左右两块面板各演示一种能力
 *
 *   clip —— 四角混合裁剪 + 边框：左上大圆角 / 右上大切角 / 左下直角 /
 *           右下小圆角，外加 3px 边框；容器内一块满铺直角 box + 一条水平
 *           往返滑动的文字，文字滑到边缘时被轮廓与边框内沿平滑裁掉；
 *   fade —— 旋转线性渐变 + 细边框：容器内 2x3 色块矩阵 + 标签，渐变方向
 *           每帧旋转，内容沿渐变方向连续渐隐到背板色；边框保持实色不受
 *           渐变影响（"相框"语义），圆角/切角与渐变叠加生效。
 *
 * 两块面板均为"挂进容器即生效"：子控件本身没有任何圆角/切角/边框/渐变代码。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/mask_group/we_widget_mask_group.h"
#include "widgets/box/we_widget_box.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

static we_label_obj_t mg_title;
static we_label_obj_t mg_fps_label;

static we_mask_group_obj_t mg_clip;     /* 左：圆角裁剪面板 */
static we_box_obj_t        mg_clip_bg;  /* 满铺直角底板（被裁出圆角） */
static we_label_obj_t      mg_clip_lbl; /* 往返滑动的文字（边缘被裁） */
static we_label_obj_t      mg_clip_tag;

static we_mask_group_obj_t mg_fade;     /* 右：旋转渐变面板 */
static we_box_obj_t        mg_fade_bg;  /* 满铺直角底板 */
static we_box_obj_t        mg_fade_cell[6];
static we_label_obj_t      mg_fade_tag;

static uint32_t mg_fps_timer;
static uint32_t mg_last_frames;
static char     mg_fps_buf[16];

static int16_t  mg_slide_x;    /* 滑动文字当前局部 X */
static int8_t   mg_slide_dir;  /* 滑动方向 */
static uint16_t mg_grad_angle; /* 渐变角度（512 步制累加器） */
static uint16_t mg_anim_acc;   /* 动画推进计时（毫秒） */

/* 布局（280x240 基准） */
#define MG_MX     14
#define MG_GAP    12
#define MG_W      120
#define MG_H      150
#define MG_Y0     46
#define MG_R      26   /* clip 面板圆角半径 */
#define MG_FADE_R 16   /* fade 面板圆角半径 */

/* 滑动文字局部 X 范围（负值/超宽让裁剪效果可见） */
#define MG_SLIDE_MIN (-30)
#define MG_SLIDE_MAX (MG_W - 40)

/* fade 面板色块调色板 */
static const uint8_t mg_pal[6][3] = {
    { 235,  96,  70 }, {  90, 170, 250 }, { 120, 210, 120 },
    { 245, 190,  70 }, { 180, 120, 235 }, {  80, 210, 200 }
};

/**
 * @brief 初始化 mask_group demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_mask_group_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    int16_t i;

    mg_fps_timer   = 0U;
    mg_last_frames = 0U;
    mg_slide_x     = 8;
    mg_slide_dir   = 1;
    mg_grad_angle  = 0U;
    mg_anim_acc    = 0U;
    memset(mg_fps_buf, 0, sizeof(mg_fps_buf));

    we_label_obj_init(&mg_title, lcd, MG_MX, 10,
                      "MASK", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&mg_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* ---- 左：四角混合裁剪 + 边框面板 ---- */
    we_mask_group_obj_init(&mg_clip, lcd, MG_MX, MG_Y0, MG_W, MG_H);
    we_mask_group_set_corner(&mg_clip, WE_MASK_GROUP_LT, WE_MASK_GROUP_CORNER_ROUND, MG_R);
    we_mask_group_set_corner(&mg_clip, WE_MASK_GROUP_RT, WE_MASK_GROUP_CORNER_CHAMFER, MG_R);
    we_mask_group_set_corner(&mg_clip, WE_MASK_GROUP_LB, WE_MASK_GROUP_CORNER_ROUND, 0U);
    we_mask_group_set_corner(&mg_clip, WE_MASK_GROUP_RB, WE_MASK_GROUP_CORNER_ROUND, 12U);
    we_mask_group_set_border(&mg_clip, RGB888TODEV(120, 168, 224), 3U);

    /* 满铺直角底板：四角被容器裁成圆角，证明裁的是"内容"而非控件自身能力 */
    we_box_obj_init(&mg_clip_bg, lcd, 0, 0, MG_W, MG_H);
    we_box_set_radius(&mg_clip_bg, 0U);
    we_box_set_color(&mg_clip_bg, RGB888TODEV(52, 96, 168));
    we_mask_group_add_child(&mg_clip, (we_obj_t *)&mg_clip_bg);
    we_mask_group_set_child_pos(&mg_clip, (we_obj_t *)&mg_clip_bg, 0, 0);

    we_label_obj_init(&mg_clip_lbl, lcd, 0, 0,
                      "SLIDE", we_font_consolas_18, RGB888TODEV(255, 255, 255), 255);
    we_mask_group_add_child(&mg_clip, (we_obj_t *)&mg_clip_lbl);
    we_mask_group_set_child_pos(&mg_clip, (we_obj_t *)&mg_clip_lbl, mg_slide_x, 24);

    we_label_obj_init(&mg_clip_tag, lcd, 0, 0,
                      "clip", we_font_consolas_18, RGB888TODEV(200, 216, 240), 255);
    we_mask_group_add_child(&mg_clip, (we_obj_t *)&mg_clip_tag);
    we_mask_group_set_child_pos(&mg_clip, (we_obj_t *)&mg_clip_tag, 12, (int16_t)(MG_H - 30));

    /* ---- 右：旋转渐变 + 细边框面板（左上/右下切角，右上/左下圆角） ---- */
    we_mask_group_obj_init(&mg_fade, lcd, MG_MX + MG_W + MG_GAP, MG_Y0, MG_W, MG_H);
    we_mask_group_set_corner(&mg_fade, WE_MASK_GROUP_LT, WE_MASK_GROUP_CORNER_CHAMFER, MG_FADE_R);
    we_mask_group_set_corner(&mg_fade, WE_MASK_GROUP_RT, WE_MASK_GROUP_CORNER_ROUND, MG_FADE_R);
    we_mask_group_set_corner(&mg_fade, WE_MASK_GROUP_LB, WE_MASK_GROUP_CORNER_ROUND, MG_FADE_R);
    we_mask_group_set_corner(&mg_fade, WE_MASK_GROUP_RB, WE_MASK_GROUP_CORNER_CHAMFER, MG_FADE_R);
    we_mask_group_set_border(&mg_fade, RGB888TODEV(168, 140, 230), 2U);
    we_mask_group_set_gradient(&mg_fade, (int16_t)mg_grad_angle, 255U, 30U);

    we_box_obj_init(&mg_fade_bg, lcd, 0, 0, MG_W, MG_H);
    we_box_set_radius(&mg_fade_bg, 0U);
    we_box_set_color(&mg_fade_bg, RGB888TODEV(40, 52, 70));
    we_mask_group_add_child(&mg_fade, (we_obj_t *)&mg_fade_bg);
    we_mask_group_set_child_pos(&mg_fade, (we_obj_t *)&mg_fade_bg, 0, 0);

    /* 2x3 色块矩阵：观察渐变方向旋转时各色块的渐隐变化 */
    for (i = 0; i < 6; i++)
    {
        int16_t cx = (int16_t)(12 + (i % 2) * 52);
        int16_t cy = (int16_t)(12 + (i / 2) * 42);

        we_box_obj_init(&mg_fade_cell[i], lcd, 0, 0, 44, 34);
        we_box_set_radius(&mg_fade_cell[i], 8U);
        we_box_set_color(&mg_fade_cell[i],
                         RGB888TODEV(mg_pal[i][0], mg_pal[i][1], mg_pal[i][2]));
        we_mask_group_add_child(&mg_fade, (we_obj_t *)&mg_fade_cell[i]);
        we_mask_group_set_child_pos(&mg_fade, (we_obj_t *)&mg_fade_cell[i], cx, cy);
    }

    we_label_obj_init(&mg_fade_tag, lcd, 0, 0,
                      "fade", we_font_consolas_18, RGB888TODEV(230, 236, 245), 255);
    we_mask_group_add_child(&mg_fade, (we_obj_t *)&mg_fade_tag);
    we_mask_group_set_child_pos(&mg_fade, (we_obj_t *)&mg_fade_tag, 12, (int16_t)(MG_H - 30));
}

/**
 * @brief mask_group demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_mask_group_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    mg_anim_acc += ms_tick;
    if (mg_anim_acc >= 32U)
    {
        mg_anim_acc = 0U;

        /* clip：文字水平往返滑动，边缘经过圆角时被平滑裁剪 */
        mg_slide_x = (int16_t)(mg_slide_x + mg_slide_dir * 2);
        if (mg_slide_x >= MG_SLIDE_MAX)
        {
            mg_slide_x = MG_SLIDE_MAX;
            mg_slide_dir = -1;
        }
        else if (mg_slide_x <= MG_SLIDE_MIN)
        {
            mg_slide_x = MG_SLIDE_MIN;
            mg_slide_dir = 1;
        }
        we_mask_group_set_child_pos(&mg_clip, (we_obj_t *)&mg_clip_lbl, mg_slide_x, 24);

        /* fade：渐变方向持续旋转（512 步制，每步 3 单位 ≈ 2.1°） */
        mg_grad_angle = (uint16_t)((mg_grad_angle + 3U) & 0x1FFU);
        we_mask_group_set_gradient(&mg_fade, (int16_t)mg_grad_angle, 255U, 30U);
    }

    we_demo_update_fps(lcd, &mg_fps_label, &mg_fps_timer,
                       &mg_last_frames, mg_fps_buf, ms_tick);
}
