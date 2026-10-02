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
- 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 主要通过周期调用 `we_chart_push(...)` 更新显示

## 注意事项
- 数据值是“像素单位”：`0 = 控件中心，正值向上，负值向下`
- 调用方要自己把原始数据先缩放到像素空间
- 缓冲区一般建议容量 = 控件宽度
- 当前是单曲线控件，不带多通道管理

## 已完成的毕业优化
- 绘制按切片剔除列：波形（含上下柔边）与当前窗口行范围不相交的列只推进游标，不再逐列走主体与柔边
- 柔边各层 alpha 每次绘制查一次表，主体可见性判断提到列循环外
- push 标脏的新旧两帧各用一个环形游标逐列 +1、比较回绕，去掉每列最多 4 次取模（M0 上每次 push 省约 1000 次库除法）；绘制入口的 3 次取模同样换成比较回绕
- 网格线直接用 `we_fill_rect`（传控件自身透明度，容器级联由入口统一乘一次），删掉两个手写的逐像素画线函数
- `set_color` 值未变不重绘，`set_opacity` 只标一次包围盒

## 对应 demo
- `Demo/demo_chart.c`
