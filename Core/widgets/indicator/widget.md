# indicator

## 功能
圆形状态指示灯控件。在"熄灭/点亮"两态之间做颜色亮灭过渡，可选外发光晕。

## 适用场景
- 电源 / 连接 / 告警等状态指示
- 需要柔和亮灭过渡动画的状态灯
- 可选点击翻转的简易开关灯

## 关键 API
- `we_indicator_obj_init(...)`
- `we_indicator_set_state(...)` / `we_indicator_get_state(...)`
- `we_indicator_toggle(...)`
- `we_indicator_set_colors(...)`
- `we_indicator_set_anim(...)` — 开关动画并设置时长
- `we_indicator_set_ease(...)`
- `we_indicator_set_glow(...)`
- `we_indicator_set_clickable(...)`
- `we_indicator_set_event_cb(...)`
- `we_indicator_set_opacity(...)`
- `we_indicator_obj_delete(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_INDICATOR_USE_ANIM` — 亮灭过渡动画编译期总开关
- `WE_INDICATOR_ANIM_MS` — 默认动画时长（毫秒）
- `WE_INDICATOR_GLOW_RATIO` — 光晕相对核心圆的额外半径占比（256=1.0）
- `WE_INDICATOR_GLOW_ALPHA` — 光晕最亮时的峰值透明度
- `WE_INDICATOR_ON_R/G/B` — 默认点亮色
- `WE_INDICATOR_OFF_R/G/B` — 默认熄灭色

## 事件与行为
- 默认**只读**：不消费事件，可穿透给背后控件
- 启用 `we_indicator_set_clickable()` 后，`CLICKED` 翻转状态
- 亮灭过渡由中央动画引擎（`we_anim_t`，不占 GUI task 槽）+ `we_lerp` / `we_ease_*` 推进，时长运行时可配；到位自动摘链，空闲零开销
- 光晕由数圈同心半透明圆叠加构成

## 注意事项
- 圆由 `we_draw_round_rect_analytic_fill(d, d, r=d/2)` 退化为抗锯齿圆绘制
- 光晕全部落在 base box 内，不会漏刷脏矩形
- 关闭动画（`anim_enabled=0`）时状态切换为瞬切

## 对应 demo
- `Demo/demo_indicator.c`
