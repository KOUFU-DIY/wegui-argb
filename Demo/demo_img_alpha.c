/**
 * @file  demo_img_alpha.c
 * @brief A1/A2/A4/A8 透明位图演示
 *
 * 演示内容：
 * 1. 同一图标按 A1/A2/A4/A8 四档位深并排显示，对比边缘精度
 * 2. 透明位图由前景色上色：四个 A8 图标各自不同颜色
 * 3. 第一个图标颜色循环渐变（we_img_obj_set_color），第三个透明度呼吸
 * 4. 右上角 FPS
 *
 * 素材来自 tool/2.img2c 例程 input/alpha 桶（48x48，内置数组）。
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "res_img.h"
#include "widgets/img/we_widget_img.h"
#include <string.h>

static we_label_obj_t ia_title;
static we_label_obj_t ia_note;
static we_label_obj_t ia_fps;
static we_label_obj_t ia_tag[4];
static we_img_obj_t   ia_ladder[4]; // 同图标 A1/A2/A4/A8 精度阶梯
static we_img_obj_t   ia_tint[4];   // 不同图标不同前景色

static uint32_t ia_ticks_ms;
static uint32_t ia_fps_timer;
static uint32_t ia_last_frames;
static char     ia_fps_buf[16];

/**
 * @brief 初始化透明位图演示场景
 * @param lcd GUI 运行时上下文
 */
void we_img_alpha_simple_demo_init(we_lcd_t *lcd)
{
    static const unsigned char *const ladder_src[4] = {
        demo_chat_a1_raw_be_48x48,
        demo_chat_a2_raw_be_48x48,
        demo_chat_a4_raw_be_48x48,
        demo_chat_a8_raw_be_48x48,
    };
    static const unsigned char *const tint_src[4] = {
        demo_windows_a8_raw_be_48x48,
        demo_powerdrill_a8_raw_be_48x48,
        demo_mapin_a8_raw_be_48x48,
        demo_picture_a8_raw_be_48x48,
    };
    static const char *const tag_text[4] = {"A1", "A2", "A4", "A8"};

    int16_t margin_x = 10;
    int16_t fps_x    = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);
    uint8_t i;

    ia_ticks_ms    = 0U;
    ia_fps_timer   = 0U;
    ia_last_frames = 0U;
    memset(ia_fps_buf, 0, sizeof(ia_fps_buf));

    we_label_obj_init(&ia_title, lcd, margin_x, 10,
                      "ALPHA IMG DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&ia_note, lcd, margin_x, 32,
                      "A1/A2/A4/A8 + tint", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&ia_fps, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* 第一排：同一图标的位深阶梯（默认白色前景），下方标注位深 */
    for (i = 0U; i < 4U; i++)
    {
        int16_t x = (int16_t)(16 + i * 66);

        we_img_obj_init(&ia_ladder[i], lcd, x, 58, ladder_src[i], 255);
        we_label_obj_init(&ia_tag[i], lcd, (int16_t)(x + 14), 110,
                          tag_text[i], we_font_consolas_18,
                          RGB888TODEV(138, 152, 170), 255);
    }

    /* 第二排：A8 图标 + 各自前景色（[0] 由 tick 循环变色） */
    for (i = 0U; i < 4U; i++)
    {
        we_img_obj_init(&ia_tint[i], lcd, (int16_t)(16 + i * 66), 150, tint_src[i], 255);
    }
    we_img_obj_set_color(&ia_tint[1], RGB888TODEV(255, 154, 102));
    we_img_obj_set_color(&ia_tint[2], RGB888TODEV(120, 230, 205));
    we_img_obj_set_color(&ia_tint[3], RGB888TODEV(102, 178, 255));
}

/**
 * @brief 透明位图演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_img_alpha_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    int16_t phase;
    uint8_t r, g, b, opacity;

    if (lcd == NULL || ms_tick == 0U)
    {
        return;
    }

    ia_ticks_ms += ms_tick;

    /* 三相位色相环：R/G/B 相位各差 1/3 圈（512 步制 ≈ 171） */
    phase = (int16_t)((ia_ticks_ms * 4U) / 100U);
    r = (uint8_t)(128 + ((we_sin((int16_t)(phase & 0x1FF)) * 120) >> 15));
    g = (uint8_t)(128 + ((we_sin((int16_t)((phase + 171) & 0x1FF)) * 120) >> 15));
    b = (uint8_t)(128 + ((we_sin((int16_t)((phase + 341) & 0x1FF)) * 120) >> 15));
    we_img_obj_set_color(&ia_tint[0], RGB888TODEV(r, g, b));

    /* 透明度呼吸：验证 位图 alpha x 控件 opacity 的乘法路径 */
    opacity = (uint8_t)(90U + (((uint32_t)(we_sin((int16_t)((ia_ticks_ms * 6U) / 100U) & 0x1FF))
                                + 32768U) * 130U >> 16));
    we_img_obj_set_opacity(&ia_tint[2], opacity);

    we_demo_update_fps(lcd, &ia_fps, &ia_fps_timer,
                       &ia_last_frames, ia_fps_buf, ms_tick);
}
