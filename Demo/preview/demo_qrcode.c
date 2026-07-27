/**
 * @file  demo_qrcode.c
 * @brief 二维码（qrcode）preview demo —— URL 与 WiFi 配网串定时切换（DEMO_ID 108）
 *
 * 中央一个 module_px=4 的二维码（两个内容均为版本 3：29 模块，
 * 含静区 37x37 模块 = 148x148 像素，切换时尺寸不变、位置稳定），
 * tick 每 5 秒在 URL 串与 WiFi 配网串之间切换，顶部说明 label 同步更新，
 * 供录 GIF 时观察"内容变化 -> 重新编码 -> 整码面重绘"。FPS 照常显示。
 */

#include "preview_demos.h"
#include "demo_common.h"
#include "widgets_preview/qrcode/we_widget_qrcode.h"
#include "widgets/label/we_widget_label.h"
#include <string.h>

/* 内容切换周期（毫秒） */
#define QD_SWITCH_MS 5000U

/* 布局（280x240 基准）：v3 含静区 (29+8)*4 = 148px，水平居中 */
#define QD_QR_X 66
#define QD_QR_Y 58
#define QD_QR_MODULE_PX 4U

static we_label_obj_t qd_title;
static we_label_obj_t qd_fps_label;
static we_label_obj_t qd_hint_label; /* 顶部说明：当前编码的内容类型 */
static we_qrcode_obj_t qd_qr;

static uint32_t qd_fps_timer;
static uint32_t qd_last_frames;
static char qd_fps_buf[16];

static uint32_t qd_acc_ms; /* 切换节拍累积器 */
static uint8_t qd_phase;   /* 0=URL 串，1=WiFi 配网串 */

/* 两个演示内容（29/30 字节，均落在 v3-M 容量 42 字节内） */
static const char *const qd_text[2] = {
    "https://github.com/wegui-argb",
    "WIFI:T:WPA;S:demo;P:12345678;;",
};
static const char *const qd_hint[2] = {
    "1/2 URL: github repo",
    "2/2 WIFI: WPA config",
};

/**
 * @brief 初始化 qrcode demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_qrcode_preview_demo_init(we_lcd_t *lcd)
{
    int16_t fps_x = we_demo_fps_x(lcd, "FPS", we_font_consolas_18);

    qd_fps_timer = 0U;
    qd_last_frames = 0U;
    qd_acc_ms = 0U;
    qd_phase = 0U;
    memset(qd_fps_buf, 0, sizeof(qd_fps_buf));

    we_label_obj_init(&qd_title, lcd, 14, 10,
                      "QRCODE", we_font_consolas_18, RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&qd_fps_label, lcd, fps_x, 10,
                      "FPS", we_font_consolas_18, RGB888TODEV(120, 230, 205), 255);

    /* 顶部说明：当前内容类型（切换时同步更新） */
    we_label_obj_init(&qd_hint_label, lcd, 14, 32,
                      qd_hint[0], we_font_consolas_18,
                      RGB888TODEV(112, 184, 255), 255);

    /* 中央二维码：近黑码色 + 近白底色（默认），module_px=4 */
    we_qrcode_obj_init(&qd_qr, lcd, QD_QR_X, QD_QR_Y, QD_QR_MODULE_PX);
    (void)we_qrcode_set_text(&qd_qr, qd_text[0]);
}

/**
 * @brief qrcode demo 周期更新：每 5 秒切换编码内容与说明文本
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_qrcode_preview_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    if (lcd == NULL || ms_tick == 0U)
        return;

    qd_acc_ms += ms_tick;
    if (qd_acc_ms >= QD_SWITCH_MS)
    {
        qd_acc_ms -= QD_SWITCH_MS;
        qd_phase ^= 1U;
        (void)we_qrcode_set_text(&qd_qr, qd_text[qd_phase]);
        we_label_set_text(&qd_hint_label, qd_hint[qd_phase]);
    }

    we_demo_update_fps(lcd, &qd_fps_label, &qd_fps_timer,
                       &qd_last_frames, qd_fps_buf, ms_tick);
}
