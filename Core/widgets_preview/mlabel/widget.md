# mlabel（preview 孵化区）

## 功能
多行文本标签：在固定 w×h 文本框内自动折行绘制 UTF-8 文本。支持显式 `\n` 强制换行、英文按空格断词（行宽超出回退到最近空格）、无空格片段（长单词/中文）按字符断行、左对齐/行居中、可调行间距、超高截断末行追加 "..."（可关）。折行在 draw 时流式执行（逐字符 UTF-8 解码 + `we_font_get_glyph_info` 步进宽累计），不缓存行表。

## 适用场景
- 说明文字 / 帮助信息 / 消息正文等多行段落
- 固定卡片区域内的可变长文本（配 ellipsis 防溢出）
- 居中排版的短诗 / 标语（`WE_MLABEL_CENTER`）

## 关键 API
- `we_mlabel_obj_init(obj, lcd, x, y, w, h, text, font)` —— 文本调用方持有，字体经 init 传入
- `we_mlabel_set_text(obj, text)` —— 换文本（重排 + 重绘；不做指针相等短路）
- `we_mlabel_set_color(obj, color)` / `we_mlabel_set_opacity(obj, opacity)`
- `we_mlabel_set_align(obj, WE_MLABEL_LEFT / WE_MLABEL_CENTER)`
- `we_mlabel_set_line_gap(obj, px)` —— 行高 = 字体行高 + gap（默认 2）
- `we_mlabel_set_ellipsis(obj, 0/1)` —— 超高截断末行 "..."（默认 1）
- `we_mlabel_obj_delete(obj)` —— 无动画节点，直接转调 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_MLABEL_DEF_LINE_GAP`（默认行间距，2px）
- `WE_MLABEL_LINE_BUF`（单行拷贝栈缓冲，默认 128 字节；draw 栈深随之变化）

## 折行算法（draw 时流式执行）
1. 逐字符 UTF-8 解码（1/2/3 字节，与 `we_font_text.c` 同口径的局部 helper；4 字节及以上按非法单字节跳过，截断序列直接停排）；
2. 每字符经 `we_font_get_glyph_info` 取 adv_w 累计行宽，无字形字符按零宽跳过（字节仍随行携带）；
3. `\n` 强制换行（换行符不计入行内容）；
4. 行宽即将超出 w 且行内已有内容时：有空格断点→回退到最近空格（该空格丢弃），无→当前字符整体归下一行（字符断行）；
5. 首字符步进宽即超 w 时仍收进本行（超宽字符独占一行，防死循环），墨迹越界部分被收窄后的 PFB 窗口裁掉；
6. 完整行高放不下的行不画；ellipsis 开启时最后一个可容纳行若还有剩余文本，按 "剩余可用宽 = w - 3×'.' 宽" 求最大前缀后追加 "..."。

## 渲染模型与成本
- 每行内容 memcpy 进栈缓冲补 '\0' 后交 `we_draw_string` 绘制；绘制期间 PFB 窗口收窄到自身矩形（marquee/group 同款 save/restore），越界墨迹自动裁掉
- 每帧成本 ≈ 全文一遍 UTF-8 解码 + 字形信息查询（折行扫描）+ 可见行的正常字形渲染；零 malloc、零浮点
- 控件本身不持文本副本，仅存 `const char *`

## 事件与行为
- 装饰性控件：event_cb 恒返回 0，输入穿透给背后控件
- 所有 setter 值未变时直接返回（set_text 除外，见 API 说明）
- 空串 / NULL 文本安全：直接不绘制

## 边界情况
- **长单词**：行内无空格断点时按字符断行，词被硬切到下一行
- **纯中文**：无空格文本天然走字符断行路径（注意：demo 默认 ASCII16 字库仅覆盖 ASCII 0x20~0x7E，CJK 码点解码正常但无字形、按零宽跳过不可见；换含 CJK 的 internal 字库即可显示）
- **单字符宽度超 w**：该字符独占一行并照画，越出 w 的墨迹被 PFB 窗口裁掉（居中时向两侧越出）
- **空串**：不进折行循环，画面为空；连续 `\n\n` 产生空行（正常占行高）
- **尾随空格/行首空格**：断词只丢弃回退点那一个空格，连续多空格断行后行尾可能残留空格、字符断行时空格可能落到下一行行首（视觉可忽略）

## 注意事项
- 单行字节数超过 `WE_MLABEL_LINE_BUF - 4` 时该行提前按字符断行（280px 屏 + 16px 字体实际达不到该上限）
- 行高恒为 `字体行高 + line_gap`，不随字形实际墨迹高变化
- draw_cb 内有 `WE_MLABEL_LINE_BUF` 字节栈缓冲，深嵌套调用链上需留意栈深

## 毕业前需优化
- **行起点缓存**：set_text/set 布局参数时扫描一次并缓存各行起点+行宽（调用方给缓存数组或上限行数），draw 直接用，去掉每帧全文重排
- 脏矩形：任何变化按整控件包围盒标脏；文本变更可做逐行 diff 只刷变化行（复用 `we_text_invalidate_lines` 思路）
- 折行扫描与绘制各解码一遍 UTF-8（扫描 + we_draw_string 内部），行表缓存后可消除重复解码
- 空格断词可扩展为 CJK 标点禁则（行首禁排 "，。" 等）、连字符断词
- 尾随/行首空格修剪；TAB 展开
- 对齐方式补 RIGHT；ellipsis 支持单行模式（h 恰好一行时当前逻辑已兼容但未特调）
- 字体参数化（已完成：init 必传）

## 对应 demo
- `Demo/preview/demo_mlabel.c`（DEMO_ID 127：三块 mlabel——长英文段落断词 + 长无空格 token 字符断行（硬截断对比）+ 居中短诗；A 块每 4 秒两段文本互换演示重排）
