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
- 若启用动画，由中央动画引擎（不占 GUI task 槽）平滑切换，到位自动摘链

## 注意事项
- `we_toggle_set_checked(...)` 是立即跳变，用于初始化或程序同步状态
- `we_toggle_toggle(...)` 更接近用户点击效果

## 对应 demo
- `Demo/demo_toggle.c`
