#include "simple_widget_demos.h"

#include "demo_common.h"
#include "widgets/slideshow/we_widget_slideshow.h"
#include "widgets/btn/we_widget_btn.h"
#include "widgets/arc/we_widget_arc.h"
#include <stdio.h>
#include <string.h>

static we_label_obj_t slideshow_title;
static we_label_obj_t slideshow_note;
static we_label_obj_t slideshow_fps;
static we_label_obj_t slideshow_page;
static we_label_obj_t slideshow_dot[3];

static we_slideshow_obj_t slideshow_viewport;

static we_label_obj_t slideshow_p1_label;
static we_btn_obj_t   slideshow_p1_prev;
static we_btn_obj_t   slideshow_p1_next;
static we_label_obj_t slideshow_p2_label;
static we_arc_obj_t   slideshow_p2_arc;
static we_label_obj_t slideshow_p3_label;
static we_arc_obj_t   slideshow_p3_arc;

static uint32_t slideshow_ticks_ms;
static uint32_t slideshow_auto_page_timer;
static uint32_t slideshow_fps_timer;
static uint32_t slideshow_last_frames;
static uint16_t slideshow_current_page;
static char     slideshow_fps_buf[16];
static char     slideshow_page_buf[20];

#define SLIDESHOW_AUTO_PAGE_MS 2800U

/**
 * @brief 刷新页码文本与底部页点高亮
 * @return 无
 */
static void _slideshow_update_page(void)
{
    uint8_t i;

    slideshow_current_page = we_slideshow_get_current_page(&slideshow_viewport);
    snprintf(slideshow_page_buf, sizeof(slideshow_page_buf), "PAGE %u/3", (unsigned)(slideshow_current_page + 1U));
    we_label_set_text(&slideshow_page, slideshow_page_buf);

    for (i = 0U; i < 3U; i++)
    {
        we_label_set_color(&slideshow_dot[i], (i == slideshow_current_page) ? RGB888TODEV(245, 214, 120)
                                                                             : RGB888TODEV(55, 68, 90));
    }
}

/**
 * @brief 处理上一页按钮点击事件
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 0 表示事件未强制截断后续处理
 */
static uint8_t _slideshow_prev_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        if (slideshow_current_page == 0U)
            slideshow_current_page = 2U;
        else
            slideshow_current_page--;
        we_slideshow_set_page(&slideshow_viewport, slideshow_current_page, 1U);
        slideshow_auto_page_timer = 0U;
        _slideshow_update_page();
    }
    return 0U;
}

/**
 * @brief 处理下一页按钮点击事件
 * @param obj 传入：按钮对象指针
 * @param event 传入：事件类型
 * @param data 传入：输入设备数据
 * @return 0 表示事件未强制截断后续处理
 */
static uint8_t _slideshow_next_cb(void *obj, we_event_t event, we_indev_data_t *data)
{
    (void)obj;
    (void)data;
    if (event == WE_EVENT_CLICKED)
    {
        slideshow_current_page = (uint16_t)((slideshow_current_page + 1U) % 3U);
        we_slideshow_set_page(&slideshow_viewport, slideshow_current_page, 1U);
        slideshow_auto_page_timer = 0U;
        _slideshow_update_page();
    }
    return 0U;
}

/**
 * @brief 初始化 slideshow demo
 * @param lcd 传入：GUI 屏幕上下文指针
 * @return 无
 */
void we_slideshow_simple_demo_init(we_lcd_t *lcd)
{
    int16_t view_x   = 10;
    int16_t view_y   = 78;
    int16_t view_w   = 260;
    int16_t view_h   = 126;
    int16_t lbl_top  = 10;
    int16_t btn_w    = 88;
    int16_t btn_h    = 38;
    int16_t btn_gap  = 28;
    int16_t btn_y    = 44;
    int16_t arc_r    = 34;
    int16_t arc_th   = 8;
    int16_t arc_diam = (int16_t)(2 * (arc_r + arc_th));
    int16_t arc_x    = (int16_t)((view_w - arc_diam) / 2);
    int16_t arc_y    = 37;
    int16_t dot_x0   = 124;
    uint8_t i;
    uint16_t page0;
    uint16_t page1;
    uint16_t page2;

    slideshow_ticks_ms = 0U;
    slideshow_auto_page_timer = 0U;
    slideshow_fps_timer = 0U;
    slideshow_last_frames = 0U;
    slideshow_current_page = 0U;
    memset(slideshow_fps_buf, 0, sizeof(slideshow_fps_buf));
    memset(slideshow_page_buf, 0, sizeof(slideshow_page_buf));

    we_label_obj_init(&slideshow_title, lcd, 10, 10,
                      "SLIDESHOW DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    we_label_obj_init(&slideshow_note, lcd, 10, 32,
                      "page-local coords", we_font_consolas_18,
                      RGB888TODEV(138, 152, 170), 255);
    we_label_obj_init(&slideshow_fps, lcd, we_demo_fps_x(lcd, "FPS", we_font_consolas_18), 10,
                      "FPS", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_label_obj_init(&slideshow_page, lcd, 10, 54,
                      "PAGE 1/3", we_font_consolas_18,
                      RGB888TODEV(245, 214, 120), 255);

    for (i = 0U; i < 3U; i++)
    {
        we_label_obj_init(&slideshow_dot[i], lcd, (int16_t)(dot_x0 + i * 16), 210,
                          "*", we_font_consolas_18,
                          (i == 0U) ? RGB888TODEV(245, 214, 120) : RGB888TODEV(55, 68, 90),
                          255);
    }

    we_slideshow_obj_init(&slideshow_viewport, lcd, view_x, view_y, view_w, view_h,
                          RGB888TODEV(24, 31, 43), 255);
    page0 = we_slideshow_add_page(&slideshow_viewport);
    page1 = we_slideshow_add_page(&slideshow_viewport);
    page2 = we_slideshow_add_page(&slideshow_viewport);

    we_label_obj_init(&slideshow_p1_label, lcd, 0, 0,
                      "< DRAG  &  SNAP >", we_font_consolas_18,
                      RGB888TODEV(255, 154, 102), 255);
    we_btn_obj_init(&slideshow_p1_prev, lcd, 0, 0, btn_w, btn_h,
                    "< PREV", we_font_consolas_18, _slideshow_prev_cb);
    we_btn_obj_init(&slideshow_p1_next, lcd, 0, 0, btn_w, btn_h,
                    "NEXT >", we_font_consolas_18, _slideshow_next_cb);
    we_slideshow_add_child(&slideshow_viewport, page0, (we_obj_t *)&slideshow_p1_label);
    we_slideshow_add_child(&slideshow_viewport, page0, (we_obj_t *)&slideshow_p1_prev);
    we_slideshow_add_child(&slideshow_viewport, page0, (we_obj_t *)&slideshow_p1_next);
    we_slideshow_set_child_pos(&slideshow_viewport, (we_obj_t *)&slideshow_p1_label, 10, lbl_top);
    we_slideshow_set_child_pos(&slideshow_viewport, (we_obj_t *)&slideshow_p1_prev, btn_gap, btn_y);
    we_slideshow_set_child_pos(&slideshow_viewport, (we_obj_t *)&slideshow_p1_next, (int16_t)(btn_gap * 2 + btn_w), btn_y);

    we_label_obj_init(&slideshow_p2_label, lcd, 0, 0,
                      "AUTO  TIMER", we_font_consolas_18,
                      RGB888TODEV(120, 230, 205), 255);
    we_arc_obj_init(&slideshow_p2_arc, lcd, 0, 0,
                    (uint16_t)arc_r, (uint16_t)arc_th,
                    WE_DEG(135), WE_DEG(405),
                    RGB888TODEV(120, 230, 205), RGB888TODEV(28, 40, 58), 255);
    we_arc_set_value(&slideshow_p2_arc, 255U);
    we_slideshow_add_child(&slideshow_viewport, page1, (we_obj_t *)&slideshow_p2_label);
    we_slideshow_add_child(&slideshow_viewport, page1, (we_obj_t *)&slideshow_p2_arc);
    we_slideshow_set_child_pos(&slideshow_viewport, (we_obj_t *)&slideshow_p2_label, 10, lbl_top);
    we_slideshow_set_child_pos(&slideshow_viewport, (we_obj_t *)&slideshow_p2_arc, arc_x, arc_y);

    we_label_obj_init(&slideshow_p3_label, lcd, 0, 0,
                      "LIVE  DATA", we_font_consolas_18,
                      RGB888TODEV(196, 170, 255), 255);
    we_arc_obj_init(&slideshow_p3_arc, lcd, 0, 0,
                    (uint16_t)arc_r, (uint16_t)arc_th,
                    WE_DEG(150), WE_DEG(390),
                    RGB888TODEV(196, 170, 255), RGB888TODEV(28, 40, 58), 255);
    we_arc_set_value(&slideshow_p3_arc, 128U);
    we_slideshow_add_child(&slideshow_viewport, page2, (we_obj_t *)&slideshow_p3_label);
    we_slideshow_add_child(&slideshow_viewport, page2, (we_obj_t *)&slideshow_p3_arc);
    we_slideshow_set_child_pos(&slideshow_viewport, (we_obj_t *)&slideshow_p3_label, 10, lbl_top);
    we_slideshow_set_child_pos(&slideshow_viewport, (we_obj_t *)&slideshow_p3_arc, arc_x, arc_y);

    _slideshow_update_page();
}

/**
 * @brief slideshow demo 周期更新
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param ms_tick 传入：本轮累计毫秒数
 * @return 无
 */
void we_slideshow_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick)
{
    uint8_t arc2_val;
    uint8_t arc3_val;
    int32_t s;

    if (lcd == NULL || ms_tick == 0U)
        return;

    slideshow_ticks_ms += ms_tick;
    slideshow_auto_page_timer += ms_tick;

    if (slideshow_auto_page_timer >= SLIDESHOW_AUTO_PAGE_MS)
    {
        slideshow_auto_page_timer = 0U;
        slideshow_current_page = (uint16_t)((slideshow_current_page + 1U) % 3U);
        we_slideshow_set_page(&slideshow_viewport, slideshow_current_page, 1U);
        _slideshow_update_page();
    }

    arc2_val = (uint8_t)(255U - (uint8_t)((slideshow_auto_page_timer * 255U) / SLIDESHOW_AUTO_PAGE_MS));
    we_arc_set_value(&slideshow_p2_arc, arc2_val);

    s = (int32_t)we_sin((int16_t)((slideshow_ticks_ms / 16U) & 0x1FFU));
    arc3_val = (uint8_t)(128 + (int8_t)((s * 100) >> 15));
    we_arc_set_value(&slideshow_p3_arc, arc3_val);

    _slideshow_update_page();
    we_demo_update_fps(lcd, &slideshow_fps, &slideshow_fps_timer,
                       &slideshow_last_frames, slideshow_fps_buf, ms_tick);
}
