#ifndef WE_IMAGE_H
#define WE_IMAGE_H

/* --------------------------------------------------------------------------
 * 图片渲染与格式分发：资源格式契约见 image_res.h。
 * -------------------------------------------------------------------------- */

#include "we_types.h"
#include "image_res.h" /* 图片格式码枚举：格式分发接口的参数类型 */

#ifdef __cplusplus
extern "C"
{
#endif

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

/**
 * @brief 渲染 ARGB8565 原始图片到屏幕（逐像素 [alpha][RGB565 大端] 3 字节）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：图片数据指针（ARGB8565 格式）
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 */
void we_img_render_argb8565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity);

/**
 * @brief 渲染 A1/A2/A4/A8 透明位图到屏幕（仅 alpha 通道，以前景色混合）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：透明位图数据指针（IMG_A1/A2/A4/A8 格式）
 * @param fg_color 传入：前景色（位图 alpha 以该颜色对背景混合）
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 * @note 取模数据每行按字节对齐（行末补零）、位序高位在前；
 *       连续位流布局的取模数据与此不兼容，不能混用
 */
void we_img_render_alpha(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, colour_t fg_color, uint8_t opacity);

#if (WE_CFG_ENABLE_INDEXQOI_MASK == 1)
/**
 * @brief 渲染索引QOI_MASK 压缩的 A8 透明蒙版（仅 alpha 通道，以前景色混合）
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：图片数据指针（IMG_A8_INDEXQOIMASK 格式）
 * @param fg_color 传入：前景色（蒙版 alpha 以该颜色对背景混合）
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 * @note 行独立编码 + 行字节偏移索引：PFB 切片只解码可见行，行内越过裁剪
 *       右缘立即停止；流式解码零额外 RAM。可选 8/7/6/5bit 量化在编码域完成，
 *       解码按高位复制还原。规格见 tool/0.tool/windows/img2bin_indexqoimask/README
 */
void we_img_render_indexqoi_mask(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img,
                                 colour_t fg_color, uint8_t opacity);
#endif

/* --------------------------------------------------------------------------
 * 图片格式分发（img / imgbtn 等所有内置图片控件共用一份）
 *
 * 分发表放在解码器身边而不是各控件内部：控件只管"画哪张图、用什么透明度"，
 * 支持哪些格式由渲染层统一回答，新增格式只改这一处。
 * -------------------------------------------------------------------------- */

/**
 * @brief 判断图片格式是否为当前编译配置支持（供控件在 init 阶段拦截）
 * @param fmt 传入：资源信息头里的格式码
 * @return 1 表示可渲染，0 表示不支持
 */
uint8_t we_img_format_supported(imgarry_type_t fmt);

/**
 * @brief 按资源格式自动选择渲染内核绘制图片
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x0 传入：目标左上角 X 坐标
 * @param y0 传入：目标左上角 Y 坐标
 * @param img 传入：图片数据指针（image_res.h 信息头格式）
 * @param fg_color 传入：前景色，仅 A1/A2/A4/A8 透明位图使用，其余格式忽略
 * @param opacity 传入：整体透明度（0~255）
 * @return 无
 * @note 不支持的格式静默跳过；控件应在 init 阶段用 we_img_format_supported 拦截。
 */
void we_img_render_auto(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img,
                        colour_t fg_color, uint8_t opacity);

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

#ifdef __cplusplus
}
#endif

#endif /* WE_IMAGE_H */
