# checkbox

## 功能
复选框控件，支持勾选/取消、按压态和右侧文本，适合设置页开关项与多选项。

## 适用场景
- 设置页选项
- 列表勾选
- 多个布尔状态控制

## 关键 API
- `we_checkbox_obj_init(...)`
- `we_checkbox_set_checked(...)`
- `we_checkbox_toggle(...)`
- `we_checkbox_is_checked(...)`
- `we_checkbox_set_text(...)`
- `we_checkbox_set_styles(...)`

## 可调宏
无专属全局宏。

## 事件与行为
- 支持 `WE_EVENT_PRESSED / RELEASED / CLICKED`
- `event_cb == NULL` 时使用默认行为：点击时切换勾选状态
- `event_cb != NULL` 时可自行扩展联动逻辑

## 注意事项
- `text == NULL` 时只显示方框
- 可通过 `styles` 自定义 4 种视觉状态：ON/OFF + PRESSED
- 如果要做“总开关联动多个子项”，建议在回调里统一处理

## 对应 demo
- `Demo/demo_checkbox.c`
