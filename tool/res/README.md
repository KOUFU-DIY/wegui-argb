# tool/res — WeGui 资源工作区

demo 图片与字库的唯一维护入口：素材放进来，一键脚本产出全部构建工件，
三个构建目标（Simulator / STM32F103 / STM32F030）只消费 `out/` 里的产物。

## 一键更新

```cmd
update_resources.cmd
```

流程：清 `build/` → [font2c scan 字符集回写（等 scan 子命令就位后启用）] →
font2c 字库 → img2bin_pack 图片取模 → 归一化（补 6 字节 v2 头 + 角色短名）→
生成 `out/res_images.c/.h`（内部图数组）→ bin2c 合并（图 + 字库 bin）→ 尺寸汇总。

## 目录

- `input2raw/` — RGB565 无压缩图片源（img_ex 旋转缩放只认这种）
- `input2indexqoi/` — RGB565 indexQOI 压缩图片源
- `alpha_indexqoi/` — ARGB8565 indexQOI 带透明图片源（映射见 img2bin_pack.json 的 folders）
- `font_cfgs/` — font2c 配置（demo 角色命名，symbol 与文件名一致）
- `prep/` — 素材预处理脚本（记录 G:\gif 来源，可重跑换图）
- `extra_chars.txt` — 字符集扫描的兜底补充字符（运行时拼接的文字写这里）
- `build/` — 中间产物，脚本每次清空（已 gitignore）
- `out/` — 最终产物（进版本库）：
  - `res_images.c/.h` — 内部 flash 图片数组（含 v2 头，直接参与编译）
  - `merged_bin.bin/.c/.h` — 外挂 flash 合并资源包（bin 烧录 / .c 供模拟器 / .h 地址表）
  - `font/` — 字库 `demo_*.c/.h/.bin`

## 资源角色表

| 角色 | 规格 | 用途 |
|---|---|---|
| demo_sprite | 64x80 RGB565 raw | img_ex 旋转、imgbtn、showcase（内部数组） |
| demo_qoi | 96x54 RGB565 indexQOI | demo_img 主图（内部数组） |
| demo_overlay | 80x80 ARGB8565 indexQOI 原生透明 | demo_img 透明度呼吸叠加贴纸（内部数组） |
| demo_alpha | 80x80 ARGB8565 indexQOI 原生透明 | demo_img 浮动图标、flash demo（内部数组 + 外挂） |
| demo_raw | 128x64 RGB565 raw | 仅外挂（flash demo RAW 展示） |
| demo_cat | 128x160 RGB565 indexQOI | 仅外挂（flash demo 竖图） |
| demo_ascii_16 | ASCII 16px 4bpp internal | 默认 UI 字体（Core 的 we_font_consolas_18 宏） |
| demo_cjk_16 | 中文 16px 4bpp external | bin 进合并包；.c 另编译进模拟器（showcase 用） |
| demo_cjk_24 | 中文 24px 2bpp external | bin 进合并包 |
| demo_ime_16 | IME 一级字库 16px internal | 仅模拟器 preview（ime_pinyin） |

外挂合并包内容 = 全部 6 张图 + `demo_cjk_16.bin` + `demo_cjk_24.bin`，
符号形如 `DEMO_RAW_ADDR` / `DEMO_CJK_16_ID`（见 out/merged_bin.h）。

## 过渡开关

`update_resources.ps1` 顶部 `$IMG2BIN_HAS_HEADER`：

- `$false`（当前）：img2bin 六工具输出纯 bin，脚本负责补 6 字节 v2 信息头
- `$true`：img2c 默认带头版本（--header wegui 默认开）就位后改为 true，脚本跳过补头

v2 头契约见 `Core/image_res.h`：`[00][格式码][宽H][宽L][高H][高L]`，宽高恒大端。
