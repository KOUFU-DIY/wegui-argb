# gauge

## 功能
仪表盘：内切于 w×h 区域的经典"开口朝下"表盘（默认 135° 起、顺时针扫 270°），由均布主刻度短线、指向当前值的圆头指针和中心圆帽组成。int32 量程线性映射到扫角，支持立即设值与中央动画平滑扫动（单 `we_anim_t` 节点，不占 GUI timer 槽）。

## 适用场景
- 转速 / 车速 / 温度 / 电压等模拟量的表盘显示
- 需要"指针平滑扫动到新值"的仪表动画
- 与 label 联动的"表盘 + 数字"组合读数

## 关键 API
- `we_gauge_obj_init(obj, lcd, x, y, w, h)`
- `we_gauge_set_range(obj, v_min, v_max)` —— int32 量程（max 必须 > min）
- `we_gauge_set_value(obj, v)` —— 立即就位（打断进行中的扫动）
- `we_gauge_anim_value(obj, target, dur_ms)` —— 中央动画平滑扫动
- `we_gauge_get_value(obj)` / `we_gauge_get_disp_value(obj)` —— 目标值 / 显示值（扫动中为插值中间量）
- `we_gauge_set_colors(obj, tick_color, pointer_color)` —— 刻度色 / 指针色（中心帽随指针同色）
- `we_gauge_set_tick_count(obj, count)` —— 主刻度条数（0 不画，建议 9~13，上限 WE_GAUGE_TICK_MAX）
- `we_gauge_set_ease(obj, ease)` —— 扫动缓动（we_motion.h，默认缓入缓出正弦）
- `we_gauge_set_opacity(obj, opacity)`
- `we_gauge_obj_delete(obj)` —— 内部先 `we_anim_stop` 再 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_GAUGE_DEF_START` / `WE_GAUGE_DEF_SWEEP`（512 步制，默认 WE_DEG(135) / WE_DEG(270)）
- `WE_GAUGE_DEF_TICK_CNT` / `WE_GAUGE_DEF_TICK_LEN` / `WE_GAUGE_TICK_W`
- `WE_GAUGE_TICK_MAX`（刻度几何缓存上限，默认 16，每条 8 字节 RAM）
- `WE_GAUGE_SMALL_SIZE`（极小表盘护栏阈值，默认 40：min(w,h) 低于该值时按最外元素 AA 晕圈钳缩外半径）
- `WE_GAUGE_DEF_PTR_W` / `WE_GAUGE_DEF_CAP_W` / `WE_GAUGE_PTR_LEN_Q8`（指针长度 Q8 比例，默认 0.72R）
- `WE_GAUGE_TICK_R/G/B`、`WE_GAUGE_PTR_R/G/B`（默认刻度灰蓝 / 指针亮红）

## 渲染模型与成本
- 全部复用现有原语：刻度与指针 `we_draw_line_round`（单遍胶囊覆盖 AA），中心帽 `we_draw_round_rect_analytic_fill` 退化实心圆；不新增渲染图元
- 主刻度内外端点偏移在 init/set_range/set_tick_count 时缓存进结构体，draw 回调内零 `we_cos/we_sin`、零乘除，仅指针端点实时算一次极坐标（每帧 2 次 Q15 三角）
- 数值变化按差分标脏：只重绘"旧指针位形 ∪ 新指针位形"（各自并入中心帽 AA 边缘），静态刻度区零重绘；新旧角度量化相同时零提交
- 角度 512 步制（0..511 一圈，90° = 128）；值→角度走 set_range 预除的 Q16 斜率（只乘 + 移位，量程跨度 > 65536 时自动回退除法保精度）
- 渲染路径零浮点、零 malloc

## 事件与行为
- 装饰性控件：event_cb 返回 0，输入穿透给背后控件
- `we_gauge_anim_value` 扫动中再次调用会以当前显示值为新起点无缝改道
- 所有 setter 值未变时直接返回，不触发重绘；set_colors/set_range/set_tick_count/set_opacity 属结构性变化，整控件标脏
- 无 set_pos_cb：几何由 base.x/y 推导、刻度缓存为相对中心偏移，`we_obj_set_pos` 默认移动逻辑即正确（移动无需重建缓存）

## 注意事项
- 量程跨度 |v_max - v_min| 需小于 2^22（we_lerp 插值的 int32 中间量防溢出；角度映射本身已由 Q16 斜率/回退除法保证安全）
- 缓动输出超过 256 时（如 we_ease_out_back）显示值会被钳制在端点，表针不过冲出量程
- 主刻度条数上限 WE_GAUGE_TICK_MAX（默认 16），超出部分被钳制
- 删除带动画的控件必须走 `we_gauge_obj_delete`（先摘动画链）

## 已完成的毕业优化
- 指针差分标脏：数值变化只提交"旧指针位形"与"新指针位形"两块包围盒（圆心与指针端点的 min/max 矩形外扩 max(指针半宽+2, 帽半径+1)，两块分别提交不合成大盒，钳制到控件矩形），刻度静态区零重绘；新旧角度量化相同时零提交；结构性 setter 仍整控件标脏
- 刻度几何缓存：主刻度内外端点相对表盘中心的偏移在 init/set_range/set_tick_count 时一次算好存入结构体（上限 WE_GAUGE_TICK_MAX=16 条、128 字节），draw 内零三角函数、零乘除，且相对中心存储使控件移动无需重算
- Q16 量程斜率：set_range 预除 `slope_q16 = round((sweep<<16)/span)`，draw/标脏内 value→角度只乘 + 移位；span ≤ 65536 时启用（误差 ≤ 1 角度步、正向扫角端点精确落位），更大跨度自动回退除法，int32 溢出边界已在实现处注释证明
- 极小表盘护栏：min(w,h) < WE_GAUGE_SMALL_SIZE(40) 时按最外元素（刻度/指针取宽者）有效像素晕圈"线宽/2 + 1px 羽化 + 1px 余量"钳缩外半径，AA 不越出控件包围盒；差分脏块同口径钳制

## 毕业前需优化（未完成项）
- 可选功能位：弧形轨道背景、次刻度、量程红区（warning zone）、数字刻度标签——属功能扩展而非性能优化，需新增 API 面与绘制路径，本轮聚焦渲染/标脏效率，留待毕业后按实际需求添加

## 对应 demo
- `Demo/demo_gauge.c`（DEMO_ID 24：大表盘居中，每 1.5s 向随机目标平滑扫动，表盘开口处 label 实时显示当前显示值）
