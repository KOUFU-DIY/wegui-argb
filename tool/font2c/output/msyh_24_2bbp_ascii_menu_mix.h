#ifndef MSYH_24_2BBP_ASCII_MENU_MIX_H
#define MSYH_24_2BBP_ASCII_MENU_MIX_H

#include <stddef.h>
#include <stdint.h>

#ifndef FONT2C_RUNTIME_TYPES_H
#define FONT2C_RUNTIME_TYPES_H

typedef struct font_range_t {
    uint32_t start;
    uint32_t end;
} font_range_t;

typedef struct font_glyph_desc_t {
    uint16_t adv_w;
    uint16_t box_w;
    uint16_t box_h;
    int16_t x_ofs;
    int16_t y_ofs;
    uint32_t bitmap_offset;
} font_glyph_desc_t;

typedef struct font_internal_t {
    uint8_t bpp;
    uint16_t line_height;
    uint16_t baseline;
    uint16_t range_count;
    uint16_t sparse_count;
    uint16_t glyph_count;
    uint16_t max_box_width;
    uint16_t max_box_height;
    const font_range_t *ranges;
    const uint32_t *sparse;
    const font_glyph_desc_t *glyph_desc;
    const uint8_t *bitmap_data;
} font_internal_t;

typedef struct font_external_t {
    uint8_t bpp;
    uint16_t line_height;
    uint16_t baseline;
    uint16_t range_count;
    uint16_t sparse_count;
    uint16_t glyph_count;
    uint16_t max_box_width;
    uint16_t max_box_height;
    const font_range_t *ranges;
    const uint32_t *sparse;
    uint32_t blob_size;
    uint32_t blob_crc32;
} font_external_t;

typedef struct font_external_handle_t {
    const font_external_t *font;
    uintptr_t blob_addr;
} font_external_handle_t;

#endif

extern const font_external_t msyh_24_2bbp_ascii_menu_mix;

#endif
