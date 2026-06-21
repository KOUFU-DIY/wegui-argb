# WeGui-ARGB · V0.2

轻量级嵌入式 GUI 框架，面向多种 MCU / SoC 平台，同时提供 SDL2 PC 模拟器。

> 当前版本 **V0.2**（版本宏定义见 `we_user_config.h` 的 `WE_GUI_VERSION`）。

## 特性

- 小内存占用的局部帧缓冲（PFB）渲染
- 脏矩形刷新策略（全屏 / 单包围盒 / 多脏矩形）
- 统一的 GUI tick / task / timer 调度
- 内置控件与完整 demo 工程
- 支持外部 Flash 图片与字体资源
- 同时支持 STM32F103 / STM32F030 硬件目标与 SDL2 模拟器目标

## 资源占用（实测）

| 目标 | ROM | RAM | 口径 |
|---|---|---|---|
| STM32F103（Cortex-M3，72 MHz） | **23.02 KB** | **6.13 KB** | dropdown 最小可交互应用，Keil `.map` 实测 |
| STM32F030（Cortex-M0，48 MHz，DMA 双缓冲） | **22.26 KB** | **6.09 KB** | 同上（= 下方逐 demo 表第 19 号） |

- 280×240 RGB565，PFB 8 行（4.48 KB，仅整屏显存的 3.3%）；ROM 含 5.9 KB 字体资产 + LCD/输入/存储端口 + 启动代码，GUI 库本体约 10 KB
- 零 malloc、零浮点路径，Cortex-M0（无硬件除法、无 FPU）原生可用

### 各 demo 单独占用（STM32F030 / Cortex-M0 实测）

每个 demo 单独编译（`DEMO_ID` 选中该 demo，链接器 `--gc-sections` 剔除未引用控件）后的 Keil `.map` 实测。每个 demo = **该主控件 + `label`（标题/FPS）**，复合类额外含子控件；第 1 号 `label` 即"最小 GUI + 字体 + 端口 + 启动"的底座。

| DEMO_ID | demo / 主要控件 | ROM | RAM | 备注 |
|---|---|---|---|---|
| 1 | label | 17.0 KB | 5.96 KB | 基准底座（含 5.9 KB 字体资产） |
| 2 | btn | 18.7 KB | 6.04 KB | |
| 3 | img | 37.0 KB | 6.02 KB | ⚠ 含 demo 内嵌 RGB 图片资产 ~18 KB |
| 4 | img_ex（旋转/缩放） | 28.3 KB | 5.98 KB | ⚠ 含 demo 内嵌图片资产 ~10 KB |
| 5 | arc | 19.8 KB | 6.02 KB | |
| 6 | group（含子控件） | 20.2 KB | 6.43 KB | |
| 7 | slideshow（group + 分页） | 25.6 KB | 6.95 KB | RAM 最高（多页子控件状态） |
| 8 | concentric arc（同心圆弧） | 19.7 KB | 6.06 KB | |
| 9 | checkbox | 20.0 KB | 6.33 KB | |
| 10 | label_ex（旋转缩放文字） | 18.7 KB | 6.01 KB | |
| 11 | chart（实时波形） | 19.1 KB | 6.53 KB | |
| 12 | toggle | 19.4 KB | 6.29 KB | |
| 13 | progress（含 btn） | 20.8 KB | 6.13 KB | |
| 14 | msgbox（含 btn） | 22.2 KB | 6.13 KB | |
| 15 | flash img（img_flash） | 19.8 KB | 6.06 KB | 需外挂 SPI flash 存储 |
| 16 | flash font（font_flash） | 19.6 KB | 6.18 KB | 需外挂 SPI flash 存储 |
| 17 | slider | 20.1 KB | 6.18 KB | |
| 18 | scroll_panel（含子控件） | 22.2 KB | 6.69 KB | |
| 19 | dropdown | 22.3 KB | 6.09 KB | 同上方汇总表 F030 行 |
| 20 | stepper | 20.0 KB | 6.23 KB | |
| 21 | indicator | 19.4 KB | 6.36 KB | |

> ROM = Code + RO-data + RW-data；RAM = RW-data + ZI-data（含 4.48 KB PFB 显存，各 demo 共有）。`img`/`img_ex` 的 ROM 偏大是 demo 内嵌了未压缩图片资产、与控件代码无关。`showcase`（仅模拟器、需 800×480）无法烧录到 MCU，故不在此表；其余 1..21 均可单独烧录到 F030。F103 同口径约再大 0.5~0.8 KB（多一份外挂 flash 端口实现）。

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
- `indicator` 圆形状态灯通过中央动画引擎（`we_anim_t`，不占 task 槽）做亮灭过渡，可选外发光晕
- `msgbox` 为水平居中的模态弹窗，采用透明度淡入/淡出 + 解析式圆角面板
- 所有带动画的控件（toggle/progress/indicator/msgbox/slideshow/scroll_panel/dropdown）统一走**中央动画引擎**，不占用 GUI task 槽、数量无上限、空闲自动摘链
- 容器透明度可向子控件级联传播（group/slideshow/scroll_panel）；裸 `group` 会把触摸事件转发给内部子控件，全透明 group 不拦截输入

## 仓库结构

- `Core/` — GUI 内核、渲染与控件实现
- `Demo/` — 各控件 demo 与模板端口文件
- `STM32F103/` — STM32F103 硬件入口、Keil 工程与 LCD/输入/外挂 Flash 端口层
- `STM32F030/` — STM32F030 硬件入口、Keil 工程与 LCD/输入端口层
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

### 2. STM32F103 / STM32F030 — Keil MDK-ARM AC5

命令行构建示例：

```powershell
UV4.exe -r "STM32F103/MDK-ARM/Project.uvprojx" -t "WeGui_ARGB"
UV4.exe -r "STM32F030/MDK-ARM/Project.uvprojx" -t "STM32F030"
```

说明：
- `UV4.exe` 的实际路径取决于你的本地 Keil 安装位置
- 构建日志：F103 在 `STM32F103/MDK-ARM/Objects/Project.build_log.htm`，F030 在 `STM32F030/MDK-ARM/STM32F030/STM32F030.build_log.htm`

当前代码状态下，F103 / F030 工程均已验证可编译通过：`0 Error(s), 0 Warning(s)`

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
