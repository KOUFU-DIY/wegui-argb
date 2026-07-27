# knob（preview 孵化区）

## 功能
弧形旋钮滑块：内切于 size×size 正方形的可拖拽圆弧（默认 135° 起顺时针扫 270°，开口朝下），由暗色 track 底弧、亮色 value 弧（起点到当前值角度）和 value 末端的拖拽小圆手柄组成。手指沿弧拖动（或点击弧上任意位置）时，触点角度经内置整数 atan2 解算并线性映射到 int32 量程，值变化触发 changed_cb。

## 适用场景
- 音量 / 亮度 / 温度设定等"旋钮式"取值交互
- 需要比水平 slider 更省布局空间的大范围调节
- 与 label 联动的"旋钮 + 数字"组合面板

## 关键 API
- `we_knob_obj_init(obj, lcd, x, y, size)` —— 包围盒 = size × size（建议 >= 60）
- `we_knob_set_range(obj, v_min, v_max)` —— int32 量程（max 必须 > min）
- `we_knob_set_value(obj, v)` / `we_knob_get_value(obj)` —— 程序设值不触发回调
- `we_knob_set_changed_cb(obj, cb)` —— 值变化回调（仅用户拖动/点击触发）
- `we_knob_set_colors(obj, track, value, dot)`
- `we_knob_set_opacity(obj, opacity)`
- `we_knob_obj_delete(obj)` —— 无动画节点，无需 we_anim_stop

## 可调宏
包含头文件前可覆盖：
- `WE_KNOB_DEF_START` / `WE_KNOB_DEF_SWEEP`（512 步制，默认 WE_DEG(135) / WE_DEG(270)）
- `WE_KNOB_TRACK_R/G/B`、`WE_KNOB_VALUE_R/G/B`、`WE_KNOB_DOT_R8/G8/B8`（默认暗灰蓝 / 亮青蓝 / 近白）

## 渲染模型与成本
- 弧带 = `we_widget_arc.c` 距离平方场扫描的简化版：外/内沿各 1px 线性 AA 带（倒数系数预算，内环无除法），角向用起点/终点向量叉积增量式判定（每像素 3 次 int32 加法），track/value 一遍扫描内合成；**端面为平头（无端帽圆角）**
- 端点手柄 = `we_draw_round_rect_analytic_fill` 退化实心抗锯齿圆，按压时向白色增亮
- 弧厚 ≈ size/8（钳 6..24），外半径 = size/2 - 3（手柄外凸 2px 仍在包围盒内）
- 内环零浮点、零 malloc；容器透明度级联（opa_scale）在 draw 入口消费一次

## 事件与行为
- 消费 PRESSED / STAY / RELEASED / CLICKED（返回 1）；滑动手势等穿透（返回 0）
- 触点角度：八分区近似整数 atan2（`atan(t) ≈ 64t + 22t(1-t)`，Q8，输出 512 步制），最大误差约 0.3°
- 中心死区（内半径一半，最小 4px）内的触点忽略，防过圆心角度跳变；弧开口区的触点就近吸附到量程端点
- 命中粒度为整个方形包围盒（弧外角落也可拖，类似实体旋钮体验）
- `we_knob_set_value` / `we_knob_set_range` 属程序侧调整，不触发 changed_cb（与 slider 语义一致）
- 所有 setter 值未变时直接返回，不触发重绘
- 无 set_pos_cb：几何全部由 base.x/y 推导，`we_obj_set_pos` 默认移动逻辑即正确

## 注意事项
- 量程跨度 |v_max - v_min| 需小于 2^22（角度↔数值 int32 映射防溢出）
- size < 24 时 init 直接拒绝；size < 60 时手柄偏小不利于触控
- 弧带端面为平头，与稳定区 arc 控件的圆头端帽外观不同（见下）

## 毕业前需优化
- 脏矩形：值变化按整控件包围盒标脏；应改为"旧手柄 + 新手柄 + 两角度间弧扇区"差分标脏（参考 arc 的 `_calc_arc_tight_bbox` + 中心空洞排除）
- atan2 可换 64/128 项查表 + 线性插值（误差 < 0.1°），或直接复用 we_sin 表做反查
- 弧带端帽圆角（参考 arc 的 `_cap_alpha` 端帽路径），与稳定区 arc 观感对齐
- 拖拽平滑：可选的"值吸附步进"（step）与拖拽惯性
- 弧带扫描目前覆盖整个包围盒行程，可按行预解弧带 x 区间跳过环外大片像素

## 对应 demo
- `Demo/preview/demo_knob.c`（DEMO_ID 108：左侧 150px 大旋钮拖拽改值（0~100），右侧 2 倍字号 label_ex 百分比实时联动，changed_cb 驱动）
