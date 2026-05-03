# msgbox

## 功能
顶部滑入式消息框 / 弹窗控件，支持“仅确认”和“确认 + 取消”两种布局。

## 适用场景
- 确认提示
- 二次确认
- 轻量模态交互

## 关键 API
- `we_msgbox_ok_obj_init(...)`
- `we_msgbox_ok_cancel_obj_init(...)`
- `we_popup_set_text(...)`
- `we_popup_set_buttons(...)`
- `we_popup_set_target_y(...)`
- `we_popup_show(...)`
- `we_popup_hide(...)`
- `we_msgbox_show(...)`
- `we_msgbox_hide(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_MSGBOX_ANIM_DURATION_MS`
- `WE_MSGBOX_USE_ANIM`
- `WE_MSGBOX_EDGE_MARGIN`
- `WE_MSGBOX_RADIUS`
- `WE_MSGBOX_BTN_RADIUS`

## 事件与行为
- 内部按钮响应 `PRESSED / RELEASED / CLICKED`
- 用户逻辑通过 `ok_cb / cancel_cb` 注入
- `show/hide` 会驱动顶部滑入滑出动画

## 注意事项
- `target_y` 是消息框停靠的最终 Y 坐标
- 删除前建议先 `hide`，库内删除辅助已经做了这一步
- 当前是模态风格，更适合页面顶部弹出，而不是任意位置浮层

## 对应 demo
- `Demo/demo_msgbox.c`
