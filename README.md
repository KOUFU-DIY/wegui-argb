# WeGui-ARGB · V0.4.1 beta

轻量级嵌入式 GUI 框架，面向多种 MCU / SoC 平台，同时提供轻量 PC 模拟器（SimLite，无 SDL 依赖，TCC 秒级编译）。

> 当前版本 **V0.4.1 beta**（版本宏定义见 `we_user_config.h` 的 `WE_GUI_VERSION`）。

## 特性

- 小内存占用的局部帧缓冲（PFB）渲染
- 脏矩形刷新策略（全屏 / 单包围盒 / 多脏矩形）
- 统一的 GUI tick / timer / 中央动画调度
- 全局聚焦 + 按键导航（方向键空间就近移动 / Tab 环序 / OK 双沿按压 / 容器下钻，触摸与按键共存，纯触摸工程可整体编译剔除）
- 图片格式：RGB565 / ARGB8565（无压缩与索引 QOI）+ A1/A2/A4/A8 透明位图（前景色上色，同一份取模反复换色复用）+ 索引QOI_MASK 压缩 A8 蒙版（alpha 推荐格式，行索引随机访问、流式解码零额外 RAM，48×48 图标约为裸 A8 的 21%~40%）
- GRAY8 色深 + 页式单色 / 灰度 OLED 端口（SSD13xx / SH11xx 全家桶 + SSD1327，SPI / 软 IIC），配套控件级单色样式（`WE_CFG_MONO_STYLE`）与全套 128×64 小屏 demo 布局——详见下方 "GRAY8 / 单色 OLED 与小屏支持"
- 内置控件与完整 demo 工程；`tool/` 编号例程式资源流水线（字体/图片取模 → 外挂 bin 合并 → 烧录）
- 支持外部 Flash 图片与字体资源（模拟器直读 `merged_bin.bin`，与硬件"重烧"流程对应）
- 同时支持 STM32F103 / STM32F030 硬件目标与 SimLite PC 模拟器目标（fenster 三平台窗口底座：Win32 GDI / macOS Cocoa / Linux X11）

## 资源占用（实测）

| 目标 | ROM | RAM | 口径 |
|---|---|---|---|
| STM32F103（Cortex-M3，72 MHz） | **24.21 KB** | **6.10 KB** | dropdown 最小可交互应用（= 下方逐 demo 表第 19 号） |
| STM32F030（Cortex-M0，48 MHz，DMA 双缓冲） | **23.5 KB** | **6.06 KB** | 同口径（V0.3.0 全表 F030 实测，整体约比 F103 小 0.7 KB） |

- 280×240 RGB565，PFB 8 行（4.48 KB，仅整屏显存的 3.3%）；ROM 含 3.21 KB 字体资产与约 6.7 KB 端口/启动/C 库，GUI 本体（内核 + 控件）占用见下表逐 demo 分项
- 共享配置实测口径：聚焦/按键导航（`WE_CFG_ENABLE_KEY_INPUT=1`）等默认功能全开——纯触摸工程关掉裁剪宏还能再瘦
- 零 malloc、零浮点路径，Cortex-M0（无硬件除法、无 FPU）原生可用

### 各 demo 单独占用与效果图（STM32F103 / Cortex-M3 实测）

每个 demo 单独编译（`DEMO_ID` 选中该 demo，链接器剔除未引用部分）后的 Keil AC5 `.map` 实测（**indexQOI V3 当前代码全表重测**）。每个 demo = **该主控件 + `label`（标题/FPS）**，复合类额外含子控件；第 1 号 `label` 即"最小 GUI + 字体 + 端口 + 启动"的底座。

分项四列均为 ROM 占用（Code + RO-data + RW-data，单位 KB，四列相加 = 总 ROM）：

- **内核** — GUI 引擎本体（`we_core` / `we_obj` / `we_tick` / `we_input` / `we_focus` / `we_refresh` / `we_draw` / `we_image` / `we_text` / `we_dirty` / `we_scroll`）；未引用的内核函数会被链接器剔除，所以逐 demo 不同——纯控件类 demo 约 7.3~9.7 KB，图片类 demo 链入索引 QOI / QOI_MASK 解码器后约 11 KB
- **控件** — 该 demo 链入的全部 `we_widget_*` 本体
- **资源** — 字体点阵与 demo 内嵌图片数组；其中约 3.21 KB 是各 demo 共有的 ASCII 字体资产
- **其他** — demo 代码 + LCD/输入/外挂存储端口 + 启动文件 + C 库，约 6.4~7.6 KB 基本恒定
- 总 RAM = RW-data + ZI-data，含 4.48 KB PFB 显存（各 demo 共有）

> 效果图为 SimLite 模拟器无头渲染第 120 帧导出（280×240，与硬件同一套渲染内核、逐像素一致）；
> 画面中的 FPS 数字是无头快进渲染的计数残影，不代表硬件帧率。

| 效果图 | DEMO_ID · demo | 总 ROM | 总 RAM | 内核 | 控件 | 资源 | 其他 | 备注 |
|---|---|---|---|---|---|---|---|---|
| <img src="docs/shots/01_label.png" width="140"> | 1 label | 18.00 | 5.88 | 7.63 | 0.55 | 3.21 | 6.61 | 基准底座：最小 GUI + 字体 + 端口 + 启动 |
| <img src="docs/shots/02_btn.png" width="140"> | 2 btn | 19.62 | 6.04 | 8.44 | 1.22 | 3.21 | 6.75 |  |
| <img src="docs/shots/03_img.png" width="140"> | 3 img | 50.00 | 5.94 | 11.02 | 0.64 | 31.78 | 6.55 | 内嵌图片资产（indexqoi V3 ×2 + argb8565 raw） |
| <img src="docs/shots/04_img_ex.png" width="140"> | 4 img_ex | 29.01 | 5.90 | 7.63 | 1.73 | 13.22 | 6.43 | 内嵌 RGB565 raw 图（img_ex 仅收 raw） |
| <img src="docs/shots/05_arc.png" width="140"> | 5 arc | 20.77 | 5.93 | 8.07 | 2.87 | 3.21 | 6.63 |  |
| <img src="docs/shots/06_group.png" width="140"> | 6 group | 21.18 | 6.05 | 9.11 | 1.94 | 3.21 | 6.91 | 含 btn/label 子控件 |
| <img src="docs/shots/07_slideshow.png" width="140"> | 7 slideshow | 25.73 | 6.28 | 9.20 | 5.79 | 3.21 | 7.53 | group + 分页指示 |
| <img src="docs/shots/08_concentric_arc.png" width="140"> | 8 concentric_arc | 20.75 | 5.98 | 8.07 | 2.80 | 3.21 | 6.67 |  |
| <img src="docs/shots/09_checkbox.png" width="140"> | 9 checkbox | 20.63 | 6.24 | 8.83 | 1.42 | 3.21 | 7.17 |  |
| <img src="docs/shots/10_label_ex.png" width="140"> | 10 label_ex | 19.36 | 5.93 | 7.69 | 1.97 | 3.21 | 6.49 |  |
| <img src="docs/shots/11_chart.png" width="140"> | 11 chart | 20.14 | 6.45 | 7.63 | 2.77 | 3.21 | 6.54 | RAM 偏高（波形环形缓冲） |
| <img src="docs/shots/12_toggle.png" width="140"> | 12 toggle | 20.09 | 6.20 | 8.52 | 1.36 | 3.21 | 7.00 |  |
| <img src="docs/shots/13_progress.png" width="140"> | 13 progress | 21.38 | 6.04 | 8.71 | 2.59 | 3.21 | 6.86 | 含 btn |
| <img src="docs/shots/14_msgbox.png" width="140"> | 14 msgbox | 22.93 | 6.05 | 9.05 | 3.66 | 3.21 | 7.00 | 含 btn；顶层非模态弹窗 |
| <img src="docs/shots/15_flash_img.png" width="140"> | 15 flash_img | 20.46 | 5.98 | 7.63 | 2.88 | 3.25 | 6.71 | 图片数据在外挂 SPI flash，片内仅地址表 |
| <img src="docs/shots/16_flash_font.png" width="140"> | 16 flash_font | 20.57 | 6.10 | 7.63 | 2.68 | 3.26 | 7.00 | 字形数据在外挂 SPI flash，片内仅索引与地址表 |
| <img src="docs/shots/17_slider.png" width="140"> | 17 slider | 21.03 | 6.09 | 8.38 | 2.45 | 3.21 | 6.99 |  |
| <img src="docs/shots/18_scroll_panel.png" width="140"> | 18 scroll_panel | 22.75 | 6.31 | 8.71 | 3.27 | 3.21 | 7.55 |  |
| <img src="docs/shots/19_dropdown.png" width="140"> | 19 dropdown | 24.21 | 6.10 | 9.47 | 4.83 | 3.21 | 6.70 | 同上方汇总表 F103 行 |
| <img src="docs/shots/20_stepper.png" width="140"> | 20 stepper | 20.79 | 6.14 | 8.51 | 2.46 | 3.21 | 6.60 |  |
| <img src="docs/shots/21_indicator.png" width="140"> | 21 indicator | 20.36 | 6.23 | 8.62 | 1.61 | 3.21 | 6.92 |  |
| <img src="docs/shots/22_line.png" width="140"> | 22 line | 20.96 | 6.38 | 9.06 | 1.77 | 3.21 | 6.92 |  |
| <img src="docs/shots/23_box.png" width="140"> | 23 box | 21.32 | 6.06 | 8.15 | 3.12 | 3.21 | 6.84 | 动画默认编译期关闭（`WE_BOX_USE_ANIM`） |
| <img src="docs/shots/24_gauge.png" width="140"> | 24 gauge | 20.89 | 6.02 | 9.08 | 2.21 | 3.21 | 6.40 |  |
| <img src="docs/shots/25_list.png" width="140"> | 25 list | 21.56 | 5.91 | 9.10 | 2.81 | 3.21 | 6.43 |  |
| <img src="docs/shots/26_roller.png" width="140"> | 26 roller | 21.85 | 6.17 | 8.58 | 3.13 | 3.21 | 6.92 |  |
| <img src="docs/shots/27_marquee.png" width="140"> | 27 marquee | 22.68 | 6.16 | 8.29 | 4.43 | 3.21 | 6.75 |  |
| <img src="docs/shots/28_toast.png" width="140"> | 28 toast | 21.01 | 5.89 | 8.69 | 2.48 | 3.21 | 6.63 |  |
| <img src="docs/shots/29_focus.png" width="140"> | 29 focus | 23.26 | 6.16 | 9.51 | 3.64 | 3.21 | 6.89 | btn/checkbox/toggle/indicator/group 组合 |
| <img src="docs/shots/30_focus2.png" width="140"> | 30 focus2 | 28.59 | 6.22 | 9.10 | 9.30 | 3.21 | 6.97 | slider/stepper/roller/list 组合 |
| <img src="docs/shots/31_img_alpha.png" width="140"> | 31 img_alpha | 28.76 | 6.18 | 11.02 | 0.64 | 10.44 | 6.66 | A8 raw 与索引QOI_MASK 压缩对比 |
| <img src="docs/shots/32_imgbtn.png" width="140"> | 32 imgbtn | 35.77 | 5.97 | 10.79 | 1.10 | 17.12 | 6.75 | 全图片格式可作按钮皮肤 |
| <img src="docs/shots/33_segdisp.png" width="140"> | 33 segdisp | 19.53 | 5.98 | 7.30 | 2.08 | 3.21 | 6.93 | 无字库外资产；控件本体极小 |

> 内核里没被引用的 API 不进固件：新增 `we_obj_set_size` 后全表逐行零变化，
> 同口径的 F103 dropdown 更是逐字节相同（Code/RO/RW/ZI 四项全等），
> 说明链接器把未引用的内核函数整体剔除了——按需付费，加 API 不涨底座。
>
> `img`/`img_ex`/`img_alpha`/`imgbtn` 的总 ROM 偏大源自**资源列**的 demo 内嵌图片资产，
> 与控件代码无关（控件列 0.6~1.7 KB）。`showcase`（仅模拟器、需 800×480）无法烧录到
> MCU，故不在此表；其余 1..33 均可单独烧录。F030 同口径整体约小 0.7 KB（少一份外挂
> flash 端口实现）。

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
- imgbtn
- segdisp

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
- `img` 支持 RGB565 / ARGB8565 raw、两种索引 QOI、A1/A2/A4/A8 透明位图与索引QOI_MASK 压缩 A8 蒙版（`we_img_obj_set_color` 前景色上色，默认白色）；`dropdown` 展开列表 / 弹层软键盘走顶层 + 模态通道（命中与语义键直送模态对象）
- 所有带动画的控件（toggle/progress/indicator/msgbox/slideshow/scroll_panel/dropdown/line/gauge/list/roller/marquee/toast）统一走**中央动画引擎**，不占用定时器槽位、数量无上限、空闲自动摘链
- 容器透明度可向子控件级联传播（group/slideshow/scroll_panel）；裸 `group` 会把触摸事件转发给内部子控件，全透明 group 不拦截输入
- `imgbtn` 为图片按钮：与 `img` 共用渲染层格式分发（全部图片格式可作按钮皮肤），无按压态图时自动变暗（带透明通道压透明度、不透明图叠半透明黑），支持运行时换图与 OK 键双沿触发
- `segdisp` 为段码数码管：每位一个段码字节（bit0~6 = a~g、bit7 = dp，与 TM1650 类驱动段码表对齐），文本便捷层与段码直控双通道，45° 斜切段形（可选矩形）、逐位标脏；字宽/字高/间距/段厚在 init 全参数化（0 = 自动推导）

除稳定区外，仓库另设 **preview 孵化区**：实验控件位于 `Core/widgets_preview/`（demo 在
`Demo/preview/`，DEMO_ID 使用 100 起的独立编号段），四个目标均参与编译（链接器剔除未引用部分），
未细致优化、随时可能下架；毕业后才迁入 `Core/widgets/` 与稳定编号段（已毕业迁出的编号留空洞
不复用，如 117→33 segdisp、120→32 imgbtn）。当前含 `mask_group` 在内共 24 个实验控件，
完整清单见 `Demo/preview/preview_demos.h` 顶部的编号一览。

## 仓库结构

- `Core/` — GUI 内核、渲染与控件实现
- `Demo/` — 各控件 demo 与模板端口文件
- `STM32F103/` — STM32F103 硬件入口、Keil 工程与 LCD/输入/外挂 Flash 端口层
- `STM32F030/` — STM32F030 硬件入口、Keil 工程与 LCD/输入端口层
- `SimLite/` — 轻量 PC 模拟器：移植模板式入口、fenster 端口与配置、TCC/gcc 构建脚本
- `tool/` — 编号例程式资源流水线（字体/图片取模 → 外挂 bin 合并 → W25Qxx 烧录）

## 快速开始

### 1. SimLite（PC 模拟器）

无 SDL、无 CMake：把 TinyCC 解压到 `SimLite/tcc/`（全套约 2.5 MB）即可秒级全量编译；
已装 MinGW 时自动回退 gcc（带 `-g`，可配合 VS Code gdb 调试）。

正式版构建（demo 由 `SimLite/main_lite.c` 顶部的 `DEMO_ID` 宏决定，与硬件目标同一套用法）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/build_lite.ps1"
```

不改源码临时换 demo / 构建完直接运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/build_lite.ps1" -Demo 25 -Run
```

macOS / Linux（clang+Cocoa / cc+libx11-dev）：`sh SimLite/build_lite.sh`

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

当前 simple demo 共 **33 个**，三个目标（SimLite / STM32F103 / STM32F030）编号已统一：`1..33` 完全一致；SimLite 额外用 `0 = showcase`（全控件汇总，仅模拟器，需把分辨率调到 800×480）。选择方式是**编译期宏**——改入口 `main` 顶部的 `#define DEMO_ID` 即可，只有选中的 demo 会被编译进去。

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
| 31 | img_alpha（A8 raw 与索引QOI_MASK 压缩透明位图） |
| 32 | imgbtn（图片按钮） |
| 33 | segdisp（数码管） |

> `0`（showcase）仅 SimLite 提供；硬件目标无此项。未列编号各目标统一回退到 `label`。`29/30` 依赖 `WE_CFG_ENABLE_KEY_INPUT=1`（共享配置默认开启；关闭时这两个 demo 编译为提示桩）。模拟器按键映射：方向键 / Tab / Shift+Tab / Enter / 空格 / Esc / 退格。

### 修改方法
改对应入口 `main` 顶部的 `#define DEMO_ID` 数字即可：`SimLite/main_lite.c`、`STM32F103/main.c`、`STM32F030/main.c`。硬件目标改完需重新编译 / 烧录；SimLite 也可用 `build_lite.ps1 -Demo N` 免改源码。

## 构建与验证

推荐的最小验证方式：

- **SimLite**：编译并运行 `wegui_lite`，检查选定 demo 是否正常绘制与动画
- **STM32**：编译 Keil 工程并在板上运行选定 demo

## GRAY8 / 单色 OLED 与小屏支持

V0.4.1 起框架原生支持 8 位灰度色深与页式单色 OLED，配套控件级单色样式与全套
128×64 小屏 demo 布局，并为该配置建立了独立的回归基线。

### GRAY8 渲染架构

- `we_user_config.h` 选 `LCD_DEEP (DEEP_GRAY8)`：PFB 每像素 1 字节灰度，渲染管线
  （抗锯齿、混色、透明度级联）与彩色档完全一致，无独立代码路径；
- 单色屏的黑白化发生在**端口层**：刷屏时按 `WE_OLED_GRAY_THRESHOLD`（默认 128）
  把灰度二值化并按页打包输出（`lcd_oled_port`）；灰度屏（SSD1327）2 像素并 1 字节
  输出 16 级灰度（`lcd_gray_port`）；
- 页式 OLED 要求脏矩形 y 向 8 行页对齐（`WE_LCD_FLUSH_ALIGN_Y (8)`），SSD1327 要求
  x 向 2 像素对齐——对齐扩边在脏矩形入库时统一完成，端口收到的窗口天然对齐；
- GRAY8 档派生参数自动锁定（`we_user_config.h` 末尾派生段）：软 SPI / 软 IIC 均为
  阻塞端口，`GRAM_DMA_BUFF_EN` 强制 0；PFB 固定 8 行页对齐（128 宽下仅 1 KB，比
  280×240 RGB565 的 4.48 KB 更轻，小 RAM MCU 反而更宽裕）。

### OLED 端口与屏 IC（STM32F103）

- 驱动接口 5 种：软 3SPI（暂无 OLED/GRAY 刷屏，编译期拦截）/ 软 4SPI / 硬 4SPI /
  DMA 4SPI / 软 IIC（`OLED_IIC_7ADDR` 默认 0x3C）；
- 屏 IC 14 款：6 款 TFT（ST77xx / GC9A01）+ 8 款页式单色 OLED（SSD1306 / 1309 /
  1312 / 1315、SH1106 / 1107 / 1108 / 1115）+ 1 款灰度 OLED（SSD1327）；
- `LCD_TYPE` 由所选 IC 自动派生（OLED / GRAY / RGB565 / RGB888），
  `stm32f103_hw_config.h` 参数检查段用 `#error` 拦截非法组合（OLED IC 配非 GRAY8
  色深、TFT 配 IIC 接口、对齐不满足等）；
- **STM32F030 现状**：只有 TFT SPI 端口，选 `DEEP_GRAY8` 会被
  `stm32f030_hw_config.h` 编译期 `#error` 拦下（防“编译通过但上电黑屏”）。移植
  路线：照 F103 的 Lcd_Port 结构搬软 IIC 端口 + 3 个 SPI 端口的 OLED/GRAY 刷屏
  分支 + 9 款 IC 驱动，RAM 无压力（GRAY8 PFB 更小），新增 `.c` 需手工加入 Keil
  工程。

### 单色样式（WE_CFG_MONO_STYLE）

- 置 1 后参与控件的默认调色盘切到纯黑 / 纯白高对比档，绘制策略同步切换：实体
  填充改“白描边 + 黑底”、按压 / 选中改“反白 + 挖空字”、半透明装饰（网格线、
  滚动条、拖尾、按压预览块）改全不透明或直接省略——经端口阈值二值化后对比关系
  仍完整可读；
- 仅控件绘制策略分支，渲染管线不受影响；彩色档（置 0，默认）行为完全不变；
- 覆盖：稳定控件 11 个（btn / toggle / slider / msgbox / progress / dropdown /
  stepper / list / roller / box / toast），预览控件 20 个全部接入；colorwheel /
  animimg / canvas 无自有调色盘、mask_group 默认色自适应 LCD 背景，四者天然无需
  宏组。

### 128×64 小屏 demo 分支

- `WE_DEMO_SMALL_SCREEN`（`SCREEN_WIDTH <= 128` 时自动置 1）：全部 33 个稳定 demo
  与 24 个预览 demo 均带独立 128×64 布局分支，demo 字体别名自动切
  `simsun_12_2bpp`（ASCII）；
- 控件头文件带小屏默认档（`SCREEN_WIDTH <= 128` 时自动生效）：keyboard 回显条
  26→13，ime_pinyin 拼音条 22→14 / 候选栏 30→18 / 翻页键 26→16，radio 指示器
  18→12 / 行距 6→3；
- 113 拼音输入法在小屏用“无键区弹层”（回显 + 拼音条 + 候选栏三条横带，中文字库
  仍为 16px msyh）：字母注入走自动演示脚本 / `we_ime_pinyin_inject_key`，候选与
  翻页键仍可触摸。

## 资源与工具

`tool/` 为**编号例程式流水线**，每个阶段自带例程输入/输出（例程输出就是 demo 资产），双击对应 `.bat` 即可复现，使用说明见各目录的编号 `.txt`：

- `tool/0.tool/` — 底层转换器（font2c / img2bin_raw / img2bin_indexqoi / bin2c）
- `tool/1.font2c/` — 字体取模（向导生成配置 + 一键构建；内置字体出 `.c/.h`，外挂字体出索引 + 字形 `.bin`）
- `tool/2.img2c/` — 图片取模（按 像素格式 × 压缩 × 去向 分桶：`*_2c` 合并为内置数组，`*_2bin` 出散 bin；alpha 蒙版默认推荐索引QOI_MASK 压缩取模，A8 raw 桶保留）
- `tool/3.bin2c/` — 外挂资源合并（字体 + 图片 bin → `merged_bin.bin` + 地址表 `.c/.h`；另出仅供烧录工程的嵌数据版——体积大不入库，烧录前跑一次 `build_bin.bat` 再生）
- `tool/4.ex_flash_download/` — 外挂 Flash 烧录工程，每款芯片一个子目录（`STM32F103/`：W25Qxx，经调试器用自制 FLM 算法写入外挂 Flash）
- `tool/0.1.2.3.update_all.bat` — 一键串联 1→2→3

外挂资产变更后：硬件需重烧 W25Qxx；模拟器直读 exe 旁的 `merged_bin.bin`（构建自动拷贝），重跑 `3.bin2c` 后重启即可，无需重新编译。

## 许可证

本项目使用 Apache License 2.0，详见根目录 [LICENSE](LICENSE)。
