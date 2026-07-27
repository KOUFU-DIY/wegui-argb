# tabview（preview）

## 功能
页签条（分段控制器样式）：整条圆角底 + 横向等分 count 段 + active 段圆角高亮块 + 各段居中文字（active 段文字更亮）。切换时高亮块 X 位置经中央动画节点（`we_anim_t` + `we_ease_out_quad` + `we_lerp`）平滑滑动。

控件本身只是页签条；页面内容显隐由调用方在 `changed_cb` 里处理（惯用法：每页一个 `group`，`we_group_set_opacity(0/255)` 显隐，全透明 group 不拦输入）。

## 适用场景
- 多页设置界面的顶部页签
- Home / Data / About 式的小型页面切换器

## 关键 API
- `we_tabview_obj_init(obj, lcd, x, y, w, h, labels, count, font)`
- `we_tabview_set_active(obj, idx)` / `we_tabview_get_active(obj)`
- `we_tabview_set_changed_cb(obj, cb)` —— `void cb(void *tv, uint8_t idx)`
- `we_tabview_set_colors(obj, 底条色, 高亮块色, 文字色)`
- `we_tabview_set_opacity(obj, opacity)`
- `we_tabview_obj_delete(obj)` —— 内部先 `we_anim_stop` 再摘链表

## 可调宏
包含头文件前可覆盖：
- `WE_TABVIEW_PAD`（高亮块与底条边缘内边距，默认 3px）
- `WE_TABVIEW_ANIM_MS`（高亮块滑动时长，默认 220ms）
- `WE_TABVIEW_DIM_TEXT_A`（非 active 文字向底条色压暗保留权重，默认 150）

## 事件与行为
- 点击某段切 active；**值变才**触发 `changed_cb(tv, idx)`
- `CLICKED` 要求按下与释放落在同一段内
- `we_tabview_set_active` 程序设置同样平滑滑动，但**不触发回调**
- 高亮块偏移相对控件左上角保存，`we_obj_set_pos` 移动控件不破坏动画几何
- 动画就位后节点自行摘链（空闲零开销）；删除前必须摘链（`we_tabview_obj_delete` 已代劳）

## 注意事项
- `labels` 数组与文本由调用方持有（控件只存 `const` 指针，零拷贝），元素不得为 NULL
- 段宽 = `(w - 2*pad) / count` 整除，余数像素堆在右缘
- 中途连点：滑动以当前显示位置为新起点重定向，衔接自然

## 毕业前需优化
- 动画每步标脏整条控件包围盒；应改为"高亮块旧位置 + 新位置"两小块标脏
- 文字未按段裁剪，过长页签名会溢出到相邻段
- 无按压视觉反馈（可加按压段文字提亮或高亮块微缩）
- 段数多时无滚动能力（等分制，不适合 >5 段）

## 对应 demo
- `Demo/preview/demo_tabview.c`（DEMO_ID 113：Home/Data/About 三页 group 显隐切换）
