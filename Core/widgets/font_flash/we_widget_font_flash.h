#ifndef WE_WIDGET_FONT_FLASH_H
#define WE_WIDGET_FONT_FLASH_H


#include "wegui.h"
#include "we_font_runtime_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define WE_FLASH_FONT_BLOB_HEADER_SIZE  6U
#define WE_FLASH_FONT_GLYPH_DESC_SIZE  14U

/* 单字形 scratch 缓冲上限（字节），we_user_config.h 可覆盖 */
#ifndef WE_FLASH_FONT_SCRATCH_MAX
#define WE_FLASH_FONT_SCRATCH_MAX     ((((SCREEN_WIDTH) * 4U) + 7U) >> 3U)
#endif

/* --------------------------------------------------------------------------
 * 字形描述缓存条目数（0 = 关闭，默认；打开时须为 2 的幂）
 *
 * 每画一个切片都要为本行每个字读一次 14 字节的字形描述，PFB 行数少时同一个字
 * 每帧要读好几次。打开后在 face 里放一张按字形序号直接映射的缓存，命中就不读
 * 外挂存储；同一 face 的所有文本对象共享。每条目 20 字节 RAM（16 条 = 320 字节）。
 * 外挂读取慢（软件 SPI、无 DMA）而 RAM 有余量时打开。
 * -------------------------------------------------------------------------- */
#ifndef WE_FLASH_FONT_DESC_CACHE
#define WE_FLASH_FONT_DESC_CACHE 0
#endif
#if (WE_FLASH_FONT_DESC_CACHE & (WE_FLASH_FONT_DESC_CACHE - 1))
#error "WE_FLASH_FONT_DESC_CACHE must be 0 or a power of two"
#endif

#if (WE_FLASH_FONT_DESC_CACHE > 0)
typedef struct
{
    uint32_t index;                                 /* 字形序号，0xFFFFFFFF = 空 */
    uint8_t desc[WE_FLASH_FONT_GLYPH_DESC_SIZE];    /* blob 里的原始描述项 */
} we_flash_font_desc_slot_t;
#endif

/* --------------------------------------------------------------------------
 * 字体描述符（Font Face）
 * -------------------------------------------------------------------------- */
typedef struct we_flash_font_face_s
{
    we_lcd_t *lcd;                      /* 绑定的 LCD（端口提供 storage_read）；初始化失败时为 NULL */
    uint32_t blob_addr;                 /* 外挂 blob 起始地址 */
    uint32_t blob_size;                 /* 等于 font->blob_size */
    uint32_t bitmap_data_offset;        /* blob 内点阵区偏移（字形描述表紧跟 6 字节头） */

    const font_range_t *ranges;         /* 指向 font->ranges */
    const uint32_t *sparse_codes;       /* 指向 font->sparse */

    uint8_t *glyph_buf;                 /* 单字形点阵 scratch 缓冲 */
    uint32_t glyph_buf_size;

    uint16_t line_height;
    uint16_t range_count;
    uint16_t sparse_count;
    uint16_t max_box_width;
    uint8_t bpp;

#if (WE_FLASH_FONT_DESC_CACHE > 0)
    we_flash_font_desc_slot_t desc_cache[WE_FLASH_FONT_DESC_CACHE];
#endif
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
    uint8_t ink_below;                  /* 字形伸出行框底边的最大行数（已计入包围盒高度） */
} we_flash_font_obj_t;

/* --------------------------------------------------------------------------
 * Face 初始化：静态缓冲版（零 malloc）
 * -------------------------------------------------------------------------- */

/**
 * @brief 校验外挂字体句柄并初始化字体面对象（静态缓冲版本）。
 * @param face 待写入的字体面对象，成功后保存字形索引与 blob 布局信息。
 * @param lcd GUI 运行时 LCD；其端口需提供 storage_read，用于从外部存储读字形数据。
 * @param handle font2c 导出的运行时句柄，提供字体元信息与 blob 地址。
 * @param glyph_buf 用户提供的字形流式读取缓冲区。
 * @param glyph_buf_size glyph_buf 的容量（字节），需至少容纳最大字形的一行位图数据，运行时按该缓冲大小分块读取并绘制。
 * @return 1 表示初始化成功，0 表示参数/格式校验失败（元信息非法 / blob 头错误 / 缓冲不足）。
 * @note 失败时 face 被置为不可用（lcd 清空），拿它初始化文本控件会直接失败。
 */
uint8_t we_flash_font_face_init(we_flash_font_face_t *face,
                                we_lcd_t *lcd,
                                const font_external_handle_t *handle,
                                uint8_t *glyph_buf,
                                uint32_t glyph_buf_size);


/* --------------------------------------------------------------------------
 * Obj：绑定已初始化的 face，多个文本对象可共享同一个 face
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
 * @param text 新的 UTF-8 文本指针（控件只保存指针，调用方须保证其生命周期）。
 * @note 不比较指针：同一缓冲区原地改写内容后再次调用也会重新测量并重绘。
 */
void we_flash_font_obj_set_text(we_flash_font_obj_t *obj, const char *text);

/**
 * @brief 设置文本颜色；颜色变化时触发控件重绘。
 * @param obj 文本控件实例。
 * @param color 新的文本前景色。
 */
void we_flash_font_obj_set_color(we_flash_font_obj_t *obj, colour_t color);

/**
 * @brief 设置文本整体透明度并重绘（只刷当前包围盒，不重新测量文字）。
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
static inline void we_flash_font_obj_delete(we_flash_font_obj_t *obj)
{
    we_obj_delete((we_obj_t *)obj);
}

#ifdef __cplusplus
}
#endif

#endif /* WE_WIDGET_FONT_FLASH_H */
