# font2c

`font2c` is a standalone bitmap font extraction tool for embedded projects.
It converts `ttf` / `otf` / `ttc` fonts into:

- `.bin` font files for flash or file storage
- `.c + .h` source files for direct firmware integration

This document only describes:

- bitmap export rules
- `font2c.exe` usage

JSON details are in `input/README.md`.
Build details are in `builder/README.md`.

## Quick Start

1. Put font files in `fonts/`
2. Put JSON configs in `input/`
3. Run:

```txt
font2c.exe
```

Default behavior:

```txt
font2c.exe build-all input -o output
```

Build one config:

```txt
font2c.exe build input\arial_16_4bbp.json -o output
```

At startup, the tool automatically creates local `fonts/`, `input/`, and `output/` directories if they do not exist.

## CLI

Supported commands:

```txt
font2c.exe build <config.json> [-o <output_dir>]
font2c.exe build-all <input_dir> [-o <output_dir>]
font2c.exe --help
font2c.exe --version
```

## Font Lookup

When resolving `font.file`, the tool checks in this order:

1. path relative to the JSON file
2. project `fonts/` directory
3. installed system font directories
   - the lookup matches font file names such as `arial.ttf`, `msyh.ttc`; it does not perform family-name lookup like `Arial`

## Output Rules

- output basename always comes from the JSON filename
- generated `.c` files include a metadata comment block with:
  - source font
  - face index
  - font size
  - `bpp`
  - `line_height`
  - `baseline`
  - `max_box_width`
  - `max_box_height`
  - normalized ranges
  - direct input chars
  - normalized sparse chars

## Deployment Modes

Version 1 keeps one shared charset model:

- one or more continuous Unicode ranges
- zero or more sparse extra codepoints

Deployment is split into two modes:

- `internal`
- `external`

### Internal Mode

Generated files:

- `<name>.c`
- `<name>.h`

The generated objects contain:

- shared font metadata
- `range_table`
- `sparse_unicode_table`
- `glyph_desc_table`
- `bitmap_data`

Recommended exported structure:

```c
typedef struct {
    uint8_t bpp;
    uint16_t line_height;
    uint16_t baseline;
    uint16_t range_count;
    uint16_t sparse_count;
    uint16_t glyph_count;
    uint16_t max_box_width;
    uint16_t max_box_height;
    const range_t *ranges;
    const uint32_t *sparse;
    const glyph_desc_t *glyph_desc;
    const uint8_t *bitmap_data;
} font_internal_t;
```

### External Mode

Generated files:

- `<name>.c`
- `<name>.h`
- `<name>.bin`

`.c/.h` contain:

- shared font metadata
- `range_table`
- `sparse_unicode_table`
- `blob_size`
- `blob_crc32`

Recommended exported structure:

```c
typedef struct {
    uint8_t bpp;
    uint16_t line_height;
    uint16_t baseline;
    uint16_t range_count;
    uint16_t sparse_count;
    uint16_t glyph_count;
    uint16_t max_box_width;
    uint16_t max_box_height;
    const range_t *ranges;
    const uint32_t *sparse;
    uint32_t blob_size;
    uint32_t blob_crc32;
} font_external_t;
```

Recommended runtime binding handle:

```c
typedef struct {
    const font_external_t *font;
    uintptr_t blob_addr;
} font_external_handle_t;
```

- `blob_addr` is provided by the user at runtime
- generated code does not define a fixed external flash address
- `blob_addr` points to the beginning of the external `.bin`

## External Bin Layout

Version 1 external blob layout:

```txt
[0]     0x02
[1]     0x00
[2:5]   crc32_be
[6:...] glyph_desc_table
[...]   bitmap_data
```

- `bin[0]` is the font blob marker
- `bin[1]` is reserved and must currently be `0x00`
- `bin[2:5]` stores `CRC-32` in big-endian order
- no extra alignment or padding is inserted

Fixed offsets:

```txt
glyph_desc_offset = 6
bitmap_data_offset = 6 + glyph_count * 14
blob_size = 6 + glyph_count * 14 + bitmap_data_size
```

## Shared Glyph Rules

Shared rules for both deployment modes:

- big-endian multi-byte integers
- 4-byte Unicode codepoints
- row-major bitmap storage
- MSB-first bit packing
- row-byte-aligned bitmap rows

### Range Table

Each range entry uses 8 bytes:

```txt
offset  size  type   name
0       4     u32be  start_unicode
4       4     u32be  end_unicode
```

- ranges are closed intervals
- ranges must be sorted by codepoint
- ranges must not overlap
- adjacent ranges are merged during normalization

### Sparse Unicode Table

Each sparse entry uses 4 bytes:

```txt
offset  size  type   name
0       4     u32be  unicode
```

- sparse entries are codepoints not covered by any range
- sparse entries are stored in ascending order
- duplicate codepoints are not allowed

### Glyph Descriptor

Each glyph uses a 14-byte descriptor:

```txt
offset  size  type   name
0       2     u16be  adv_w
2       2     u16be  box_w
4       2     u16be  box_h
6       2     i16be  x_ofs
8       2     i16be  y_ofs
10      4     u32be  bitmap_offset
```

Glyph order:

1. all range glyphs
2. all sparse glyphs

Metric rules:

- `adv_w` is pen advance in pixels
- `x_ofs` is the glyph bitmap left offset relative to pen position
- `y_ofs` is the glyph bitmap top offset relative to line top
- `bitmap_offset` is relative to the beginning of `bitmap_data`
- empty glyphs may use `box_w = 0` and `box_h = 0`
- for empty glyphs, `row_stride = 0` and `bitmap_size = 0`
- empty glyphs are advanced by `adv_w` without reading bitmap bytes

Recommended FreeType mapping:

```txt
adv_w = round(advance.x / 64)
box_w = bitmap.width
box_h = bitmap.rows
x_ofs = bitmap_left
y_ofs = baseline - bitmap_top
```

Runtime placement:

```txt
glyph_x = pen_x + x_ofs
glyph_y = line_top + y_ofs
```

### Bitmap Data

Bitmap rows are tightly packed, with no extra alignment between glyphs.

```txt
row_stride = (box_w * bpp + 7) / 8
bitmap_size = row_stride * box_h
```

Pixel packing:

- `1bpp`: 1 byte stores 8 pixels, highest bit is the leftmost pixel
- `2bpp`: 1 byte stores 4 pixels in `7:6`, `5:4`, `3:2`, `1:0`
- `4bpp`: 1 byte stores 2 pixels, high nibble first
- `8bpp`: 1 byte stores 1 pixel

### Charset Normalization

Export uses a normalized charset:

- `ranges` are sorted first
- overlapping or adjacent `ranges` are merged
- `chars` are deduplicated by Unicode codepoint
- `chars` already covered by `ranges` are removed from sparse output
- sparse codepoints are stored in ascending order
- Unicode normalization such as NFC or NFKC is not applied

Final glyph count:

```txt
glyph_count = total range codepoints + sparse_count
```

### Runtime Lookup

Recommended runtime lookup order:

1. scan `range_table`
2. if the codepoint is inside a range, compute the glyph index directly
3. otherwise search `sparse_unicode_table`
4. if found, map it to the sparse glyph area
5. otherwise report a missing glyph

Range hit formula:

```txt
glyph_index = sum(previous_range_lengths) + (codepoint - start_unicode)
```

Sparse hit formula:

```txt
glyph_index = total_range_codepoints + sparse_index
```

Range length formula:

```txt
range_length = end_unicode - start_unicode + 1
```

For external mode:

1. locate `glyph_index` using the internal `range_table` and `sparse_unicode_table`
2. read `glyph_desc_table[glyph_index]` from the external blob
3. locate `bitmap_data` using `bitmap_offset`
4. decode and draw

### CRC Rules

Version 1 uses `CRC-32/IEEE`:

```txt
poly   = 0x04C11DB7
init   = 0xFFFFFFFF
refin  = true
refout = true
xorout = 0xFFFFFFFF
```

Storage:

- `crc32` is stored at `bin[2:5]`
- the stored byte order is big-endian

CRC scope:

```txt
bin[6 .. end]
```

This means:

- the 2-byte magic is not included in CRC
- the `crc32` field itself is not included in CRC

Recommended validation strategies:

- quick validation:
  - check `bin[0] == 0x02`
  - check `bin[1] == 0x00`
  - read `bin[2:5]`
  - compare with internal `blob_crc32`
- full validation:
  - perform quick validation first
  - then compute CRC over `bin[6 .. blob_size - 1]`
  - compare the result with internal `blob_crc32`

### Rasterization Rules

All `1bpp / 2bpp / 4bpp / 8bpp` output uses the same pipeline:

1. rasterize glyphs to 8-bit grayscale
2. quantize grayscale to target `bpp`
3. pack pixels with the rules above

Quantization rule:

```txt
maxv  = (1 << bpp) - 1
level = (gray * maxv + 127) / 255
```

## Notes

- the current release is Windows-first
- users do not need to install FreeType separately when using the prebuilt exe
- Chinese documentation is available in `README_CN.md`

## Release Coverage

- prebuilt artifact: `font2c.exe` for Windows x64 (MSYS64 toolchain)
- verified on Windows 10/11 with the bundled `font2c.exe`
- source build: `builder/build_win.cmd` + `builder/build_posix.sh`
- Windows releases embed `builder/icon_64x64.ico` automatically when present

## Common Issues

- `no .json files found in input` -> ensure `input/` contains `.json` configs
- `cannot find font file` -> check `fonts/` and system font directories for the exact file name
- `missing glyph U+xxxx` -> requested charset includes codepoints not supported by the font
- empty `output/` after running -> verify `-o output` was passed or `output` directory exists

## Typical Release Layout

```txt
font2c/
├─ font2c.exe
├─ fonts/
├─ input/
└─ output/
```
