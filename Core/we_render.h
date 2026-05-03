#ifndef WE_RENDER_H
#define WE_RENDER_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * WeGui 渲染内核内部接口
 *
 * 这份头文件只给控件层和底层渲染层之间做内部收口使用，不对外暴露成公共 API。
 *
 * 当前先集中图片渲染相关入口。后面如果继续把其它底层渲染能力从 we_gui_driver.c
 * 拆出来，也可以继续放到这里统一管理。
 * -------------------------------------------------------------------------- */

#if (LCD_DEEP == DEEP_RGB565)
/* --------------------------------------------------------------------------
 * RGB565 快速混色公共内联
 *
 * 这里专门给 we_gui_driver.c 和 we_widget_img_ex.c 共用，避免两边各维护一份
 * 完全相同的 RGB565 混色算法。后面如果继续调公式、改阈值、或做性能微调，
 * 只需要改这里一处。
 *
 * 算法步骤：
 * 1. 先处理接近全透和接近全不透明两种快速返回
 * 2. 把 R/B 打包成一组，G 单独一组，用整数做线性混色
 * 3. 最后重新拼回 RGB565
 * -------------------------------------------------------------------------- */
/**
 * @brief RGB565 双色按透明度混合
 * @param fg 传入：前景色（RGB565）
 * @param bg 传入：背景色（RGB565）
 * @param alpha 传入：前景透明度（0~255）
 * @return 混合后的 RGB565 颜色值
 * @note 内含近全透/近全不透明快速路径，并使用 255 分母四舍五入混色
 */
static __inline uint16_t we_blend_rgb565(uint16_t fg, uint16_t bg, uint8_t alpha)
{
    uint32_t ia;
    uint32_t r;
    uint32_t g;
    uint32_t b;
    uint32_t fg_r;
    uint32_t fg_g;
    uint32_t fg_b;
    uint32_t bg_r;
    uint32_t bg_g;
    uint32_t bg_b;

    if (alpha >= 250U)
    {
        return fg;
    }
    if (alpha <= 5U)
    {
        return bg;
    }

    /* 这里改成按 R/G/B 三个通道分别做 255 分母的四舍五入混色。
     * 原先的 packed RGB565 快速公式更省指令，但在文字这种“灰阶字模 alpha
     * 再叠加全局 opacity”的场景里，低透明度时更容易出现偏色和台阶感。 */
    ia = 255U - alpha;

    fg_r = (fg >> 11) & 0x1FU;
    fg_g = (fg >> 5) & 0x3FU;
    fg_b = fg & 0x1FU;
    bg_r = (bg >> 11) & 0x1FU;
    bg_g = (bg >> 5) & 0x3FU;
    bg_b = bg & 0x1FU;

    r = (fg_r * alpha + bg_r * ia + 127U) / 255U;
    g = (fg_g * alpha + bg_g * ia + 127U) / 255U;
    b = (fg_b * alpha + bg_b * ia + 127U) / 255U;

    return (uint16_t)((r << 11) | (g << 5) | b);
}
#endif

/* --------------------------------------------------------------------------
 * 通用双色混合，返回混合后的 colour_t，RGB565 和 RGB888 均适用。
 *
 * 与 we_store_blended_color (we_gui_driver.c) 的区别：
 * 1. 这里返回结果值，调用方自行决定如何写入目标
 * 2. 适合"先算出混色结果、再按需写入"的场景（如 img_ex 的逐像素内层循环）
 * 3. 统一使用 255 分母，与 we_blend_rgb565 精度一致
 * -------------------------------------------------------------------------- */
/**
 * @brief 通用颜色混合（RGB565/RGB888）
 * @param fg 传入：前景色
 * @param bg 传入：背景色
 * @param alpha 传入：前景透明度（0~255）
 * @return 混合后的颜色（colour_t）
 * @note 返回值由调用方决定写入方式，适合先混色再按需落盘的内层像素流程
 */
static __inline colour_t we_colour_blend(colour_t fg, colour_t bg, uint8_t alpha)
{
    colour_t out;

    if (alpha >= 250U)
        return fg;
    if (alpha <= 5U)
        return bg;

#if (LCD_DEEP == DEEP_RGB565)
    out.dat16 = we_blend_rgb565(fg.dat16, bg.dat16, alpha);
#elif (LCD_DEEP == DEEP_RGB888)
    {
        uint32_t ia = 255U - alpha;
        out.rgb.r = (uint8_t)(((uint32_t)fg.rgb.r * alpha + (uint32_t)bg.rgb.r * ia + 127U) / 255U);
        out.rgb.g = (uint8_t)(((uint32_t)fg.rgb.g * alpha + (uint32_t)bg.rgb.g * ia + 127U) / 255U);
        out.rgb.b = (uint8_t)(((uint32_t)fg.rgb.b * alpha + (uint32_t)bg.rgb.b * ia + 127U) / 255U);
    }
#endif
    return out;
}

/* --------------------------------------------------------------------------
 * 颜色写入 / 混色辅助内联函数
 *
 * 定义在这里供多个 .c 文件共用（we_gui_driver.c / we_widget_img_flash.c），
 * 避免在各 .c 里重复定义相同的 static 版本。
 * -------------------------------------------------------------------------- */
/**
 * @brief 将颜色直接写入目标像素
 * @param dst 传入/传出：目标像素地址
 * @param color 传入：要写入的颜色
 * @return 无
 */
static __inline void we_store_color(colour_t *dst, colour_t color)
{
#if (LCD_DEEP == DEEP_RGB565)
    dst->dat16 = color.dat16;
#elif (LCD_DEEP == DEEP_RGB888)
    dst->rgb.r = color.rgb.r;
    dst->rgb.g = color.rgb.g;
    dst->rgb.b = color.rgb.b;
#endif
}

/**
 * @brief 以前景色按透明度混合写入目标像素
 * @param dst 传入/传出：目标像素地址（作为背景色输入并被更新）
 * @param fg 传入：前景色
 * @param alpha 传入：前景透明度（0~255）
 * @return 无
 * @note alpha 近似全透/全不透明时走快速路径
 */
static __inline void we_store_blended_color(colour_t *dst, colour_t fg, uint8_t alpha)
{
    if (alpha >= 250U)
    {
        we_store_color(dst, fg);
        return;
    }
    if (alpha <= 5U)
    {
        return;
    }
    *dst = we_colour_blend(fg, *dst, alpha);
}

#define WE_MASK_QUADRANT_LT 0U
#define WE_MASK_QUADRANT_RT 1U
#define WE_MASK_QUADRANT_LB 2U
#define WE_MASK_QUADRANT_RB 3U

/**
 * @brief 绘制解析抗锯齿的 1/4 圆角填充
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：外接正方形左上角 X 坐标
 * @param y 传入：外接正方形左上角 Y 坐标
 * @param radius 传入：圆角半径
 * @param color 传入：填充颜色
 * @param opacity 传入：整体透明度（0~255）
 * @param quadrant 传入：象限标识（WE_MASK_QUADRANT_LT/RT/LB/RB）
 * @return 无
 */
void we_draw_quarter_circle_analytic(we_lcd_t *p_lcd, int16_t x, int16_t y,
                                     uint16_t radius, colour_t color, uint8_t opacity,
                                     uint8_t quadrant);

/**
 * @brief 绘制解析抗锯齿胶囊形填充
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：外接矩形左上角 X 坐标
 * @param y 传入：外接矩形左上角 Y 坐标
 * @param w 传入：外接矩形宽度
 * @param h 传入：外接矩形高度
 * @param color 传入：填充颜色
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 */
void we_draw_capsule_analytic(we_lcd_t *p_lcd, int16_t x, int16_t y,
                              uint16_t w, uint16_t h, colour_t color, uint8_t opacity);

/**
 * @brief 绘制解析抗锯齿圆角矩形填充
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：外接矩形左上角 X 坐标
 * @param y 传入：外接矩形左上角 Y 坐标
 * @param w 传入：外接矩形宽度
 * @param h 传入：外接矩形高度
 * @param radius 传入：圆角半径
 * @param color 传入：填充颜色
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 */
void we_draw_round_rect_analytic_fill(we_lcd_t *p_lcd, int16_t x, int16_t y,
                                      uint16_t w, uint16_t h, uint16_t radius,
                                      colour_t color, uint8_t opacity);

/**
 * @brief 将 RGB565 像素值转换为当前色深颜色类型
 * @param pixel 传入：RGB565 像素值
 * @return 转换后的 colour_t 颜色
 * @note 在 RGB565 模式下直接赋值；RGB888 模式下按位扩展到 8 位通道
 */
static __inline colour_t we_color_from_rgb565(uint16_t pixel)
{
    colour_t out;
#if (LCD_DEEP == DEEP_RGB565)
    out.dat16 = pixel;
#elif (LCD_DEEP == DEEP_RGB888)
    out.rgb.r = (uint8_t)(((pixel >> 11) & 0x1FU) << 3);
    out.rgb.g = (uint8_t)(((pixel >> 5) & 0x3FU) << 2);
    out.rgb.b = (uint8_t)((pixel & 0x1FU) << 3);
#endif
    return out;
}

/**
 * @brief 渲染 RGB565 原始图片到屏幕
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：图片数据指针（RGB565 格式）
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 */
void we_img_render_rgb565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity);

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
/**
 * @brief 渲染 indexed QOI（RGB565 调色板）图片到屏幕
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：indexed QOI 图片数据指针
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 */
void we_img_render_indexed_qoi_rgb565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity);

/**
 * @brief 渲染 indexed QOI（ARGB8565 调色板）图片到屏幕
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：indexed QOI 图片数据指针
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 */
void we_img_render_indexed_qoi_argb8565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity);
#endif

#endif
