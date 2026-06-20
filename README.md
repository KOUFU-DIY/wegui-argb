# WeGui-ARGB

轻量级嵌入式 GUI 框架，面向多种 MCU / SoC 平台，同时提供 SDL2 PC 模拟器。

## 特性

- 小内存占用的局部帧缓冲（PFB）渲染
- 脏矩形刷新策略（全屏 / 单包围盒 / 多脏矩形）
- 统一的 GUI tick / task / timer 调度
- 内置控件与完整 demo 工程
- 支持外部 Flash 图片与字体资源
- 同时支持 STM32F103 硬件目标与 SDL2 模拟器目标

## 资源占用（实测）

| 目标 | ROM | RAM | 口径 |
|---|---|---|---|
| STM32F103（Cortex-M3，72 MHz） | **22.00 KB** | **6.10 KB** | dropdown 最小可交互应用，Keil `.map` 实测 |
| STM32F030（Cortex-M0，48 MHz，DMA 双缓冲） | **21.75 KB** | **6.07 KB** | 同上 |

- 280×240 RGB565，PFB 8 行（4.48 KB，仅整屏显存的 3.3%）；ROM 含 5.9 KB 字体资产 + LCD/输入/存储端口 + 启动代码，GUI 库本体约 10 KB
- 零 malloc、零浮点路径，Cortex-M0（无硬件除法、无 FPU）原生可用
- 逐组件构成与复现命令见 [Report/07_footprint_实测.md](Report/07_footprint_实测.md)

## 当前控件状态

当前仓库已包含并维护以下主要控件：

- label
- btn
- img
- img_ex
- arc
- group
- slideshow
- checkbox
- label_ex
- chart
- toggle
- progress
- msgbox
- img_flash
- font_flash
- slider
- scroll_panel
- dropdown
- stepper
- indicator

其中：

- `toggle` 的轨道和滑块已统一复用公用解析式圆角矩形填充函数
- `checkbox` 的方框绘制已统一复用公用解析式圆角填充函数
- `chart` 的波形主体与柔边绘制思路参考自 Arm-2D，但实现已按 WeGui 的环形缓冲、脏矩形、PFB 裁剪与整数坐标体系重写
- `dropdown` 展开列表支持无级（像素级）拖拽滚动，滚动条按空闲时间自动淡出至常驻最低透明度
- `stepper` 数值用定点 int32 存储，按住可连续步进且不占用 timer 槽
- `indicator` 圆形状态灯通过每对象 task 做亮灭过渡，可选外发光晕

## 仓库结构

- `Core/` — GUI 内核、渲染与控件实现
- `Demo/` — 各控件 demo 与模板端口文件
- `STM32F103/` — STM32F103 硬件入口、Keil 工程与端口层
- `Simulator/` — SDL2 模拟器入口、SDL 端口与模拟器配置
- `tool/` — 图片、字库与外挂 Flash 相关工具链

## 快速开始

### 1. Simulator

推荐方式：通过仓库内置脚本构建，脚本会自动探测当前环境中的可用工具链。
优先使用 `ninja + gcc + g++`，找不到时回退到 `mingw32-make + gcc + g++`。

清理并重建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/build_sim.ps1" -Clean
```

普通构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/build_sim.ps1"
```

运行最新模拟器：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/run_latest_sim.ps1"
```

### 2. STM32F103 / Keil MDK-ARM AC5

命令行构建示例：

```powershell
UV4.exe -r "STM32F103/MDK-ARM/Project.uvprojx" -t "WeGui_ARGB"
```

说明：
- `UV4.exe` 的实际路径取决于你的本地 Keil 安装位置
- 构建日志默认在：`STM32F103/MDK-ARM/Objects/Project.build_log.htm`

当前代码状态下，STM32 工程已验证可编译通过：
- `0 Error(s), 0 Warning(s)`

## Demo 选择

当前 simple demo 共 **21 个**，三个目标（Simulator / STM32F103 / STM32F030）编号已统一：`1..21` 完全一致；Simulator 额外用 `0 = showcase`（全控件汇总，仅模拟器，需 800×480）。选择方式是**编译期宏**——改入口 `main` 顶部的 `#define DEMO_ID` 即可，只有选中的 demo 会被编译进去。

下表为统一后的 `DEMO_ID`：

| DEMO_ID | 内容 |
| --- | --- |
| 0  | showcase（全控件汇总，仅模拟器，需 800×480） |
| 1  | label |
| 2  | btn |
| 3  | img |
| 4  | img_ex |
| 5  | arc |
| 6  | group |
| 7  | slideshow |
| 8  | concentric arc |
| 9  | checkbox |
| 10 | label_ex |
| 11 | chart |
| 12 | toggle |
| 13 | progress |
| 14 | msgbox |
| 15 | flash img |
| 16 | flash font |
| 17 | slider |
| 18 | scroll_panel |
| 19 | dropdown |
| 20 | stepper |
| 21 | indicator |

> `0`（showcase）仅 Simulator 提供；STM32 无此项。未列编号回退到 `label`（Simulator）/ `btn`（STM32）。

### 修改方法
改对应入口 `main` 顶部的 `#define DEMO_ID` 数字即可：`Simulator/main_sim.c`、`STM32F103/main.c`、`STM32F030/main.c`。STM32 改完需重新编译 / 烧录。

## 构建与验证

推荐的最小验证方式：

- **Simulator**：编译并运行 `wegui_sim`，检查选定 demo 是否正常绘制与动画
- **STM32**：编译 Keil 工程并在板上运行选定 demo

## 资源与工具

- `tool/bin2c/` — 将多个 bin 资源合成为单个 bin，并可转换生成对应的 `.c` / `.h` 文件
- `tool/font2c/` — 字库生成工具
- `tool/STM32F103_ex_flash_download/` — 外挂 Flash 下载相关工具

## 许可证

本项目使用 Apache License 2.0，详见根目录 [LICENSE](LICENSE)。
