# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

img2bin 工具集：把 PNG/BMP/JPG/JPEG 图片转换为嵌入式设备可直接使用的纯 `.bin` 像素数据。共 6 个取模 CLI 工具（每个 exe 对应一种编码算法：`raw`、`imprle`、`rle`、`qoi`、`qoif`、`indexqoi`）+ 1 个统筹管理器 `img2bin_pack`（批量调度取模工具并把 `.bin` 汇总生成 `.c/.h`）。

重要：`.bin` 输出 = **6 字节通用资源头 + 算法 payload**（头：类型 0x00 + 算法/格式 nibble 格式码 + 恒大端宽高；nibble 映射的唯一权威在 `format.c` 的 `img2bin_get_format_header_nibble`，与枚举顺序无关）。头由 tool_app 写出层统一前置，六个 `img2bin_encode_*` 编码函数仍返回纯 payload（黄金测试基于 payload）。indexQOI 自己的 13 字节结构头在通用头之后。工具不输出 `.c/.h`、数组文本——那些由 `img2bin_pack` 在上层生成，数组内容与 bin 文件逐字节一致（含头）。协议优先级：实际 bin 输出 > `--info` JSON > 用户文档（见 [docs/user/README-protocol.md](docs/user/README-protocol.md)）。

本仓库位于 OneDrive 同步目录，已 git init（无远端）。主要语言为中文；工具描述符中的用户可见字符串同时带 zh_CN 和 en 两份。

## 目录布局

- `windows/` — 面向用户的产物目录
  - `img2bin_pack.exe` + `img2bin_pack.json`（默认配置，`root` 指向仓库根）
  - `tools/` — 六个取模 exe（发布脚本复制的产物）
  - `examples/*.cmd` — 预设批处理脚本（全量默认 / 只出 bin / 分批参数 / 配合外部 bin2c 合并资源包），bat 模式与 json 配置是同一引擎的两种入口
- `input2raw/` `input2imprle/` `input2rle/` `input2qoi/` `input2qoif/` `input2indexqoi/` — 按算法分类的图片输入文件夹（`input2<算法代号>` 约定，pack 自动匹配）
- `output/` — pack 的输出（.bin + img_resources.c/.h + manifest），已被 .gitignore 排除
- `builder/` — CMake 工程根（注意：仓库根目录不是 CMake 根）
  - `src/`、`tests/`、`third_party/`（stb_image）、`icon/`、`release/README.template.txt`
  - `gui_tauri/` — Tauri v2 + Vue 3 + TS 的 GUI 骨架，目前用 `src/data/toolCatalog.ts` 中的模拟数据，规划后续改为消费 `--info` JSON（或直接包装 img2bin_pack）
  - `build/`、`build-ninja/`、`build-workspace/` — 残留的临时构建目录，内容不完整，不要依赖（已 gitignore）
- `docs/user/` — 面向最终用户的文档，发布时会被打包进 dist；改动工具行为后必须同步更新
- `dist/` — 打包好的发布目录
- 根目录 `input/` 是旧的单工具默认工作目录，测试引用的 `参考/` 样例目录在本机缺失（见下）

## 构建与测试

依赖：VS 2022（MSVC）、CMake、Ninja。MSVC 环境需先用 `VsDevCmd.bat -arch=x64` 初始化。本机 VS 2022 Community 安装在 `d:\Program Files\Microsoft Visual Studio\2022\Community`（`build_release.ps1` 通过 vswhere 自动发现）；PATH 上还有 msys64 的 gcc，配置时用 `set CC=cl` 强制 MSVC。

```powershell
# 配置 + 构建（在仓库根执行；产物在 <build>/bin/）
cmake -S builder -B builder/build-ninja -G Ninja
cmake --build builder/build-ninja

# 运行测试
ctest --test-dir builder/build-ninja --output-on-failure
```

测试只有一个可执行文件 `img2bin_tests`（`builder/tests/test_main.c`），`main()` 顺序调用全部测试函数，无单测过滤参数；要"跑单个测试"只能直接运行测试 exe（跑全量，本来也很快）或临时注释 `main()` 中的调用。测试以黄金字节序列断言各编码器输出，另覆盖 CLI 行为、manifest、`--info` JSON、Unicode 路径、pack 全链路（JSON 解析/配置/codegen 黄金文本/工具发现/端到端），并会用 Windows `version` API 校验同目录下已构建 exe 的图标和版本资源。`IMG2BIN_SOURCE_DIR` 编译宏指向仓库根，供测试定位源内资源。

已知基线：`参考/取模例子/` 样例目录在本机 OneDrive 副本中缺失，导致 5 个 reference-sample 测试恒定失败（imprle/rle/qoi/qoif 参考样例 + indexqoi 默认间隔）。这是既有状态，与代码无关；其余全部测试应通过。恢复该目录后 ctest 应全绿。测试套件不幂等：重跑前需删除 `<build>/bin/test_artifacts/`（`default_create_dirs` 测试要求其 stage 不存在）。

### 发布

```powershell
.\build_release.ps1              # ctest 失败即中止（参考/ 缺失时会因 5 个既有失败而中止）
.\build_release.ps1 -SkipTests   # 显式跳过测试门禁
```

在 `%LOCALAPPDATA%\Temp\img2bin_tools_build_release\<时间戳>` 全新配置构建 + 跑 ctest，然后把 6 个取模 exe 复制到 `windows/tools/`、`img2bin_pack.exe` 复制到 `windows/`，并在 `dist/img2bin-tools-<版本>-windows-x64/` 生成完整发布布局（windows/ + input2* + docs/user + README.txt）。工具清单在脚本的 `$toolNames` 数组中——新增取模工具时须同步。

### GUI（builder/gui_tauri）

```powershell
npm run dev          # Vite 前端开发
npm run tauri:dev    # 需要 Rust 环境
npm run build        # vue-tsc --noEmit + vite build
```

## 架构

C99，MSVC 下 `/W4 /utf-8`。四层静态库结构（见 `builder/CMakeLists.txt`）：

1. **`img2bin_core`**（`src/core/`）— 基础层：`cli.c`（参数解析）、`format.c`（9 种像素格式定义与转换）、`image_io.c`（stb 图片加载）、`filesystem.c`（Windows/POSIX 路径与 UTF-8/宽字符转换）、`util.c`。`version.h` 是唯一版本号来源——`build_release.ps1` 用正则读它，`windows_exe.rc.in` 资源模板也引用它。
2. **`img2bin_tool_app_core`**（`src/apps/tool_app.c`）— 共享应用骨架，**所有工具行为都在这里**：默认 input/output 目录创建、拖拽（位置参数）输入、目录批处理、manifest.json 写出、`--info` JSON、错误 JSON（NDJSON）、退出码。
3. **算法库**（`src/algorithms/<algo>/`）— 纯编码函数 `img2bin_encode_*`，与 CLI/IO 无关。`imprle` 和 `rle` 依赖 `raw`；`qoi`、`qoif`、`indexqoi` 共用 `qoi_encoder.c`。
4. **工具应用库**（`src/apps/img2bin_<tool>/app.c`）— 每个工具只是一个静态 `img2bin_tool_descriptor_t`（元数据 + encode 适配函数指针），委托给骨架层。`main.c` 极薄：Windows 上 `wmain` 把宽参数转 UTF-8 后调用 `*_run`。
5. **`img2bin_decoder`**（`src/decoder/img2bin_decode.c/.h`）— 参考解码器：纯 C99、零依赖、无动态内存，独立于其他库（下位机直接拷用，发布包复制到 `decoder/`）。六算法解码到 RAW 打包字节流 + indexQOI 头解析/跳转解码。测试对 9 格式 × 2 字节序 × 7 路径做编码→解码→与 RAW 逐字节比对的回环验证——改动任何编码器语义都会被这批测试抓住，解码器必须同步改。
6. **`img2bin_pack_core`**（`src/pack/`）— 统筹管理器，独立于工具描述符体系：`pack_json.c`（极简 JSON DOM 解析器）、`pack_config.c`（img2bin_pack.json）、`pack_discovery.c`（扫描 `img2bin_*.exe` 并逐个跑 `--info` 读取 algorithm_code/能力）、`pack_process.c`（CreateProcessW/popen 捕获输出）、`pack_codegen.c`（bin 文件名解析 + 符号 sanitize + .c/.h 生成，无时间戳、确定性输出）、`pack_run.c`（CLI + 编排 + pack manifest）。

**pack 的关键安全约束**：discovery 会执行候选 exe，`img2bin_pack*` 和 `img2bin_tests*` 必须保持在 `img2bin_pack_is_tool_candidate` 的排除名单里——测试 exe 一旦被探测会递归跑整个测试套件（含 pack e2e），形成进程炸弹。新增会自我派生子进程的 `img2bin_*` 可执行目标时必须同步加入排除。

新增一个取模工具需要改动：`src/algorithms/` 新编码器、`src/apps/img2bin_<name>/`（app.c/app.h/main.c + 描述符）、`CMakeLists.txt`（算法库/应用库/可执行目标/图标 + 测试目标的 include 和链接）、`version.h` 宏、`build_release.ps1` 的 `$toolNames`、`tests/test_main.c`、`docs/user/` 各文档。pack 通过 `--info` 自动发现新工具，`input2<新算法>` 文件夹随之自动生效，pack 侧无需改代码。

## 行为约定

- 默认值：`rgb565`、大端、`<exe_dir>/input` → `<exe_dir>/output`、背景色 `000000`
- 输出命名：`<原图名>_<像素格式>_<算法>_<be|le>_<宽>x<高>.bin`——这个命名是机器接口：pack 的 codegen 完全靠解析它取得格式/算法/字节序/宽高元数据
- 不带 Alpha 的目标格式先按 `--bg-color` 混合透明区域再转换
- indexQOI：索引处写未压缩原始像素块（无 Alpha 标记 `0xFE`，带 Alpha 标记 `0xFF`），索引偏移相对 payload 起点，默认索引间隔为图片宽度（`--index-interval` 可改），无尾部结束码
- `--info` JSON 是机器接口入口（GUI/自动化集成/pack 的工具发现都以它为准），schema 版本见 `version.h` 的 `IMG2BIN_INFO_SCHEMA_VERSION`
- pack 配置合并优先级：CLI > `img2bin_pack.json` > 内置默认；配置文件查找顺序：`--config` > `<root>/img2bin_pack.json` > `<exe_dir>/img2bin_pack.json`；相对路径语义：`root`/`tools_dir` 相对配置文件目录，`output` 相对 root
- pack 的 `--folders a,b`（只处理列出的文件夹，缺失名按失败处理并禁用 bootstrap）与 `--emit bin|ch`（codegen 开关，`--no-codegen` 为别名）支撑纯 bat 工作流；`--emit ch` 在本次无成功任务时也会按默认输出目录现状重新生成 `.c/.h`（保证多次调用的 bat 最后一行总能汇总）
- 用户的独立 bin2c 项目（`E:\OneDrive\APP\bin2c`，合并 bin + ID 枚举 + 地址表）可直接消费 pack 的 output；其符号与 pack 的 `.c/.h` 同源会宏冲突，文档已注明二选一 include
- pack 退出码：0 成功、1 CLI/配置错误、2 环境错误、5 内部错误、6 部分文件夹失败（与工具的 0-6 语义对齐）
- pack 生成的 `.c/.h` 不含时间戳，输入相同则输出逐字节相同（黄金测试锁定格式）
- 错误 JSON（工具与 pack）统一输出到 stderr 单行；机器接口逐字段文档在 [docs/user/README-schema.md](docs/user/README-schema.md)，改 `--info`/manifest 结构时必须同步该文档
