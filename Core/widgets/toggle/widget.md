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
- `we_toggle_set_opacity(...)`

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
- `CLICKED` 时默认翻转状态
- 若启用动画，滑块会自动通过内部 task 平滑切换

## 注意事项
- `we_toggle_set_checked(...)` 是立即跳变，用于初始化或程序同步状态
- `we_toggle_toggle(...)` 更接近用户点击效果
- `we_toggle_update_anim(...)` 现在只是兼容旧代码的空接口，动画已自动推进

## 对应 demo
- `Demo/demo_toggle.c`
