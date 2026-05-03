# group

## 功能
控件组容器，负责统一管理一批子控件的相对坐标、整体透明度和整体位移。

## 适用场景
- 一个面板里挂多个子控件
- 整体移动一组控件
- 给 `slideshow` 等复合控件提供基础分组能力

## 关键 API
- `we_group_obj_init(...)`
- `we_group_obj_delete(...)`
- `we_group_add_child(...)`
- `we_group_remove_child(...)`
- `we_group_set_child_pos(...)`
- `we_group_relayout(...)`
- `we_group_shift_children(...)`
- `we_group_set_opacity(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_GROUP_CHILD_MAX`

## 事件与行为
- `group` 主要负责布局与子控件管理，不是通用交互容器
- 子控件的事件仍由各自控件处理
- `we_group_shift_children(...)` 可选择是否给子控件派发 `WE_EVENT_SCROLLED`

## 注意事项
- 子控件使用的是“相对 group 左上角”的局部坐标
- 修改 group 位置后，通常需要 `we_group_relayout(...)` 同步子控件绝对位置
- 这是 `slideshow` 的基础承载层之一

## 对应 demo
- `Demo/demo_group.c`
