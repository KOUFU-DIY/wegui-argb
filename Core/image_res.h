#ifndef IMAGE_RES_H
#define IMAGE_RES_H

#include "stdint.h"
#include "we_user_config.h"

/*
 * 纯数组图片资源格式 v2
 * 信息头格式: [资源类型][格式代码][宽H][宽L][高H][高L][取模数据...]
 *
 * 格式代码[7:4] 压缩类型:
 *   0x0 = 无压缩
 *   0x1 = 原始RLE
 *   0x2 = 改进RLE
 *   0x3 = 原始QOI(无字典)
 *   0x4 = 索引QOI
 *
 * 格式代码[3:0] 像素格式:
 *   0x0 = RGB565    0x1 = RGB888    0x2 = RGB555    0x3 = RGB444
 *   0x4 = RGB332    0x5 = ARGB8888  0x6 = ARGB6666  0x7 = ARGB4444
 *   0x8 = ARGB8565  0xF = OLED点阵
 */

/* 信息头偏移 */
#define IMG_HDR_RES_TYPE    0
#define IMG_HDR_FORMAT      1
#define IMG_HDR_WIDTH_H     2
#define IMG_HDR_WIDTH_L     3
#define IMG_HDR_HEIGHT_H    4
#define IMG_HDR_HEIGHT_L    5
#define IMG_HDR_SIZE        6

/* 信息头解析宏 (p = const uint8_t* 指向数组首地址) */
#define IMG_DAT_RES_TYPE(p)  ((p)[IMG_HDR_RES_TYPE])
#define IMG_DAT_FORMAT(p)    ((imgarry_type_t)(p)[IMG_HDR_FORMAT])
#define IMG_DAT_WIDTH(p)     ((uint16_t)(((p)[IMG_HDR_WIDTH_H]  << 8) | (p)[IMG_HDR_WIDTH_L]))
#define IMG_DAT_HEIGHT(p)    ((uint16_t)(((p)[IMG_HDR_HEIGHT_H] << 8) | (p)[IMG_HDR_HEIGHT_L]))
#define IMG_DAT_PIXELS(p)    ((p) + IMG_HDR_SIZE)

/* 资源类型 */
typedef enum
{
    FILE_TYPE_IMG  = 0x00,
    FILE_TYPE_FONT = 0x01,
} file_type;

/* 图片格式代码 */
typedef enum
{
    IMG_RGB565          = 0x00,
    IMG_RGB888          = 0x01,
    IMG_RGB555          = 0x02,
    IMG_RGB444          = 0x03,
    IMG_RGB332          = 0x04,
    IMG_ARGB8888        = 0x05,
    IMG_ARGB6666        = 0x06,
    IMG_ARGB4444        = 0x07,
    IMG_ARGB8565        = 0x08,
    IMG_OLED            = 0x0F,

    IMG_RGB565_ORIRLE   = 0x10,
    IMG_RGB888_ORIRLE   = 0x11,
    IMG_RGB555_ORIRLE   = 0x12,
    IMG_RGB444_ORIRLE   = 0x13,
    IMG_RGB332_ORIRLE   = 0x14,
    IMG_ARGB8888_ORIRLE = 0x15,
    IMG_ARGB6666_ORIRLE = 0x16,
    IMG_ARGB4444_ORIRLE = 0x17,
    IMG_ARGB8565_ORIRLE = 0x18,
    IMG_OLED_ORIRLE     = 0x1F,

    IMG_RGB565_IMPRLE   = 0x20,
    IMG_RGB888_IMPRLE   = 0x21,
    IMG_RGB555_IMPRLE   = 0x22,
    IMG_RGB444_IMPRLE   = 0x23,
    IMG_RGB332_IMPRLE   = 0x24,
    IMG_ARGB8888_IMPRLE = 0x25,
    IMG_ARGB6666_IMPRLE = 0x26,
    IMG_ARGB4444_IMPRLE = 0x27,
    IMG_ARGB8565_IMPRLE = 0x28,
    IMG_OLED_IMPRLE     = 0x2F,

    IMG_RGB565_ORIQOI   = 0x30,
    IMG_RGB888_ORIQOI   = 0x31,
    IMG_RGB555_ORIQOI   = 0x32,
    IMG_RGB444_ORIQOI   = 0x33,
    IMG_RGB332_ORIQOI   = 0x34,
    IMG_ARGB8888_ORIQOI = 0x35,
    IMG_ARGB6666_ORIQOI = 0x36,
    IMG_ARGB4444_ORIQOI = 0x37,
    IMG_ARGB8565_ORIQOI = 0x38,

    IMG_RGB565_INDEXQOI   = 0x40,
    IMG_RGB888_INDEXQOI   = 0x41,
    IMG_RGB555_INDEXQOI   = 0x42,
    IMG_RGB444_INDEXQOI   = 0x43,
    IMG_RGB332_INDEXQOI   = 0x44,
    IMG_ARGB8888_INDEXQOI = 0x45,
    IMG_ARGB6666_INDEXQOI = 0x46,
    IMG_ARGB4444_INDEXQOI = 0x47,
    IMG_ARGB8565_INDEXQOI = 0x48,
} imgarry_type_t;

extern const unsigned char img_rgb565_64x80[10246];
extern const unsigned char img_rgb565_indexqoi_96x54[8074];
extern const unsigned char img_argb8565_indexqoi_208x42[5236];
extern const unsigned char img_argb8565_indexqoi_80x80[4784];

#endif
