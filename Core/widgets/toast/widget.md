# toast

## 功能
轻提示横幅：从屏幕顶部滑入的非模态自动消失提示条。滑入（位移 + 透明度同步过渡，约 200ms）→ 停留 `duration_ms` → 滑出隐藏，全程不阻挡任何输入。超宽文本尾部自动截断并追加 "..."。

## 适用场景
- "保存成功 / 连接断开 / 收到消息"一类瞬态状态提示
- 不需要用户确认、不该打断操作流的轻量反馈
- 替代"为了提示临时摆一个 label 再手动删掉"的场景

## 关键 API
- `we_toast_obj_init(obj, lcd, font)` —— 初始隐藏；宽 = 屏宽 − 2×边距，高 = 字体行高 + 2×`WE_TOAST_PAD_Y`，停靠顶部
- `we_toast_show(obj, text, duration_ms)` —— 滑入 → 停留 → 滑出；`duration_ms=0` 用默认 `WE_TOAST_DEF_DURATION`
- `we_toast_set_colors(obj, 底色, 文字色)`
- `we_toast_set_font(obj, font)` —— 字体可配置；高度按新字体行高重算，隐藏态零标脏、显示中旧/新几何各标脏一次立即生效
- `we_toast_set_margin(obj, top, side)` —— 停靠 Y 与左右边距可调（宽度/位置重算）；滑入/停留中调用会复用重入机制平滑滑到新停靠位
- `we_toast_obj_delete(...)` —— 内部先 `we_anim_stop` 再 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_TOAST_MARGIN_X`（默认 10）/ `WE_TOAST_DOCK_Y`（默认 8）——
  仅为 init 默认值，运行期以 `we_toast_set_margin` 为准
- `WE_TOAST_PAD_Y`（默认 8）/ `WE_TOAST_RADIUS`（默认 8）
- `WE_TOAST_TEXT_PAD`（文本左右内边距，默认 4；超出可用宽走省略号）
- `WE_TOAST_ANIM_MS`（默认 200）/ `WE_TOAST_DEF_DURATION`（默认 1500）

## 事件与行为
- **非模态真穿透**：class 的 `event_cb` 为 NULL，核心输入分发完全跳过本控件，横幅覆盖区域的触摸仍然落到背后控件
- **不占 popup_layer**：LCD 级 overlay popup 是单槽资源（归 dropdown 等真弹层），toast 走普通对象 + `we_obj_bring_to_front` 置顶
- 单个**中央动画节点**驱动 ENTER/STAY/EXIT 三阶段状态机（Q8 进度 + `we_ease_out_quad`，不占 GUI timer 槽）；滑出完成后自行摘链，空闲零开销
- 显示中（含滑入/滑出动画中）再次 `we_toast_show`：重置文本与停留计时，从**当前位置/透明度**平滑重入，不跳变；文本/配色即时生效
- 隐藏态 draw_cb 直接 return，包围盒停在屏外
- **滑动步进标脏合并**：位置由控件手动管理（直接改 `base.y`，绕开 `we_obj_set_pos`——其通用路径固定"旧一次 + 新一次"两次提交，无法表达 union）；每步把旧/新包围盒 union 成一个矩形单次 `we_obj_invalidate_area` 提交，比两次提交省一个脏矩形合并器槽位

## 渲染说明
- PFB 窗口收窄到面板矩形，任何文字墨迹不会画出横幅之外
- 文本放得下（≤ 面板宽 − 2×`WE_TOAST_TEXT_PAD`）时水平居中；超宽时
  左对齐起绘，尾部截断加 "..."：逐字符累计 `adv_w`（`we_font_get_glyph_info`，
  mlabel 省略号思路的简化版）求出前缀可容纳宽度，前缀**零拷贝**绘制
  （临时把 PFB 右界收到前缀末端画整串，右侧字形被窗口裁掉），
  省略号在前缀末端单独补画；面板窄到连 "..." 都放不下时退化为硬裁剪
- 零 malloc、零栈缓冲、渲染路径零浮点

## 注意事项
- 文本字符串由**调用方持有**且需在显示期间保持有效（控件只存指针）
- 每个 toast 实例独立；同屏多条 toast 会互相重叠（本控件不做堆叠排队）
- 超宽文本每次重绘都会重新量取前缀宽（滑动期间每帧一次，字形信息
  查表量级与绘制本身相当，可接受）
- `we_toast_set_margin` 在停留中调用会重置停留计时（复用平滑重入机制）

## 已完成的毕业优化
- [x] 滑动期间标脏合并：动画每步旧/新 bbox 做 union 后一次
  `we_obj_invalidate_area` 提交（x/w/h 不变仅 y 平移，union 只比单帧
  bbox 高 |dy|）；位置维持"手动管理 + 手动标脏"口径并注释了绕开
  `we_obj_set_pos` 的理由
- [x] 字体参数化：新增 `we_toast_set_font`（高度按新字体行高重算，
  隐藏态更新屏外停放位零标脏，显示中旧/新几何各标脏一次）
- [x] 停靠参数化：新增 `we_toast_set_margin(top, side)`（宽度/位置
  重算；显示中平滑滑到新停靠位，滑出中不干预）
- [x] 文本超宽省略：尾部截断加 "..."（`we_font_get_glyph_info` 逐字符
  累计前缀宽，前缀经 PFB 右界收窄零拷贝绘制），替代原硬裁剪
- [x] 保底行为不变：ENTER/STAY/EXIT 状态机、重复 show 平滑重入、
  event_cb 为 NULL 不拦输入，全部保持原语义

## 毕业前需优化
- [ ] 无排队/堆叠机制：连续多条提示只保留最后一条（重入覆盖）。
  原因：FIFO 队列需要额外文本槽或调用方缓冲协议（零 malloc 约束下
  属 API 面设计决策），且"最后一条最重要"在多数 MCU 场景可接受，
  留给毕业迁移时统一定夺
- [ ] 底部停靠未实现：`set_margin` 只参数化了顶部停靠位。
  原因：底部停靠需要滑入/滑出方向与屏外停放位整套镜像（状态机
  分支翻倍），本轮聚焦参数化，方向扩展留待毕业评估

## 对应 demo
- `Demo/demo_toast.c`（DEMO_ID 28）：每 2.2 秒轮换弹出 5 条不同文案/底色的 toast（最后一条超宽长文本演示省略号），配固定说明 label 与一个 btn（点击立即弹出一条），演示"动画进行中重复 show"平滑重入
