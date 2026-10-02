# dropdown

## 功能
数据驱动的下拉选择控件。闭合态只显示当前选中项 + 方向箭头；展开态由挂在 LCD 顶层链上的
内嵌 overlay 对象（模态）绘制选项列表，不会被 `group` / `scroll_panel` / `slideshow` 等父容器裁剪。

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
- `we_dropdown_set_enabled(...)` — 禁用后不响应触摸/按键、拒绝聚焦，文字用禁用色
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
- 同一时刻只允许一个模态弹层：打开其他模态弹层（另一个 dropdown、msgbox、软键盘）会先收起当前列表
- 展开中调用 `set_options` / `set_item_height` / `set_max_visible_items`（列表几何变化）或禁用控件，会先收起列表
- 展开时删除本控件（含删除它所在的容器）由删除回调先收起列表，不会在顶层链上留下失效的 overlay
- 滚动条淡出由中央动画引擎驱动：滚动唤醒时挂链，收敛到常驻最低透明度或收起时自动摘链
- 数值/文本以 UTF-8 显示，依赖传入的字体资源

## 已完成的毕业优化
- 列表只处理与当前 PFB 切片相交的行（行文字测量也随之只在相交行上做），高亮行的覆盖率计算夹紧到切片行
- 箭头覆盖率只算与当前窗口相交的像素；像素内 4x4 子采样落在 1/8、3/8、5/8、7/8 处
- 滚动条在轨道与切片不相交时跳过滑块几何（两次除法）
- 滚动位置只存一份（`sc.pos`）；弹层几何直接写入 overlay，不再在各处临时换算矩形
- 修正：列表项高 ≤ 24 时顶部过冲把列表画成空白（首项索引向零取整后越界）

## 对应 demo
- `Demo/demo_dropdown.c`
