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
- 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 常由定时器更新 `value` 做动画

## 注意事项
- 角度使用 512 步制
- 初始化时输入的是圆心坐标，不是左上角坐标
- `value` 取值范围是 `0~255`
- 放在 group / slideshow / scroll_panel 里时随容器透明度一起淡入淡出

## 已完成的毕业优化
- 接入容器透明度级联（`we_opa_apply`），容器淡出时圆弧同步变淡
- 端帽覆盖按行预判：端帽圆心与本行纵向距离已超出外扩半径时整行不再逐像素求端帽覆盖
- 绘制窗口直接取内核收窄后的 `lcd->clip`，不再重复求交；端帽圆心复用已算好的正余弦
- 标脏统一走“排除内圆空洞”的路径（简易脏矩形档的 `set_value` 也一样）

## 对应 demo
- `Demo/demo_arc.c`
- `Demo/demo_concentric_arc.c`
- `Demo/demo_slideshow.c`
