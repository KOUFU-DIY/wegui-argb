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
- `we_checkbox_set_changed_cb(...)` — 勾选改变回调（替代轮询）
- `we_checkbox_set_text(...)`
- `we_checkbox_set_styles(...)`

## 可调宏
无专属全局宏。

## 事件与行为
- 支持 `WE_EVENT_PRESSED / RELEASED / CLICKED`
- **按压视觉始终由控件默认维护**（叠加语义），用户回调只写业务、无需补样式
- `event_cb == NULL`：点击自动切换勾选并触发 `changed_cb`（若已注册）
- `event_cb != NULL`：点击业务由回调接管（默认勾选不执行，需要时回调内自行 `we_checkbox_toggle`）
- 类回调恒返回 1，容器（group/slideshow/scroll_panel）据此正确锁定转发

## 注意事项
- `text == NULL` 时只显示方框
- 可通过 `styles` 自定义 4 种视觉状态：ON/OFF + PRESSED
- 如果要做“总开关联动多个子项”，建议在回调里统一处理

## 对应 demo
- `Demo/demo_checkbox.c`
