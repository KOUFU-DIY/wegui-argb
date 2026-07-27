# ime_pinyin（preview）

## 功能
拼音输入法面板：自上而下 = 拼音缓冲条（回显已敲字母）+ 候选栏（一页最多 7 字 + `<` `>` 翻页键）+ 内嵌 `we_keyboard_obj_t` 软键盘。敲小写字母实时检索候选，点候选字经 commit 回调输出 UTF-8 串；宿主（如 textarea）只需把回调透传给自己的 input 接口。

检索引擎（`we_pinyin.h/.c`）与控件解耦：纯查表零 GUI 依赖，可脱离框架用 gcc 独立编译单测（`tool/pinyin2c/test_we_pinyin.c`，34/36 条断言两种 L2 编译模式全绿）。

## 数据与编码
- 音节表/候选池由 `tool/pinyin2c/gen_pinyin_table.py` 生成（`we_pinyin_table.c/.h`），全 `const` ROM 表：音节 ~400 项 × 12 字节 + 候选池每字 2 字节；
- **候选池定死为 uint16 Unicode 码点**（非 UTF-8 内联、非 GB2312 内码），commit 上屏时才经 `we_pinyin_cp_to_utf8` 转 3 字节 UTF-8；
- 当前生效表为 **l1l2 预设**（GB2312 一级 3755 + 二级 3008 字）；`WE_PINYIN_ENABLE_L2` 默认 0 = 二级段在 ROM 但不遍历，运行期 `we_ime_pinyin_set_l2` 为空操作；想彻底裁掉二级段 ROM 就用 l1 预设表覆盖（见 `tool/pinyin2c/README.md`）；
- 段内一级字在前、按内嵌高频字次序排序（"的一是不了…"头 100 字为标准频序），二级字接在后。

## 组合复用（与 keyboard 的关系）
面板本体只画/只管上面两条横带；键盘区交给内嵌 `we_keyboard_obj_t`：

1. init 时面板先挂 LCD 链、键盘后挂——事件命中取"链上最后命中者"，重叠的键盘区自然归键盘；绘制顺序面板在前、键盘在后，各画各的互不覆盖；
2. 键盘键值经 `key_cb`（`offsetof` 反推面板实例）汇入 `_ime_handle_key`，与 `we_ime_pinyin_inject_key` **完全同一入口**——自动脚本、实体键盘、真实触摸走同一条路；
3. keyboard 控件本轮**零修改**（三页布局/退格 `"\b"`/空格 `" "` 约定原样复用）。

## 键值路由（中英切换的设计）
| 键 | 缓冲空 | 缓冲非空 |
| --- | --- | --- |
| 小写字母（键盘小写页） | 进缓冲 | 进缓冲（满 7 忽略） |
| `"\b"` | 透传（宿主删字） | 删最后一个字母 |
| `" "` | 透传 | 选当前页第 1 候选（无候选忽略） |
| 其它键（数字/符号/大写页字母） | **原样透传 = 英文/符号直通** | 忽略（preview 放宽） |

即"中英切换"不设显式开关：键盘切到数字符号页（`123`）或大写页（`SH`）敲出的键值不进拼音缓冲、直接上屏；切回小写页即回中文态。

## 候选栏与翻页
- 填充候选页时**逐字调 `we_font_get_glyph_info`，返回 0（字库无此字）的候选直接跳过**——字库过滤全靠它，demo 用全字库时可换小字库观察"缺字跳过"；
- 精确命中音节按字典序必为前缀区间首项，故候选顺序天然=「精确命中优先，其后是前缀联想音节（如敲 `zho` 出 zhong+zhou 两段）」；
- 向后翻页：迭代器游标续走（`iter` 恒停在下一页起点，`has_more` 用副本预探）；回翻：最近页起点栈（深 16，溢出挤掉最旧页）；
- 非法音节/全部缺字：候选栏整栏灰字"无候选"。

## 关键 API
- `we_ime_pinyin_obj_init(obj, lcd, x, y, w, h, font)` —— h 为总高（含键盘区），font 须含中文字形
- `we_ime_pinyin_set_commit_cb(obj, cb)` —— `void cb(void *ime, const char *utf8)`；utf8 指向控件内部小缓冲或键盘 static 键面，**仅回调期间有效**
- `we_ime_pinyin_inject_key(obj, key)` / `we_ime_pinyin_select(obj, slot)` / `we_ime_pinyin_page(obj, dir)` —— 脚本化输入三件套
- `we_ime_pinyin_set_l2(obj, 0/1)` —— 运行期二级字开关（引擎级全局状态；未编译 L2 时空操作）
- `we_ime_pinyin_set_colors(obj, 面板色, 拼音条色, 文字色, 按压色)` —— 提示灰字自动派生；键盘配色走 `we_keyboard_set_colors(&obj->kb, ...)`
- `we_ime_pinyin_set_opacity(obj, opacity)` —— 同步透传内嵌键盘；0 = 整体不拦截输入
- `we_ime_pinyin_obj_delete(obj)` —— 先删内嵌键盘再摘本体；无动画节点，无需 `we_anim_stop`

引擎侧：`we_pinyin_match(input, &first, &count)`（二分前缀区间，精确命中返回索引否则 -1）、`we_pinyin_iter_init/next`、`we_pinyin_set_l2/get_l2`、`we_pinyin_cp_to_utf8`。

## 可调宏
包含头文件前可覆盖：`WE_IME_PINYIN_BUF_H`（拼音条高，默认 22）、`WE_IME_PINYIN_CAND_H`（候选栏高，默认 30）、`WE_IME_PINYIN_PAGER_W`（翻页键宽，默认 26）。

## 硬约束达成
- 零 malloc（所有状态内嵌结构体，回翻栈定长数组）；
- 渲染/检索内环无 float（二分 + 整数边缘求值 + `we_get_text_bbox` 整数居中）；
- 引擎表全 `const` ROM；
- 无动画节点（按压反馈即时翻转，delete 无摘链负担）；
- 事件返回 1 消费；setter 全部幂等（值未变直接返回）。

## 标脏粒度
- 缓冲/候选变化：拼音条、候选栏两条横带各自标脏（键盘区永不连带）；
- 按压/释放/拖出：只标单个槽位矩形；
- 翻页：只标候选栏横带。

## 注意事项
- `has_more` 预探与缺字过滤都要扫池，最坏 O(区间剩余候选数)，只发生在翻页/重填时（≈几千次 `we_font_get_glyph_info`，PC/主流 MCU 无感）；
- 二级字开关是引擎级全局（`we_pinyin.c` 内 static），多实例共享；
- 不支持 `we_obj_set_pos` 移动（内嵌键盘坐标不跟随）。

## 毕业前需优化
- 词组输入（当前纯单字）与用户词频学习；
- 缓冲非空时敲符号应"上屏首选 + 跟标点"而非忽略；
- 回翻栈溢出后无法回到最早页（可加"回第一页"手势）；
- 候选栏横带整条标脏可再细分到变化槽位；
- 移动支持（set_pos_cb 联动内嵌键盘）；
- 多实例隔离的 L2 开关（引擎状态入参化）。

## 对应 demo
- `Demo/preview/demo_ime_pinyin.c`（DEMO_ID 113：textarea 输入框 + 弹层 IME（点击呼出/收回）；自动演示脚本循环敲 `ni`→你、`hao`→好、`shi`→世、`jie`→界再退格清空，真实触摸全程可用）

## 弹层模式（2026-07-27 新增，keyboard 弹层同款壳）
- `we_ime_pinyin_popup_init(obj, lcd, h, font)` —— 面板与内嵌键盘均不挂对象链表，弹层统一承载；h = 回显条(WE_KEYBOARD_ECHO_H) + 拼音条 + 候选栏 + 键盘区
- `we_ime_pinyin_popup_show(obj, target_textarea)` / `we_ime_pinyin_popup_hide(obj)` —— 屏底 Q8 滑入/收回；收回途径：点面板上方 / BACK / hide API；被其他弹层顶掉经 close_cb 自动复位
- `we_ime_pinyin_bind_textarea(ta, ime)` —— 点击输入框呼出（textarea 通用编辑器绑定的输入法特化）
- 顶部回显条实时镜像目标输入框内容（键盘挡住输入框也能看到输入）；候选上屏/透传键经 _ime_emit 直接 we_textarea_input 注入目标
- 弹层键通道：BACK 收回，其余转发 `we_keyboard_key_nav`（方向键移键光标、OK 双沿击键）
- 弹层触摸按"按下时的区域"路由整个触摸序列（候选栏/键盘区不串台）