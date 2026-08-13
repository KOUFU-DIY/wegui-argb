/**
 * @file  sim_demo_registry.c
 * @brief demo 运行时注册表 —— 编号与四目标 DEMO_ID 一致（0=showcase 需把
 *        we_user_config.h 分辨率调到 800x480；117/120 为毕业空洞；
 *        113 ime_pinyin 无周期 tick）。
 */

#include "sim_demo_registry.h"
#include "simple_widget_demos.h"
#include "preview_demos.h"
#include <stddef.h>

static const sim_demo_entry_t k_demos[] = {
    /* ---- 0 = showcase（全控件汇总，按 800x480 布局编写） ---- */
    {0, "showcase (needs 800x480)", we_showcase_simple_demo_init, we_showcase_simple_demo_tick},

    /* ---- 稳定区 1..33 ---- */
    {1, "label", we_label_simple_demo_init, we_label_simple_demo_tick},
    {2, "btn", we_btn_simple_demo_init, we_btn_simple_demo_tick},
    {3, "img", we_img_simple_demo_init, we_img_simple_demo_tick},
    {4, "img_ex", we_img_ex_simple_demo_init, we_img_ex_simple_demo_tick},
    {5, "arc", we_arc_simple_demo_init, we_arc_simple_demo_tick},
    {6, "group", we_group_simple_demo_init, we_group_simple_demo_tick},
    {7, "slideshow", we_slideshow_simple_demo_init, we_slideshow_simple_demo_tick},
    {8, "concentric_arc", we_concentric_arc_simple_demo_init, we_concentric_arc_simple_demo_tick},
    {9, "checkbox", we_checkbox_simple_demo_init, we_checkbox_simple_demo_tick},
    {10, "label_ex", we_label_ex_simple_demo_init, we_label_ex_simple_demo_tick},
    {11, "chart", we_chart_simple_demo_init, we_chart_simple_demo_tick},
    {12, "toggle", we_toggle_simple_demo_init, we_toggle_simple_demo_tick},
    {13, "progress", we_progress_simple_demo_init, we_progress_simple_demo_tick},
    {14, "msgbox", we_msgbox_simple_demo_init, we_msgbox_simple_demo_tick},
    {15, "flash_img", we_flash_img_simple_demo_init, we_flash_img_simple_demo_tick},
    {16, "flash_font", we_flash_font_simple_demo_init, we_flash_font_simple_demo_tick},
    {17, "slider", we_slider_simple_demo_init, we_slider_simple_demo_tick},
    {18, "scroll_panel", we_scroll_panel_simple_demo_init, we_scroll_panel_simple_demo_tick},
    {19, "dropdown", we_dropdown_simple_demo_init, we_dropdown_simple_demo_tick},
    {20, "stepper", we_stepper_simple_demo_init, we_stepper_simple_demo_tick},
    {21, "indicator", we_indicator_simple_demo_init, we_indicator_simple_demo_tick},
    {22, "line", we_line_simple_demo_init, we_line_simple_demo_tick},
    {23, "box", we_box_simple_demo_init, we_box_simple_demo_tick},
    {24, "gauge", we_gauge_simple_demo_init, we_gauge_simple_demo_tick},
    {25, "list", we_list_simple_demo_init, we_list_simple_demo_tick},
    {26, "roller", we_roller_simple_demo_init, we_roller_simple_demo_tick},
    {27, "marquee", we_marquee_simple_demo_init, we_marquee_simple_demo_tick},
    {28, "toast", we_toast_simple_demo_init, we_toast_simple_demo_tick},
    {29, "focus", we_focus_simple_demo_init, we_focus_simple_demo_tick},
    {30, "focus2", we_focus2_simple_demo_init, we_focus2_simple_demo_tick},
    {31, "img_alpha", we_img_alpha_simple_demo_init, we_img_alpha_simple_demo_tick},
    {32, "imgbtn", we_imgbtn_simple_demo_init, we_imgbtn_simple_demo_tick},
    {33, "segdisp", we_segdisp_simple_demo_init, we_segdisp_simple_demo_tick},

    /* ---- preview 孵化区 101..126（117/120 已毕业留洞） ---- */
    {101, "keyboard", we_keyboard_preview_demo_init, we_keyboard_preview_demo_tick},
    {102, "textarea", we_textarea_preview_demo_init, we_textarea_preview_demo_tick},
    {103, "menu", we_menu_preview_demo_init, we_menu_preview_demo_tick},
    {104, "spectrum", we_spectrum_preview_demo_init, we_spectrum_preview_demo_tick},
    {105, "logview", we_logview_preview_demo_init, we_logview_preview_demo_tick},
    {106, "calendar", we_calendar_preview_demo_init, we_calendar_preview_demo_tick},
    {107, "table", we_table_preview_demo_init, we_table_preview_demo_tick},
    {108, "qrcode", we_qrcode_preview_demo_init, we_qrcode_preview_demo_tick},
    {109, "chart_bar", we_chart_bar_preview_demo_init, we_chart_bar_preview_demo_tick},
    {110, "scale", we_scale_preview_demo_init, we_scale_preview_demo_tick},
    {111, "joystick", we_joystick_preview_demo_init, we_joystick_preview_demo_tick},
    {112, "statusbar", we_statusbar_preview_demo_init, we_statusbar_preview_demo_tick},
    {113, "ime_pinyin", we_ime_pinyin_preview_demo_init, NULL},
    {114, "spinner", we_spinner_preview_demo_init, we_spinner_preview_demo_tick},
    {115, "btnmatrix", we_btnmatrix_preview_demo_init, we_btnmatrix_preview_demo_tick},
    {116, "radio", we_radio_preview_demo_init, we_radio_preview_demo_tick},
    {118, "knob", we_knob_preview_demo_init, we_knob_preview_demo_tick},
    {119, "colorwheel", we_colorwheel_preview_demo_init, we_colorwheel_preview_demo_tick},
    {121, "animimg", we_animimg_preview_demo_init, we_animimg_preview_demo_tick},
    {122, "canvas", we_canvas_preview_demo_init, we_canvas_preview_demo_tick},
    {123, "tabview", we_tabview_preview_demo_init, we_tabview_preview_demo_tick},
    {124, "hold_btn", we_hold_btn_preview_demo_init, we_hold_btn_preview_demo_tick},
    {125, "mask_group", we_mask_group_preview_demo_init, we_mask_group_preview_demo_tick},
    {126, "mlabel", we_mlabel_preview_demo_init, we_mlabel_preview_demo_tick},
};

const sim_demo_entry_t *sim_demo_find(int id)
{
    size_t i;

    for (i = 0; i < sizeof(k_demos) / sizeof(k_demos[0]); i++)
    {
        if (k_demos[i].id == id)
            return &k_demos[i];
    }
    return NULL;
}

const sim_demo_entry_t *sim_demo_table(int *count)
{
    if (count != NULL)
        *count = (int)(sizeof(k_demos) / sizeof(k_demos[0]));
    return k_demos;
}
