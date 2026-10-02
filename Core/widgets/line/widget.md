# line

## 功能
一根可配置起点/终点、线宽、线帽、颜色、透明度的抗锯齿线段。端点、颜色、透明度各有一个独立的中央动画节点，三种动画可同时进行；整体平移也可做成动画。

## 适用场景
- 分隔线、连接线、指示线
- 需要端点平滑移动的连线（如折线端点跟随数据）
- 颜色 / 明灭过渡的装饰线

## 关键 API
- `we_line_obj_init(obj, lcd, x0, y0, x1, y1)` —— 端点为屏幕绝对坐标，包围盒自动推导
- `we_line_set_points` / `we_line_move` —— 立即改端点 / 整体平移
- `we_line_set_width` / `we_line_set_cap` / `we_line_set_color` / `we_line_set_opacity`
- `we_line_anim_points` / `we_line_anim_move` / `we_line_anim_color` / `we_line_anim_opacity` —— 动画版本（`ease` 传 NULL 用默认缓入缓出正弦）
- `we_line_set_event_cb(obj, cb)` —— 设回调后才接收输入，传 NULL 恢复装饰性穿透
- `we_line_obj_delete(obj)` —— 即 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_LINE_USE_ANIM`（默认 1）：置 0 去掉三个动画节点与快照字段，`we_line_anim_*` 退化为立即生效
- `WE_LINE_ANIM_MS`：默认动画时长（毫秒）
- `WE_LINE_DEF_WIDTH`：默认线宽
- `WE_LINE_DEF_R/G/B`：默认线色

## 事件与行为
- 默认装饰性：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 设了用户回调后换成带 event_cb 的类，全部输入事件交给用户回调；命中测试按包围盒粒度
- 三个动画通道共用一个缓动函数（最后一次 `we_line_anim_*` 调用传入的那个）
- `we_line_move`（容器滚动/移动子控件也走它）在端点动画进行中会把动画起止点一起平移

## 渲染与标脏
- 圆头线走 `we_draw_line_round`：线身与半圆端帽按到线段的距离一次画成，半透明时两者不叠色
- 平头线走 `we_draw_line`（Wu 抗锯齿）：每步画 `width` 个实心像素加两侧各 1 个 AA 像素，视觉线宽约 `width + 1`，比同宽圆头线粗约 1px
- 包围盒左/上外扩 半线宽+2px、右/下外扩 半线宽+1px，端点或线宽变化时新旧包围盒各标脏一次
- 端点、颜色、透明度、线宽、线帽没变时 setter 与动画步都不标脏

## 注意事项
- 端点是屏幕绝对坐标；放进容器后随容器移动由 `set_pos_cb` 换算
- 斜线的包围盒可能很大，需要交互时注意它会挡住包围盒内的其他控件

## 已完成的毕业优化
- 装饰性线段真正穿透输入：无用户回调时 event_cb 为 NULL（返回 0 的回调仍会被命中测试选中、挡住背后控件）
- 值未变不重绘：端点动画中大量帧插值结果不变，颜色/透明度动画与全部 setter 同样加了相等判断
- 容器移动线段时连同进行中的端点动画一起平移，不再被下一帧插值拉回原处
- 动画进度推进改用 `we_motion_advance`（box/gauge/indicator 共用），删除接口改为内联包装（动画节点由内核摘链）

## 对应 demo
- `Demo/demo_line.c`（DEMO_ID 22：四条线分别演示端点缓动、平移动画、颜色动画、透明度呼吸）
