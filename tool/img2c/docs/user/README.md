# 用户总览

这套工具用于把常见图片转换为嵌入式场景可直接使用的 `bin` 资源。

当前版本特点：

- 每个 `exe` 对应一种压缩算法
- 六个取模工具输出 `.bin`：6 字节通用资源头（类型 + 算法/格式码 + 宽高）+ 算法 payload
- 统筹管理器 `img2bin_pack.exe` 可批量调度全部工具，并把 `.bin` 汇总生成 `.c/.h`
- 默认大端模式
- 单个工具默认从 `input` 目录读取；统筹管理器按 `input2<算法>` 文件夹分发
- 支持双击运行、拖拽输入和命令行调用
- 批处理模式会输出 `manifest.json`

## 文档导航

- [统筹管理器说明](README-pack.md)
- [工具说明](README-tools.md)
- [像素格式说明](README-formats.md)
- [解码编写说明](README-decoder.md)（含现成的 C99 参考解码器）
- [协议与验证说明](README-protocol.md)
- [接口 Schema 说明](README-schema.md)（`--info` 与 manifest 的逐字段文档）

## 各个程序分别做什么

| 程序 | 用途 |
| --- | --- |
| `img2bin_pack.exe` | 统筹管理器：按 `input2<算法>` 文件夹批量调度下面六个工具，并生成 `.c/.h` |
| `img2bin_raw.exe` | 无压缩输出 |
| `img2bin_imprle.exe` | 改进 RLE 压缩输出 |
| `img2bin_rle.exe` | 原始 RLE 压缩输出 |
| `img2bin_qoi.exe` | 原始 QOI 压缩输出 |
| `img2bin_qoif.exe` | 原始 QOI（无字典）压缩输出 |
| `img2bin_indexqoi.exe` | 索引 QOI 压缩输出 |

发布目录中，六个取模工具在 `windows\tools\`，统筹管理器在 `windows\`。
推荐的批量用法见 [统筹管理器说明](README-pack.md)；下面是单个工具的用法。

## 默认使用方式

1. 把任意一个 `exe` 放在你想工作的目录
2. 双击运行
3. 工具会自动创建 `input` 和 `output`
4. 把图片放进 `input`
5. 再次运行，结果会出现在 `output`

默认规则：

- 输入目录：`<exe_dir>\input`
- 输出目录：`<exe_dir>\output`
- 默认像素格式：`RGB565`
- 默认字节序：大端

## 拖拽与命令行

可以直接把以下对象拖到 `exe` 上：

- 单张图片
- 多张图片
- 一个文件夹
- 多个文件夹

也可以用命令行：

```powershell
.\img2bin_raw.exe --format rgb565
.\img2bin_qoi.exe --input .\demo.png --output .\out --format argb8888
.\img2bin_indexqoi.exe --formats all --index-interval 512
```

## 你最需要先知道的几点

- 六个取模工具只输出 `.bin`（6 字节通用资源头 + payload），不输出结构体文本、数组文本
- 需要 `.c/.h` 数组时用统筹管理器 `img2bin_pack.exe`，见 [统筹管理器说明](README-pack.md)
- 没有 GUI
- 若目标格式不带 Alpha，透明区域会先按背景色混合，再转目标格式
- `img2bin_indexqoi.exe` 默认索引间隔是图片宽度，可用 `--index-interval` 改
- 如果你要自己写解码器，请重点看 [解码编写说明](README-decoder.md) 和 [像素格式说明](README-formats.md)

## 输出命名规则

统一使用下划线命名：

```text
<原图名>_<像素格式>_<算法>_<be|le>_<宽>x<高>.bin
```

例如：

```text
screen_rgb565_raw_be_36x45.bin
screen_argb8888_qoi_be_36x45.bin
screen_argb8888_indexqoi_be_36x45.bin
```

## 批处理结果文件

当你处理目录、拖拽多个输入，或显式进行批处理时，工具会在输出目录中生成结果清单：

- `img2bin_raw-manifest.json`
- `img2bin_imprle-manifest.json`
- `img2bin_rle-manifest.json`
- `img2bin_qoi-manifest.json`
- `img2bin_qoif-manifest.json`
- `img2bin_indexqoi-manifest.json`

它会记录：

- 输入文件
- 成功或失败状态
- 输出文件列表
- 尺寸
- 错误信息

## 输入图片格式

- `PNG`
- `BMP`
- `JPG`
- `JPEG`

## 输出像素格式

- `ARGB8888`
- `ARGB6666`
- `ARGB4444`
- `ARGB2222`
- `ARGB8565`
- `RGB888`
- `RGB565`
- `RGB332`
- `RAGB5155`
