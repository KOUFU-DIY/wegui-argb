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
- 当前不提供外部事件回调，外部通过 `get_value()` 轮询
- 轨道和滑块尺寸都支持运行时调整

## 对应 demo
- `Demo/demo_slider.c`
