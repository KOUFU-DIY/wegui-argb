# radio（preview）

## 功能
单选组：一个字符串数组渲染垂直排列的互斥单选行，每行左侧圆形指示器（空心外环，选中叠中心实心圆）+ 右侧文字。行高按字体行高与指示器直径自动算出，控件总高 = 行数 x 行高。

## 适用场景
- 设置页里的互斥档位选择（低/中/高、模式 A/B/C）
- 任何"多选一"的紧凑纵向列表

## 关键 API
- `we_radio_obj_init(obj, lcd, x, y, w, labels, count, font)` —— 总高自动算出
- `we_radio_set_selected(obj, idx)` / `we_radio_get_selected(obj)`
- `we_radio_set_changed_cb(obj, cb)` —— `void cb(void *radio, uint8_t idx)`
- `we_radio_set_colors(obj, 圈色, 选中色, 文字色)`
- `we_radio_set_opacity(obj, opacity)`
- `we_radio_obj_delete(obj)`

## 可调宏
包含头文件前可覆盖：
- `WE_RADIO_ROW_PAD`（行上下内边距，默认 6px）
- `WE_RADIO_IND_D`（指示器直径，默认 18px）
- `WE_RADIO_RING_W`（外环厚度，默认 2px）
- `WE_RADIO_TEXT_GAP`（指示器与文字间距，默认 8px）
- `WE_RADIO_PRESS_LIGHTEN`（按压行向白色混合 alpha，默认 26）

## 事件与行为
- 点击行切换选中（互斥）；**值变才**触发 `changed_cb(radio, idx)`，且只标脏受影响的新旧两行
- 按压行绘制轻微高亮条；拖出行取消按压态（本次触摸不再切换）
- `we_radio_set_selected` 程序设置**不触发回调**（与 checkbox `set_checked` 约定一致）
- 无动画节点，删除无需 `we_anim_stop`

## 注意事项
- `labels` 数组与文本由调用方持有（控件只存 `const` 指针，零拷贝），元素不得为 NULL
- 指示器掏环用行底色回填：按压行时自动改用高亮底色，视觉连贯
- 控件宽 `w` 决定整行可点击区域与按压高亮条宽度，应给足文字长度

## 毕业前需优化
- 文字未按控件宽裁剪，过长文本会溢出右缘
- 重绘遍历全部行，依赖 PFB 原语裁剪；应按脏区求交只画相关行
- 选中切换无过渡动画（可仿 toggle 加中心圆缩放/颜色渐变）
- 无 disabled 行、无逐行独立配色

## 对应 demo
- `Demo/preview/demo_radio.c`（DEMO_ID 105：Low/Mid/High 三选项 + 顶部回显）
