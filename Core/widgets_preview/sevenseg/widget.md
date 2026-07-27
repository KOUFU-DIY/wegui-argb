# sevenseg（preview 孵化区）

## 功能
七段数码管显示控件：用直角矩形段模拟经典 a~g 七段布局，不依赖字库，
支持 `'0'~'9'`、`'-'`、`':'`、`' '`。适合大字号时钟、计数器、仪表读数。

## 适用场景
- 数字时钟 / 倒计时（`"12:34"` 风格，冒号占一个字符位）
- 计数器、转速/温度等大数字读数
- 不想为超大数字单独生成字库的场合

## 关键 API
- `we_sevenseg_obj_init(obj, lcd, x, y, digit_h, digit_cnt)` —— 单字高 + 位数（含冒号位），总宽自动推导
- `we_sevenseg_set_text(obj, str)` —— 调用方持有字符串，控件存指针；内容变才重绘
- `we_sevenseg_set_colors(obj, on, off)` —— 亮段色 / 灭段鬼影色
- `we_sevenseg_set_ghost(obj, 0/1)` —— 灭段是否画低透明鬼影底纹
- `we_sevenseg_set_opacity(...)` / `we_sevenseg_set_pos(...)` / `we_sevenseg_obj_delete(...)`

## 可调宏
包含头文件前可用宏覆盖：
- `WE_SEVENSEG_MAX_CHARS`（文本快照上限 / 位数上限，默认 16）
- `WE_SEVENSEG_GHOST_OPA`（鬼影透明度，默认 70，会再与整体 opacity 相乘）
- `WE_SEVENSEG_DEF_ON_R/G/B`、`WE_SEVENSEG_DEF_OFF_R/G/B`（默认配色）

## 事件与行为
- 装饰性控件：`event_cb` 恒返回 0，输入穿透给背后控件
- 几何在 init 一次性定死：段厚 = `digit_h/8`（最小 2）、单字宽 = `digit_h/2`、字间距 = 段厚；每个字符（含 `':'`）占同宽单元格
- `set_text` 用内部定长快照做内容比较，demo 每帧 sprintf 进同一块静态缓冲再调用也不会白白重绘
- 渲染全部走 `we_fill_rect`（自带 PFB 裁剪与容器透明度级联），零 malloc、零浮点

## 注意事项
- 字符串由调用方持有，控件只存指针；调用方换缓冲区后必须再调一次 `set_text`
- 超出 `digit_cnt` 的字符被忽略；不足的位按空位处理（ghost 开启时画鬼影骨架）
- `':'` 恒为亮色两点，不参与鬼影

## 毕业前需优化
- 标脏按整控件包围盒；应改为"只标脏发生亮灭变化的段"（逐段 diff 上次段码）
- 段为纯直角矩形：毕业版可加 45° 斜切段帽 + 单像素 AA，观感更接近真实数码管
- 冒号位与数字位同宽偏松，可考虑可选的窄冒号位（需要把总宽推导做成内容相关或显式参数）
- 未做小数点 `'.'` 支持（右下角点位）

## 对应 demo
- `Demo/preview/demo_sevenseg.c`（DEMO_ID 107）：大号 `"12:34"` 时钟每秒分钟 +1（ghost 开启），下方小号计数器快速自增（ghost 关闭），展示两种尺寸与配色
