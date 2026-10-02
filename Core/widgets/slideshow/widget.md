# slideshow

## 功能
分页式幻灯片控件，每页都可以挂载子控件，页面使用局部坐标 `(0,0)` 作为吸附到位后的基准原点。

## 适用场景
- 横向翻页 UI
- 引导页
- 轮播页
- 由多个控件组成的分页面板

## 关键 API
- `we_slideshow_obj_init(...)`
- `we_slideshow_obj_delete(...)`
- `we_slideshow_add_page(...)`
- `we_slideshow_get_page_count(...)`
- `we_slideshow_get_current_page(...)`
- `we_slideshow_add_child(...)`
- `we_slideshow_set_child_pos(...)`
- `we_slideshow_set_page(...)`
- `we_slideshow_next(...)`
- `we_slideshow_prev(...)`
- `we_slideshow_set_swipe_enabled(...)`
- `we_slideshow_set_opacity(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_SLIDESHOW_SNAP_ANIM_MODE`
- `WE_SLIDESHOW_SNAP_SIMPLE_STEP`
- `WE_SLIDESHOW_SNAP_COMPLEX_PULL_DIV`
- `WE_SLIDESHOW_SNAP_COMPLEX_DAMP_NUM`
- `WE_SLIDESHOW_SNAP_COMPLEX_DAMP_DEN`
- `WE_SLIDESHOW_SNAP_COMPLEX_MAX_STEP`
- `WE_SLIDESHOW_PAGE_MAX`
- （子控件数量无上限）

## 事件与行为
- 支持拖拽、释放吸附、左右滑动切页
- 子控件依然可以收到自己的点击事件
- 常见事件路径：`PRESSED / STAY / RELEASED / SWIPE_LEFT / SWIPE_RIGHT`

## 注意事项
- 当前是横向分页模型
- 页面内子控件位置使用局部坐标，不是屏幕绝对坐标
- 页面宽高固定等于控件宽高
- 如果要做“翻页按钮 + 手势翻页”混合模式，直接给子页按钮传自定义回调即可

## 已完成的毕业优化
- 修正：没有移动回调，`we_obj_set_pos` 移动幻灯片（或它嵌在 group / scroll_panel 里被带着走）时只挪外框、页面留在原地——背景、子控件区、移动三个类回调直接复用 group 的
- 删掉只写不读的 `last_touch_y`；删除接口改为内联包装（吸附动画节点由内核摘链）

## 对应 demo
- `Demo/demo_slideshow.c`
