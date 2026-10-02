# box

## 功能
可作卡片 / 面板底板的矩形：四个角各自独立配置为圆角、45° 切角或直角（半径 0），可加边框（厚度 + 颜色，圆角/切角处边框随轮廓走），纯色填充 + 整体透明度；可选填充颜色动画与透明度动画。

## 适用场景
- 卡片、分组底板、信息面板
- 只有部分角需要圆角/切角的异形面板（如贴边的抽屉、标签页）
- 需要颜色或明暗过渡的装饰底块

## 关键 API
- `we_box_obj_init(obj, lcd, x, y, w, h)` —— 默认四角圆角 `WE_BOX_DEF_RADIUS`、无边框、不透明
- `we_box_set_corner(obj, idx, style, r)` —— 单独配置一个角（`WE_BOX_LT/RT/LB/RB`，`WE_BOX_CORNER_ROUND/CHAMFER`，r=0 为直角）
- `we_box_set_radius(obj, r)` —— 四角统一圆角
- `we_box_set_color(obj, c)` / `we_box_set_border(obj, c, width)` / `we_box_set_opacity(obj, opa)`
- `we_box_set_pos(obj, x, y)` / `we_box_set_size(obj, w, h)`
- `we_box_anim_color(obj, c, ms, ease)` / `we_box_anim_opacity(obj, opa, ms, ease)`
- `we_box_set_event_cb(obj, cb)` —— 设回调后才接收输入，传 NULL 恢复装饰性穿透
- `we_box_obj_delete(obj)` —— 即 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_BOX_USE_ANIM`（默认 0）：置 1 编入两个动画节点（颜色 / 透明度，可同时进行、共用一个缓动）；为 0 时 `we_box_anim_*` 立即生效
- `WE_BOX_ANIM_MS`：默认动画时长
- `WE_BOX_DEF_RADIUS`、`WE_BOX_DEF_R/G/B`：默认半径与填充色

## 事件与行为
- 默认装饰性：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 设了用户回调后换成带 event_cb 的类，全部输入事件交给用户回调
- 所有 setter 值未变时直接返回，不重绘

## 渲染说明
- 中央与四条直边（含边框直段）走 `we_fill_rect` 整块填充；只有四个 K×K 角落方块做合成（K = max(各角半径, 边框厚, 边框厚 + 内半径)）
- 圆角：带边框时 `we_mask_quarter_ring_alpha` 一遍 4×4 子采样同时求外/内覆盖，先按外覆盖画边框、再按内覆盖压填充，外沿以内的像素不透出背后内容
- 切角：45° 覆盖率精确整数解析（0/128/255），每行两个断点按段整块写；边框内轮廓收缩 0.586×边框厚，对角段与直边段等厚
- 当前 PFB 窗口整个落在上下两条角落横带之间时，只画左右边框柱与中央填充
- 各角半径绘制时钳制到宽高各半

## 注意事项
- 半径 / 切角尺寸上限 255
- 边框厚度超过宽高一半时整块画成边框色

## 已完成的毕业优化
- 修正：带边框圆角先画填充再叠边框，内沿 AA 像素最多透出 25% 背后内容；切角内沿像素同理——改为边框在下、填充在上
- 装饰性穿透改为 event_cb 为 NULL（设用户回调时换类），移动改走内核默认逻辑（删掉重复的移动回调）
- 窗口只落在中段时跳过横带与四角；角落方块的裁剪与行指针合为一个函数；平直区写像素合并成一次调用
- 动画推进改用共用的 `we_motion_advance`，值未变的动画步不标脏；删除接口改为内联包装（动画节点由内核摘链）

## 对应 demo
- `Demo/demo_box.c`（DEMO_ID 23：混合四角样式、带边框、换色、明暗呼吸四块面板）
- `Demo/demo_marquee.c`（走马灯的底板）
