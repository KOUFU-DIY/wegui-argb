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
- 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 常作为其他交互控件的说明文字或状态文字使用

## 注意事项
- `we_label_set_text(...)` 会自动处理旧文本区和新文本区的标脏
- 当前工程里的动态文本（FPS、状态行）主要都基于这个控件

## 已完成的毕业优化
- 包围盒宽度计入字形向右伸出的墨迹（如 simsun_12 的 m、msyh 的部分 ASCII），行尾字形不再被内核按包围盒裁掉最右一列
- `set_text` / `set_opacity` 不再为标脏重复测量文字（包围盒就是文字框）；`set_color` 在 GRAY8 下也做相等判断
- 初始化补上 `base.parent = NULL`

## 对应 demo
- `Demo/demo_label.c`
- 各 demo 顶部标题 / 状态行
