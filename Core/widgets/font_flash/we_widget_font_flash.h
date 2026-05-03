#ifndef WE_WIDGET_FONT_FLASH_H
#define WE_WIDGET_FONT_FLASH_H


#include "we_gui_driver.h"
#include "we_font_runtime_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define WE_FLASH_FONT_BLOB_HEADER_SIZE  6U
#define WE_FLASH_FONT_GLYPH_DESC_SIZE  14U
#define WE_FLASH_FONT_SCRATCH_MAX     ((((SCREEN_WIDTH) * 4U) + 7U) >> 3U)

/* --------------------------------------------------------------------------
 * 字体描述符（Font Face）
 * -------------------------------------------------------------------------- */
typedef struct we_flash_font_face_s
{
    we_lcd_t *lcd;                      /* 绑定的 LCD（提供 storage_read_cb） */
    const font_external_t *font;        /* font2c 生成的 external 元信息 */
    uint32_t blob_addr;                 /* 外挂 blob 起始地址 */
    uint32_t blob_size;                 /* 等于 font->blob_size */

    uint16_t line_height;
    uint16_t baseline;
    uint8_t bpp;

    uint16_t range_count;
    uint16_t sparse_count;
    uint16_t glyph_count;
    uint16_t max_box_width;
    uint16_t max_box_height;

    uint32_t range_total;               /* 所有 range 累计字形数 */
    uint32_t glyph_desc_offset;         /* blob 内字形描述表偏移，固定为 6 */
    uint32_t bitmap_data_offset;        /* blob 内点阵区偏移 */

    const font_range_t *ranges;         /* 指向 font->ranges */
    const uint32_t *sparse_codes;       /* 指向 font->sparse */

    uint8_t *glyph_buf;                 /* 单字形点阵 scratch 缓冲 */
    uint32_t glyph_buf_size;
} we_flash_font_face_t;

/* --------------------------------------------------------------------------
 * 控件实例（Font Obj）
 * -------------------------------------------------------------------------- */
typedef struct we_flash_font_obj_s
{
    we_obj_t base;                      /* 必须是第一个成员 */
    we_flash_font_face_t *face;         /* 指向共享字体描述符，不拥有所有权 */
    const char *text;
    colour_t color;
    uint8_t opacity;
} we_flash_font_obj_t;

/* --------------------------------------------------------------------------
 * Face 初始化：静态缓冲版（零 malloc）
 *
 * @param face            待初始化的字体描述符
 * @param lcd             已绑定 storage_read_cb 的 LCD 上下文
 * @param handle          由 font2c external 元信息 + 外挂地址组成的运行时句柄
 * @param glyph_buf       用户提供的单字形点阵 scratch 缓冲
 * @param glyph_buf_size  glyph_buf 字节数；至少要能容纳 1 行字模数据，运行时按 buffer 大小分块读画
 * @return 1 成功，0 失败（元信息非法 / blob 头错误 / 缓冲不足）
 * -------------------------------------------------------------------------- */

/**
 * @brief 校验外挂字体句柄并初始化字体面对象（静态缓冲版本）。
 * @param face 待写入的字体面对象，成功后保存字形索引与 blob 布局信息。
 * @param lcd GUI 运行时 LCD；要求已绑定 storage_read_cb 用于从外部存储读字形数据。
 * @param handle font2c 导出的运行时句柄，提供字体元信息与 blob 地址。
 * @param glyph_buf 用户提供的字形流式读取缓冲区。
 * @param glyph_buf_size glyph_buf 的容量（字节），需至少容纳最大字形的一行位图数据。
 * @return 1 表示初始化成功，0 表示参数/格式校验失败。
 */
uint8_t we_flash_font_face_init(we_flash_font_face_t *face,
                                we_lcd_t *lcd,
                                const font_external_handle_t *handle,
                                uint8_t *glyph_buf,
                                uint32_t glyph_buf_size);


/* --------------------------------------------------------------------------
 * Obj 初始化：两种 face 初始化路径通用
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化外部字库文本控件并加入 LCD 对象链表。
 * @param obj 待初始化的文本控件实例。
 * @param face 已初始化的字体面对象，控件仅引用其内容。
 * @param x 控件左上角 X 坐标。
 * @param y 控件左上角 Y 坐标。
 * @param text 初始 UTF-8 文本指针（可为 NULL）。
 * @param color 文本前景色。
 * @param opacity 文本整体透明度（0~255）。
 * @return 1 表示初始化成功，0 表示输入无效或字体面不可用。
 */
uint8_t we_flash_font_obj_init(we_flash_font_obj_t *obj,
                               we_flash_font_face_t *face,
                               int16_t x,
                               int16_t y,
                               const char *text,
                               colour_t color,
                               uint8_t opacity);

/**
 * @brief 更新文本内容并按新文本重算包围盒后重绘。
 * @param obj 文本控件实例。
 * @param text 新的 UTF-8 文本指针。
 */
void we_flash_font_obj_set_text(we_flash_font_obj_t *obj, const char *text);

/**
 * @brief 设置文本颜色；颜色变化时触发控件重绘。
 * @param obj 文本控件实例。
 * @param color 新的文本前景色。
 */
void we_flash_font_obj_set_color(we_flash_font_obj_t *obj, colour_t color);

/**
 * @brief 设置文本整体透明度，并对旧/新可见区域做失效刷新。
 * @param obj 文本控件实例。
 * @param opacity 新透明度（0~255）。
 */
void we_flash_font_obj_set_opacity(we_flash_font_obj_t *obj, uint8_t opacity);

/**
 * @brief 设置文本控件位置（封装 we_obj_set_pos）。
 * @param obj 文本控件实例。
 * @param x 新的左上角 X 坐标。
 * @param y 新的左上角 Y 坐标。
 */
static inline void we_flash_font_obj_set_pos(we_flash_font_obj_t *obj, int16_t x, int16_t y)
{
we_obj_set_pos((we_obj_t *)obj, x, y);
}

/**
 * @brief 删除文本控件并从对象链表移除。
 * @param obj 文本控件实例。
 */
void we_flash_font_obj_delete(we_flash_font_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* WE_WIDGET_FONT_FLASH_H */
