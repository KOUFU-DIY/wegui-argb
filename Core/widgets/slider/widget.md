# slider

## 功能
可交互滑条控件，支持横向与竖向两种方向，支持拖动滑块和点击轨道跳转。

## 适用场景
- 音量
- 亮度
- 阈值
- 百分比参数调节

## 关键 API
- `we_slider_obj_init(...)`
- `we_slider_obj_delete(...)`
- `we_slider_set_value(...)`
- `we_slider_add_value(...)`
- `we_slider_sub_value(...)`
- `we_slider_get_value(...)`
- `we_slider_set_changed_cb(...)` — 数值改变回调（替代轮询）
- `we_slider_set_opacity(...)`
- `we_slider_set_colors(...)`
- `we_slider_set_thumb_size(...)`
- `we_slider_set_track_thickness(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_SLIDER_TRACK_THICKNESS`
- `WE_SLIDER_THUMB_SIZE`
- `WE_SLIDER_ENABLE_VERTICAL`

## 事件与行为
- `PRESSED` 和 `STAY` 会按触点实时更新数值
- `CLICKED` 会跳转到轨道对应位置
- `RELEASED` 只结束按压状态，并只刷新滑块区域
- 横向：最小值在左，最大值在右
- 竖向：最小值在下，最大值在上
- 数值范围由 `we_slider_obj_init(...)` 的 `min_value/max_value` 决定

## 注意事项
- 当前默认同时支持横向与竖向滑条
- 将 `WE_SLIDER_ENABLE_VERTICAL` 设为 `0` 时，会裁掉竖向相关代码，`WE_SLIDER_ORIENT_VER` 会回退为横向
- 可用 `we_slider_set_changed_cb()` 注册数值改变回调（仅用户交互触发，程序 API 设值不触发）；未注册时仍可 `get_value()` 轮询
- 轨道和滑块尺寸都支持运行时调整

## 已完成的毕业优化
- 修正：竖向滑块尺寸为奇数时差 1px（最小值时底行出界被裁掉、最大值时顶部留缝）；绘制、标脏、触摸换算共用 `_slider_thumb_origin`
- 滑块尺寸不再超出控件宽高（原先最小 4px 的下限会把它撑出包围盒）
- 全透明时不接触摸，按压交给外层（与拒绝聚焦的口径一致）
- MONO 滑块改用 `we_draw_round_rect_outline_fill`；删掉内核已保证的空指针/空尺寸判断与永不成立的 255 钳位
- `set_colors` 值未变不重绘；删除接口改为内联包装

## 对应 demo
- `Demo/demo_slider.c`
