# logview（preview 孵化区）

## 功能
滚动日志窗：深色圆角面板 + 等行高日志文本 + 右缘细滚动条。`we_logview_push()` 逐条追加，最新行在底部；行存储为**调用方提供**的扁平二维缓冲（line_cnt 行 × line_len 字节），环形复用（写满覆盖最旧行），控件自身零 malloc。拖拽上下滚动查看历史，拖离底部自动暂停跟随，拖回最底或 `we_logview_set_follow(1)` 恢复贴底跟随。

## 适用场景
- 设备调试/运行日志实时输出
- 串口/协议收发记录窗
- 任意"滚动追加 + 回看历史"的文本流面板

## 关键 API
- `we_logview_obj_init(obj, lcd, x, y, w, h, line_buf, line_len, line_cnt, font)` —— line_buf 生命周期须覆盖控件；容量参数非法直接拒绝初始化
- `we_logview_push(obj, str)` —— 拷贝进环形行缓冲（超长截断恒补 `\0`），跟随态自动滚底
- `we_logview_clear(obj)` —— 清空全部日志并复位滚动（跟随开关维持原状）
- `we_logview_set_colors(obj, bg, text)` —— 面板底色 / 文字色
- `we_logview_set_follow(obj, 0/1)` / `we_logview_get_follow(obj)` —— 跟随暂停/恢复（恢复立即滚到最新）
- `we_logview_set_opacity(obj, opacity)`
- `we_logview_obj_delete(obj)` —— 无动画节点，直接摘链

## 可调宏
包含头文件前可覆盖：
- `WE_LOGVIEW_TEXT_PAD`（默认 8，行文字左内边距）
- `WE_LOGVIEW_V_PAD`（默认 4，面板上下内边距）
- `WE_LOGVIEW_DEF_RADIUS`（默认 8，面板圆角）
- `WE_LOGVIEW_DRAG_THRESHOLD`（默认 5，进入拖拽的位移阈值）
- `WE_LOGVIEW_SB_WIDTH` / `WE_LOGVIEW_SB_MARGIN` / `WE_LOGVIEW_SB_OPA`（滚动条几何/透明度）

## 存储与滚动模型
- 环形行缓冲：`head` 为下一写入槽，逻辑行 j（0=最旧）→ 物理槽 `(head - used + j) mod line_cnt`
- `scroll_px` = 距"内容底部对齐"的**向上**偏移（0 = 贴底最新行），硬夹紧无回弹
- 内容不足一屏时从面板顶部排布（终端习惯）
- push 语义：跟随态 scroll_px 钉 0；非跟随态 scroll_px += row_h 让视口画面保持不动（含写满覆盖最旧行的情形，越界夹紧兜底）
- 行高 = `we_font_get_line_height(init 字体) + 2`

## 渲染模型与成本
- 面板背景 `we_draw_round_rect_analytic_fill`；行文字经 PFB 收窄裁剪（group/list 同款 save/restore 套路）绘制在内边距矩形内，半露行不渗出面板
- 每行做一次 `we_get_text_bbox` 垂直居中；字体经 init 传入
- 滚动条 = 胶囊滑块（无轨道），高按"视口/内容"比例（下限 8px），位置按 `(max - scroll) / max` 距顶比例插值，内容溢出才显示
- 零 malloc、内环零浮点；无动画节点

## 事件与行为
- event_cb 恒返回 1（交互控件，消费全部触摸序列事件）
- PRESSED 记录起点；STAY 位移超阈值进入拖拽，内容跟手（手指下移看更早历史）；无惯性
- 拖拽把 scroll_px 拖到 > 0 → follow 自动清 0；拖回 0 → follow 自动置 1
- 所有 setter 值未变时直接返回；无 set_pos_cb（几何全由 base.x/y 推导）

## 注意事项
- `line_len` 含结尾 `\0`，建议 >= 24；单行超长截断（按字节截断，UTF-8 多字节字符可能截半，显示端字库查不到即跳过）
- push 的 str 拷贝后即可释放/复用
- 行数容量 = line_cnt，写满后旧行不可找回
- 面板高度建议 >= 3 行高，宽度建议 >= 120px

## 毕业前需优化
- 脏矩形：push/拖拽均整控件包围盒标脏；应改为滚动区块搬移 + 仅新行/露出行差分重绘
- 拖拽可加松手惯性（参考 list 的 7/8 速度衰减惯性节点）与到边回弹
- 超长行目前截断，可加水平滚动或跑马灯（复用 marquee 思路）
- UTF-8 截断可回退到字符边界（当前按字节硬截）
- 行级颜色（error/warn/info 分色）与时间戳前缀钩子
- 滚动条可加淡出动画（参考 dropdown 滚动条渐隐）

## 对应 demo
- `Demo/preview/demo_logview.c`（DEMO_ID 120：200x150 日志窗占中部，每 400ms push 一条模拟日志（循环模板 + 递增序号），演示自动滚底与拖拽暂停跟随；顶部说明 label "drag to pause follow"）
