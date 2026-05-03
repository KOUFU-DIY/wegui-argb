#ifndef WE_FONT_TEXT_H
#define WE_FONT_TEXT_H

#include "we_gui_driver.h"

typedef struct
{
    int16_t y;
    int16_t w;
    int16_t h;
} we_text_line_rect_t;

/**
 * @brief 查询指定码点的字形排版信息
 * @param font_array 传入：字库数组指针（仅支持 font2c internal 格式）
 * @param code 传入：Unicode 码点
 * @param out_info 传出：字形信息结构体，成功时写入 adv_w / box_w / box_h / x_ofs / y_ofs / offset
 * @return 1 表示找到字形，0 表示未找到或参数非法
 */
uint8_t we_font_get_glyph_info(const unsigned char *font_array, uint16_t code, we_glyph_info_t *out_info);

/**
 * @brief 获取字库的标准行高
 * @param font_array 传入：字库数组指针
 * @return 行高（像素），失败返回 0
 */
uint16_t we_font_get_line_height(const unsigned char *font_array);

/**
 * @brief 获取字形点阵地址、位深和行跨度信息
 * @param font_array 传入：字库数组指针
 * @param info 传入：已有的字形信息结构体
 * @param out_bitmap 传出：字形点阵起始地址
 * @param out_bpp 传出：字形位深（1/2/4/8 bpp）
 * @param out_row_stride 传出：点阵每行字节数
 * @return 1 表示成功，0 表示失败或参数非法
 */
uint8_t we_font_get_bitmap_info(const unsigned char *font_array, const we_glyph_info_t *info,
                                const uint8_t **out_bitmap, uint8_t *out_bpp, uint32_t *out_row_stride);

/**
 * @brief 计算一段文本的像素宽度
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @return 文本总宽度（像素）
 * @note 遇到换行符会停止宽度累加，仅统计第一行宽度
 */
uint16_t we_get_text_width(const unsigned char *font_array, const char *str);

/**
 * @brief 测量文本每一行的包围矩形
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @param out_rects 传出：每行矩形数组，可为 NULL（仅统计行数时使用）
 * @param max_rects 传入：out_rects 数组可容纳的最大行数
 * @return 实际文本行数
 * @note 每个矩形的 y 是相对 base_y 的偏移，w/h 为该行真实像素范围
 */
uint16_t we_text_measure_line_rects(const unsigned char *font_array, const char *str,
                                    we_text_line_rect_t *out_rects, uint16_t max_rects);

/**
 * @brief 按行对文本区域进行标脏
 * @param obj 传入：所属控件对象，用于通过 parent 链裁剪脏区
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @param base_x 传入：文本绘制基准 X 坐标
 * @param base_y 传入：文本绘制基准 Y 坐标
 * @return 无
 * @note 常用于 label / flash_font 这类控件的精细文本刷新
 */
void we_text_invalidate_lines(we_obj_t *obj, const unsigned char *font_array, const char *str,
                              int16_t base_x, int16_t base_y);

/**
 * @brief 获取字符串可见内容的垂直包围范围
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串，仅测量第一行
 * @param out_y_top 传出：相对绘制基准 y 的顶部偏移
 * @param out_y_bottom 传出：相对绘制基准 y 的底部偏移
 * @return 无
 * @note 可用于基于文字真实可见范围做垂直居中
 */
void we_get_text_bbox(const unsigned char *font_array, const char *str, int8_t *out_y_top, int8_t *out_y_bottom);

/**
 * @brief 在指定位置绘制一段 UTF-8 字符串
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param x 传入：起始绘制 X 坐标
 * @param y 传入：起始绘制 Y 坐标
 * @param font_array 传入：字库数组指针
 * @param str 传入：UTF-8 字符串
 * @param fg_color 传入：前景颜色
 * @param opacity 传入：整体透明度
 * @return 无
 */
void we_draw_string(we_lcd_t *p_lcd, int16_t x, int16_t y, const unsigned char *font_array, const char *str,
                    colour_t fg_color, uint8_t opacity);

#endif
