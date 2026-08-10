# 工具说明

## 特性

- 双击运行
- 拖拽图片或目录到 `exe`
- `--input <file-or-dir>`
- `--output <dir>`
- `--format <name>`
- `--formats <all|fmt1,fmt2,...>`
- `--little-endian`
- `--bg-color <RRGGBB>`
- `--help`
- `--info`
- `--list-formats`

默认值：

- 默认格式：`rgb565`
- 默认字节序：大端
- 默认输入目录：`<exe_dir>\input`
- 默认输出目录：`<exe_dir>\output`
- 默认背景色：`000000`

## 1. img2bin_raw.exe

用途：无压缩输出。

输出命名：

```text
<原图名>_<像素格式>_raw_<be|le>_<宽>x<高>.bin
```

示例：

```powershell
.\img2bin_raw.exe --format rgb565
.\img2bin_raw.exe --formats all
.\img2bin_raw.exe --input .\demo.png --output .\out --format argb8888
.\img2bin_raw.exe --input .\icon.png --output .\out --format a4
```

特有能力：Alpha 蒙版格式 `a8 / a4 / a2 / a1`（只保存透明度通道，供 GUI 运行时染色）**只有本工具支持**。其余五个工具显式点名这些格式会报错退出（码 1），`--formats all` 会自动跳过。蒙版的行打包规则见[像素格式说明](README-formats.md)。

## 2. img2bin_indexqoi.exe

用途：索引 QOI 压缩输出。

特点：

- 在指定索引点取消压缩，直接写原始像素块
- 在文件开头附加索引头和索引表
- 便于快速跳转到局部像素位置开始解码

输出命名：

```text
<原图名>_<像素格式>_indexqoi_<be|le>_<宽>x<高>.bin
```

额外参数：

- `--index-interval <count>`

说明：

- 不传 `--index-interval` 时，默认使用图片宽度作为索引间隔
- 例如图片宽度为 `36`，则默认每 `36` 个像素建立一个索引

示例：

```powershell
.\img2bin_indexqoi.exe --format argb8888
.\img2bin_indexqoi.exe --format argb8888 --index-interval 512
.\img2bin_indexqoi.exe --formats rgb565,argb8888 --index-interval 128
```

## 背景色参数什么时候有用

以下格式不带 Alpha，因此透明区域会先和背景色混合：

- `RGB888`
- `RGB565`
- `RGB332`

示例：

```powershell
.\img2bin_raw.exe --format rgb565 --bg-color FFFFFF
```

## 大端和小端

默认是大端，也就是文件名里的 `be`。

如果使用 `--little-endian`，文件名里会变成 `le`。

示例：

```powershell
.\img2bin_raw.exe --format rgb565 --little-endian
```

输出文件会变成类似：

```text
demo_rgb565_raw_le_128x64.bin
```

## 查询工具元数据

每个工具都支持：

```powershell
.\img2bin_raw.exe --info
```

可返回：

- 工具名
- 版本号
- 算法类型
- 默认格式
- 支持的输入格式
- 支持的像素格式
- 输出命名模板
- 参数元数据

这部分主要给后续 GUI 或自动化程序集成使用。
