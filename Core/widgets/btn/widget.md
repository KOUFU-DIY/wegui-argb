# btn

## 功能
普通按钮控件，支持默认按压态切换，也支持传入用户事件回调处理 `PRESSED / RELEASED / CLICKED`。

## 适用场景
- 普通页面按钮
- 对话框确认/取消按钮皮肤复用
- 需要演示事件回调时的基础交互控件

## 关键 API
- `we_btn_obj_init(...)`
- `we_btn_set_state(...)`
- `we_btn_set_text(...)`
- `we_btn_set_style(...)`
- `we_btn_set_radius(...)`
- `we_btn_set_border_width(...)`
- `we_btn_draw_skin(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_BTN_USE_CUSTOM_STYLE`：是否让每个按钮实例持有独立样式表
- `WE_BTN_DRAW_MODE`：边框圆角绘制模式

## 事件与行为
- `event_cb == NULL`：使用内部默认行为，按下/释放时自动切换按钮状态
- `event_cb != NULL`：事件优先交给用户回调处理，适合作为事件例程
- 常用事件：`WE_EVENT_PRESSED`、`WE_EVENT_RELEASED`、`WE_EVENT_CLICKED`

## 注意事项
- 只传 `NULL` 回调时，按钮仍会有基础外观响应，但不构成“用户事件回调例程”
- 需要对话框按钮统一风格时，可直接复用 `we_btn_draw_skin(...)`

## 对应 demo
- `Demo/demo_btn.c`
- `Demo/demo_key.c`（事件回调示例）
