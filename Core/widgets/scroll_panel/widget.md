# scroll_panel

## 功能
竖向滚动面板容器：1px 边框 + 内容区背景（可选圆角外壳）+ 可选右缘滚动条。子控件挂进面板后随滚动整体平移，内容区（边框以内）之外的部分由内核裁掉、也不参与命中；支持拖动、惯性滑行、越界回弹和快速轻扫。

## 适用场景
- 设置页局部滚动区
- 由若干控件组成、超出一屏的内容块
- 需要在滚动区里放按钮 / 复选框等交互控件的面板

## 关键 API
- `we_scroll_panel_obj_init(obj, lcd, x, y, w, h, bg, border, radius, opacity)`
- `we_scroll_panel_add_child(obj, child)` / `we_scroll_panel_remove_child(obj, child)`
- `we_scroll_panel_set_child_pos(obj, child, local_x, local_y)` —— 内容局部坐标（以面板外框左上角为原点，不含滚动量）
- `we_scroll_panel_set_content_h(obj, h)` —— 内容总高（不小于内容区高），滚动量随新范围夹紧
- `we_scroll_panel_set_scroll_y(obj, y)` —— 程序化定位（硬夹紧，无动画）
- `we_scroll_panel_set_scrollbar(obj, show, w)` / `we_scroll_panel_set_scrollbar_opacity(obj, opa)`
- `we_scroll_panel_set_colors(obj, bg, border, scrollbar)` / `we_scroll_panel_set_radius(obj, r)` / `we_scroll_panel_set_opacity(obj, opa)`
- `we_scroll_panel_relayout(obj)` —— 整体标脏
- `we_scroll_panel_get_inner_rect(obj, &x, &y, &w, &h)` —— 内容区矩形
- `we_scroll_panel_obj_delete(obj)` —— 即 `we_obj_delete`（子控件一并删除，动画节点由内核摘链）

## 可调宏
包含头文件前可覆盖：
- `WE_SCROLL_PANEL_SCROLLBAR_W`（滚动条默认宽度，4）
- `WE_SCROLL_PANEL_DRAG_THRESHOLD`（空白处按下后开始拖动的位移，8）
- `WE_SCROLL_PANEL_USE_INERTIA` / `WE_SCROLL_PANEL_USE_REBOUND`（惯性 / 越界回弹开关，默认都开）
- `WE_SCROLL_PANEL_INERTIA_FRICTION_NUM/DEN`（惯性衰减，7/8）
- `WE_SCROLL_PANEL_OVERSCROLL_LIMIT`（越界上限，32）
- `WE_SCROLL_PANEL_REBOUND_PULL_DIV` / `WE_SCROLL_PANEL_REBOUND_MAX_STEP`（回弹每步拉回 过冲/3，1..24）
- `WE_SCROLL_PANEL_VELOCITY_GAIN_NUM/DEN`（松手速度增益，1/1）
- `WE_SCROLL_PANEL_USE_KEY`（按键 / 焦点支持）

## 事件与行为
- 子控件的命中、按压、点击由内核直接派发；面板只应答命中查询（内容区之外返回 0）和拖拽接管
- 子控件上按下后拖动越过内核阈值：内核把手势交给面板（子控件收到一次 RELEASED 回弹），进行中的惯性/回弹先停下
- 空白处按下：位移越过 `WE_SCROLL_PANEL_DRAG_THRESHOLD` 才开始拖动，越过后从按下点起整段跟手
- 松手带速度进入惯性滑行，越界后回弹；无 STAY 的快速轻扫整体翻 `WE_CFG_SWIPE_THRESHOLD` 像素
- 按键：面板是焦点停靠点，OK 下钻进子控件、BACK 退回面板；子控件获得焦点时自动滚动，连光标环一起滚进内容区

## 渲染与标脏
- 边框只画 1px 环带、背景只画环内，两者不重叠，半透明时各像素只混合一次；`radius > 0` 时外壳为圆角
- 圆角只作用于外壳，子控件区仍是矩形：靠近四角的子控件需留出内边距
- 滚动条在子控件之后画（`draw_post_cb`），透明度 = 滚动条透明度 × 面板透明度；过冲期间滑块按夹紧后的滚动量落轨
- 滚动时先标整个面板再平移子控件，子控件的新旧足迹落在已标区域里，脏矩形管理器直接丢弃

## 注意事项
- 局部坐标以面板**外框**左上角为原点，内容区从 (1,1) 开始
- `set_content_h` 应覆盖最下方子控件的底边（局部 y + 高度 + 底部留白），否则最后一项滚不出来
- `remove_child` 会擦掉子控件的像素并摘链，摘下的控件不再绘制

## 已完成的毕业优化
- 修正：内容高下限按整框高而非内容区高，内容不溢出时仍能滚 2px
- 修正：`set_content_h` 缩小时只改滚动量、不平移子控件，子控件永久错位；过冲中调用会卡在负滚动量
- 修正：拖拽接管时不停止惯性/回弹，动画与手指跟随叠加
- 修正：滚动条不随面板透明度淡出；`radius` 参数存下却从不绘制（现在画圆角外壳）
- 修正：动画单步没有时长上限；惯性冲出越界上限后停顿几帧才回弹
- 修正：`add_child` / `remove_child` 不擦旧像素
- 修正：`WE_SCROLL_PANEL_DRAG_THRESHOLD` 定义了却没用，空白处轻点的抖动也会滚动
- 内容区矩形改由外框直接推导（每实例省 8 字节，消除按键回调读到过期缓存）；删掉只写不读的 `animating`
- 松手在界内时不再无条件整面板重绘；删除接口改为内联包装

## 对应 demo
- `Demo/demo_scroll_panel.c`（DEMO_ID 18：8 行文字 + 两个按钮的滚动区，可拖动、惯性、回弹，按钮可正常点击）
