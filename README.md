# WeGui-ARGB

轻量级嵌入式 GUI 框架，面向 STM32 等 ARM Cortex-M MCU，同时提供 SDL2 PC 模拟器。

## 特性

- 小内存占用的局部帧缓冲（PFB）渲染
- 脏矩形刷新策略（全屏 / 单包围盒 / 多脏矩形）
- 统一的 GUI tick / task / timer 调度
- 内置控件与完整 demo 工程
- 支持外部 Flash 图片与字体资源
- 同时支持 STM32F103 硬件目标与 SDL2 模拟器目标

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

其中：

- `toggle` 的轨道和滑块已统一复用公用解析式胶囊绘制函数
- `checkbox` 的方框绘制已统一复用公用解析式圆角填充函数
- `chart` 的波形主体与柔边绘制思路参考自 Arm-2D，但实现已按 WeGui 的环形缓冲、脏矩形、PFB 裁剪与整数坐标体系重写

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
UV4.exe -r "STM32F103/MDK-ARM/Project.uvprojx" -t "WeGui_RGB"
```

说明：
- `UV4.exe` 的实际路径取决于你的本地 Keil 安装位置
- 构建日志默认在：`STM32F103/MDK-ARM/Objects/Project.build_log.htm`

当前代码状态下，STM32 工程已验证可编译通过：
- `0 Error(s), 0 Warning(s)`

## Demo 选择

当前 simple demo 共 **17 个**，Simulator 与 STM32 入口文件使用同一套 demo 顺序：

1. label
2. btn
3. img
4. img_ex
5. arc
6. group
7. slideshow
8. concentric arc
9. key
10. checkbox
11. label_ex
12. chart
13. toggle
14. progress
15. msgbox
16. flash img
17. flash font

### Simulator
在 `Simulator/main_sim.c` 中修改 `demo_id`。

### STM32
在 `STM32F103/main.c` 中切换你希望运行的 demo 初始化逻辑，按当前工程入口写法选择对应 demo。

## 构建与验证

推荐的最小验证方式：

- **Simulator**：编译并运行 `wegui_sim`，检查选定 demo 是否正常绘制与动画
- **STM32**：编译 Keil 工程并在板上运行选定 demo

## 资源与工具

- `tool/bin2c/` — 图片二进制转 C 数组
- `tool/font2c/` — 字库生成工具
- `tool/STM32F103_ex_flash_download/` — 外挂 Flash 下载相关工具

## 许可证

本项目使用 Apache License 2.0，详见根目录 [LICENSE](LICENSE)。
