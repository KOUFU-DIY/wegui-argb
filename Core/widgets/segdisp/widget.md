# segdisp

## 功能
七段数码管显示控件：经典 a~g 七段 + dp 小数点布局，不依赖字库。
内容统一存为"每位一个段码字节"（bit0~6=a~g、bit7=dp，与 TM1650 等
数码管驱动芯片的段码表位序一致）：既可走文本便捷层（`'0'~'9'`、
`'A'~'F'/'a'~'f'`、`'-'`、`':'`、`'.'`、`' '`），也可整组/单位直喂段码。
段形默认 45° 斜切收尖（六边形段，可切回直角矩形）。适合大字号时钟、
计数器、仪表读数。

## 适用场景
- 数字时钟 / 倒计时（`"12:34"` 风格，冒号占一个字符位，`set_colon` 可直控闪烁）
- 计数器、转速/温度等大数字读数（`"98.6"` —— `'.'` 合并进前一位的 dp，不额外占位）
- 复用现成硬件段码表的场合（跑马灯、自定义段效果、lamp test 等段级控制）
- 不想为超大数字单独生成字库的场合

## 关键 API
- `we_segdisp_obj_init(obj, lcd, x, y, digit_w, digit_h, gap, digit_cnt, seg_t)` —— 单字宽/单字高/字间距/位数（含冒号位）/段厚全部 init 定死；除 digit_h（推导基准，必填）外传 0 = 自动：字宽 = digit_h/2（最小 3×段厚）、间距 = 段厚、段厚 = digit_h/8（自定义钳到 [2, (digit_h-2)/3]）；总宽自动推导
- `we_segdisp_set_text(obj, str)` —— 文本便捷层：set 时解码进段码数组，字符串不被引用（无存活要求）；逐位 diff，内容变才重绘
- `we_segdisp_set_segs(obj, codes, count)` —— 段码整组直控（全量快照：count 之外的位清灭、冒号标记清除；NULL = 全灭）
- `we_segdisp_set_seg(obj, pos, code)` —— 单位段码（该位若是冒号位会转回数字位）
- `we_segdisp_set_colon(obj, pos, on)` —— 把某位置成冒号位并开/关两点（时钟闪烁；熄灭时 ghost 画暗点）
- `we_segdisp_set_style(obj, style)` —— `WE_SEGDISP_STYLE_BEVEL`（默认，45° 斜切）/ `WE_SEGDISP_STYLE_RECT`（直角矩形）
- `we_segdisp_set_colors(obj, on, off)` —— 亮段色 / 灭段鬼影色
- `we_segdisp_set_ghost(obj, 0/1)` —— 灭段是否画低透明鬼影底纹
- `we_segdisp_set_dp(obj, 0/1)` —— dp 小数点显示开关（默认关：段码 bit7 照常存储但不绘制，兼容把 bit7 挪作它用的硬件段码表）
- `we_segdisp_set_opacity(...)` / `we_segdisp_set_pos(...)` / `we_segdisp_obj_delete(...)`

段码位宏：`WE_SEGDISP_SEG_A..G`、`WE_SEGDISP_SEG_DP`（bit0~bit6 = a~g，bit7 = dp）。

## 可调宏
- `WE_SEGDISP_MAX_CHARS`（位数上限，默认 8；参与结构体布局，覆盖时放 `we_user_config.h` 保证全工程一致）
- `WE_SEGDISP_GHOST_OPA`（鬼影透明度，默认 70，会再与整体 opacity 相乘）
- `WE_SEGDISP_DEF_ON_R/G/B`、`WE_SEGDISP_DEF_OFF_R/G/B`（默认配色）

## 事件与行为
- 装饰性控件：`event_cb` 恒返回 0，输入穿透给背后控件
- 几何在 init 一次性定死：单字宽/字间距/段厚均可显式指定，传 0 走自动（字宽 = `digit_h/2`、间距 = 段厚、段厚 = `digit_h/8`）；钳位规则：段厚 `[2, (digit_h-2)/3]`（保证竖段不退化）、字宽最小 3×段厚（保证横段长度 >= 段厚）；每位（含冒号位）占同宽单元格；dp 画在本位右下角空白角区（不额外占位，上/左留 1px 与 c/d 段分离）
- 斜切段形：逐行/逐列 1px `we_fill_rect` 扫描、向段中线 45° 收尖（六边形内切于矩形段，每段最多"段厚"次填充调用）；段厚 < 3 自动退化为矩形；纯整数运算
- 标脏按位：所有内容 setter 汇聚到统一的逐位 diff，只有段码/冒号变化的单元格才标脏（`"12:34"→"12:35"` 只重绘末位；demo 每帧 sprintf 进同一块静态缓冲再无脑 set_text 也不会白白重绘）
- 渲染全部走 `we_fill_rect`（自带 PFB 裁剪与容器透明度级联），零 malloc、零浮点

## 注意事项
- `set_text` 在调用时解码，不持有字符串指针（与旧版"存指针零拷贝"不同：缓冲区随便复用，无存活要求）
- `set_segs` 是全量语义：count 不足 digit_cnt 的位会清灭并清除冒号标记
- 文本中的 `'.'` 紧跟可显示字符时合并为前一位 dp；开头/连续的 `'.'` 独占一位（只亮 dp）
- **dp 显示默认关闭**：`set_text` 带 `'.'` 或段码带 bit7 都要先 `set_dp(obj, 1)` 才可见（默认关是为兼容 bit7 挪作它用的硬件段码表，如时钟模组的冒号线）
- ghost 作用于 a~g 七段与"熄灭状态的冒号点"；dp 只在点亮且开关打开时绘制（不画鬼影）
- 冒号位被 `set_colon(pos, 0)` 熄灭后单元格保持冒号形态（ghost 暗点）；用 `set_seg`/`set_text` 可把该位改回数字位
- 横段长度 = `字宽 - 2×段厚`：字宽走自动（`digit_h/2`）时段厚建议 <= 自动值（`digit_h/8`），否则横段被挤得很短、数字明显变糊；想要粗段厚就同时加大字宽（保持字宽 >= 4×段厚 观感较稳）
- `set_text` 会把 `':'` 位重置为常亮，闪烁场景在 set_text 后再按当前相位调一次 `set_colon` 即可（状态未变时零代价）

## 资源占用（STM32F030 / Cortex-M0 实测）
DEMO_ID 117 单独编译：**ROM 18.8 KB / RAM 5.95 KB**（口径同 README 逐 demo 表）。
相对基准底座（demo 1 label，17.3 KB）净增约 1.5 KB，其中控件全功能本体
（含斜切渲染、文本解码、ghost、dp、冒号、全部 setter）Code 1616 B +
RO 44 B ≈ 1.66 KB，demo 自身约 0.94 KB。无图片/字库资产，无动画节点；
RAM 侧每实例 ≈ 50 B（含 8 位段码 + 冒号位图，`WE_SEGDISP_MAX_CHARS` 可调）。

## 已完成的毕业优化
- 段码直控 API（`set_segs`/`set_seg`/`set_colon`，bit7 = dp 与硬件段码表对齐）
- dp 小数点支持（右下角点位，文本 `'.'` 自动合并；显示开关默认关）
- 45° 斜切段帽（默认风格，直角矩形可选）
- 逐位标脏（只重绘段码/冒号变化的单元格）
- `'A'~'F'` 十六进制解码
- 几何全参数化（字宽/字高/间距/段厚 init 显式指定，0 = 自动推导）

## 毕业前需优化
- 斜切边缘无抗锯齿：每条斜切行两端各补一颗半透明像素即可近似 1px AA（`we_draw_pixel`，每段最多 2×段厚 次单像素混合，仅重绘时发生），成本极低、观感提升明显 —— 建议毕业前第一优先
- 更细的"逐段标脏"（单元格内只标变化的段）：时钟分钟翻动通常只改 1~3 段，64px 大字上收益明显；小字号收益趋零，待评估
- `set_uint`/`set_int` 便捷接口（右对齐/前导零/小数位参数，免 snprintf 直推整数）：对 libc 无 snprintf 的目标（如 AD14N）是刚需级便利
- 冒号位与数字位同宽偏松，可考虑可选的窄冒号位（需要把总宽推导做成内容相关或显式参数）
- 冒号/dp 小方点可跟随斜切风格切角（纯审美，几行代码）
- 可选裁剪宏（未实现，按收益排序）：`WE_SEGDISP_USE_TEXT 0` 裁文本解码层（纯段码直控工程，估 ~400 B）、`WE_SEGDISP_USE_BEVEL 0` 强制矩形段裁逐行扫描（估 ~200 B，参照 box/line 的 `WE_xxx_USE_ANIM` 惯例）、`WE_SEGDISP_USE_GHOST 0` 裁鬼影分支（估 ~130 B）；dp/冒号/十六进制解码体量过小不值得单独设宏
- 明确不做：控件内建闪烁/动画 —— 保持零动画节点的被动装饰件定位，闪烁交给业务 tick 每帧无脑调 `set_colon`（状态未变零代价，demo 已示范）

## 对应 demo
- `Demo/demo_segdisp.c`（DEMO_ID 33）：大号 `"12:34"` 时钟（斜切段形 + 自定义段厚 6 + ghost + `set_colon` 冒号闪烁）、小号 `"NN.N"` 计数器（自定义字宽 20 + 间隔 5 + 段厚 2 宽扁细体 + 直角矩形段 + dp 小数点 + 橙色配色）、6 位段码直控外圈跑马灯（全自动几何，`set_segs` 整组喂段码）
