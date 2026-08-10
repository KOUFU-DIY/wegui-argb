# WeGui-ARGB · V0.2.3

轻量级嵌入式 GUI 框架，面向多种 MCU / SoC 平台，同时提供 SDL2 PC 模拟器。

> 当前版本 **V0.2.3**（版本宏定义见 `we_user_config.h` 的 `WE_GUI_VERSION`）。

## 特性

- 小内存占用的局部帧缓冲（PFB）渲染
- 脏矩形刷新策略（全屏 / 单包围盒 / 多脏矩形）
- 统一的 GUI tick / timer / 中央动画调度
- 全局聚焦 + 按键导航（方向键空间就近移动 / Tab 环序 / OK 双沿按压 / 容器下钻，触摸与按键共存，纯触摸工程可整体编译剔除）
- 图片格式：RGB565 / ARGB8565（无压缩与索引 QOI）+ A1/A2/A4/A8 透明位图（前景色上色，同一份取模反复换色复用）
- 内置控件与完整 demo 工程；`tool/` 编号例程式资源流水线（字体/图片取模 → 外挂 bin 合并 → 烧录）
- 支持外部 Flash 图片与字体资源（模拟器直读 `merged_bin.bin`，与硬件"重烧"流程对应）
- 同时支持 STM32F103 / STM32F030 硬件目标与 SDL2 模拟器目标
- headless 基准哈希回归：67 项逐帧 CRC 金样（含 10 条交互轨迹脚本），一条命令全量校验

## 资源占用（实测）

| 目标 | ROM | RAM | 口径 |
|---|---|---|---|
| STM32F103（Cortex-M3，72 MHz） | **24.21 KB** | **6.10 KB** | dropdown 最小可交互应用（= 逐 demo 表第 19 号，F103 实测） |
| STM32F030（Cortex-M0，48 MHz，DMA 双缓冲） | **23.5 KB** | **6.06 KB** | 同口径（= 下方逐 demo 表第 19 号） |

- 280×240 RGB565，PFB 8 行（4.48 KB，仅整屏显存的 3.3%）；ROM 含约 3.5 KB 字体资产 + LCD/输入/存储端口 + 启动代码，GUI 库本体约 10 KB
- 共享配置实测口径：聚焦/按键导航（`WE_CFG_ENABLE_KEY_INPUT=1`）等默认功能全开——纯触摸工程关掉裁剪宏还能再瘦
- 零 malloc、零浮点路径，Cortex-M0（无硬件除法、无 FPU）原生可用

### 各 demo 单独占用（STM32F030 / Cortex-M0 实测）

每个 demo 单独编译（`DEMO_ID` 选中该 demo，链接器 `--gc-sections` 剔除未引用控件）后的 Keil `.map` 实测（V0.2.3 全表重测）。每个 demo = **该主控件 + `label`（标题/FPS）**，复合类额外含子控件；第 1 号 `label` 即"最小 GUI + 字体 + 端口 + 启动"的底座。

| DEMO_ID | demo / 主要控件 | ROM | RAM | 备注 |
|---|---|---|---|---|
| 1 | label | 17.3 KB | 5.84 KB | 基准底座（含约 3.5 KB 字体资产） |
| 2 | btn | 18.9 KB | 6.00 KB | |
| 3 | img | 52.8 KB | 5.91 KB | ⚠ 含 demo 内嵌图片资产 ~33 KB（indexqoi ×2 + argb8565 raw） |
| 4 | img_ex（旋转/缩放） | 28.6 KB | 5.86 KB | ⚠ 含 demo 内嵌 RGB565 raw 图 ~10 KB |
| 5 | arc | 20.1 KB | 5.90 KB | |
| 6 | group（含子控件） | 20.4 KB | 6.02 KB | |
| 7 | slideshow（group + 分页） | 25.0 KB | 6.25 KB | |
| 8 | concentric arc（同心圆弧） | 20.0 KB | 5.95 KB | |
| 9 | checkbox | 19.9 KB | 6.21 KB | |
| 10 | label_ex（旋转缩放文字） | 19.0 KB | 5.89 KB | |
| 11 | chart（实时波形） | 19.4 KB | 6.41 KB | RAM 最高（波形环形缓冲） |
| 12 | toggle | 19.3 KB | 6.17 KB | |
| 13 | progress（含 btn） | 20.6 KB | 6.00 KB | |
| 14 | msgbox（含 btn） | 22.1 KB | 6.02 KB | |
| 15 | flash img（img_flash） | 20.2 KB | 5.95 KB | 需外挂 SPI flash 存储 |
| 16 | flash font（font_flash） | 19.9 KB | 6.06 KB | 需外挂 SPI flash 存储 |
| 17 | slider | 20.2 KB | 6.06 KB | |
| 18 | scroll_panel（含子控件） | 22.0 KB | 6.27 KB | |
| 19 | dropdown | 23.5 KB | 6.06 KB | 同上方汇总表 F030 行 |
| 20 | stepper | 20.0 KB | 6.10 KB | |
| 21 | indicator | 19.6 KB | 6.20 KB | |
| 22 | line | 20.3 KB | 6.34 KB | |
| 23 | box | 20.7 KB | 6.02 KB | 动画默认编译期关闭（`WE_BOX_USE_ANIM`） |
| 24 | gauge（仪表盘） | 20.1 KB | 5.98 KB | |
| 25 | list（列表菜单） | 20.9 KB | 5.88 KB | |
| 26 | roller（滚轮选值） | 21.1 KB | 6.14 KB | |
| 27 | marquee（跑马灯） | 22.1 KB | 6.12 KB | |
| 28 | toast（轻提示） | 20.3 KB | 5.86 KB | |
| 29 | focus（聚焦导航） | 22.5 KB | 6.12 KB | btn/checkbox/toggle/indicator/group 组合 |
| 30 | focus2（聚焦编辑态） | 27.9 KB | 6.19 KB | slider/stepper/roller/list 组合 |
| 31 | img_alpha（透明位图） | 33.8 KB | 6.14 KB | ⚠ 含 A1/A2/A4/A8 位图资产 ~13 KB |

> ROM = Code + RO-data + RW-data；RAM = RW-data + ZI-data（含 4.48 KB PFB 显存，各 demo 共有）。`img`/`img_ex`/`img_alpha` 的 ROM 偏大是 demo 内嵌了图片资产、与控件代码无关。`showcase`（仅模拟器、需 800×480）无法烧录到 MCU，故不在此表；其余 1..31 均可单独烧录到 F030。F103 同口径约再大 0.7 KB（多一份外挂 flash 端口实现）。

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
- line
- box
- gauge
- list
- roller
- marquee
- toast

其中：

- `toggle` 的轨道和滑块已统一复用公用解析式圆角矩形填充函数
- `checkbox` 的方框绘制已统一复用公用解析式圆角填充函数
- `chart` 的波形主体与柔边绘制思路参考自 Arm-2D，但实现已按 WeGui 的环形缓冲、脏矩形、PFB 裁剪与整数坐标体系重写
- `dropdown` 展开列表支持无级（像素级）拖拽滚动，滚动条按空闲时间自动淡出至常驻最低透明度
- `stepper` 数值用定点 int32 存储，按住可连续步进且不占用 timer 槽
- `indicator` 圆形状态灯通过中央动画引擎（`we_anim_t`，不占定时器槽位）做亮灭过渡，可选外发光晕
- `msgbox` 为水平居中的顶层弹窗（非模态，挂 LCD 顶层链保证置顶），采用透明度淡入/淡出 + 解析式圆角面板
- `line` 为抗锯齿线段（圆头/平头），端点几何、颜色、透明度三通道各占独立动画节点、可同时动画
- `box` 为矩形面板：四角可各自独立配置圆角/切角/直角，支持边框厚度与颜色；中央与直边整块快速填充，仅四角方块逐像素抗锯齿合成；颜色/透明度动画为编译期可选项（`WE_BOX_USE_ANIM`，默认关闭）
- `gauge` 为仪表盘（刻度/指针/中心帽全部复用现有抗锯齿原语）：指针差分标脏——数值变化只重绘新旧指针位形两块包围盒、静态刻度区零重绘；值→角度走 set_range 预除的 Q16 斜率，draw 内零三角函数、零乘除
- `list` 为数据驱动列表菜单（字符串数组由调用方持有，控件只存指针）：拖拽松手与快速轻扫都注入惯性，越界橡皮筋过冲后回弹，滚动条空闲自动渐隐至常驻低透明（dropdown 同款口径）；行按压/滚动/滚动条各自精细标脏
- `roller` 为滚轮选值器：慢速松手就近吸附，快速甩动惯性继承拖拽速度、滑过多行再减速吸附，轻点上/下可见行直达该行；滚动只标文本列带，文字测量走 y-bbox 常量化 + 行宽缓存（绘制内环零测量调用）
- `marquee` 为跑马灯标签："放得下静止、放不下循环滚动"自适应（两段绘制无缝接缝 + 可停留）；窗口化字形绘制对不可见字形只做游标快进（零位图取址、零像素扫描），滚动由单个中央动画节点推进
- `toast` 为非模态轻提示横幅（挂 LCD 顶层链，浮于一切之上且不拦截输入）：顶部滑入 → 停留 → 滑出；滑动每步把新旧包围盒 union 成单个矩形一次标脏，超宽文本尾部自动截断加省略号
- `img` 支持 RGB565 / ARGB8565 raw、两种索引 QOI 与 A1/A2/A4/A8 透明位图（`we_img_obj_set_color` 前景色上色，默认白色）；`dropdown` 展开列表 / 弹层软键盘走顶层 + 模态通道（命中与语义键直送模态对象）
- 所有带动画的控件（toggle/progress/indicator/msgbox/slideshow/scroll_panel/dropdown/line/gauge/list/roller/marquee/toast）统一走**中央动画引擎**，不占用定时器槽位、数量无上限、空闲自动摘链
- 容器透明度可向子控件级联传播（group/slideshow/scroll_panel）；裸 `group` 会把触摸事件转发给内部子控件，全透明 group 不拦截输入

除稳定区外，仓库另设 **preview 孵化区**：实验控件位于 `Core/widgets_preview/`（demo 在
`Demo/preview/`，DEMO_ID 使用 100 起的独立编号段），仅模拟器编译、不进 Keil 硬件工程，
未细致优化、随时可能下架；毕业后才迁入 `Core/widgets/` 与稳定编号段。当前含 `mask_group`
在内共 26 个实验控件，完整清单见 `Demo/preview/preview_demos.h` 顶部的编号一览。

## 仓库结构

- `Core/` — GUI 内核、渲染与控件实现
- `Demo/` — 各控件 demo 与模板端口文件
- `STM32F103/` — STM32F103 硬件入口、Keil 工程与 LCD/输入/外挂 Flash 端口层
- `STM32F030/` — STM32F030 硬件入口、Keil 工程与 LCD/输入端口层
- `Simulator/` — SDL2 模拟器入口、SDL 端口与模拟器配置
- `tool/` — 编号例程式资源流水线（字体/图片取模 → 外挂 bin 合并 → W25Qxx 烧录）

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

当前 simple demo 共 **31 个**，三个目标（Simulator / STM32F103 / STM32F030）编号已统一：`1..31` 完全一致；Simulator 额外用 `0 = showcase`（全控件汇总，仅模拟器，需 800×480）。选择方式是**编译期宏**——改入口 `main` 顶部的 `#define DEMO_ID` 即可，只有选中的 demo 会被编译进去。

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
| 22 | line |
| 23 | box |
| 24 | gauge |
| 25 | list |
| 26 | roller |
| 27 | marquee |
| 28 | toast |
| 29 | focus（聚焦/按键导航） |
| 30 | focus2（聚焦编辑态：方向键调值） |
| 31 | img_alpha（A1/A2/A4/A8 透明位图） |

> `0`（showcase）仅 Simulator 提供；STM32 无此项。未列编号三个目标统一回退到 `label`。`29/30` 依赖 `WE_CFG_ENABLE_KEY_INPUT=1`（共享配置默认开启；关闭时这两个 demo 编译为提示桩）。模拟器按键映射：方向键 / Tab / Shift+Tab / Enter / 空格 / Esc / 退格。

### 修改方法
改对应入口 `main` 顶部的 `#define DEMO_ID` 数字即可：`Simulator/main_sim.c`、`STM32F103/main.c`、`STM32F030/main.c`。STM32 改完需重新编译 / 烧录。

## 构建与验证

推荐的最小验证方式：

- **Simulator**：编译并运行 `wegui_sim`，检查选定 demo 是否正常绘制与动画
- **STM32**：编译 Keil 工程并在板上运行选定 demo

回归验证（headless 基准哈希，无需人工看屏）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/autotest.ps1"
```

对 1..31 与 preview 101..126 逐 demo 跑 180 帧（SDL dummy 视频驱动、固定 16ms 步进），
对每帧屏幕缓冲做链式 FNV-1a 哈希并与 `Simulator/autotest/golden.txt` 比对（67 项）；
`Simulator/autotest/scripts/*.evt` 存在时额外注入触摸/按键轨迹回放，把拖拽惯性、
焦点导航、弹层开合锁到逐帧精确。改动导致 FAIL 时先确认是回归还是有意变更，
有意变更用 `-Update` 重录基准。

## 资源与工具

`tool/` 为**编号例程式流水线**，每个阶段自带例程输入/输出（例程输出就是 demo 资产），双击对应 `.bat` 即可复现，使用说明见各目录的编号 `.txt`：

- `tool/0.tool/` — 底层转换器（font2c / img2bin_raw / img2bin_indexqoi / bin2c）
- `tool/1.font2c/` — 字体取模（向导生成配置 + 一键构建；内置字体出 `.c/.h`，外挂字体出索引 + 字形 `.bin`）
- `tool/2.img2c/` — 图片取模（按 像素格式 × 压缩 × 去向 分桶：`*_2c` 合并为内置数组，`*_2bin` 出散 bin；含 A1/A2/A4/A8 透明位图）
- `tool/3.bin2c/` — 外挂资源合并（字体 + 图片 bin → `merged_bin.bin` + 地址表 `.c/.h`；另出仅供烧录工程的嵌数据版）
- `tool/4.STM32F103_ex_flash_download/` — W25Qxx 烧录工程（经调试器用自制 FLM 算法写入外挂 Flash）
- `tool/0.1.2.3.update_all.bat` — 一键串联 1→2→3

外挂资产变更后：硬件需重烧 W25Qxx；模拟器直读 exe 旁的 `merged_bin.bin`（构建自动拷贝），重跑 `3.bin2c` 后重启即可，无需重新编译。

## 许可证

本项目使用 Apache License 2.0，详见根目录 [LICENSE](LICENSE)。
