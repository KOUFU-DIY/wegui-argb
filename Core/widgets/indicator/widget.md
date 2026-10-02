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
- 默认**只读**：只读且没有用户回调时 event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件，也不可聚焦
- 启用 `we_indicator_set_clickable()` 后，`CLICKED` 翻转状态；改回只读时收起按压视觉，正持有焦点则清掉焦点
- 设了 `we_indicator_set_event_cb()` 后全部输入事件交给用户回调
- 亮灭过渡由中央动画引擎（`we_anim_t`）+ `we_ease_*` 推进，时长运行时可配；到位自动摘链，空闲零开销
- 光晕由数圈同心半透明圆叠加构成

## 注意事项
- 圆由 `we_draw_round_rect_analytic_fill(d, d, r=d/2)` 退化为抗锯齿圆绘制
- 光晕全部落在 base box 内，不会漏刷脏矩形
- 关闭动画（`anim_enabled=0`）时状态切换为瞬切

## 已完成的毕业优化
- 只读灯真正穿透输入：按“可点击或有用户回调”在两个类描述符之间切换，只读类 event_cb 为 NULL、不可聚焦
- 修正：按住时改为只读，灯不再停在压暗状态
- 光晕三圈的 alpha / 半径计算不再做库除法（`we_div255` 与乘移位代替 /255、/3，结果逐位相同）
- 亮灭推进改用共用的 `we_motion_advance`（熄灭方向按距离镜像）；删掉只写不读的 `anim_acc_ms`
- `set_colors` 值未变不重绘；删除接口改为内联包装（动画节点由内核摘链）

## 对应 demo
- `Demo/demo_indicator.c`
