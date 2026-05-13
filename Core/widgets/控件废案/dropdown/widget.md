# dropdown

## 功能
下拉选择控件。点击主框后在控件下方展开选项列表，点击选项后更新当前值并自动收起。

## 适用场景
- 串口波特率选择
- 模式切换
- 主题切换
- 任何离散枚举值单选场景

## 关键 API
- `we_dropdown_obj_init(...)`
- `we_dropdown_obj_delete(...)`
- `we_dropdown_set_items(...)`
- `we_dropdown_set_selected(...)`
- `we_dropdown_get_selected(...)`
- `we_dropdown_get_selected_text(...)`
- `we_dropdown_set_expanded(...)`
- `we_dropdown_set_visible_rows(...)`
- `we_dropdown_set_popup_scrollbar(...)`
- `we_dropdown_set_opacity(...)`
- `we_dropdown_set_colors(...)`
- `we_dropdown_set_pos(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_DROPDOWN_RADIUS`
- `WE_DROPDOWN_ROW_HEIGHT`
- `WE_DROPDOWN_VISIBLE_ROWS`
- `WE_DROPDOWN_ARROW_W`

## 事件与行为
- `PRESSED`：主框记录按下态；展开后由独立 popup 面板接管列表点击
- `CLICKED`：
  - 点击主框：展开 / 收起
  - 点击列表项：更新选中项并收起
- `RELEASED`：清除按下态
- 展开时会创建一个同级前置 popup 对象来绘制和接管下拉面板输入
- popup 会被提升到同级最前，以保证列表显示在其它同级控件上层
- 当前版本为单选，不支持键盘导航

## 显示与数据
- 默认主框风格为接近 Win11 的浅色圆角输入框
- 选项文本数组由外部静态提供：`const char * const *items`
- 当前仅支持纯文本选项
- 当 `item_count > visible_rows` 时，popup 会使用 `scroll_panel` 提供滚动面板外壳与滚动条
- 当前已支持基于 `scroll_y` 的真实项偏移绘制与点击命中

## 脏矩形策略
- 展开 / 收起：刷新主框与列表区
- 选中项切换：刷新主框与列表区
- 按下视觉反馈：刷新主框或列表区

## 注意事项
- 展开列表绘制在主框下方，因此建议控件下方预留足够显示空间
- 当前实现采用“主框对象 + popup sibling”结构，popup 会在展开时挂到顶层链表尾部并显示在最前面
- popup 的面板外壳、圆角、滚动条能力来自 `scroll_panel`
- 若运行时更换选项列表，建议使用 `we_dropdown_set_items(...)`
