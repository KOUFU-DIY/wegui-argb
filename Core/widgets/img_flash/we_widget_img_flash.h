#ifndef WE_WIDGET_IMG_FLASH_H
#define WE_WIDGET_IMG_FLASH_H


#include "image_res.h"
#include "wegui.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 外挂读取参数（均可在 we_user_config.h 覆盖）
 * -------------------------------------------------------------------------- */
/* 数据读块字节数：未压缩图半透明时每笔最多读这么多字节（不透明时整行直接读进 PFB）；
 * 也是索引 QOI 数据窗口的大小 */
#ifndef WE_FLASH_IMG_CHUNK
#define WE_FLASH_IMG_CHUNK 128U
#endif

/* 索引 QOI 行索引预取窗口字节数：行号顺序递增，一笔读取覆盖后续多行的
 * 索引项（u16 索引表每项 2 字节，64 字节即 32 行） */
#ifndef WE_FLASH_IMG_IDX_WIN
#define WE_FLASH_IMG_IDX_WIN 64U
#endif

/* 索引 QOI 读取缓存
 * 1：头与调色盘、行索引窗口、数据窗口放静态 RAM（约 400 字节），跨切片、
 *    跨帧保留，同一张图的后续切片不再重复读取；单槽按地址命中，多张外挂
 *    压缩图交替绘制时互相挤出，最坏退化为 0 档的读取量
 * 0：不占静态 RAM，读取状态放栈上（约 400 字节），只在单次绘制调用内有效 */
#ifndef WE_FLASH_IMG_CACHE
#define WE_FLASH_IMG_CACHE 1
#endif

#if (WE_FLASH_IMG_CHUNK < 2U) || (WE_FLASH_IMG_CHUNK > 0xFFFFU)
#error "WE_FLASH_IMG_CHUNK must be within 2..65535"
#endif
#if (WE_FLASH_IMG_IDX_WIN < 4U) || (WE_FLASH_IMG_IDX_WIN > 0xFFFFU)
#error "WE_FLASH_IMG_IDX_WIN must be within 4..65535 (one u32 row-index entry at least)"
#endif

/* --------------------------------------------------------------------------
 * 外挂 Flash 图片对象
 * -------------------------------------------------------------------------- */
typedef struct
{
    we_obj_t base;         /* base.w/h 复用存储图片宽高 */
    uint32_t flash_addr;   /* 资源头在 flash 中的起始地址 */
    uint8_t  opacity;
    uint8_t  fmt;          /* 资源格式（imgarry_type_t 取值） */
    uint8_t  pal_count;    /* 索引 QOI：静态调色盘条目数（init 时读出） */
    uint16_t m16;          /* 索引 QOI：u16 行索引表项数（init 时读出） */
} we_flash_img_obj_t;

/**
 * @brief 初始化外部 Flash 图片控件并挂入 LCD 对象链表。
 * @param obj 待初始化的图片控件实例。
 * @param lcd GUI 运行时 LCD 对象，其端口需提供 storage_read。
 * @param x 图片左上角 X 坐标。
 * @param y 图片左上角 Y 坐标。
 * @param flash_addr 图片资源头在外部 Flash 的起始地址。
 * @param opacity 图片整体透明度（0~255）。
 * @return 1 表示初始化成功，0 表示资源头或参数无效（含非 V3 的索引 QOI 流）。
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
static inline void we_flash_img_obj_set_pos(we_flash_img_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_set_pos((we_obj_t *)obj, x, y);
}

#ifdef __cplusplus
}
#endif
#endif /* WE_WIDGET_IMG_FLASH_H */
