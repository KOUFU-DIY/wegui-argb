/**
 * @file  demo_flash_font.c
 * @brief 外挂 Flash 字体控件演示
 *
 * 演示内容：
 * 1. 读取 font2c external 字体渲染中英混排
 * 2. 文本每 1500ms 自动切换
 * 3. 正文透明度呼吸变化
 * 4. 顶部状态行显示当前文本索引与透明度
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "merged_bin.h"
#include "msyh_16_4bbp_ascii_menu_mix.h"
#include "msyh_24_2bbp_ascii_menu_mix.h"
#include "widgets/font_flash/we_widget_font_flash.h"
#include "widgets/label/we_widget_label.h"
#include <stdio.h>
#include <string.h>

static we_flash_font_face_t ff_face;
static uint8_t ff_glyph_scratch[WE_FLASH_FONT_SCRATCH_MAX];

static we_label_obj_t      ff_title;
static we_label_obj_t      ff_note;
static we_label_obj_t      ff_status;
static we_label_obj_t      ff_fps;
static we_flash_font_obj_t ff_body;

static uint32_t ff_ticks_ms;
static uint32_t ff_text_timer;
static uint8_t  ff_text_idx;
static uint8_t  ff_font_ready;
static uint32_t ff_fps_timer;
static uint32_t ff_last_frames;
static char     ff_status_buf[40];
static char     ff_fps_buf[16];

static const char *const ff_text_pool[] = {
    "Menu: 系统设置",
    "网络  WiFi\n显示  100%\n时间  12:00",
    "菜单 设置 返回\n确定 语言 显示",
    "一二三四五六七八\n九十中文测试\nHello, WeGui!",
    "确定 (OK)\n返回 (Back)",
};

#define FF_TEXT_POOL_SIZE (sizeof(ff_text_pool) / sizeof(ff_text_pool[0]))
#define FF_TEXT_PERIOD_MS 1500U

static const colour_t ff_color_pool[] = {
    RGB888_CONST(244, 244, 244),
    RGB888_CONST(90, 220, 255),
    RGB888_CONST(255, 196, 80),
    RGB888_CONST(120, 230, 165),
    RGB888_CONST(255, 140, 180),
};
#define FF_COLOR_POOL_SIZE (sizeof(ff_color_pool) / sizeof(ff_color_pool[0]))

/**
 * @brief 初始化外挂 Flash 字体演示场景
 * @param lcd GUI 运行时上下文
 */
void we_flash_font_simple_demo_init(we_lcd_t *lcd)
{
    font_external_handle_t font_handle;

    if (lcd == NULL)
        return;

    ff_ticks_ms    = 0U;
    ff_text_timer  = 0U;
    ff_text_idx    = 0U;
    ff_font_ready  = 0U;
    ff_fps_timer   = 0U;
    ff_last_frames = 0U;
    memset(ff_status_buf, 0, sizeof(ff_status_buf));
    memset(ff_fps_buf, 0, sizeof(ff_fps_buf));

    we_label_obj_init(&ff_title, lcd, 10, 10,
                      "FLASH FONT DEMO", we_font_consolas_18,
                      RGB888TODEV(82, 220, 255), 255);
    we_label_obj_init(&ff_note, lcd, 10, 32,
                      "font2c external", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&ff_fps, lcd, we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    snprintf(ff_status_buf, sizeof(ff_status_buf), "EXT OP 255 TXT %u/%u",
             (unsigned)(ff_text_idx + 1U), (unsigned)FF_TEXT_POOL_SIZE);
    we_label_obj_init(&ff_status, lcd, 10, 54,
                      ff_status_buf, we_font_consolas_18,
                      RGB888TODEV(255, 196, 80), 255);

    font_handle.font = &msyh_16_4bbp_ascii_menu_mix;
    font_handle.blob_addr = MSYH_16_4BBP_ASCII_MENU_MIX_ADDR;

    if (!we_flash_font_face_init(&ff_face, lcd, &font_handle,
                                 ff_glyph_scratch, (uint32_t)sizeof(ff_glyph_scratch)))
    {
        we_label_set_text(&ff_status, "FLASH FONT FACE ERR");
        return;
    }

    if (!we_flash_font_obj_init(&ff_body, &ff_face, 12, 82,
                                ff_text_pool[ff_text_idx],
                                ff_color_pool[ff_text_idx % FF_COLOR_POOL_SIZE], 255U))
    {
        we_label_set_text(&ff_status, "FLASH FONT OBJ ERR");
        memset(&ff_face, 0, sizeof(ff_face));
        return;
    }

    ff_font_ready = 1U;
}

/**
 * @brief 外挂 Flash 字体演示周期更新函数
 * @param lcd GUI 运行时上下文
 * @param ms_tick 距上次调用的毫秒增量
 */
void we_flash_font_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t opacity;

    if (lcd == NULL || ms_tick == 0U)
        return;

    if (ff_font_ready == 0U)
    {
        we_demo_update_fps(lcd, &ff_fps, &ff_fps_timer,
                           &ff_last_frames, ff_fps_buf, ms_tick);
        return;
    }

    ff_ticks_ms += ms_tick;
    ff_text_timer += ms_tick;

    opacity = (uint8_t)(90U + (((uint32_t)(we_sin((int16_t)((ff_ticks_ms * 25U) / 100U) & 0x1FF))
                                + 32768U) * 165U >> 16));
    we_flash_font_obj_set_opacity(&ff_body, opacity);

    if (ff_text_timer >= FF_TEXT_PERIOD_MS)
    {
        ff_text_timer = 0U;
        ff_text_idx = (uint8_t)((ff_text_idx + 1U) % FF_TEXT_POOL_SIZE);
        we_flash_font_obj_set_text(&ff_body, ff_text_pool[ff_text_idx]);
        we_flash_font_obj_set_color(&ff_body, ff_color_pool[ff_text_idx % FF_COLOR_POOL_SIZE]);
    }

    snprintf(ff_status_buf, sizeof(ff_status_buf), "EXT OP %03u TXT %u/%u",
             (unsigned)opacity, (unsigned)(ff_text_idx + 1U), (unsigned)FF_TEXT_POOL_SIZE);
    we_label_set_text(&ff_status, ff_status_buf);

    we_demo_update_fps(lcd, &ff_fps, &ff_fps_timer,
                       &ff_last_frames, ff_fps_buf, ms_tick);
}
