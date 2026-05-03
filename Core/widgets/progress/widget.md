# progress

## 功能
横向进度条控件，使用 `0~255` 的手动进度值，支持平滑数值动画和细分脏矩形更新。

## 适用场景
- 下载 / 加载进度
- 电量 / 音量 / 百分比条
- 需要手动驱动进度动画的页面元素

## 关键 API
- `we_progress_obj_init(...)`
- `we_progress_set_value(...)`
- `we_progress_add_value(...)`
- `we_progress_sub_value(...)`
- `we_progress_get_value(...)`
- `we_progress_get_display_value(...)`
- `we_progress_set_opacity(...)`
- `we_progress_set_radius(...)`
- `we_progress_set_colors(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_PROGRESS_ANIM_DURATION_MS`
- `WE_PROGRESS_RADIUS`
- `WE_PROGRESS_USE_BTN_SKIN`

## 事件与行为
- 当前不处理用户输入事件
- 内部使用 GUI task 自动推进数值过渡动画

## 注意事项
- 目标值范围固定为 `0~255`
- `value` 是目标值，`display_value` 是当前动画中的显示值
- 进度变化时只标脏变化带，不整条全刷

## 对应 demo
- `Demo/demo_progress.c`
