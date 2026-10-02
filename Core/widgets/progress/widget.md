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

## 事件与行为
- 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 数值过渡动画由中央动画引擎驱动，仅动画进行期间挂链、到位自动摘链

## 注意事项
- 目标值范围固定为 `0~255`
- `value` 是目标值，`display_value` 是当前动画中的显示值
- 进度变化时只标脏变化带，不整条全刷

## 已完成的毕业优化
- 已填段改为“窗口收窄到填充宽度 + 画整条圆角轨道”，删掉逐像素调用 7 参数圆角蒙版的专用填充（像素逐位不变，直段走 32 位快速填充）
- MONO 描边轨道直接用 `we_draw_round_rect_outline_fill`
- 不处理输入改为 event_cb 为 NULL（返回 0 的空回调仍会被命中测试选中、挡住背后控件）
- 修正：初始化时未标脏，运行期创建的进度条要等别处刷新才显示
- 动画总时长直接用宏、已走时间改 16 位（每个实例省 4 字节 RAM）；`set_colors` 值未变不重绘；删除接口改为内联包装

## 对应 demo
- `Demo/demo_progress.c`
