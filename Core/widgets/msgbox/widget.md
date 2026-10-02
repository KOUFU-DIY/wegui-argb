# msgbox

## 功能
淡入淡出式消息框 / 弹窗控件，支持“仅确认”和“确认 + 取消”两种布局。

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

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_MSGBOX_ANIM_DURATION_MS`
- `WE_MSGBOX_USE_ANIM`
- `WE_MSGBOX_EDGE_MARGIN`
- `WE_MSGBOX_RADIUS`
- `WE_MSGBOX_USE_KEY`：按键操作，默认 1；置 0 只裁掉按键操作，模态照常吞键

## 事件与行为
- 内部按钮响应 `PRESSED / RELEASED / CLICKED`
- 用户逻辑通过 `ok_cb / cancel_cb` 注入
- `show/hide` 会驱动淡入淡出动画
- 显示期间是模态（`we_modal_open`）：背后控件收不到触摸与按键；点在面板外的触摸被吞掉，不关闭弹窗
- 按键（经模态键通道）：
  - 首个方向键或 OK 只把键光标落到确认按钮，不触发；键光标是按钮四周的 2px 导航色描边
  - 双按钮布局：左键选取消、右键选确认，上/下/前/后在两个按钮间轮换（单键板只用 NEXT 也能选到取消）
  - OK 按下沿按压、松开沿触发；弹出瞬间收到的孤立松开沿被忽略
  - BACK 走取消按钮，单按钮布局走确认按钮
- 淡出期间仍拦截输入但不再响应按钮（防重复触发），淡出结束才解除模态

## 注意事项
- `target_y` 是消息框停靠的最终 Y 坐标
- 删除前建议先 `hide`，库内删除辅助已经做了这一步
- 水平居中、停靠在 `target_y` 处原地淡入淡出；单色样式（MONO）改为从屏幕上方滑入滑出
- 同一时刻只有一个模态：弹出 msgbox 会先收起已展开的 dropdown 列表或软键盘；msgbox 被别的模态顶掉时立即收起，不触发任何按钮回调
- 命中区只覆盖面板，同时也是内核的绘制剔除范围，屏幕其他地方的重绘不会调用 msgbox 的 draw_cb
- `WE_CFG_ENABLE_TOP_LAYER=0` 裁剪档没有模态：显示时命中区铺满全屏，只能拦住触摸，按键拦不住

## 对应 demo
- `Demo/demo_msgbox.c`
