# img2bin 用户入口

这是一个面向嵌入式图片资源取模的工具集项目。当前提供的 Windows 程序：

- `windows\img2bin_pack.exe` — 统筹管理器：批量调度取模工具，并汇总生成 `.c/.h`
- `windows\tools\` 下的六个取模工具（输出 = 6 字节通用资源头 + 算法 payload 的 `.bin`）：
  - `img2bin_raw.exe`
  - `img2bin_imprle.exe`
  - `img2bin_rle.exe`
  - `img2bin_qoi.exe`
  - `img2bin_qoif.exe`
  - `img2bin_indexqoi.exe`

## 目录结构

```text
img2c/
├─ windows/
│  ├─ img2bin_pack.exe       # 统筹管理器
│  ├─ img2bin_pack.json      # 默认配置（root 指向仓库根目录）
│  ├─ tools/                 # 六个取模 exe
│  └─ examples/
│     ├─ run_batch.cmd       # 自动批处理示例：处理根目录 input2* 文件夹
│     └─ img2bin_pack.example.json
├─ input2raw/  input2imprle/  input2rle/
├─ input2qoi/  input2qoif/    input2indexqoi/   # 按算法分类放图片
├─ output/                    # 转换结果（.bin + .c/.h + manifest）
├─ docs/user/                 # 用户文档
└─ builder/                   # C 源码工程（CMake）
```

## 快速开始（推荐：统筹管理器）

1. 把图片按目标算法放进根目录的 `input2raw`、`input2qoi` 等文件夹
2. 双击 `windows\img2bin_pack.exe`（或 `windows\examples\run_batch.cmd`）
3. 结果输出到根目录 `output\`：
   - 每张图片的 `.bin`
   - 汇总的 `img_resources.c` / `img_resources.h`
   - `img2bin_pack-manifest.json` 运行清单

像素格式、字节序、索引间隔、自定义文件夹映射等通过 `windows\img2bin_pack.json` 配置，
详见 [统筹管理器说明](docs/user/README-pack.md)。

## 单个工具用法

每个取模工具仍可独立使用：双击、拖拽图片/文件夹到 exe 上，或命令行：

```powershell
.\windows\tools\img2bin_raw.exe --format rgb565
.\windows\tools\img2bin_imprle.exe --format argb8888
.\windows\tools\img2bin_indexqoi.exe --format argb8888 --index-interval 512
```

## 用户文档

- [用户总览](docs/user/README.md)
- [统筹管理器说明](docs/user/README-pack.md)
- [工具说明](docs/user/README-tools.md)
- [像素格式说明](docs/user/README-formats.md)
- [解码编写说明](docs/user/README-decoder.md)
- [协议与验证说明](docs/user/README-protocol.md)
- [接口 Schema 说明](docs/user/README-schema.md)

## 参考解码器

[builder/src/decoder/](builder/src/decoder/) 提供纯 C99、零依赖、不用动态内存的参考解码器
`img2bin_decode.c/.h`（发布包里在 `decoder\` 目录），覆盖全部六种算法 × 九种像素格式 × 大小端，
含 indexQOI 跳转解码接口。测试套件对每种组合做"编码 → 解码 → 与 RAW 逐字节比对"回环验证。

## 当前支持

- 输入格式：`PNG`、`BMP`、`JPG`、`JPEG`
- 像素格式：`ARGB8888`、`ARGB6666`、`ARGB4444`、`ARGB2222`、`ARGB8565`、`RGB888`、`RGB565`、`RGB332`、`RAGB5155`
- 默认行为：`RGB565`、大端、统筹管理器按 `input2<算法>` 文件夹分发，输出到根目录 `output`

## 协议与验证

`.bin` 输出统一为 **6 字节通用资源头（类型 + 算法/格式码 + 宽高）+ 算法 payload**，
`.c/.h` 由统筹管理器在上层生成，数组内容与 `.bin` 逐字节一致。用户使用、程序集成和
解码编写，都以"当前工具实际输出 + `--info` 元数据 + 当前用户文档"为准。
详细说明见 [协议与验证说明](docs/user/README-protocol.md)。

## 从源码构建

```powershell
.\build_release.ps1          # 完整发布构建（需要 VS 2022 + CMake + Ninja）
.\build_release.ps1 -SkipTests
```
