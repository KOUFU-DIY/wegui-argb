# arc

## 功能
单色圆弧进度条控件，用 0~255 的值表示进度，适合仪表、环形进度和装饰圆弧。

## 适用场景
- 环形进度条
- 仪表类百分比显示
- 同心圆弧动画

## 关键 API
- `we_arc_obj_init(...)`
- `we_arc_set_value(...)`
- `we_arc_set_opacity(...)`
- `we_arc_obj_set_pos(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_ARC_OPT_MODE`：精细脏矩形 / 简易脏矩形

## 事件与行为
- `arc` 本身不处理交互事件
- 常由定时器更新 `value` 做动画

## 注意事项
- 角度使用 512 步制
- 初始化时输入的是圆心坐标，不是左上角坐标
- `value` 取值范围是 `0~255`

## 对应 demo
- `Demo/demo_arc.c`
- `Demo/demo_concentric_arc.c`
- `Demo/demo_slideshow.c`
