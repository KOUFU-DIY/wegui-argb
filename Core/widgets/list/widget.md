# list

## 功能
数据驱动列表菜单：背景面板（圆角可选）+ 逐行左对齐文字 + 行底 1px
低透明度分隔线 + 按压行高亮背景（首/末行贴合面板圆角）；内容超出
控件高度时右缘细滚动条（胶囊滑块，位置按滚动比例，活动全显 / 空闲
600ms 后渐隐到常驻低透明，dropdown 同款口径）。拖拽跟手滚动，越界
允许橡皮筋过冲（上限 24px）松手回弹；拖拽松手与快速轻扫（无 STAY
的 SWIPE）都注入惯性（速度每步衰减 7/8，中央动画节点推进）。

## 适用场景
- 设置菜单 / 选项列表
- 任何"一列可点击文字条目"的导航界面
- 条目数远超可见行数、需要滚动浏览的场合

## 关键 API
- `we_list_obj_init(obj, lcd, x, y, w, h, font)`
- `we_list_set_options(obj, items, count)`
  —— 数据驱动：`const char *const *` 字符串数组由**调用方持有**，
  控件只存指针绝不拷贝；绑定后滚动条短暂全显提示可滚动
- `we_list_set_clicked_cb(obj, cb)`
  —— `void (*cb)(void *list, uint16_t idx)`，未拖拽且按压/释放
  落在同一行时触发
- `we_list_set_row_height(obj, row_h)` —— 行高可调
  （默认 = 字体行高 + 2 × `WE_LIST_ROW_PAD`）
- `we_list_set_font(obj, font)` —— 字体可配置；行高按新字体行高
  重推导（覆盖之前的 set_row_height），滚动范围同步钳制
- `we_list_set_radius(obj, radius)` —— 面板圆角（0 = 直角）
- `we_list_set_scroll(obj, scroll_px)` —— 程序化定位（硬夹紧，无动画）
- `we_list_obj_delete(obj)` —— 内部先摘除**两个**动画节点
  （惯性/回弹 + 滚动条淡出）再 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_LIST_ROW_PAD`（行内上下边距，默认 7）
- `WE_LIST_TEXT_PAD`（文字左内边距 / 分隔线两端内缩，默认 10）
- `WE_LIST_DEF_RADIUS`（默认圆角 10）
- `WE_LIST_DRAG_THRESHOLD`（拖拽判定阈值，默认 6px）
- `WE_LIST_INERTIA_NUM` / `WE_LIST_INERTIA_DEN`（惯性衰减，默认 7/8）
- `WE_LIST_SWIPE_SLICE_MS`（快扫测速时间片，默认 128ms）
- `WE_LIST_OVERSCROLL_LIMIT`（越界过冲上限，默认 24px）
- `WE_LIST_REBOUND_PULL_DIV` / `WE_LIST_REBOUND_MAX_STEP`
  （回弹拉力/单步上限，默认 3 / 24，对齐 scroll_panel）
- `WE_LIST_PRESS_INSET` / `WE_LIST_PRESS_RADIUS`（高亮内缩/圆角，默认 2 / 6）
- `WE_LIST_SEP_OPA`（分隔线透明度，默认 46）
- `WE_LIST_SB_WIDTH` / `WE_LIST_SB_MARGIN`（滚动条几何）
- `WE_LIST_SB_OPA`（滚动条活动期峰值透明度，默认 255）
- `WE_LIST_SB_HOLD_MS` / `WE_LIST_SB_FADE_MS` / `WE_LIST_SB_IDLE_ALPHA`
  （空闲淡出：保持 600ms / 渐隐 400ms / 常驻最低 80，
  命名风格对齐 dropdown 的 `WE_DROPDOWN_SB_*`）

## 事件与行为
- PRESSED 记录命中行 + 起点 Y，命中行进入按压高亮（只标脏该行条带），
  并打断进行中的惯性/回弹（越界时定格，松手再回弹）
- STAY 位移超阈值（6px）进入拖拽滚动模式，同时取消行按压态；
  拖拽软夹紧到 [-24, max+24]，越界呈橡皮筋过冲
- RELEASED 未拖拽且释放点仍在按压行内 → 触发 clicked 回调
  （回调在 RELEASED 阶段触发，不依赖 CLICKED 事件）
- 拖拽松手：以最近一次 STAY 步进为初速度做惯性滑行，速度每步衰减
  7/8（越界段再减半加速收敛），过冲后经回弹动画（过冲/3 每步，
  1..24px 整数缓动）拉回边界
- 快速轻扫（无 STAY）：内核在 RELEASED 后补发 SWIPE_UP/DOWN，
  按"总位移 × 16 / `WE_LIST_SWIPE_SLICE_MS`"估算初速度注入同一条
  惯性动画；有 STAY 的拖拽被内核 `gesture_had_stay` 抑制不发 SWIPE，
  两路天然互斥不叠加
- 过冲期间命中测试对顶部露出的空隙返回 -1（不误判为第 0 行）
- 所有事件返回 1（消费，不穿透）

## 渲染说明
- 行内容（高亮条/文字/分隔线）经 PFB 窗口收窄（save/restore
  `pfb_area`/`pfb_y_start`/`pfb_y_end`/`pfb_gram`）裁剪在控件矩形内，
  半露行不会渗出边界；负向过冲时首行索引夹到 0、屏幕 Y 由行号反推
- 按压高亮条先与"面板内缩矩形"求交（半露行不外溢），贴到内缩边界
  （落在面板圆角带）时圆角半径改用 max(默认 6, 面板半径-内缩)，
  与面板圆角同心贴合，首/末行不再溢出直角
- 滚动条在 PFB 恢复后叠加绘制，透明度由淡出动画节点驱动
  （sb_alpha：峰值 255 → 空闲渐隐 → 常驻 80）；过冲期间滑块位置按
  夹紧后的滚动值计算，不越出轨道
- **标脏粒度**：行按压/释放只标该行条带；滚动位移标内容裁剪矩形
  （= 面板矩形，不越过面板边界外扩）；滚动条淡出/唤醒只标右缘
  滚动条窄条带
- 零 malloc、渲染内环零浮点

## 注意事项
- 删除前必须走 `we_list_obj_delete`（**两个**动画节点归控件所有：
  惯性/回弹 `anim` + 滚动条淡出 `sb_anim`，内核无法代摘）
- 条目数组生命周期须覆盖控件生命周期（推荐 `static const char *const []`）
- `we_list_set_font` 会把行高重置为按新字体推导的默认值，如需自定义
  行高请在 set_font 之后再调 `we_list_set_row_height`

## 已完成的毕业优化
- [x] 交互级精细标脏：按压/释放行高亮按"行条带 ∩ 面板矩形"经
  `we_obj_invalidate_area`（屏幕绝对坐标）单行标脏；滚动只标内容
  裁剪矩形；滚动条透明度变化只标滚动条条带
- [x] 滚动条空闲淡出：dropdown 同款 wake + HOLD/FADE/IDLE_ALPHA
  三段口径（600ms 保持 → 400ms 线性渐隐 → 常驻 80），独立
  `we_anim_t` 节点（`sb_anim`）驱动，delete 内已 `we_anim_stop`
- [x] 快速轻扫惯性：SWIPE_UP/DOWN（无 STAY 快扫）按"总位移/固定
  时间片（128ms）"估算初速度，注入与拖拽松手同一条惯性动画
- [x] 边界回弹：拖拽/惯性软夹紧允许 ±24px 过冲，松手后按
  "过冲/3（1..24px）"整数缓动回弹，参数命名对齐 scroll_panel 的
  REBOUND 系列；替换原硬夹紧
- [x] 按压高亮贴合圆角：高亮矩形与面板内缩矩形求交 + 贴边时改用
  同心半径（面板半径-内缩），首/末行在面板圆角处不再溢出直角
- [x] 字体参数化：新增 `we_list_set_font`（行高按新字体重推导、
  内容高随之重算、滚动范围钳制、整控件标脏）

## 毕业前需优化
- [ ] 行仅支持纯文本；图标/右侧箭头/副标题等富行样式未实现
  （需要行数据模型从 `const char *` 扩展为结构体数组，属 API 面
  设计决策，留给毕业迁移时统一定夺，本轮"只增不改"不动数据模型）

## 对应 demo
- `Demo/demo_list.c`（DEMO_ID 25：10 项设置菜单 + 点击回显；
  可体验快扫惯性、过冲回弹与滚动条空闲渐隐）
