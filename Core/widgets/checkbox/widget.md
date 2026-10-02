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
- 按压被本控件消费（返回 1），成为按压对象；全透明时不接按压，交给外层

## 注意事项
- `text == NULL` 时只显示方框
- 可通过 `styles` 自定义 4 种视觉状态：ON/OFF + PRESSED
- 如果要做“总开关联动多个子项”，建议在回调里统一处理

## 已完成的毕业优化
- 修正：空心框原先用屏幕底色填内部，放在容器/面板上时内部颜色不对——改为只画边框环带（新内核图元 `we_draw_round_rect_ring`），内部露出下面的内容
- 修正：持有焦点时 `set_text` 改变宽度会留下光标环残影——尺寸变化改走 `we_obj_set_size`
- 修正：自定义样式里 ON/OFF 文字颜色不同时，勾选翻转只重画方框、文字停在旧色
- 包围盒宽度计入最后一个字形的右侧伸出（如 simsun_12 的 m），文字不再被内核按包围盒裁掉 1px
- 文字竖直中线随文本缓存（占结构体原有填充字节），对勾 6 个百分比坐标改乘移位，每个切片不再逐字测量、不再做库除法
- 全透明时不接触摸

## 对应 demo
- `Demo/demo_checkbox.c`
