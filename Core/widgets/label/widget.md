# label

## 功能
最基础的文本控件，用 UTF-8 字符串和内置字库绘制普通文字。

## 适用场景
- 标题
- 状态栏
- 数值显示
- 简单提示文本

## 关键 API
- `we_label_obj_init(...)`
- `we_label_set_text(...)`
- `we_label_set_color(...)`
- `we_label_set_opacity(...)`
- `we_label_obj_set_pos(...)`

## 可调宏
无专属宏。

## 事件与行为
- `label` 本身不处理交互事件
- 常作为其他交互控件的说明文字或状态文字使用

## 注意事项
- `we_label_set_text(...)` 会自动处理旧文本区和新文本区的标脏
- 当前工程里的动态文本（FPS、状态行）主要都基于这个控件

## 对应 demo
- `Demo/demo_label.c`
- 各 demo 顶部标题 / 状态行
