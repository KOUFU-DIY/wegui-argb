# 协议与验证说明

本页只说明“当前工具自己定义并已经实现”的协议，以及如何用本工具自己生成验证资料。  
阅读和实现解码器时，不需要依赖任何外部历史资料、旧样例或同行文档。

## 协议优先级

如果出现理解冲突，按以下优先级判断：

1. 当前工具实际输出的 `.bin`
2. 当前工具 `--info` 返回的元数据
3. 当前用户文档

对当前项目来说，这三项已经足够覆盖使用、集成和解码实现。

## 当前工具范围

当前已实现并可直接使用的工具有：

- `img2bin_raw.exe`
- `img2bin_imprle.exe`
- `img2bin_rle.exe`
- `img2bin_qoi.exe`
- `img2bin_qoif.exe`
- `img2bin_indexqoi.exe`

当前共同支持的像素格式：

- `ARGB8888`
- `ARGB6666`
- `ARGB4444`
- `ARGB2222`
- `ARGB8565`
- `RGB888`
- `RGB565`
- `RGB332`
- `RAGB5155`

当前工具输出：

- `.bin` 文件 = **6 字节通用资源头 + 算法 payload**
- 批处理 `manifest.json`

当前工具不输出：

- `.c/.h`
- 数组文本
- 结构体文本

补充：`.c/.h` 数组由上层的统筹管理器 `img2bin_pack.exe` 根据 `.bin` 文件生成（见 [统筹管理器说明](README-pack.md)），数组内容与 `.bin` 逐字节一致（含通用头）。

## 通用资源头

所有 `.bin` 输出（六种算法全部）都以 6 字节通用资源头开始：

| 偏移 | 字节 | 含义 | 取值 |
| --- | --- | --- | --- |
| 0 | 1 | 资源类型 | 恒 `0x00`（图片；`0x01` 预留给字库） |
| 1 | 1 | 格式码 | 高 nibble = 压缩算法，低 nibble = 像素格式 |
| 2~3 | 2 | 宽 | 恒大端 |
| 4~5 | 2 | 高 | 恒大端 |

算法 nibble：`raw=0x0`、`rle=0x1`、`imprle=0x2`、`qoi=0x3`、`indexqoi=0x4`、`qoif=0x5`。

像素格式 nibble：`RGB565=0x0`、`RGB888=0x1`、`RGB332=0x4`、`ARGB8888=0x5`、`ARGB6666=0x6`、`ARGB4444=0x7`、`ARGB8565=0x8`、`ARGB2222=0x9`、`RAGB5155=0xA`（`0x2/0x3` 属于旧枚举的 RGB555/RGB444，本工具不使用；`0xF` 保留给 OLED 点阵）。

要点：

- 头内**不含字节序**，字节序仍由文件名的 `be|le` 段或工程约定提供
- 通用头之后紧跟算法 payload；`indexQOI` 自己的 13 字节结构头位于通用头**之后**
- 宽高各占 16 位，上限 65535

## 怎样从工具里拿到协议信息

`--info` JSON 与各类 manifest 的逐字段说明见 [接口 Schema 说明](README-schema.md)。

## 1. 用 `--info` 获取机器可读元数据

每个工具都支持：

```powershell
.\img2bin_raw.exe --info
.\img2bin_indexqoi.exe --info
```

它会返回：

- 工具名
- 版本号
- 算法类型
- 默认格式
- 默认字节序
- 支持的输入格式
- 支持的像素格式
- 输出命名模板
- 参数元数据
- 退出码约定

如果你在做 GUI、自动化程序集成或下位机配套工具，这个 JSON 应该作为机器接口入口。

## 2. 用用户文档获取人工可读规则

建议按这个顺序看：

1. [用户总览](README.md)
2. [工具说明](README-tools.md)
3. [像素格式说明](README-formats.md)
4. [解码编写说明](README-decoder.md)

## 怎样自己生成验证资料

如果你要写解码器，最稳的做法不是去找历史样例，而是直接用当前工具生成你自己的验证集。

推荐做法：

1. 准备一张固定输入图片
2. 记录使用的工具、命令行、像素格式、字节序
3. 保留生成的 `.bin`
4. 如为批处理，保留对应 `manifest.json`
5. 如需程序侧检索，再保存一份 `--info` 输出

例如：

```powershell
.\img2bin_raw.exe --input .\demo.png --output .\out --format rgb565
.\img2bin_qoif.exe --input .\demo.png --output .\out --format argb8888
.\img2bin_indexqoi.exe --input .\demo.png --output .\out --format argb8888 --index-interval 128
.\img2bin_indexqoi.exe --info
```

这样你就能得到一组完全由当前工具生成、与当前版本协议一致的验证资料：

- 输入图片
- 输出 `.bin`
- 运行参数
- `manifest.json`
- `--info` JSON

## 建议保留哪些验证材料

对于每个需要对接的算法，建议至少保留：

- 一张原始输入图
- 一个目标 `.bin`
- 一份命令记录
- 一份 `--info` 输出

如果你要做批处理流程验证，再额外保留：

- `img2bin_<algo>-manifest.json`

如果你要做跳转解码验证，再额外保留：

- `indexQOI` 的不同 `--index-interval` 输出

## indexQOI 的当前协议结论

当前 `indexQOI` 规则以工具实现为准：

- 文件布局：6 字节通用资源头 + 13 字节 indexQOI 结构头 + 索引表 + `QOIF` payload
- 基于 `QOIF`
- 索引位置取消压缩，直接写原始像素块
- 非 Alpha 原始块使用 `0xFE`
- 带 Alpha 原始块使用 `0xFF`
- 默认索引间隔为图片宽度，可用 `--index-interval` 自定义
- 索引值相对 `QOIF payload` 起点，不是文件起点
- 当前实现没有尾部结束码

## 用户如何利用这份说明

如果你只是要正常使用工具：

- 先看 [用户总览](README.md)
- 再看 [工具说明](README-tools.md)

如果你要自己写解码器：

- 先看 [像素格式说明](README-formats.md)
- 再看 [解码编写说明](README-decoder.md)
- 然后用当前工具自己生成测试样例

如果你要做 GUI、自动化程序集成或批处理接入：

- 先读 `--info`
- 再结合 [工具说明](README-tools.md) 和 `manifest.json`
