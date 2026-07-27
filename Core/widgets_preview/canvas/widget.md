# canvas（preview 孵化区）

## 功能
用户自绘壳：给业务层"直接用绘图原语自绘"的逃生舱控件。框架负责 PFB 窗口收窄（越界自动裁剪）、整体透明度级联（`lcd->opa_scale` 乘子）与事件转发，内容完全由用户回调决定。

## 适用场景
- 框架现有控件覆盖不到的自定义图形（示波器波形、雷达图、小游戏画面……）
- 快速原型验证一段绘制逻辑，暂不值得做成独立控件
- 混用多种原语（`we_fill_rect` / `we_draw_line_round` / `we_draw_string` …）的复合装饰区

## 关键 API
- `we_canvas_obj_init(obj, lcd, x, y, w, h, user_draw_cb, user_data)`
- `we_canvas_set_event_cb(...)` —— 非 NULL 时接管全部输入事件（返回 1 消费 / 0 穿透）
- `we_canvas_invalidate(...)` —— 业务数据变了就调它请求重绘（透传 `we_obj_invalidate`）
- `we_canvas_set_opacity(...)`
- `we_canvas_obj_delete(...)`

## 事件与行为
- 未设置 `user_event_cb` 时事件回调返回 0，输入穿透给背后控件
- `user_draw_cb(lcd, canvas, user_data)`：`canvas` 可 cast 回 `we_canvas_obj_t*` 读取 `base.x/y/w/h`；坐标用**屏幕绝对坐标**（以 base.x/base.y 为原点自行偏移）
- draw_cb 已把控件 opacity 乘入 `opa_scale` 级联乘子——用户回调内原语按各自 opacity 正常传参，整体淡入淡出自动叠乘（与 group 子控件一致）
- 控件不感知内容变化，**不会自动重绘**；每次数据更新后必须调用 `we_canvas_invalidate()`

## 注意事项
- 用户回调运行在渲染阶段、可能因脏矩形被同一帧多次调用（每个 PFB 条带一次）——回调必须是**纯绘制**（幂等、无状态推进），状态推进放 demo tick / timer 里
- 回调内禁止调用 `we_obj_*` 结构性接口（挂链/删除/标脏），只用绘图原语
- 零 malloc、无动画节点；`user_data` 生命周期由调用方保证

## 毕业前需优化
- 标脏只有整框粒度，业务方无法只刷新局部（可透传 `we_obj_invalidate_area`）
- 用户回调每个 PFB 条带都完整执行一遍，复杂图形在窄条带上有重复计算，可考虑条带相交预判辅助宏
- 无内容缓存（每次重绘都全量重画），毕业时可评估可选的行缓存/快照模式

## 对应 demo
- `Demo/preview/demo_canvas.c`（DEMO_ID 112）：用户回调里画十字网格背景 + 李萨如曲线（`we_cos`/`we_sin` 参数方程取 48 点、`we_draw_line_round` 连线），相位由 demo tick 推进后调 `we_canvas_invalidate` 形成旋转变形动画
