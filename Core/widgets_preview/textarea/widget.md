# textarea（preview）

## 功能
单行输入框：圆角底框 + 左对齐文本 + 末尾 2px 光标（仅编辑中显示，500ms 亮灭；弹层键盘/输入法 show/hide 自动接管编辑态，we_textarea_set_editing 可手动控制）。文本缓冲由调用方提供（零 malloc），键值注入式输入（`"\b"` 退格），文本超宽时左移显示尾部（PFB 收窄裁剪掉头部）。空内容显示灰色占位提示。

## 适用场景
- 与 keyboard 软键盘配合的文本输入回显框
- 任何"程序注入字符流 + 光标反馈"的单行显示（扫码回显、串口输入等）

## 关键 API
- `we_textarea_obj_init(obj, lcd, x, y, w, buf, buf_size, font)` —— `buf` 调用方提供（含结尾 0，既有内容保留显示）；高度自动 = 字体行高 + 2 × `WE_TEXTAREA_PAD_Y`
- `we_textarea_input(obj, key)` —— 追加键面字符串；`"\b"` 按 UTF-8 字符退格（多字节字符整体删除）；余量不足整体忽略；与 keyboard 的 `key_cb` 键值约定一致，可直接透传
- `we_textarea_clear(obj)` —— 清空内容（空内容时直接返回）
- `we_textarea_get_text(obj)` —— 返回缓冲区指针（始终 0 结尾）
- `we_textarea_set_placeholder(obj, str)` —— 空内容时灰色提示（调用方持有）
- `we_textarea_set_colors(obj, 底色, 文字色, 光标色)`
- `we_textarea_set_opacity(obj, opacity)`
- `we_textarea_obj_delete(obj)` —— 内部先 `we_anim_stop` 摘除闪烁动画节点

## 可调宏
包含头文件前可覆盖：
- `WE_TEXTAREA_PAD_X`（左右内边距，默认 8px）
- `WE_TEXTAREA_PAD_Y`（上下内边距，默认 6px，参与控件高度计算）
- `WE_TEXTAREA_RADIUS`（底框圆角，默认 8px，按宽高自动钳制）
- `WE_TEXTAREA_CURSOR_W`（光标宽度，默认 2px）
- `WE_TEXTAREA_BLINK_MS`（闪烁半周期，默认 500ms）

## 事件与行为
- 无全局焦点系统：控件视为**常聚焦**，光标常闪
- `PRESSED`/`RELEASED`：仅作按压视觉反馈（底色向白整数混色微亮），`event_cb` 返回 1 消费；`opacity == 0` 时不拦截
- 任何内容变化（input/clear）都把光标拉回亮相位并复位闪烁计时（输入期间光标常亮的通用手感）
- 光标闪烁走中央动画引擎（`we_anim_t`，不占 GUI task 槽）；**删除控件必须走 `we_textarea_obj_delete`**（内部摘链），直接 `we_obj_delete` 会在动画链上留悬空指针

## 溢出滚动细节
- 可视文本宽 = 内容区宽（`w - 2*PAD_X`）− 光标预留（`CURSOR_W`）
- 文本实测宽超出时绘制起点左移 `text_w - avail_w`，光标恰好落在内容区最右缘；文本头部落在收窄后的 PFB 窗口外被逐像素裁掉，不污染圆角与边距

## 注意事项
- `buf`/`placeholder` 均由调用方持有；控件只写 `buf`，不持有副本
- `buf_size` 含结尾 0：可容纳文本字节数 = `buf_size - 1`
- init 对既有缓冲内容做一次 strlen（超过 `buf_size-1` 会截断写 0，API 边界防御）

## 毕业前需优化
- ~~光标闪烁整控件标脏~~（已完成：翻转只标光标竖条精细脏矩形）；内容变化仍整控件标脏（可再细化为文本区）
- 每次输入全量 `we_get_text_width` 重测：应做增量宽度缓存（追加时累加、退格时回减）
- 无文本选择/插入点移动（光标固定在末尾）；无横向手势拖动查看头部
- 无输入过滤/最大字符数（非字节数）限制钩子
- 按压反馈色为固定白色混合，未开放配置

## 对应 demo
- `Demo/preview/demo_textarea.c`（DEMO_ID 102：双输入框 + 共享弹层软键盘完整输入链路，演示 placeholder、光标闪烁、溢出滚动与键盘滑入收回）
