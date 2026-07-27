/**
 * @file  demo_showcase.c
 * @brief 全控件汇总演示（仅模拟器）：单画面同时摆放全部 20 个控件。
 *
 * ⚠️ 分辨率要求：本 demo 按 800×480 布局编写。
 *    请在 we_user_config.h 中设置：
 *        #define SCREEN_WIDTH  (800)
 *        #define SCREEN_HEIGHT (480)
 *    （USER_GRAM_NUM = SCREEN_WIDTH*8 会自动随宽度放大，模拟器无压力；
 *      多控件动画下建议把 WE_CFG_DIRTY_MAX_NUM 提到 16 以减少爆满合并。）
 *
 * 控件覆盖清单（20/20）：
 *   A 列：label / btn / toggle / indicator / checkbox / slider / progress /
 *         stepper / dropdown / label_ex
 *   B 列：group（命中转发+子件计数）/ scroll_panel（惯性滚动）/
 *         slideshow（自动翻页吸附）
 *   C 列：img / img_ex（持续旋转）/ arc（数值动画）/ img_flash（外挂图）/
 *         font_flash（外挂中文字库）/ msgbox（按钮弹出）
 *   D 列：chart（实时波形，推流速度由 dropdown 控制）/ indicator×3 轮闪
 */

#include "simple_widget_demos.h"

#include "demo_common.h"
#include "res_images.h"
#include "merged_bin.h"
#include "demo_cjk_16.h"
#include "widgets/btn/we_widget_btn.h"
#include "widgets/toggle/we_widget_toggle.h"
#include "widgets/checkbox/we_widget_checkbox.h"
#include "widgets/indicator/we_widget_indicator.h"
#include "widgets/slider/we_widget_slider.h"
#include "widgets/progress/we_widget_progress.h"
#include "widgets/stepper/we_widget_stepper.h"
#include "widgets/dropdown/we_widget_dropdown.h"
#include "widgets/label_ex/we_widget_label_ex.h"
#include "widgets/group/we_widget_group.h"
#include "widgets/scroll_panel/we_widget_scroll_panel.h"
#include "widgets/slideshow/we_widget_slideshow.h"
#include "widgets/img/we_widget_img.h"
#include "widgets/img_ex/we_widget_img_ex.h"
#include "widgets/arc/we_widget_arc.h"
#include "widgets/img_flash/we_widget_img_flash.h"
#include "widgets/font_flash/we_widget_font_flash.h"
#include "widgets/chart/we_widget_chart.h"
#include "widgets/msgbox/we_widget_msgbox.h"
#include <stdio.h>
#include <string.h>

/* 分辨率不足(800x480)的告警已移到 Simulator/main_sim.c，
 * 仅当 DEMO_ID 选中本 demo(0) 时才提示（main_sim.c 的 #if (DEMO_ID==0)
 * 分支内 #warning），避免编译其他 demo 时误报。 */

/* ---- 顶栏 ---- */
static we_label_obj_t sc_title;
static we_label_obj_t sc_fps;

/* ---- A 列：基础交互 ---- */
static we_label_obj_t     sc_lbl_sample;
static we_btn_obj_t       sc_btn;
static we_toggle_obj_t    sc_toggle;
static we_indicator_obj_t sc_lamp;
static we_indicator_obj_t sc_lamp_click;
static we_checkbox_obj_t  sc_check;
static we_slider_obj_t    sc_slider;
static we_label_obj_t     sc_val_lbl;
static we_progress_obj_t  sc_progress;
static we_stepper_obj_t   sc_stepper;
static we_dropdown_obj_t  sc_speed_dd;
static we_label_ex_obj_t  sc_rot_text;
static we_label_obj_t     sc_status;

/* ---- B 列：容器 ---- */
static we_group_obj_t     sc_panel;
static we_label_obj_t     sc_panel_title;
static we_btn_obj_t       sc_panel_btn_l;
static we_btn_obj_t       sc_panel_btn_r;
static we_label_obj_t     sc_panel_tip;
static we_scroll_panel_obj_t sc_scroll;
static we_label_obj_t     sc_sp_lbl1;
static we_btn_obj_t       sc_sp_btn;
static we_checkbox_obj_t  sc_sp_check;
static we_label_obj_t     sc_sp_lbl2;
static we_label_obj_t     sc_sp_lbl3;
static we_slideshow_obj_t sc_mini;
static we_label_obj_t     sc_mini_p0;
static we_label_obj_t     sc_mini_p1;

/* ---- C 列：媒体 / 弹层 ---- */
static we_img_obj_t       sc_img;
static we_img_ex_obj_t    sc_img_ex;
static we_arc_obj_t       sc_arc;
static we_flash_img_obj_t sc_fimg;
static we_flash_font_face_t  sc_ff_face;
static font_external_handle_t sc_ff_handle;
static we_flash_font_obj_t   sc_ff_text;
static uint8_t            sc_ff_scratch[WE_FLASH_FONT_SCRATCH_MAX];
static we_btn_obj_t       sc_msg_btn;
static we_msgbox_obj_t    sc_msgbox;
static we_label_obj_t     sc_res_note;

/* ---- D 列：数据 ---- */
static we_label_obj_t     sc_chart_lbl;
static we_chart_obj_t     sc_chart;
static int16_t            sc_chart_buf[185];
static we_indicator_obj_t sc_blink[3];
static we_label_obj_t     sc_speed_lbl;

/* 8 个选项 + 限制可见 4 行 → 列表可滚动，展示滚动条及其空闲淡出 */
static const we_dropdown_option_t sc_speed_opts[] = {
    {"PUSH x1", 1, 0},
    {"PUSH x2", 2, 0},
    {"PUSH x3", 3, 0},
    {"PUSH x4", 4, 0},
    {"PUSH x5", 5, 0},
    {"PUSH x6", 6, 0},
    {"PUSH x8", 8, 0},
    {"PUSH x10", 10, 0},
};

/* ---- 运行态 ---- */
static uint32_t sc_ticks_ms;
static uint32_t sc_fps_timer;
static uint32_t sc_last_frames;
static uint16_t sc_push_acc_ms;
static uint16_t sc_blink_acc_ms;
static uint16_t sc_page_acc_ms;
static int16_t  sc_wave_phase;
static int16_t  sc_spin_angle;
static uint8_t  sc_speed_mul;
static uint8_t  sc_blink_idx;
static uint8_t  sc_fx_on;
static uint8_t  sc_ff_ready;
static uint32_t sc_btn_cnt;
static uint32_t sc_panel_cnt;
static uint32_t sc_msg_cnt;
static char     sc_fps_buf[16];
static char     sc_val_buf[8];
static char     sc_btn_buf[16];
static char     sc_tip_buf[20];
static char     sc_speed_buf[16];
static char     sc_status_buf[24];

/**
 * @brief 主按钮回调：点击计数并改写自身文本
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费
 */
static uint8_t _sc_btn_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        sc_btn_cnt++;
        snprintf(sc_btn_buf, sizeof(sc_btn_buf), "BTN x%u", (unsigned)sc_btn_cnt);
        we_btn_set_text((we_btn_obj_t *)obj, sc_btn_buf);
    }
    return 1U;
}

/**
 * @brief group 内左右按钮回调：联动面板提示行
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费
 */
static uint8_t _sc_panel_btn_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        sc_panel_cnt++;
        snprintf(sc_tip_buf, sizeof(sc_tip_buf), "%s x%u",
                 ((we_btn_obj_t *)obj == &sc_panel_btn_l) ? "LEFT" : "RIGHT",
                 (unsigned)sc_panel_cnt);
        we_label_set_text(&sc_panel_tip, sc_tip_buf);
    }
    return 1U;
}

/**
 * @brief scroll_panel 内按钮回调：写到状态行
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费
 */
static uint8_t _sc_sp_btn_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
        we_label_set_text(&sc_status, "scroll btn ok");
    return 1U;
}

/**
 * @brief toggle 状态改变回调：联动 A 列指示灯
 * @param obj 传入：开关对象指针
 * @param checked 传入：当前开关状态
 * @return 无
 */
static void _sc_toggle_changed(void *obj, uint8_t checked)
{
    (void)obj;
    we_indicator_set_state(&sc_lamp, checked);
}

/**
 * @brief FX 勾选改变回调：开关动效组（面板浮动+呼吸、缩放脉动）
 * @param obj 传入：复选框对象指针
 * @param checked 传入：当前勾选状态
 * @return 无
 */
static void _sc_fx_changed(void *obj, uint8_t checked)
{
    (void)obj;
    sc_fx_on = checked;
    if (!checked)
    {
        /* 关闭动效：面板归位、整组恢复不透明（缩放由 tick 回落 256） */
        we_obj_set_pos((we_obj_t *)&sc_panel, 205, 40);
        we_group_set_opacity(&sc_panel, 255U);
    }
}

/**
 * @brief slider 数值改变回调：联动进度条与数值文本
 * @param obj 传入：滑条对象指针
 * @param value 传入：当前数值
 * @return 无
 */
static void _sc_slider_changed(void *obj, uint8_t value)
{
    (void)obj;
    we_progress_set_value(&sc_progress, value);
    snprintf(sc_val_buf, sizeof(sc_val_buf), "%3u", (unsigned)value);
    we_label_set_text(&sc_val_lbl, sc_val_buf);
}

/**
 * @brief dropdown 选中改变回调：调整 D 列波形推流速度
 * @param obj 传入：下拉框对象指针
 * @param selected_idx 传入：选中项索引
 * @param value 传入：选项关联值（速度倍率）
 * @return 无
 */
static void _sc_speed_changed(we_dropdown_obj_t *obj, int16_t selected_idx, int32_t value)
{
    (void)obj;
    (void)selected_idx;
    sc_speed_mul = (uint8_t)value;
    snprintf(sc_speed_buf, sizeof(sc_speed_buf), "PUSH x%ld", (long)value);
    we_label_set_text(&sc_speed_lbl, sc_speed_buf);
}

/**
 * @brief msgbox 确认回调：累计计数写到状态行
 * @param obj 传入：弹窗对象指针
 * @return 无
 */
static void _sc_msg_ok(we_popup_obj_t *obj)
{
    sc_msg_cnt++;
    snprintf(sc_status_buf, sizeof(sc_status_buf), "MSG OK x%u", (unsigned)sc_msg_cnt);
    we_label_set_text(&sc_status, sc_status_buf);
    /* msgbox 契约：按钮回调自行决定是否关闭（便于"校验失败保持弹窗"场景），
     * 常规确认流程需在回调里显式 hide。 */
    we_popup_hide(obj);
}

/**
 * @brief MSGBOX 按钮回调：弹出确认框
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 1 表示事件已消费
 */
static uint8_t _sc_msg_btn_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
        we_popup_show(&sc_msgbox);
    return 1U;
}

/**
 * @brief 初始化汇总 demo（单画面全控件）
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_showcase_simple_demo_init(we_lcd_t *lcd)
{
    uint16_t mp0;
    uint16_t mp1;
    uint8_t i;

    sc_ticks_ms     = 0U;
    sc_fps_timer    = 0U;
    sc_last_frames  = 0U;
    sc_push_acc_ms  = 0U;
    sc_blink_acc_ms = 0U;
    sc_page_acc_ms  = 0U;
    sc_wave_phase   = 0;
    sc_spin_angle   = 0;
    sc_speed_mul    = 1U;
    sc_blink_idx    = 0U;
    sc_ff_ready     = 0U;
    sc_btn_cnt      = 0U;
    sc_panel_cnt    = 0U;
    sc_msg_cnt      = 0U;
    memset(sc_fps_buf, 0, sizeof(sc_fps_buf));
    memset(sc_val_buf, 0, sizeof(sc_val_buf));
    memset(sc_btn_buf, 0, sizeof(sc_btn_buf));
    memset(sc_tip_buf, 0, sizeof(sc_tip_buf));
    memset(sc_speed_buf, 0, sizeof(sc_speed_buf));
    memset(sc_status_buf, 0, sizeof(sc_status_buf));

    /* ---- 顶栏 ---- */
    we_label_obj_init(&sc_title, lcd, 10, 6,
                      "WEGUI  ALL-WIDGETS SHOWCASE", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&sc_fps, lcd, we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 6,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    /* ================= A 列 x=10 ================= */
    we_label_obj_init(&sc_lbl_sample, lcd, 10, 40,
                      "label: hello", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    we_btn_obj_init(&sc_btn, lcd, 10, 64, 130, 34,
                    "BTN x0", we_font_consolas_18, _sc_btn_cb);

    we_toggle_obj_init(&sc_toggle, lcd, 10, 108, 56, 28, NULL);
    we_toggle_set_changed_cb(&sc_toggle, _sc_toggle_changed);
    we_indicator_obj_init(&sc_lamp, lcd, 80, 104, 36, 36);

    /* 可点击指示灯：直接点灯本体翻转（we_indicator_set_clickable），
     * 与左侧"由 toggle 驱动的只读灯"形成对照 */
    we_indicator_obj_init(&sc_lamp_click, lcd, 128, 104, 36, 36);
    we_indicator_set_colors(&sc_lamp_click, RGB888TODEV(255, 196, 80), RGB888TODEV(60, 60, 66));
    we_indicator_set_clickable(&sc_lamp_click, 1U);

    /* FX 总开关：面板浮动+整组呼吸透明度（级联）、img_ex/label_ex 缩放脉动 */
    we_checkbox_obj_init(&sc_check, lcd, 10, 146, 22, "FX", we_font_consolas_18, NULL);
    we_checkbox_set_changed_cb(&sc_check, _sc_fx_changed);
    we_checkbox_set_checked(&sc_check, 1U);
    sc_fx_on = 1U;

    we_slider_obj_init(&sc_slider, lcd, 10, 182, 150, 24,
                       WE_SLIDER_ORIENT_HOR, 0U, 255U, 96U,
                       RGB888TODEV(46, 56, 74), RGB888TODEV(120, 230, 205),
                       RGB888TODEV(236, 241, 248), 255U);
    we_slider_set_changed_cb(&sc_slider, _sc_slider_changed);
    we_label_obj_init(&sc_val_lbl, lcd, 168, 186,
                      " 96", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);

    we_progress_obj_init(&sc_progress, lcd, 10, 214, 180, 14, 96U,
                         RGB888TODEV(46, 56, 74), RGB888TODEV(255, 154, 102), 255U);

    we_stepper_obj_init(&sc_stepper, lcd, 10, 236, 168, 38,
                        we_font_consolas_18, 1U, 0, 100, 5, 25);

    we_dropdown_obj_init(&sc_speed_dd, lcd, 10, 282, 170, 34, we_font_consolas_18);
    we_dropdown_set_options(&sc_speed_dd, sc_speed_opts,
                            (uint16_t)(sizeof(sc_speed_opts) / sizeof(sc_speed_opts[0])));
    we_dropdown_set_max_visible_items(&sc_speed_dd, 4U); /* 8 选项只显 4 行 → 出滚动条 */
    we_dropdown_set_selected(&sc_speed_dd, 0);
    we_dropdown_set_changed_cb(&sc_speed_dd, _sc_speed_changed);

    we_label_ex_obj_init(&sc_rot_text, lcd, 95, 372,
                         "ROTATE", we_font_consolas_18,
                         RGB888TODEV(196, 170, 255), 255);

    we_label_obj_init(&sc_status, lcd, 10, 434,
                      "tap & play", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    /* ================= B 列 x=205 ================= */
    /* group：验证命中转发与整组管理 */
    we_group_obj_init(&sc_panel, lcd, 205, 40, 190, 120, RGB888TODEV(24, 31, 43), 255);
    we_label_obj_init(&sc_panel_title, lcd, 0, 0,
                      "GROUP", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    we_btn_obj_init(&sc_panel_btn_l, lcd, 0, 0, 80, 32,
                    "LEFT", we_font_consolas_18, _sc_panel_btn_cb);
    we_btn_obj_init(&sc_panel_btn_r, lcd, 0, 0, 80, 32,
                    "RIGHT", we_font_consolas_18, _sc_panel_btn_cb);
    we_label_obj_init(&sc_panel_tip, lcd, 0, 0,
                      "hit-forward", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_group_add_child(&sc_panel, (we_obj_t *)&sc_panel_title);
    we_group_add_child(&sc_panel, (we_obj_t *)&sc_panel_btn_l);
    we_group_add_child(&sc_panel, (we_obj_t *)&sc_panel_btn_r);
    we_group_add_child(&sc_panel, (we_obj_t *)&sc_panel_tip);
    we_group_set_child_pos(&sc_panel, (we_obj_t *)&sc_panel_title, 10, 6);
    we_group_set_child_pos(&sc_panel, (we_obj_t *)&sc_panel_btn_l, 10, 36);
    we_group_set_child_pos(&sc_panel, (we_obj_t *)&sc_panel_btn_r, 100, 36);
    we_group_set_child_pos(&sc_panel, (we_obj_t *)&sc_panel_tip, 10, 84);

    /* scroll_panel：内容高于视口，可拖动/惯性 */
    we_scroll_panel_obj_init(&sc_scroll, lcd, 205, 170, 190, 160,
                             RGB888TODEV(20, 26, 38), RGB888TODEV(46, 56, 74),
                             10U, 255U);
    we_scroll_panel_set_scrollbar(&sc_scroll, 1U, 4U);
    we_label_obj_init(&sc_sp_lbl1, lcd, 0, 0,
                      "SCROLL ME", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
    we_btn_obj_init(&sc_sp_btn, lcd, 0, 0, 110, 32,
                    "S-BTN", we_font_consolas_18, _sc_sp_btn_cb);
    we_checkbox_obj_init(&sc_sp_check, lcd, 0, 0, 20, "opt A", we_font_consolas_18, NULL);
    we_label_obj_init(&sc_sp_lbl2, lcd, 0, 0,
                      "inertia +", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&sc_sp_lbl3, lcd, 0, 0,
                      "rebound", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_scroll_panel_add_child(&sc_scroll, (we_obj_t *)&sc_sp_lbl1);
    we_scroll_panel_add_child(&sc_scroll, (we_obj_t *)&sc_sp_btn);
    we_scroll_panel_add_child(&sc_scroll, (we_obj_t *)&sc_sp_check);
    we_scroll_panel_add_child(&sc_scroll, (we_obj_t *)&sc_sp_lbl2);
    we_scroll_panel_add_child(&sc_scroll, (we_obj_t *)&sc_sp_lbl3);
    we_scroll_panel_set_child_pos(&sc_scroll, (we_obj_t *)&sc_sp_lbl1, 12, 8);
    we_scroll_panel_set_child_pos(&sc_scroll, (we_obj_t *)&sc_sp_btn, 12, 40);
    we_scroll_panel_set_child_pos(&sc_scroll, (we_obj_t *)&sc_sp_check, 12, 86);
    we_scroll_panel_set_child_pos(&sc_scroll, (we_obj_t *)&sc_sp_lbl2, 12, 130);
    we_scroll_panel_set_child_pos(&sc_scroll, (we_obj_t *)&sc_sp_lbl3, 12, 188);
    we_scroll_panel_set_content_h(&sc_scroll, 230);

    /* slideshow：迷你两页，自动翻页演示吸附动画 */
    we_slideshow_obj_init(&sc_mini, lcd, 205, 340, 190, 110,
                          RGB888TODEV(24, 31, 43), 255);
    mp0 = we_slideshow_add_page(&sc_mini);
    mp1 = we_slideshow_add_page(&sc_mini);
    we_label_obj_init(&sc_mini_p0, lcd, 0, 0,
                      "SLIDE A", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&sc_mini_p1, lcd, 0, 0,
                      "SLIDE B", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    we_slideshow_add_child(&sc_mini, mp0, (we_obj_t *)&sc_mini_p0);
    we_slideshow_add_child(&sc_mini, mp1, (we_obj_t *)&sc_mini_p1);
    we_slideshow_set_child_pos(&sc_mini, (we_obj_t *)&sc_mini_p0, 52, 44);
    we_slideshow_set_child_pos(&sc_mini, (we_obj_t *)&sc_mini_p1, 52, 44);

    /* ================= C 列 x=405 ================= */
    we_img_obj_init(&sc_img, lcd, 405, 40, demo_sprite, 255U);
    we_img_ex_obj_init(&sc_img_ex, lcd, 540, 80, demo_sprite, 255U);

    /* 注意：arc 的 (cx,cy) 是圆心坐标，外接框为 ±(r+thickness)。
     * 圆心 (443,180) → 包围盒约 (405..481, 142..218)，避开上方 img 与左侧 B 列。 */
    we_arc_obj_init(&sc_arc, lcd, 443, 180,
                    30U, 8U, WE_DEG(135), WE_DEG(405),
                    RGB888TODEV(120, 230, 205), RGB888TODEV(28, 40, 58), 255);
    we_arc_set_value(&sc_arc, 128U);

    /* 外挂 flash 图片（模拟器存储口由 merged_bin 提供）。
     * 注意：flash 资源的宽高来自资源头（此图实测 128×64），布局必须按
     * 真实尺寸摆放——放 (405,330) 占 405..533 × 330..394，
     * 上方 MSGBOX 按钮(止于322)、下方说明行(434)、右侧 D 列(605)均留余量。 */
    (void)we_flash_img_obj_init(&sc_fimg, lcd, 405, 330,
                                bin_addr_table[DEMO_RAW_ID], 255U);

    /* 外挂 flash 中文字库 */
    sc_ff_handle.font = &demo_cjk_16;
    sc_ff_handle.blob_addr = DEMO_CJK_16_ADDR;
    if (we_flash_font_face_init(&sc_ff_face, lcd, &sc_ff_handle,
                                sc_ff_scratch, (uint32_t)sizeof(sc_ff_scratch)))
    {
        if (we_flash_font_obj_init(&sc_ff_text, &sc_ff_face, 405, 240,
                                   "外挂字库正常",
                                   RGB888TODEV(245, 214, 120), 255U))
        {
            sc_ff_ready = 1U;
        }
    }

    we_btn_obj_init(&sc_msg_btn, lcd, 405, 286, 120, 36,
                    "MSGBOX", we_font_consolas_18, _sc_msg_btn_cb);

    we_msgbox_ok_obj_init(&sc_msgbox, lcd, 220, 120, 120,
                          "SHOWCASE", "All 20 widgets alive",
                          "OK",
                          we_font_consolas_18, we_font_consolas_18, we_font_consolas_18,
                          _sc_msg_ok);

    we_label_obj_init(&sc_res_note, lcd, 405, 434,
                      "needs 800x480", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);

    /* ================= D 列 x=605 ================= */
    we_label_obj_init(&sc_chart_lbl, lcd, 605, 40,
                      "LIVE", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_chart_obj_init(&sc_chart, lcd, 605, 66, 185U, 130U,
                      sc_chart_buf, 185U,
                      RGB888TODEV(120, 230, 205), 2U, 255U);

    for (i = 0U; i < 3U; i++)
    {
        we_indicator_obj_init(&sc_blink[i], lcd, (int16_t)(605 + i * 46), 212, 30, 30);
    }
    we_indicator_set_colors(&sc_blink[0], RGB888TODEV(255, 110, 110), RGB888TODEV(60, 60, 66));
    we_indicator_set_colors(&sc_blink[1], RGB888TODEV(120, 230, 205), RGB888TODEV(60, 60, 66));
    we_indicator_set_colors(&sc_blink[2], RGB888TODEV(255, 196, 80), RGB888TODEV(60, 60, 66));

    we_label_obj_init(&sc_speed_lbl, lcd, 605, 256,
                      "PUSH x1", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);
}

/**
 * @brief 汇总 demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_showcase_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint16_t push_interval;
    int32_t s;

    if (lcd == NULL || ms_tick == 0U)
        return;

    sc_ticks_ms += ms_tick;

    /* img_ex 持续旋转（512 分度制），label_ex 反向旋转；
     * FX 开启时叠加缩放脉动（一正一反相） */
    sc_spin_angle = (int16_t)((sc_spin_angle + (int16_t)(ms_tick / 4U) + 1) & 0x1FF);
    {
        uint16_t img_scale = 256U;
        uint16_t txt_scale = 256U;

        if (sc_fx_on)
        {
            uint16_t sph = (uint16_t)((sc_ticks_ms / 6U) & 0x1FFU);

            /* 旋转图片的扫掠半径 = 对角线一半 × 缩放（64×80 → 51px×s），
             * 不是宽高的一半！幅度 ±24（峰值 280/256）把下缘控制在
             * 80+51×280/256 ≈ 136，与下方外挂图保持安全间距。 */
            img_scale = (uint16_t)(256 + (int16_t)(((int32_t)we_sin((int16_t)sph) * 24) >> 15));
            txt_scale = (uint16_t)(256 + (int16_t)(((int32_t)we_sin((int16_t)((sph + 256U) & 0x1FFU)) * 48) >> 15));
        }
        we_img_ex_obj_set_transform(&sc_img_ex, sc_spin_angle, img_scale);
        we_label_ex_set_transform(&sc_rot_text, (int16_t)((512 - sc_spin_angle) & 0x1FF), txt_scale);
    }

    /* FX：group 面板圆形浮动 + 整组呼吸透明度——
     * 同时演示"容器移动子控件跟随（set_pos 级联）"与"透明度级联"，
     * 浮动很慢（±6px / 4 秒），按钮在移动中依然可点。 */
    if (sc_fx_on)
    {
        uint16_t fph = (uint16_t)((sc_ticks_ms / 8U) & 0x1FFU);
        int16_t gx = (int16_t)(205 + (int16_t)(((int32_t)we_sin((int16_t)fph) * 6) >> 15));
        int16_t gy = (int16_t)(40 + (int16_t)(((int32_t)we_sin((int16_t)((fph + 128U) & 0x1FFU)) * 5) >> 15));
        uint8_t opa = (uint8_t)(190 + (int16_t)(((int32_t)we_sin((int16_t)((fph * 2U) & 0x1FFU)) * 60) >> 15));

        we_obj_set_pos((we_obj_t *)&sc_panel, gx, gy);
        we_group_set_opacity(&sc_panel, opa);
    }

    /* 顶部标题色相缓变（始终开启，纯定点三相正弦） */
    {
        uint16_t cph = (uint16_t)((sc_ticks_ms / 20U) & 0x1FFU);
        uint8_t cr = (uint8_t)(170 + (int16_t)(((int32_t)we_sin((int16_t)cph) * 70) >> 15));
        uint8_t cg = (uint8_t)(170 + (int16_t)(((int32_t)we_sin((int16_t)((cph + 170U) & 0x1FFU)) * 70) >> 15));
        uint8_t cb2 = (uint8_t)(190 + (int16_t)(((int32_t)we_sin((int16_t)((cph + 340U) & 0x1FFU)) * 60) >> 15));

        we_label_set_color(&sc_title, RGB888TODEV(cr, cg, cb2));
    }

    /* arc 数值正弦摆动 */
    s = (int32_t)we_sin((int16_t)((sc_ticks_ms / 12U) & 0x1FFU));
    we_arc_set_value(&sc_arc, (uint8_t)(128 + (int16_t)((s * 100) >> 15)));

    /* chart 推流：基准 40ms/样本，倍率由 dropdown 控制 */
    push_interval = (uint16_t)(40U / sc_speed_mul);
    sc_push_acc_ms = (uint16_t)(sc_push_acc_ms + ms_tick);
    while (sc_push_acc_ms >= push_interval)
    {
        sc_push_acc_ms = (uint16_t)(sc_push_acc_ms - push_interval);
        sc_wave_phase = (int16_t)((sc_wave_phase + 9) & 0x1FF);
        s = (int32_t)we_sin(sc_wave_phase) * 48;
        we_chart_push(&sc_chart, (int16_t)(s >> 15));
    }

    /* 三盏指示灯每 400ms 轮转点亮（中央动画引擎驱动过渡） */
    sc_blink_acc_ms = (uint16_t)(sc_blink_acc_ms + ms_tick);
    if (sc_blink_acc_ms >= 400U)
    {
        sc_blink_acc_ms = 0U;
        we_indicator_set_state(&sc_blink[sc_blink_idx], 0U);
        sc_blink_idx = (uint8_t)((sc_blink_idx + 1U) % 3U);
        we_indicator_set_state(&sc_blink[sc_blink_idx], 1U);
    }

    /* 迷你 slideshow 每 2.2s 自动翻页（吸附动画） */
    sc_page_acc_ms = (uint16_t)(sc_page_acc_ms + ms_tick);
    if (sc_page_acc_ms >= 2200U)
    {
        uint16_t next;

        sc_page_acc_ms = 0U;
        next = (uint16_t)((we_slideshow_get_current_page(&sc_mini) + 1U) % 2U);
        we_slideshow_set_page(&sc_mini, next, 1U);
    }

    (void)sc_ff_ready;
    we_demo_update_fps(lcd, &sc_fps, &sc_fps_timer,
                       &sc_last_frames, sc_fps_buf, ms_tick);
}
