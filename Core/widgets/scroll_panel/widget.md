# scroll_panel

## 功能
轻量滚动裁剪面板。支持固定区域显示、圆角外壳、内容圆角裁剪、竖向滚动以及可选滚动条。

## 适用场景
- dropdown 弹出列表面板
- 设置页局部滚动区
- 简易列表容器
- picker / menu / popup 的内容面板

## 第一版能力
- 固定可视区域 `x/y/w/h`
- 子对象局部坐标管理
- 竖向滚动 `scroll_y`
- 可选竖向滚动条
- 圆角外壳 + 内层内容底
- 基于 `clip` 模块的 7 区裁剪（3 个矩形快路径 + 4 个圆角 mask 区）
- 子内容绘制后的四角背景修正

## 当前限制
- 当前只支持竖向滚动
- 暂不支持惯性、回弹、拖拽滚动条、嵌套滚动冲突处理

## 关键 API
- `we_scroll_panel_obj_init(...)`
- `we_scroll_panel_obj_delete(...)`
- `we_scroll_panel_add_child(...)`
- `we_scroll_panel_remove_child(...)`
- `we_scroll_panel_set_child_pos(...)`
- `we_scroll_panel_relayout(...)`
- `we_scroll_panel_set_scroll_y(...)`
- `we_scroll_panel_set_content_h(...)`
- `we_scroll_panel_set_radius(...)`
- `we_scroll_panel_set_colors(...)`
- `we_scroll_panel_set_scrollbar(...)`
- `we_scroll_panel_set_opacity(...)`
- `we_scroll_panel_draw_only(...)`
- `we_scroll_panel_get_inner_rect(...)`

## 设计说明
- 如果作为独立容器使用，可通过 `add_child/remove_child` 管理子对象
- 如果作为别的复合控件的“公用面板外壳”使用，可调用 `draw_only()` 只绘制容器本身和滚动条
- `get_inner_rect()` 可用于让上层控件把内容绘制到统一的内边距区域内

## 对应复用场景
- 当前已用于 `dropdown` popup 的面板外壳与滚动条能力复用
