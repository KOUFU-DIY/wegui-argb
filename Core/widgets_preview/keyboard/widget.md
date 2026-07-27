# keyboard（preview）

## 功能
软键盘：内置 3 个页面布局（小写字母 / 大写字母 / 数字符号），单控件承载整面键盘。普通键通过回调回传键面字符串，退格传 `"\b"`、空格传 `" "`；SH（shift）与 `123`/`abc` 页面切换在控件内部消化，不回调。

## 实现方式选择（方案 B：自绘份数网格）
任务给出两个方案：A（内嵌 `we_btnmatrix_obj_t` 组合 + 给 btnmatrix 增加 labels 切换 API）、B（自绘网格）。本控件选择 **B**，原因：

1. **等分网格无法表达键盘布局**：btnmatrix 每格等宽，而键盘必须有通长空格键（本实现占 9/20 份）、加宽的 Shift/退格/切换键（3~4 份），否则空格只有一格宽、`"123"` 标签在 22px 等分格里放不下；
2. **组合方案的链表管理成本更高**：btnmatrix 在自身 init 里 `we_obj_attach_to_lcd` 直接挂顶层链表，内嵌后删除/移动/Z 序都要 keyboard 额外同步（还需 offsetof 从 btnmatrix 指针反推 keyboard 实例），复杂度反而高于自绘；
3. **preview 区允许适度重复**：绘制（圆角键底 + 居中键名）与命中/按压状态机直接借鉴 btnmatrix 的代码结构，重复量小且可控。btnmatrix 本轮**未做任何修改**，其 demo（DEMO_ID 104）行为不变。

## 适用场景
- 文本输入页：停靠下半屏，与 textarea 等回显控件配合
- 需要多页字符集（字母/数字符号）切换的输入面板

## 关键 API
- `we_keyboard_obj_init(obj, lcd, x, y, w, h, font)`
- `we_keyboard_set_key_cb(obj, cb)` —— `void cb(void *kb, const char *key)`；`key` 指向 static const 存储，生命周期贯穿运行期
- `we_keyboard_set_page(obj, WE_KEYBOARD_PAGE_LOWER/UPPER/SYMBOL)`
- `we_keyboard_set_colors(obj, 面板色, 普通键色, 按压键色, 文字色)` —— 功能键底色自动由键色向面板色混合派生
- `we_keyboard_set_opacity(obj, opacity)`
- `we_keyboard_obj_delete(obj)` —— 弹层模式内部先收弹层并摘滑动动画节点

## 弹层模式（隐藏/收回软键盘）
- `we_keyboard_popup_init(obj, lcd, h, font)` —— 不挂普通对象链表，宽=屏宽、停靠屏底，初始全隐藏
- `we_keyboard_popup_show(obj, target_textarea)` —— 占用 LCD 弹层（WE_POPUP_TYPE_KEYBOARD）自屏底滑入（Q8 状态机，WE_KEYBOARD_ANIM_MS 默认 220ms，中途可反向）；绑定目标后普通键/退格直接 we_textarea_input 注入
- `we_keyboard_popup_hide(obj)` —— 滑出收回；触发途径：点键盘上方区域 / BACK 键 / "OK" 确定键 / 本 API
- `we_keyboard_set_done_cb(obj, cb)` —— 底行新增 "OK" 确定键（4 份宽）：先收回再回调 cb(kb, target)
- 被其他弹层顶掉时经 close_cb 自动复位状态（防状态失同步）
- 顶部回显条（WE_KEYBOARD_ECHO_H，默认 26px，0 关闭）：实时镜像目标输入框内容 + 末尾光标——键盘滑入挡住输入框也能看到正在输入的文本；popup_init 的 h 为含回显条的总高
- `we_keyboard_key_nav(obj, code)`（公开）：键光标导航核心，供 ime_pinyin 等组合宿主在自己的弹层键通道里转发（BACK 返回 0 交宿主收回）

## 键盘按键聚焦（WE_CFG_ENABLE_KEY_INPUT 且 WE_KEYBOARD_USE_KEY，默认 1）
- 弹层键通道接管：方向键在键位网格移动键光标（行内回绕，跨行按 x 中心就近落键，行环回绕）
- 键光标 = 聚焦键四周 2px 描边（画在键间距带内，用 WE_CFG_FOCUS_CURSOR_* 导航色），新旧键格精细标脏
- OK 双沿击键：按下沿按压高亮、松开沿触发（弹层键通道的松开沿编码 = 键值|WE_KEY_RELEASE_FLAG）
- BACK 收回键盘；首个方向键落位到第一个键；切页后光标钳制到新页范围

## 可调宏
包含头文件前可覆盖：
- `WE_KEYBOARD_GAP`（键间距，默认 4px）
- `WE_KEYBOARD_PAD`（面板内边距，默认 3px）
- `WE_KEYBOARD_RADIUS`（键圆角，默认 6px，按键宽高自动钳制）

## 布局与几何
- 固定 4 行：3 行字符键 + 底行功能键；每行总宽划分为 20 份，键位表给出每键份数；行内份数不足 20 时自动居中（如字母页第二行 9 键）
- 键矩形边缘按 `inner_x + units * inner_w / 20` 整数求值，无累计漂移；间距通过两侧各让 `GAP/2` 实现
- 键位表（labels/spans/row_cnt 平行数组）全部 `static const` 存放在 .c 中，控件零拷贝引用；全部键面字符均在 ASCII `U+0020~U+007E` 内（内置 arial 字库全覆盖）

## 事件与行为
- `PRESSED` 命中键：记录键序号并切按压底色；命中键间距/面板边距不高亮但**仍消费**（键盘是不透明停靠面板，不允许误触穿透到下层）
- `STAY` 拖出原键：取消按压态，本次触摸不再产生点击
- `CLICKED` 在原键释放：功能键内部消化（SH 在小写/大写页间切换、`123` 切符号页、`abc` 切回小写页），普通键触发 `key_cb`
- shift 非 sticky：大写页敲出一个字母后自动切回小写页
- `opacity == 0` 时不拦截输入（与 group 隐藏语义一致）

## 注意事项
- 回调里收到的 `"\b"` 需调用方自行翻译为删除动作（与 textarea 的 `we_textarea_input` 约定一致，可直接对接）
- 切页按整控件包围盒标脏；按压/释放按单键包围盒标脏

## 毕业前需优化
- 切页整控件标脏，可改为仅标脏键面变化区域
- 每次重绘遍历当前页全部键并逐键算矩形，应按脏区求交只画相关键，并缓存行几何
- 键名文字未按键格裁剪（内置表无超长标签，自定义布局开放后需借鉴 btn 的 `_btn_draw_text_clipped`）
- shift 无 sticky（双击锁定大写）模式；无按键长按连发（退格连删）；无自定义键位表注入 API
- 无按键弹起动画/音效钩子

## 对应 demo
- `Demo/preview/demo_keyboard.c`（DEMO_ID 115：下半屏键盘 + 顶部 textarea 回显，敲键上屏、退格删除、三页切换）
