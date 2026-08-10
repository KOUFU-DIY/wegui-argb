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
- `we_group_shift_children(...)`
- `we_group_set_opacity(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- （子控件数量无上限，槽位表已移除）

## 事件与行为
- 子控件命中/按压锁定/CLICKED 复核由**内核统一派发**（group 只应答 HIT_TEST：全透明时连子树一起跳过），组内的 btn/checkbox 等可正常交互
- `we_group_set_opacity(...)` 会**级联到全部子控件**（经 lcd 级 `opa_scale` 乘子在原语入口生效，无淡入淡出时零开销；嵌套容器自动链乘）
- 完全透明（opacity=0）的 group 不拦截输入
- `we_group_shift_children(...)` 可选择是否给子控件派发 `WE_EVENT_SCROLLED`

## 注意事项
- 子控件使用的是“相对 group 左上角”的局部坐标
- 移动 group（`we_obj_set_pos`）时子控件自动跟随平移，无需手工同步
- 这是 `slideshow` 的基础承载层之一

## 对应 demo
- `Demo/demo_group.c`
