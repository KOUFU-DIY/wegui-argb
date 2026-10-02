#ifndef WE_INTERNAL_H
#define WE_INTERNAL_H

/* --------------------------------------------------------------------------
 * 内核各模块之间的内部接口：只给 Core 根目录下的内核 .c 使用，
 * 控件、应用与端口不要包含。
 * -------------------------------------------------------------------------- */

#include "wegui.h"
#include "we_dirty.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 将当前 PFB 的一块区域推送到底层显示端口
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x 传入：目标区域左上角 X 坐标
     * @param y 传入：目标区域左上角 Y 坐标
     * @param w 传入：区域宽度
     * @param h 传入：区域高度
     * @return 无
     */
    void we_push_pfb(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h);
    /**
     * @brief 清空当前 PFB 显存内容
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @return 无
     */
    void we_clear_gram(we_lcd_t *p_lcd);
    /**
     * @brief 用纯色填充整个 PFB
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param c 传入：填充颜色
     * @return 无
     */
    void we_fill_gram(we_lcd_t *p_lcd, colour_t c);
    /* we_draw_alpha_mask 的内核版：opacity 已由调用方乘入容器级联 */
    void _we_draw_alpha_mask_raw(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h,
                                 const uint8_t *src_data, uint32_t row_stride, uint8_t bpp,
                                 colour_t fg_color, uint8_t opacity);

    /* 主循环各阶段（we_gui_run 按此顺序调用） */
    void _we_input_poll(we_lcd_t *p_lcd);
    void _we_tick_run_timers(we_lcd_t *p_lcd, uint16_t elapsed_ms);
    void _we_tick_run_anims(we_lcd_t *p_lcd, uint16_t elapsed_ms);
    uint32_t _we_tick_next_timer(const we_lcd_t *p_lcd);
    void _we_refresh_run(we_lcd_t *p_lcd);

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 焦点模块对外的内核钩子 */
    void _we_focus_process_keys(we_lcd_t *p_lcd);
    void _we_focus_cursor_invalidate(we_obj_t *obj);
    uint8_t _we_focus_try_set(we_lcd_t *lcd, we_obj_t *obj);
    void _we_focus_draw_cursor(we_lcd_t *p_lcd);
#endif

#ifdef __cplusplus
}
#endif

#endif /* WE_INTERNAL_H */
