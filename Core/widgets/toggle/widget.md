# toggle

## 功能
iOS 风格拨动开关控件，支持按压态、开关态和可选平滑动画。

## 适用场景
- 设置页 ON/OFF 开关
- 布尔选项切换
- 需要更直观视觉反馈的单选布尔控件

## 关键 API
- `we_toggle_obj_init(...)`
- `we_toggle_set_checked(...)`
- `we_toggle_toggle(...)`
- `we_toggle_is_checked(...)`
- `we_toggle_set_changed_cb(...)` — 状态改变回调（替代轮询）
- `we_toggle_set_opacity(...)`
- `we_toggle_obj_delete(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_TOGGLE_USE_ANIM`
- `WE_TOGGLE_ANIM_STEPS`
- `WE_TOGGLE_ANIM_STEP_MS`
- `WE_TOGGLE_THUMB_PAD`
- `WE_TOGGLE_COLOR_ON_R/G/B`
- `WE_TOGGLE_COLOR_OFF_R/G/B`
- `WE_TOGGLE_COLOR_THUMB_R/G/B`
- `WE_TOGGLE_PRESS_DARKEN`

## 事件与行为
- 支持 `WE_EVENT_PRESSED / RELEASED / CLICKED`
- `CLICKED` 时默认翻转状态，并触发 `changed_cb`（若已注册）
- 若启用动画，由中央动画引擎平滑切换，到位自动摘链
- 设了用户 `event_cb` 时全部触摸事件交给它：不再有默认的按压压暗与点击翻转（需要时在回调里自行 `we_toggle_toggle`）；按键 OK 同样转成 CLICKED 交给用户回调
- 全透明时不接触摸，按压交给外层

## 注意事项
- `we_toggle_set_checked(...)` 是立即跳变，用于初始化或程序同步状态
- `we_toggle_toggle(...)` 更接近用户点击效果

## 已完成的毕业优化
- 全透明时不接触摸（与拒绝聚焦的口径一致），按压交给外层
- 删除接口改为内联包装（动画节点由内核摘链）；补上用户回调接管全部触摸事件的说明

## 对应 demo
- `Demo/demo_toggle.c`
