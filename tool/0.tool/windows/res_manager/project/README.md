# res_manager — WeGui 资源管理器

WeGui 取模工具链（`tool/` 目录）的图形前端：预览字体 / 图片 / bin 取模资料，一键批处理取模。
基于 Tauri v2 + React + TypeScript。**只调度 `0.tool/` 下的取模 exe，不内置转换逻辑。**

## 功能

- 总览：流水线状态（1 字体 / 2 图片 → 3 合并）、工具 exe 健康检查、一键构建
- 阶段页：输入 / 输出文件树浏览 + 预览
  - 图片：棋盘格底 + 像素级缩放
  - bin：6 字节资源头解析（格式 / 压缩 / 尺寸）、字体 blob CRC、hex dump
  - 字体 ttf/otf/ttc：浏览器内实时渲染样张
  - 字体取模配置 json：参数摘要卡片 + 单配置构建
  - 代码 / 文本：UTF-8 / GBK 自动识别
- 设置页：根目录、工具 exe 路径、阶段目录、合并来源目录全部可配置，
  保存于根目录 `res_manager.json`（缺省时使用与现有 tool/ 布局一致的默认值）
- 控制台：批处理任务实时输出，任务结束自动刷新文件列表

## 目录布局

```text
res_manager/
├── res_manager.exe   发布产物（单文件；同款复制一份到 tool/ 下日常使用）
└── project/          Tauri 工程（本目录：源码 + 依赖 + 编译产物）
```

## 开发

需要 Node.js ≥ 20 与 Rust（MSVC 工具链）。在 `project/` 目录下执行：

```powershell
npm install
npm run tauri dev                  # 开发运行
npm run tauri build -- --no-bundle # 发布构建（仅单文件 exe，不打安装包）
```

发布后把 `src-tauri/target/release/res_manager.exe` 复制到上一级 `res_manager/`
和 `tool/` 下即可分发运行：程序启动时从 exe 所在目录向上查找根目录（标记：含
`res_manager.json`，或含 `0.tool` 与 `1.font2c`），也可以在设置页手动选择任意
目录作为根目录（会被记住）。

## 目录约定与移植性

- 目录不写死：工具 exe 路径、三个阶段目录、合并来源目录都读取根目录下的
  `res_manager.json`（设置页可视化编辑；相对根目录或绝对路径均可）。
- 默认工具路径：`0.tool/<平台>/<工具>/<工具>[.exe]`，当前平台目录为 `windows/`，
  后续 macOS 版工具放入 `0.tool/macos/` 即可，调度逻辑无需修改（`src-tauri/src/paths.rs`）。
- 批处理步骤在 `src-tauri/src/runner.rs` 中用 Rust 复刻了 `tool/` 下各 `.bat` 的逻辑
  （清目录 → 依次调 exe → 清临时物），不依赖 cmd/.bat，天然跨平台。
- 前端所有文件访问都通过后端命令完成，并被限制在根目录及配置目录范围内。
