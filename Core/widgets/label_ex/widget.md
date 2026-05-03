# label_ex

## 功能
可旋转、可缩放的高级文字控件，渲染模型与 `img_ex` 类似，但对象本体仍然很轻量。

## 适用场景
- 旋转标题
- 仪表刻度文字
- 呼吸/摆动文字动画
- 需要文字变换效果的页面元素

## 关键 API
- `we_label_ex_obj_init(...)`
- `we_label_ex_set_text(...)`
- `we_label_ex_set_center(...)`
- `we_label_ex_set_transform(...)`
- `we_label_ex_set_color(...)`
- `we_label_ex_set_opacity(...)`

## 可调宏
无专属宏。

## 事件与行为
- `label_ex` 本身不处理交互事件
- 常用于动画或装饰性文字

## 注意事项
- 角度单位和 `img_ex` 一致，使用 512 步制
- 缩放单位 `scale_256` 中，`256 = 1.0x`
- `cx/cy` 是屏幕变换中心，不是左上角坐标

## 对应 demo
- `Demo/demo_label_ex.c`
