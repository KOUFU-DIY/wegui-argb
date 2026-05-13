#include "demo_common.h"
#include "we_font_text.h"

#include <stdio.h>

#define WE_DEMO_DESIGN_W WE_DEMO_BASE_W
#define WE_DEMO_DESIGN_H WE_DEMO_BASE_H

/**
 * @brief 按目标分辨率执行正数尺寸/坐标缩放。
 * @param value 待缩放值。
 * @param src 设计基准边长。
 * @param dst 当前目标边长。
 * @return 缩放结果；当输入非正或参数无效时返回原值。
 */
static int16_t _we_demo_scale(int16_t value, uint16_t src, uint16_t dst)
{
    int32_t scaled;

    if (value <= 0 || src == 0U || dst == 0U)
    {
        return value;
    }

    scaled = ((int32_t)value * (int32_t)dst + ((int32_t)src / 2)) / (int32_t)src;
    if (scaled <= 0)
    {
        scaled = 1;
    }
    return (int16_t)scaled;
}

/**
 * @brief 按当前屏幕宽度缩放 X 坐标。
 * @param lcd LCD 运行实例。
 * @param base_x 基准坐标（以 280x240 为基准）。
 * @return 缩放后的 X 坐标。
 */
int16_t we_demo_scale_x(const we_lcd_t *lcd, int16_t base_x)
{
    return _we_demo_scale(base_x, WE_DEMO_DESIGN_W, lcd->width);
}

/**
 * @brief 按当前屏幕高度缩放 Y 坐标。
 * @param lcd LCD 运行实例。
 * @param base_y 基准坐标（以 280x240 为基准）。
 * @return 缩放后的 Y 坐标。
 */
int16_t we_demo_scale_y(const we_lcd_t *lcd, int16_t base_y)
{
    return _we_demo_scale(base_y, WE_DEMO_DESIGN_H, lcd->height);
}

/**
 * @brief 按当前屏幕宽度缩放宽度值。
 * @param lcd LCD 运行实例。
 * @param base_w 基准宽度（以 280x240 为基准）。
 * @return 缩放后的宽度。
 */
int16_t we_demo_scale_w(const we_lcd_t *lcd, int16_t base_w)
{
    return _we_demo_scale(base_w, WE_DEMO_DESIGN_W, lcd->width);
}

/**
 * @brief 按当前屏幕高度缩放高度值。
 * @param lcd LCD 运行实例。
 * @param base_h 基准高度（以 280x240 为基准）。
 * @return 缩放后的高度。
 */
int16_t we_demo_scale_h(const we_lcd_t *lcd, int16_t base_h)
{
    return _we_demo_scale(base_h, WE_DEMO_DESIGN_H, lcd->height);
}

/**
 * @brief 计算对象右对齐时的 X 坐标。
 * @param lcd LCD 运行实例。
 * @param right_margin 距右边距（基准分辨率下）。
 * @param obj_w 对象宽度（当前分辨率下）。
 * @return 右对齐后对象左上角 X 坐标。
 */
int16_t we_demo_right_x(const we_lcd_t *lcd, int16_t right_margin, int16_t obj_w)
{
    return (int16_t)(lcd->width - we_demo_scale_x(lcd, right_margin) - obj_w);
}

/**
 * @brief 计算对象底对齐时的 Y 坐标。
 * @param lcd LCD 运行实例。
 * @param bottom_margin 距底边距（基准分辨率下）。
 * @param obj_h 对象高度（当前分辨率下）。
 * @return 底对齐后对象左上角 Y 坐标。
 */
int16_t we_demo_bottom_y(const we_lcd_t *lcd, int16_t bottom_margin, int16_t obj_h)
{
    return (int16_t)(lcd->height - we_demo_scale_y(lcd, bottom_margin) - obj_h);
}

/**
 * @brief 计算 FPS 标签初始右对齐 X 坐标。
 * @param lcd LCD 运行实例。
 * @param text FPS 初始文本。
 * @param font 字体资源指针。
 * @return 右对齐后的 X 坐标。
 */
int16_t we_demo_fps_x(const we_lcd_t *lcd, const char *text, const unsigned char *font)
{
    uint16_t text_w;

    if (lcd == NULL || text == NULL || font == NULL)
    {
        return 0;
    }

    text_w = we_get_text_width(font, text);
    return we_demo_right_x(lcd, 10, (int16_t)text_w);
}

/**
 * @brief 更新并显示 FPS 文本。
 * @param lcd LCD 运行实例。
 * @param fps_label 用于显示 FPS 的标签对象。
 * @param fps_timer FPS 累计计时器（毫秒）。
 * @param last_frames 上次统计时的总渲染帧计数。
 * @param fps_buf FPS 文本缓冲区。
 * @param ms_tick 本次调用累计的毫秒增量。
 * @return 无。
 */
void we_demo_update_fps(we_lcd_t *lcd,
                        we_label_obj_t *fps_label,
                        uint32_t *fps_timer,
                        uint32_t *last_frames,
                        char *fps_buf,
                        uint16_t ms_tick)
{
    uint32_t frames_now;
    uint32_t delta_frames;
    uint32_t fps;
    int16_t fps_x;
    int16_t fps_y;

    if (lcd == NULL || fps_label == NULL || fps_timer == NULL || last_frames == NULL || fps_buf == NULL)
    {
        return;
    }

    *fps_timer += ms_tick;
    if (*fps_timer < 500U)
    {
        return;
    }

    frames_now = lcd->stat_render_frames;
    delta_frames = frames_now - *last_frames;
    fps = (delta_frames * 1000U) / *fps_timer;
    sprintf(fps_buf, "FPS %u", (unsigned)fps);
    we_label_set_text(fps_label, fps_buf);
    fps_x = we_demo_fps_x(lcd, fps_buf, fps_label->font);
    fps_y = fps_label->base.y;
    we_label_obj_set_pos(fps_label, fps_x, fps_y);
    *last_frames = frames_now;
    *fps_timer = 0U;
}
