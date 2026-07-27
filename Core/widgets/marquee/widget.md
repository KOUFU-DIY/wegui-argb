# marquee

## 功能
跑马灯标签：固定宽度的单行文本窗口。文本宽不超过控件宽时静止左对齐；超过时循环滚动，绘制 "文本 + 间隔(40px) + 文本开头" 两段形成无缝循环，滚完一轮在接缝处停留 `pause_ms` 后继续。

## 适用场景
- 歌名 / 通知 / 长状态文本在窄条区域内展示
- 列表条目里放不下的单行说明
- 需要"放得下就别动，放不下才滚"自适应行为的文本条

## 关键 API
- `we_marquee_obj_init(obj, lcd, x, y, w, text, font, color)` —— 高度自动 = 字体行高 + 2×`WE_MARQUEE_PAD_Y`
- `we_marquee_set_text(...)` —— 换文本并重置滚动位置
- `we_marquee_set_font(obj, font)` —— 换字库（font2c internal）：重测宽度、重置滚动、高度随行高更新
- `we_marquee_set_speed(px_per_s)` —— 默认 30 px/s，钳制 1..2000
- `we_marquee_set_pause(ms)` —— 接缝停留，默认 800ms，0 = 不停留
- `we_marquee_set_color(...)` / `we_marquee_set_opacity(...)`
- `we_marquee_obj_delete(...)` —— 内部先 `we_anim_stop` 再 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_MARQUEE_DEF_SPEED`（默认 30）
- `WE_MARQUEE_DEF_PAUSE`（默认 800）
- `WE_MARQUEE_GAP`（默认 40）
- `WE_MARQUEE_PAD_Y`（默认 2）
- `WE_MARQUEE_SPEED_MAX`（默认 2000）

## 事件与行为
- 装饰性控件：class 的 `event_cb` 为 NULL，核心输入分发完全跳过（触摸真穿透给背后控件）
- 滚动由**单个中央动画节点**推进（`we_anim_t`，不占 GUI timer 槽）；文本装得下或透明度为 0 时自动摘链，零空转
- 毫像素整数累计（`frac_acc += elapsed_ms × speed`，int32），无浮点、无累计漂移
- `we_marquee_set_text` 不做指针相等短路——调用方可能原缓冲区改写内容后重新 set
- `we_marquee_set_font` 指针未变直接返回；行高读不出的非法字库被拒绝，控件状态不变

## 注意事项
- 文本字符串由**调用方持有**，控件只存 `const char *` 指针，不拷贝
- 字体经 init 传入，可用 `we_marquee_set_font` 更换（仅支持 font2c internal 字库；外挂 flash 字库不适用）
- 仅显示第一行：绘制与测宽同口径，遇 `\n` 即止（截断语义已明确）
- draw_cb 用 group 同款 PFB 窗口收窄（save/restore `pfb_area/pfb_y_start/pfb_y_end/pfb_gram`）裁剪越界字形

## 已完成的毕业优化
- 不可见字形跳过：自建窗口化字形绘制循环（`we_font_get_glyph_info` + `we_font_get_bitmap_info` + 行对齐 alpha blit）取代 `we_draw_string` 全量遍历——窗口左侧完全裁掉的字形只做 UTF-8 解码 + adv_w 游标快进（零位图取址、零像素扫描），游标越过窗口右缘立即 break，两段文本各自提前结束；窗口 = 控件矩形 ∩ 当前脏区带，局部重绘时快进范围自动进一步收窄；blit 与核心行对齐路径同语义（任意 bpp 单游标 + `a_raw × (255/((1<<bpp)-1))` 单乘 alpha 展开），弃用 `we_draw_string` 是因为它无法从字符串中段起绘、且对行对齐位图无公开入口
- 字体参数化：新增 `we_marquee_set_font`（重测 text_w、重置滚动、控件高度随行高更新；高度变化先标脏旧矩形再改再标脏新矩形，缩小时下沿不留残影）
- 文本像素宽缓存：text_w 在 init/set_text/set_font 时一次测量缓存，draw 内零重测（核对确认原实现已缓存，本轮补上 set_font 路径并在文档固化该约定）
- 单行截断语义明确：绘制遇 `\n` 即止，与 `we_get_text_width` 只测第一行的口径一致

## 毕业前需优化（未完成项）
- 标脏按整控件包围盒：滚动每帧仍全窗重绘。原因：滚动是整窗内容平移，窗口内每个像素列的内容都在变化，"按水平位移做增量脏区"只对新露出的边条成立、对存量区域不成立；除非引入 PFB 帧间搬移（scroll blit）图元，否则增量脏区无收益。该项属核心渲染管线能力，不在控件层解决

## 对应 demo
- `Demo/demo_marquee.c`（DEMO_ID 27）：长文本滚动、装得下静止对照、不同速度/颜色/停留三条对比，底衬 box 展示裁剪边界
