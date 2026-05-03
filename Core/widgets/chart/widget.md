# chart

## 功能
单曲线滚动波形图控件，使用像素坐标系推入采样值，适合实时曲线与示波器风格显示。

## 适用场景
- 波形显示
- 传感器曲线
- 电压/速度/音频等滚动趋势图

## 关键 API
- `we_chart_obj_init(...)`
- `we_chart_push(...)`
- `we_chart_clear(...)`
- `we_chart_set_color(...)`
- `we_chart_set_opacity(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_CHART_GRID_COLS`
- `WE_CHART_GRID_ROWS`
- `WE_CHART_GRID_R/G/B`
- `WE_CHART_AA_MAX`
- `WE_CFG_CHART_DIRTY_MODE`
- `WE_CHART_DIRTY_BLOCK_W`
- `WE_CFG_CHART_AA_CURVE`

## 事件与行为
- `chart` 本身不处理交互事件
- 主要通过周期调用 `we_chart_push(...)` 更新显示

## 注意事项
- 数据值是“像素单位”：`0 = 控件中心，正值向上，负值向下`
- 调用方要自己把原始数据先缩放到像素空间
- 缓冲区一般建议容量 = 控件宽度
- 当前是单曲线控件，不带多通道管理

## 对应 demo
- `Demo/demo_chart.c`
