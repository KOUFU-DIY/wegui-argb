#ifndef WE_DRAW_H
#define WE_DRAW_H

/* --------------------------------------------------------------------------
 * 绘图原语：PFB 裁剪窗、混色与颜色写入、矩形/像素/线段、
 * 四分之一圆与圆角矩形覆盖率蒙版、圆角矩形填充。
 * 所有原语只写当前 PFB 切片窗口内的像素，入口处消费一次容器透明度级联。
 * -------------------------------------------------------------------------- */

#include "we_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * PFB 裁剪窗三件套（save / clip_to / restore）
 *
 * 控件把 PFB 窗口临时收窄到某个矩形（子控件区、单元格、滑入可见区……），
 * 画完恢复。clip_to 始终以 save 保存的"原始现场"为基准计算（非嵌套，
 * 同一现场可反复收窄），窗口无交集返回 0，本次不必绘制。
 * lcd->clip 与 pfb_gram 的偏移一体维护——该算术写错即越界写 PFB，
 * 务必经由本组接口，不要在控件里手抄。内核调用 draw_cb 前已把窗口
 * 收窄到对象包围盒，控件只在需要更小的区域时才用它。
 * -------------------------------------------------------------------------- */
typedef struct
{
    we_rect_t clip; /* 窗口现场 */
    colour_t *gram; /* pfb_gram 现场 */
} we_pfb_clip_t;

/**
 * @brief 保存当前 PFB 裁剪现场
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param sv 传出：现场保存结构
 * @return 无
 */
static __inline void we_pfb_clip_save(const we_lcd_t *lcd, we_pfb_clip_t *sv)
{
    sv->clip = lcd->clip;
    sv->gram = lcd->pfb_gram;
}

/**
 * @brief 从原始现场出发把 PFB 窗口收窄到指定矩形
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param sv 传入：save 保存的原始现场（收窄基准，始终相对它计算）
 * @param x0 传入：目标矩形左缘（屏幕绝对坐标）
 * @param y0 传入：目标矩形上缘
 * @param x1 传入：目标矩形右缘（含端点）
 * @param y1 传入：目标矩形下缘（含端点）
 * @return 1 收窄后窗口非空；0 无交集（窗口未改动，本次不必绘制）
 */
uint8_t we_pfb_clip_to(we_lcd_t *lcd, const we_pfb_clip_t *sv,
                       int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/**
 * @brief 恢复 PFB 裁剪现场
 * @param lcd 传入：GUI 屏幕上下文指针
 * @param sv 传入：save 保存的现场
 * @return 无
 */
static __inline void we_pfb_clip_restore(we_lcd_t *lcd, const we_pfb_clip_t *sv)
{
    lcd->clip = sv->clip;
    lcd->pfb_gram = sv->gram;
}

#if (LCD_DEEP == DEEP_RGB565)
/* --------------------------------------------------------------------------
 * RGB565 快速混色公共内联
 *
 * 这里给内核混色与 we_widget_img_ex.c 共用，避免两边各维护一份
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
static __inline uint16_t _we_blend_rgb565_core(uint16_t fg, uint16_t bg, uint8_t alpha)
{
    /* 核心公式（无快速路径判定）：仅供带判定入口内部复用，
     * 调用方必须已确认 alpha 落在 6..249 中段。
     * 输入 alpha 仍是 0~255，先无成本扩展到 0~256：
     *   a256 = alpha + (alpha >> 7)  →  0→0, 255→256, 中间近似线性
     * 这样分母变成 256，三个通道的 /255 全部替换为 >>8，
     * 在没有硬件除法的 Cortex-M0（如 STM32F030）上能省掉大量软除法周期。
     * 误差 < 0.4%，RGB565 5/6 位深度下视觉无差异。 */
    uint32_t a = (uint32_t)alpha + (uint32_t)(alpha >> 7);
    uint32_t ia = 256U - a;
    uint32_t fg_r = (fg >> 11) & 0x1FU;
    uint32_t fg_g = (fg >> 5) & 0x3FU;
    uint32_t fg_b = fg & 0x1FU;
    uint32_t bg_r = (bg >> 11) & 0x1FU;
    uint32_t bg_g = (bg >> 5) & 0x3FU;
    uint32_t bg_b = bg & 0x1FU;
    uint32_t r = (fg_r * a + bg_r * ia) >> 8;
    uint32_t g = (fg_g * a + bg_g * ia) >> 8;
    uint32_t b = (fg_b * a + bg_b * ia) >> 8;

    return (uint16_t)((r << 11) | (g << 5) | b);
}

static __inline uint16_t we_blend_rgb565(uint16_t fg, uint16_t bg, uint8_t alpha)
{
    if (alpha >= 250U)
    {
        return fg;
    }
    if (alpha <= 5U)
    {
        return bg;
    }
    return _we_blend_rgb565_core(fg, bg, alpha);
}
#endif

/**
 * @brief 核心混色（无快速路径判定，按当前色深）
 * @param fg 传入：前景色
 * @param bg 传入：背景色
 * @param alpha 传入：前景透明度（调用方保证已落在 6..249 中段）
 * @return 混合后的颜色
 * @note 仅供 we_colour_blend / we_store_blended_color 等带判定入口内部
 *       复用：入口判定一次后逐像素直进公式，省去逐层重复的快路径比较。
 */
static __inline colour_t _we_colour_blend_core(colour_t fg, colour_t bg, uint8_t alpha)
{
    colour_t out;

#if (LCD_DEEP == DEEP_RGB565)
    out.dat16 = _we_blend_rgb565_core(fg.dat16, bg.dat16, alpha);
#elif (LCD_DEEP == DEEP_RGB888)
    {
        uint32_t a  = (uint32_t)alpha + (uint32_t)(alpha >> 7);
        uint32_t ia = 256U - a;
        out.rgb.r = (uint8_t)(((uint32_t)fg.rgb.r * a + (uint32_t)bg.rgb.r * ia) >> 8);
        out.rgb.g = (uint8_t)(((uint32_t)fg.rgb.g * a + (uint32_t)bg.rgb.g * ia) >> 8);
        out.rgb.b = (uint8_t)(((uint32_t)fg.rgb.b * a + (uint32_t)bg.rgb.b * ia) >> 8);
    }
#elif (LCD_DEEP == DEEP_GRAY8)
    {
        /* 单通道亮度线性插值，比 RGB565 拆位更快 */
        uint32_t a  = (uint32_t)alpha + (uint32_t)(alpha >> 7);
        uint32_t ia = 256U - a;
        out.dat8 = (uint8_t)(((uint32_t)fg.dat8 * a + (uint32_t)bg.dat8 * ia) >> 8);
    }
#endif
    return out;
}

/* --------------------------------------------------------------------------
 * 通用双色混合，返回混合后的 colour_t，RGB565 和 RGB888 均适用。
 *
 * 与 we_store_blended_color（本文件下方）的区别：
 * 1. 这里返回结果值，调用方自行决定如何写入目标
 * 2. 适合"先算出混色结果、再按需写入"的场景（如 img_ex 的逐像素内层循环）
 * 3. 内部把 0~255 alpha 扩展到 0~256 后用 >>8 替代 /255，
 *    与 we_blend_rgb565 走同一套近似公式，精度一致
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
    if (alpha >= 250U)
        return fg;
    if (alpha <= 5U)
        return bg;
    return _we_colour_blend_core(fg, bg, alpha);
}

/* --------------------------------------------------------------------------
 * 快速 a*b/255 近似（Cortex-M0 无硬件除法专用）
 *
 * 输入 v 应为两个 0~255 量的乘积（范围 0~65025）。
 * 用 (v + (v>>8) + 1) >> 8 近似 v/255，省掉一次软件除法
 * （M0 上 /255 约 90+ cycle）。与精确取整最多差 1 LSB，alpha 视觉无差异。
 * -------------------------------------------------------------------------- */
/**
 * @brief 快速近似计算 v/255（v 为两个 0~255 量之积）
 * @param v 传入：被除数（0~65025）
 * @return 近似商（0~255）
 */
static __inline uint8_t we_div255(uint32_t v)
{
    return (uint8_t)((v + (v >> 8) + 1U) >> 8);
}

/**
 * @brief 比较两个颜色是否相等（按当前色深逐通道比较）
 * @param a 传入：颜色 A
 * @param b 传入：颜色 B
 * @return 1 相等；0 不等
 */
static __inline uint8_t we_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#elif (LCD_DEEP == DEEP_GRAY8)
    return (uint8_t)(a.dat8 == b.dat8);
#elif (LCD_DEEP == DEEP_RGB888)
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 将透明度按控件整体不透明度缩放
 * @param a 传入：原始透明度（0~255）
 * @param opacity 传入：控件整体不透明度（0~255）
 * @return 缩放后的透明度（0~255）
 */
static __inline uint8_t we_opa_scale(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/* --------------------------------------------------------------------------
 * 颜色写入 / 混色辅助内联函数
 *
 * 定义在这里供内核绘图与各控件的 .c 文件共用，
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
#elif (LCD_DEEP == DEEP_GRAY8)
    dst->dat8 = color.dat8;
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
    /* 快路径已在本入口判定完，直进核心公式（省去内层逐级重复比较） */
    *dst = _we_colour_blend_core(fg, *dst, alpha);
}

#define WE_MASK_QUADRANT_LT 0U
#define WE_MASK_QUADRANT_RT 1U
#define WE_MASK_QUADRANT_LB 2U
#define WE_MASK_QUADRANT_RB 3U

/**
 * @brief 计算轴对齐 90 度四分之一圆在单个像素上的 alpha mask
 * @param x 传入：外接正方形左上角 X 坐标
 * @param y 传入：外接正方形左上角 Y 坐标
 * @param radius 传入：圆角半径
 * @param quadrant 传入：象限标识（WE_MASK_QUADRANT_LT/RT/LB/RB）
 * @param px 传入：目标像素 X 坐标
 * @param py 传入：目标像素 Y 坐标
 * @return 返回 0~255 alpha（255 表示满覆盖）
 * @note 先做快速内外判定，仅在抗锯齿边界带内执行 4x4 子采样
 */
uint8_t we_mask_quarter_circle_alpha(int16_t x, int16_t y, uint16_t radius,
                                     uint8_t quadrant, int16_t px, int16_t py);

/**
 * @brief 计算同心内外两个四分之一圆在单个像素上的 alpha mask（单次子采样）
 * @param x 传入：外圆外接正方形左上角 X 坐标
 * @param y 传入：外圆外接正方形左上角 Y 坐标
 * @param r_out 传入：外圆半径
 * @param r_in 传入：内圆半径（<= r_out，0 表示无内圆）
 * @param quadrant 传入：象限标识（WE_MASK_QUADRANT_LT/RT/LB/RB）
 * @param px 传入：目标像素 X 坐标
 * @param py 传入：目标像素 Y 坐标
 * @param p_fill_alpha 传出：内圆覆盖 alpha（0~255）
 * @return 返回外圆覆盖 alpha（0~255）；环带覆盖 = 返回值 - *p_fill_alpha
 * @note 带边框圆角专用：内外覆盖共享一遍 4x4 子采样，比调两次 quarter-circle 省一半
 */
uint8_t we_mask_quarter_ring_alpha(int16_t x, int16_t y, uint16_t r_out, uint16_t r_in,
                                   uint8_t quadrant, int16_t px, int16_t py,
                                   uint8_t *p_fill_alpha);

/**
 * @brief 计算轴对齐圆角矩形在单个像素上的 alpha mask
 * @param x 传入：外接矩形左上角 X 坐标
 * @param y 传入：外接矩形左上角 Y 坐标
 * @param w 传入：外接矩形宽度
 * @param h 传入：外接矩形高度
 * @param radius 传入：圆角半径
 * @param px 传入：目标像素 X 坐标
 * @param py 传入：目标像素 Y 坐标
 * @return 返回 0~255 alpha（255 表示满覆盖）
 * @note 中央区域与直边区域直接快速返回，仅在四角边界带调用 quarter-circle mask
 */
uint8_t we_mask_round_rect_alpha(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                 uint16_t radius, int16_t px, int16_t py);

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
 * @brief 绘制 1px 描边 + 内填充的双色圆角矩形（描边套路的单点实现）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：外接矩形左上角 X 坐标
 * @param y 传入：外接矩形左上角 Y 坐标
 * @param w 传入：外接矩形宽度
 * @param h 传入：外接矩形高度
 * @param radius 传入：外轮廓圆角半径（内填充自动取同心半径 radius-1）
 * @param outline 传入：描边颜色
 * @param fill 传入：内填充颜色
 * @param opacity 传入：整体透明度（0~255，两层同一份，无亮度断层）
 * @return 无
 * @note 实现为外层整填 + 内缩 1px 再填：w/h 不足 3px 时退化为纯描边色块。
 *       单色样式的"白描边黑底"面板统一经此绘制，口径只此一处。
 */
void we_draw_round_rect_outline_fill(we_lcd_t *p_lcd, int16_t x, int16_t y,
                                     uint16_t w, uint16_t h, uint16_t radius,
                                     colour_t outline, colour_t fill, uint8_t opacity);

/**
 * @brief 绘制抗锯齿圆角矩形边框（只画环带，内部不动、露出下面的内容）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：外接矩形左上角 X 坐标
 * @param y 传入：外接矩形左上角 Y 坐标
 * @param w 传入：外接矩形宽度
 * @param h 传入：外接矩形高度
 * @param radius 传入：外圆角半径（内轮廓取同心半径 radius-bw，不足 0 取直角）
 * @param bw 传入：边框宽度；bw*2 >= min(w,h) 时整块实心填充
 * @param color 传入：边框颜色
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 * @note 空心框放在任意底色（容器背景、图片、渐变）上都正确：内部不填色。
 *       与 outline_fill 的区别是后者用填充色盖住内部。
 */
void we_draw_round_rect_ring(we_lcd_t *p_lcd, int16_t x, int16_t y,
                             uint16_t w, uint16_t h, uint16_t radius, uint16_t bw,
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
#elif (LCD_DEEP == DEEP_GRAY8)
    {
        /* 565 → 8 位通道 → 整数亮度 */
        uint16_t r = (uint16_t)(((pixel >> 11) & 0x1FU) << 3);
        uint16_t g = (uint16_t)(((pixel >> 5) & 0x3FU) << 2);
        uint16_t b = (uint16_t)((pixel & 0x1FU) << 3);
        out.dat8 = (uint8_t)((r * 77U + g * 150U + b * 29U) >> 8);
    }
#endif
    return out;
}

    /**
     * @brief 绘制纯色矩形
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x 传入：左上角 X 坐标
     * @param y 传入：左上角 Y 坐标
     * @param w 传入：宽度
     * @param h 传入：高度
     * @param color 传入：填充颜色
     * @param opacity 传入：透明度
     * @return 无
     */
    void we_fill_rect(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h, colour_t color, uint8_t opacity);
    /**
     * @brief 在 PFB 缓冲区内写入单个像素
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param px 传入：像素 X 坐标 (屏幕绝对坐标)
     * @param py 传入：像素 Y 坐标
     * @param color 传入：像素颜色
     * @param opacity 传入：透明度 (0~255)
     * @return 无
     */
    void we_draw_pixel(we_lcd_t *p_lcd, int16_t px, int16_t py, colour_t color, uint8_t opacity);

    /**
     * @brief 绘制粗线段 (Bresenham + 厚度扩展)
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x0 传入：起点 X
     * @param y0 传入：起点 Y
     * @param x1 传入：终点 X
     * @param y1 传入：终点 Y
     * @param thickness 传入：线宽 (像素)
     * @param color 传入：线段颜色
     * @param opacity 传入：透明度 (0~255)
     * @return 无
     */
    void we_draw_line(we_lcd_t *p_lcd, int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1, uint8_t thickness,
                      colour_t color, uint8_t opacity);

    /**
     * @brief 绘制圆头抗锯齿线段（单遍胶囊覆盖，半透明下圆头与线身不叠色）
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x0 传入：起点 X
     * @param y0 传入：起点 Y
     * @param x1 传入：终点 X
     * @param y1 传入：终点 Y
     * @param width 传入：线宽 (像素)
     * @param color 传入：线段颜色
     * @param opacity 传入：透明度 (0~255)
     * @return 无
     */
    void we_draw_line_round(we_lcd_t *p_lcd, int16_t x0, int16_t y0,
                            int16_t x1, int16_t y1, uint8_t width,
                            colour_t color, uint8_t opacity);

    /**
     * @brief 绘制按行字节对齐、高位在前的 alpha 位图（字形点阵 / A1~A8 图标）
     * @param p_lcd 传入：GUI 屏幕上下文指针
     * @param x 传入：位图左上角 X 坐标
     * @param y 传入：位图左上角 Y 坐标
     * @param w 传入：位图宽度（像素）
     * @param h 传入：位图高度（行数）
     * @param src_data 传入：第 0 行数据首地址
     * @param row_stride 传入：每行字节数
     * @param bpp 传入：位深（1/2/4/8）
     * @param fg_color 传入：前景颜色
     * @param opacity 传入：整体透明度（入口乘入容器透明度级联）
     * @return 无
     */
    void we_draw_alpha_mask(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h,
                            const uint8_t *src_data, uint32_t row_stride, uint8_t bpp,
                            colour_t fg_color, uint8_t opacity);

    /**
     * @brief 旋转缩放的逆映射步进：屏幕上走 1 像素，源坐标走多少（Q12，四舍五入）
     * @param angle 传入：512 步制角度
     * @param scale_256 传入：缩放（256 = 1.0x，须 > 0）
     * @param st 传出：{屏幕 X+1 时源 X 增量, 屏幕 X+1 时源 Y 增量,
     *                  屏幕 Y+1 时源 X 增量, 屏幕 Y+1 时源 Y 增量}
     * @return 无
     * @note 纯 32 位运算（不用 64 位乘法）；0°、1.0x 时恰为 4096，逐像素不漂移。
     *       供 img_ex / label_ex 共用。
     */
    void we_xform_steps(int16_t angle, uint16_t scale_256, int32_t st[4]);

    /**
     * @brief 求某个屏幕像素的逆映射采样点（Q12）
     * @param st 传入：we_xform_steps 求出的步进
     * @param w 传入：源宽（像素）
     * @param h 传入：源高（像素）
     * @param ofs_x 传入：旋转轴心相对源几何中心的 X 偏移（像素）
     * @param ofs_y 传入：旋转轴心相对源几何中心的 Y 偏移（像素）
     * @param dx 传入：该像素 X 减去屏幕变换中心 X
     * @param dy 传入：该像素 Y 减去屏幕变换中心 Y
     * @param out 传出：{源 X, 源 Y}（Q12）
     * @return 无
     * @note 屏幕变换中心映射到源图轴心（几何中心 + 偏移）。偶数宽/高的轴心落在
     *       两像素之间，采样取屏幕像素中心（补半个步进）；奇数时轴心本身在源像素
     *       中心，像素左上角已对准。0°、1.0x 时源图逐像素原样画出；每个像素的采样
     *       点只取决于它自己的屏幕坐标，与 PFB 切片、脏矩形的切法无关。
     */
    void we_xform_origin(const int32_t st[4], uint16_t w, uint16_t h, int16_t ofs_x, int16_t ofs_y,
                         int32_t dx, int32_t dy, int32_t out[2]);

    /**
     * @brief 旋转缩放后源矩形在屏幕上的外接框（相对变换中心）
     * @param w 传入：源宽（像素）
     * @param h 传入：源高（像素）
     * @param ofs_x 传入：旋转轴心相对源几何中心的 X 偏移（像素）
     * @param ofs_y 传入：旋转轴心相对源几何中心的 Y 偏移（像素）
     * @param angle 传入：512 步制角度
     * @param scale_256 传入：缩放（256 = 1.0x，上限 64x）
     * @param out_x 传出：外接框左上角相对变换中心的 X 偏移
     * @param out_y 传出：外接框左上角相对变换中心的 Y 偏移
     * @param out_w 传出：外接框宽
     * @param out_h 传出：外接框高
     * @return 无
     * @note 四角按半像素精度变换，下界向下取整、上界向上取整，再各留 1px 余量：
     *       任何缩放倍率下采样到的像素都落在框内。
     */
    void we_xform_bbox(int16_t w, int16_t h, int16_t ofs_x, int16_t ofs_y,
                       int16_t angle, uint16_t scale_256,
                       int16_t *out_x, int16_t *out_y, int16_t *out_w, int16_t *out_h);

#ifdef __cplusplus
}
#endif

#endif /* WE_DRAW_H */
