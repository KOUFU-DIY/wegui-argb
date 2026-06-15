# dropdown

## 功能
数据驱动的下拉选择控件。闭合态只显示当前选中项 + 方向箭头；展开态借助 LCD 级
overlay popup 绘制选项列表，不会被 `group` / `scroll_panel` / `slideshow` 等父容器裁剪。

## 适用场景
- 设置页的枚举选项选择
- 选项较多、需要滚动浏览的列表选择
- 嵌在滚动容器/分页容器内、但展开列表需浮在最上层的选择框

## 关键 API
- `we_dropdown_obj_init(...)`
- `we_dropdown_set_options(...)` — 仅保存指针，不复制文本
- `we_dropdown_set_selected(...)` / `we_dropdown_get_selected(...)`
- `we_dropdown_get_value(...)`
- `we_dropdown_set_changed_cb(...)`
- `we_dropdown_open(...)` / `we_dropdown_close(...)` / `we_dropdown_toggle(...)`
- `we_dropdown_set_max_visible_items(...)`
- `we_dropdown_set_item_height(...)`
- `we_dropdown_obj_delete(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_DROPDOWN_DEF_MAX_VISIBLE` — 默认可见选项数
- `WE_DROPDOWN_SB_HOLD_MS` — 停止滚动后滚动条保持全显的时长（毫秒）
- `WE_DROPDOWN_SB_FADE_MS` — 滚动条淡出时长（毫秒）
- `WE_DROPDOWN_SB_IDLE_ALPHA` — 空闲时滚动条淡出到的常驻最低透明度（0~255，>0 表示不完全消失）

## 事件与行为
- 主框 `CLICKED` 切换展开/收起
- 展开列表支持**无级（像素级）拖拽滚动**：松手停在任意位置，首/末项可半露，不吸附对齐
- 半露行的文本与按下高亮跟随 popup 圆角与边界裁剪，不溢出圆角
- 选项超出可视高度时右侧叠加半透明胶囊滚动条，且**自动淡出**：
  滚动时全显，停手后保持 `HOLD` 再用 `FADE` 时长淡到 `IDLE_ALPHA` 常驻最低透明度
- 点击 popup 外部区域收起列表

## 注意事项
- 选项数组由调用者持有（通常是 const 静态数组），需在控件生命周期内保持有效
- 全屏同时只允许一个 popup，由 driver 的单 `popup_layer` 槽保证
- 滚动条淡出由中央动画引擎驱动（不占 GUI task 槽）：滚动唤醒时挂链，收敛到常驻最低透明度或收起时自动摘链
- 数值/文本以 UTF-8 显示，依赖传入的字体资源

## 对应 demo
- `Demo/demo_dropdown.c`
