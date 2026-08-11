# img2bin_raw 使用说明

无压缩取模工具：把 PNG/BMP/JPG/JPEG 图片转换为按目标像素格式逐像素打包的 `.bin`。输出可 O(1) 随机访问任意像素，是所有压缩算法体积率的对照基准（体积率恒 100%），也是六个取模工具中唯一支持 Alpha 蒙版格式（`a8/a4/a2/a1`）的一个。

## 快速开始

- **双击运行**：第一次双击会在 exe 旁自动创建 `input`、`output` 文件夹；把图片放进 `input` 再双击一次，结果出现在 `output`（默认 `rgb565`、大端）
- **拖拽**：把图片或文件夹直接拖到 `img2bin_raw.exe` 上
- **命令行**：

```powershell
.\img2bin_raw.exe --input .\demo.png --output .\out --format argb8888
```

## 参数

| 参数 | 说明 |
| --- | --- |
| `--input <file-or-dir>` | 输入文件或目录（默认 `<exe_dir>\input`） |
| `--output <dir>` | 输出目录（默认 `<exe_dir>\output`） |
| `--format <name>` | 单一像素格式（默认 `rgb565`） |
| `--formats <all\|f1,f2,...>` | 一次输出多种格式 |
| `--little-endian` | 小端输出（默认大端） |
| `--bg-color <RRGGBB>` | 非 Alpha 格式的透明区域先与该背景色混合（默认 `000000`） |
| `--manifest` | 在输出目录写 `img2bin_raw-manifest.json` 运行清单（默认关闭） |
| `--info` / `--list-formats` / `--help` | 元数据 JSON / 格式清单 / 帮助 |

每写出一个文件，stdout 报告一行体积率（raw 恒为 100%，通用头 6 字节两边不计入）：

```text
Wrote out\demo_rgb565_raw_be_128x64.bin (16390 bytes, payload 16384 / raw 16384 = 100.0%)
```

退出码：`0` 成功；`1` 参数错误；`2` 输入错误；`3` 编码错误；`4` 写入错误；`5` 内部错误；`6` 批处理部分失败。错误详情以单行 JSON 写到 stderr（字段：`code`、`exit_code`、`message.zh_cn/en`、`file`、`detail`、`stage`）。

## 输出文件结构

### 文件命名（机器接口）

```text
<原图名>_<像素格式>_raw_<be|le>_<宽>x<高>.bin
```

字节序**不存在于文件内部**，只体现在文件名的 `be|le` 段（或由工程约定提供）。

### 6 字节通用资源头

所有 `.bin` 都以 6 字节头开始，之后紧跟 payload：

```text
byte0   = 资源类型，恒 0x00（图片）
byte1   = 格式码：高 nibble 压缩算法，低 nibble 像素格式
byte2-3 = 宽，恒大端
byte4-5 = 高，恒大端
```

- 算法 nibble：`raw=0x0`（其余工具：`rle=0x1 imprle=0x2 qoi=0x3 indexqoi=0x4 qoif=0x5`）
- 像素格式 nibble：`RGB565=0x0 RGB888=0x1 RGB332=0x4 ARGB8888=0x5 ARGB6666=0x6 ARGB4444=0x7 ARGB8565=0x8 ARGB2222=0x9 RAGB5155=0xA A8=0xB A4=0xC A2=0xD A1=0xE`（`0x2/0x3/0xF` 保留不用）
- 宽高上限 65535；宽高恒大端，与 `--little-endian` 无关

## 像素来源与预处理（编码语义）

1. 输入图片统一解码为每像素 8bit 的 `R,G,B,A`；没有 Alpha 通道的输入（如 JPG）视为 `A=255`
2. **非 Alpha 目标格式**（`rgb888/rgb565/rgb332`）先把透明区域与背景色混合（整数运算，除法向下取整）：

   ```text
   out = (src * A + bg * (255 - A) + 127) / 255
   ```

3. 8bit 通道量化到 n 位（整数运算）：

   ```text
   Qn = (v8 * ((1 << n) - 1) + 127) / 255
   ```

4. 例外：`RAGB5155` 的 1bit Alpha 不用上式，而是阈值 `A1 = (A8 >= 128) ? 1 : 0`
5. 带 Alpha 的目标格式不做背景混合，RGB 通道直接量化原始值

## 彩色格式打包规则（9 种）

像素顺序恒为**从左到右、从上到下**。大端布局如下；**小端 = 把单个像素的全部字节整体反转**；单字节格式（`rgb332/argb2222`）两种模式输出一致。

| 格式 | 字节数 | 大端布局（bit 高→低） |
| --- | --- | --- |
| `argb8888` | 4 | `byte0=A8, byte1=R8, byte2=G8, byte3=B8` |
| `argb6666` | 3 | `byte0=A[5:0]R[5:4]`，`byte1=R[3:0]G[5:2]`，`byte2=G[1:0]B[5:0]` |
| `argb4444` | 2 | `byte0=A[3:0]R[3:0]`，`byte1=G[3:0]B[3:0]` |
| `argb2222` | 1 | `A[1:0] R[1:0] G[1:0] B[1:0]` |
| `argb8565` | 3 | `byte0=A8`，`byte1-2 = (R5<<11)|(G6<<5)|B5` 大端 |
| `rgb888` | 3 | `byte0=R8, byte1=G8, byte2=B8` |
| `rgb565` | 2 | `(R5<<11)|(G6<<5)|B5` 大端 |
| `rgb332` | 1 | `(R3<<5)|(G3<<2)|B2` |
| `ragb5155` | 2 | `(R5<<11)|(A1<<10)|(G5<<5)|B5` 大端 |

小端示例：`argb8888` 变为 `B,G,R,A`；`argb8565` 变为 `565低字节, 565高字节, A8`；`rgb565/ragb5155` 低字节在前；`argb6666/rgb888` 三字节反转。

## RAW payload（彩色格式）

```text
payload = 像素0字节 + 像素1字节 + ... + 像素(宽×高-1)字节
```

- payload 大小 = `宽 × 高 × 每像素字节数`，多一字节或少一字节都视为非法
- 定位第 N 个像素：`offset = N × 每像素字节数`——RAW 是唯一可 O(1) 随机访问任意像素的算法

## Alpha 蒙版格式（a8/a4/a2/a1，本工具独有）

只保存 Alpha 通道（取输入图的 A，忽略 `--bg-color`），供 GUI 在运行时用前景色染色。量化：`(A8 × ((1<<bpp)-1) + 127) / 255`。

**按行打包**（与彩色格式的逐像素打包不同）：

- 行字节数 `row_stride = (宽 × bpp + 7) / 8`，payload 大小 = `高 × row_stride`
- 每行从新字节开始；**MSB-first**：最左像素放在字节高位
  - `a8`：1 像素/字节；`a4`：2 像素/字节（左像素在高 nibble）；`a2`：4 像素/字节（左像素在 bits7..6）；`a1`：8 像素/字节（左像素在 bit7）
- 行尾不足一字节的位补 `0`
- 无字节序维度：`be`/`le` 输出逐字节一致

解码还原到 8bit Alpha 的唯一规则：

```text
a8: A = byte          a4: A = (v << 4) | v
a2: A = v * 0x55      a1: A = v ? 255 : 0
```

其余五个工具不支持这四种格式：显式点名报参数错误（退出码 1），`--formats all` 自动跳过。

## 使用示例

```powershell
.\img2bin_raw.exe --format rgb565
.\img2bin_raw.exe --formats all
.\img2bin_raw.exe --input .\icons --output .\out --format a4 --manifest
.\img2bin_raw.exe --format rgb565 --bg-color FFFFFF --little-endian
```
