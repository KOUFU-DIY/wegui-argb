# font2c

`font2c` 是一个面向嵌入式项目的单文件字体取模工具。
它可以把 `ttf` / `otf` / `ttc` 字体导出为：

- 可外挂到 Flash / 文件系统的 `.bin`
- 可直接参与固件编译的 `.c + .h`

这份文档只描述：

- 取模规则
- `font2c.exe` 的使用方式

JSON 相关说明放在 `input/README_CN.md`。
构建相关说明放在 `builder/README_CN.md`。

## 快速开始

1. 把字体文件放到 `fonts/`
2. 把 JSON 配置放到 `input/`
3. 运行：

```txt
font2c.exe
```

默认等价于：

```txt
font2c.exe build-all input -o output
```

如果只构建一个配置：

```txt
font2c.exe build input\arial_16_4bbp.json -o output
```

程序启动时，如果当前目录下没有 `fonts/`、`input/`、`output/`，会自动创建。

## CLI

支持的命令：

```txt
font2c.exe build <config.json> [-o <output_dir>]
font2c.exe build-all <input_dir> [-o <output_dir>]
font2c.exe --help
font2c.exe --version
```

## 字体查找顺序

解析 `font.file` 时，程序按下面顺序查找：

1. 当前 JSON 文件相对路径
2. 项目 `fonts/` 目录
3. 系统已安装字体目录
   - 这里沿文件名匹配（例如 `arial.ttf`、`msyh.ttc`），不支持字体族名如 `Arial` 的查找

## 输出规则

- 输出文件基名固定来自 JSON 文件名
- 生成的 `.c` 文件头部会带有注释信息：
  - 源字体
  - face index
  - 字号
  - `bpp`
  - `line_height`
  - `baseline`
  - `max_box_width`
  - `max_box_height`
  - 归一化区间
  - 直接输入字符
  - 归一化后的离散字符

## 部署模式

第一版只维护一套统一字符映射规则：

- 一个或多个连续 Unicode 区间
- 零个或多个离散补充字符

部署层分成两种模式：

- `internal`
- `external`

### 内部模式

生成：

- `<name>.c`
- `<name>.h`

`.c/.h` 中包含：

- 公共头信息
- `range_table`
- `sparse_unicode_table`
- `glyph_desc_table`
- `bitmap_data`

推荐结构体：

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

### 外挂模式

生成：

- `<name>.c`
- `<name>.h`
- `<name>.bin`

`.c/.h` 中包含：

- 公共头信息
- `range_table`
- `sparse_unicode_table`
- `blob_size`
- `blob_crc32`

推荐结构体：

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

推荐运行时句柄：

```c
typedef struct {
    const font_external_t *font;
    uintptr_t blob_addr;
} font_external_handle_t;
```

说明：

- `blob_addr` 由用户在初始化时传入
- 生成代码不写死外挂地址
- `blob_addr` 指向外挂 `bin` 的起始位置

## 外挂 bin 布局

第一版外挂 `bin` 固定布局：

```txt
[0]     0x02
[1]     0x00
[2:5]   crc32_be
[6:...] glyph_desc_table
[...]   bitmap_data
```

说明：

- `bin[0]` 是字体外挂标识
- `bin[1]` 当前固定为 `0x00`
- `bin[2:5]` 保存 `CRC-32`，按大端存储
- 不做额外对齐，不做填充

固定偏移：

```txt
glyph_desc_offset = 6
bitmap_data_offset = 6 + glyph_count * 14
blob_size = 6 + glyph_count * 14 + bitmap_data_size
```

## 公共字形规则

统一规则：

- 所有多字节整数使用大端
- 所有 Unicode 使用 4 字节无符号整数
- bitmap 采用行优先
- 位打包采用 `MSB first`
- 每行按字节对齐

### 连续区表

每个 range 项固定为 8 字节：

```txt
偏移  长度  类型    名称
0     4     u32be   start_unicode
4     4     u32be   end_unicode
```

- 区间是闭区间
- 区间必须按码点升序
- 区间不能重叠
- 相邻区间在归一化阶段自动合并

### 离散 Unicode 表

每个 sparse 项固定为 4 字节：

```txt
偏移  长度  类型    名称
0     4     u32be   unicode
```

- 这里只保存没有被连续区覆盖的离散码点
- 离散项按升序保存
- 不允许重复码点

### 字形描述项

每个 glyph 使用固定 14 字节描述项：

```txt
偏移  长度  类型    名称
0     2     u16be   adv_w
2     2     u16be   box_w
4     2     u16be   box_h
6     2     i16be   x_ofs
8     2     i16be   y_ofs
10    4     u32be   bitmap_offset
```

字形顺序：

1. 全部连续区 glyph
2. 全部离散补充 glyph

度量语义：

- `adv_w` 表示水平前进宽度，单位像素
- `x_ofs` 表示 bitmap 左上角相对笔位置的 X 偏移
- `y_ofs` 表示 bitmap 左上角相对当前行顶的 Y 偏移
- `bitmap_offset` 相对 `bitmap_data` 起始位置
- 空白 glyph 可以使用 `box_w = 0` 和 `box_h = 0`
- 对空白 glyph，`row_stride = 0`，`bitmap_size = 0`
- 对空白 glyph，不读取 bitmap，仅按 `adv_w` 前进

推荐的 FreeType 映射：

```txt
adv_w = round(advance.x / 64)
box_w = bitmap.width
box_h = bitmap.rows
x_ofs = bitmap_left
y_ofs = baseline - bitmap_top
```

运行时绘制位置：

```txt
glyph_x = pen_x + x_ofs
glyph_y = line_top + y_ofs
```

### Bitmap 数据

各 glyph 的 bitmap 紧密存放，glyph 之间不做额外对齐。

```txt
row_stride = (box_w * bpp + 7) / 8
bitmap_size = row_stride * box_h
```

像素打包规则：

- `1bpp`：1 字节存 8 像素，最高位对应最左像素
- `2bpp`：1 字节存 4 像素，依次放入 `7:6`、`5:4`、`3:2`、`1:0`
- `4bpp`：1 字节存 2 像素，高 4 位在前
- `8bpp`：1 字节存 1 像素

### 字符集归一化

导出前必须先归一化字符集：

- `ranges` 先排序
- 重叠或相邻的 `ranges` 自动合并
- `chars` 按 Unicode 码点去重
- 已被 `ranges` 覆盖的 `chars` 自动从离散表里剔除
- 最终离散码点按升序输出
- 不做 NFC、NFKC 等 Unicode 规范化

最终字形总数：

```txt
glyph_count = 所有连续区码点总数 + sparse_count
```

### 运行时查表规则

推荐的运行时查表顺序：

1. 先扫描 `range_table`
2. 如果码点落在某个连续区内，直接计算 glyph 下标
3. 否则搜索 `sparse_unicode_table`
4. 如果找到，则映射到离散 glyph 区
5. 如果仍未找到，则返回缺字

连续区命中公式：

```txt
glyph_index = 前面所有 range 长度之和 + (codepoint - start_unicode)
```

离散区命中公式：

```txt
glyph_index = total_range_codepoints + sparse_index
```

区间长度公式：

```txt
range_length = end_unicode - start_unicode + 1
```

外挂模式建议流程：

1. 先用内部 `range_table / sparse_unicode_table` 找到 `glyph_index`
2. 到外挂 `bin` 的 `glyph_desc_table` 读取对应描述项
3. 再根据 `bitmap_offset` 找到 `bitmap_data`
4. 解码并绘制

### CRC 规则

第一版固定使用 `CRC-32/IEEE`：

```txt
poly   = 0x04C11DB7
init   = 0xFFFFFFFF
refin  = true
refout = true
xorout = 0xFFFFFFFF
```

存储方式：

- `crc32` 保存在 `bin[2:5]`
- 按大端存储

校验范围：

```txt
bin[6 .. end]
```

也就是说：

- 前 2 字节标识不参与 CRC
- `crc32` 字段本身不参与 CRC

推荐两种校验策略：

- 快速校验：
  - 检查 `bin[0] == 0x02`
  - 检查 `bin[1] == 0x00`
  - 读取 `bin[2:5]`
  - 与内部 `blob_crc32` 比较
- 完整校验：
  - 先执行快速校验
  - 再对 `bin[6 .. blob_size - 1]` 计算 CRC
  - 与内部 `blob_crc32` 比较

### 渲染与量化规则

所有 `1bpp / 2bpp / 4bpp / 8bpp` 共用同一条流程：

1. 先渲染为 8 位灰度
2. 再量化到目标 `bpp`
3. 最后按上面的规则打包像素

量化公式：

```txt
maxv  = (1 << bpp) - 1
level = (gray * maxv + 127) / 255
```

## 说明

- 当前版本以 Windows 为主
- 使用预编译 exe 时，用户无需额外安装 FreeType
- 英文说明见 `README.md`

## 发布覆盖

- 预编译产物：面向 Windows x64 的 `font2c.exe`（MSYS64 工具链）
- 已在 Windows 10/11 下验证 `font2c.exe`
- 源码构建：`builder/build_win.cmd` 和 `builder/build_posix.sh`
- Windows 构建若检测到 `builder/icon_64x64.ico`，会自动嵌入到 exe

## 常见问题

- `no .json files found in input` → 确认 `input/` 里有 `.json` 配置
- `cannot find font file` → 检查 `fonts/` 以及系统字体目录里是否存在指定文件名
- `missing glyph U+xxxx` → 配置中请求的码点不在当前字体里
- 运行后 `output/` 为空 → 确认使用了 `-o output` 或 `output` 已创建

## 发布目录示意

```txt
font2c/
├─ font2c.exe
├─ fonts/
├─ input/
└─ output/
```
