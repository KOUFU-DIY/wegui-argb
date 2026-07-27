# chart_bar（preview 孵化区）

## 功能
N 根竖直柱的柱状图：底部 1px 基线 + 可选横向低透明网格线 + 不透明柱体。数据为**像素高度值**（uint8_t，绘制时钳到量程 h-1），与绘制解耦：内部 `uint8_t values[WE_CHART_BAR_MAX]` 环形缓冲 + head 指针（独立实现，思路同 chart 控件推值，但按"整柱"而非"逐列采样"组织）。支持两种喂数方式：`push`（滚动模式，最新值进右端、整体视觉左移一格）与 `set_all`（整帧覆盖）。

## 适用场景
- 采样率较低的滚动趋势图（温度/流量/RSSI 每秒级采样）
- 直方图/分布图整帧刷新（ADC 分布、任务耗时分桶）
- 与 chart（连续波形）互补：离散量、少柱数、强调单柱对比时用 chart_bar

## 关键 API
- `we_chart_bar_obj_init(obj, lcd, x, y, w, h, bar_cnt)` —— 槽宽 = w/bar_cnt，缝隙 = 槽宽/4（最小 1），余数居中吸收；初始全零（只显示基线）
- `we_chart_bar_push(obj, value_px)` —— 环形覆盖最旧值；display[i] = values[(head+i) 回绕]，最新值恒在最右
- `we_chart_bar_set_all(obj, values)` —— head 归零后整帧拷入（values[0] = 最左柱）；与当前显示序列逐柱一致时直接返回
- `we_chart_bar_set_colors(obj, bar, grid)` —— 柱色 / 网格+基线色，双色均未变直接返回
- `we_chart_bar_set_grid(obj, rows)` —— 横向网格线数（0 关闭，上限 WE_CHART_BAR_GRID_MAX）；网格 y = 基线 - 量程*k/(rows+1)
- `we_chart_bar_obj_delete(obj)` —— 无动画节点，内联转调 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_CHART_BAR_MAX`（默认 32，数据数组按此静态分配，每柱 1 字节）
- `WE_CHART_BAR_GRID_MAX`（默认 16，网格线数钳制上限）

## 渲染模型与成本
- 全部 `we_fill_rect`：grid_rows 条 1px 网格线（alpha 70）+ 最多 bar_cnt 个柱矩形（不透明）+ 1px 基线（alpha 200）
- 环形索引用比较回绕（idx++ / 归零）替代 %，内环零除法、零浮点
- 网格线 Y 每帧含 `usable*k/(rows+1)` 的 int32 除法（rows 条/帧，模拟器无所谓）
- 数据存像素高度：控件不做 Y 轴缩放，调用方自行把原始量程预换算为像素（与 chart 同一约定）

## 事件与行为
- 装饰性控件：event_cb 恒返回 0，输入穿透给背后控件
- `set_all`/`set_colors`/`set_grid` 值未变时直接返回；`push` 恒标脏（滚动本就整幅变化）
- 无 set_pos_cb：几何全部由 base.x/y 推导，`we_obj_set_pos` 默认移动逻辑即正确

## 注意事项
- `bar_cnt` 超过 `WE_CHART_BAR_MAX` 时钳制；0 按 1 处理
- 值超过量程（h-1）时绘制端钳制，不改写存储值（改小控件高度不丢数据）
- 宽度建议 >= bar_cnt * 4（保证柱宽 >= 2px + 缝隙 1px）；w 不能被 bar_cnt 整除的余数在左右两侧对半留白
- push 滚动的"左移"是数据视角的重绘，不是像素搬移；两次 push 之间不重绘则中间状态不可见

## 毕业前需优化
- 脏矩形：整控件包围盒标脏 → 应改为差分标脏（push 时全部柱变化不可避免，但 set_all 可只标值变化的柱；set_grid/set_colors 可跳过空白区）
- 网格线 Y 的 `/(rows+1)` 除法可在 set_grid 时预计算到结构体（同 chart 的 grid_dx/dy 做法），消除 M0 每帧软除法
- 柱体可选垂直渐变/按值变色（低/中/高阈值三段色），观感与 spectrum 对齐
- 可选峰值线（每柱历史最大值细横线）与负值支持（基线居中双向柱）
- push 可选批量接口（一次推 N 个值只标脏一次）
- 未满 bar_cnt 个有效样本时当前显示为 0 高度柱；如需"右对齐渐进填充"（同 chart 数据不满时的行为）需补 count 字段

## 对应 demo
- `Demo/preview/demo_chart_bar.c`（DEMO_ID 125：上面板 24 柱每 250ms push 一个 LCG+we_sin 合成值演示滚动；下面板 16 柱每 600ms set_all 一组随机分布演示整帧覆盖；各配说明 label）
