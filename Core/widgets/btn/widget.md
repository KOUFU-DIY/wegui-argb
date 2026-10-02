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
- `we_btn_set_style(...)`（背景色 + 文字色）
- `we_btn_set_radius(...)`
- `we_btn_draw_skin(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_BTN_USE_CUSTOM_STYLE`：是否让每个按钮实例持有独立样式表

## 事件与行为
- **按压视觉始终由控件默认维护**（叠加语义）：按下变 PRESSED、释放回 NORMAL，用户回调无需补样式
- `event_cb != NULL`：在默认视觉之后被调用，只需处理业务（通常只看 `WE_EVENT_CLICKED`）
- 按压被按钮消费（返回 1），成为按压对象，后续事件由内核直接派发；`DISABLED` 或全透明时不接按压，状态不变、交给外层
- 常用事件：`WE_EVENT_PRESSED`、`WE_EVENT_RELEASED`、`WE_EVENT_CLICKED`

## 注意事项
- 只传 `NULL` 回调时，按钮仍会有基础外观响应，但不构成“用户事件回调例程”
- 需要对话框按钮统一风格时，可直接复用 `we_btn_draw_skin(...)`
- `we_btn_set_text(...)` 每次都重新测量并重绘：原地改写同一缓冲区后再调用一次即可刷新，不要每帧轮询调用

## 已完成的毕业优化
- 文字宽度与竖直中线在 init / `set_text` 时测量缓存（每个实例 +4 字节），每个切片不再逐字测量两遍
- 删掉文字绘制前多余的窗口收窄（内核已按包围盒收窄）与皮肤里重复的圆角钳制
- 修正：`DISABLED` 状态下触摸仍会把状态改成 PRESSED/NORMAL 并派发 CLICKED
- 修正：`set_text` 传同一指针时直接返回，原地改写缓冲区后不刷新
- MONO 常态改用 `we_draw_round_rect_outline_fill`

## 对应 demo
- `Demo/demo_btn.c`
