#ifndef WE_WIDGET_IMG_FLASH_H
#define WE_WIDGET_IMG_FLASH_H


#include "image_res.h"
#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * Flash 分块流：每次最多读 128 字节，按需加载
 * -------------------------------------------------------------------------- */
#ifndef WE_FLASH_IMG_CHUNK
#define WE_FLASH_IMG_CHUNK 128U
#endif

/* --------------------------------------------------------------------------
 * 外挂 Flash 图片对象
 * -------------------------------------------------------------------------- */
typedef struct
{
    we_obj_t       base;            /* base.w/h 复用存储图片宽高 */
    uint32_t       flash_addr;      /* 资源头在 flash 中的起始地址 */
    uint8_t        opacity;
    imgarry_type_t fmt;
} we_flash_img_obj_t;

/**
 * @brief 初始化外部 Flash 图片控件并挂入 LCD 对象链表。
 * @param obj 待初始化的图片控件实例。
 * @param lcd GUI 运行时 LCD 对象，需绑定 storage_read_cb。
 * @param x 图片左上角 X 坐标。
 * @param y 图片左上角 Y 坐标。
 * @param flash_addr 图片资源头在外部 Flash 的起始地址。
 * @param opacity 图片整体透明度（0~255）。
 * @return 1 表示初始化成功，0 表示资源头或参数无效。
 */
uint8_t we_flash_img_obj_init(we_flash_img_obj_t *obj, we_lcd_t *lcd,
                               int16_t x, int16_t y,
                               uint32_t flash_addr, uint8_t opacity);

/**
 * @brief 设置图片透明度并触发重绘。
 * @param obj 图片控件实例。
 * @param opacity 新透明度（0~255）。
 */
void we_flash_img_obj_set_opacity(we_flash_img_obj_t *obj, uint8_t opacity);

/**
 * @brief 设置图片控件位置（封装 we_obj_set_pos）。
 * @param obj 图片控件实例。
 * @param x 新的左上角 X 坐标。
 * @param y 新的左上角 Y 坐标。
 */
void we_flash_img_obj_set_pos(we_flash_img_obj_t *obj, int16_t x, int16_t y);

#ifdef __cplusplus
}
#endif
#endif /* WE_WIDGET_IMG_FLASH_H */
