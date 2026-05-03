# img

## 功能
基础图片控件，直接显示片内图片资源，支持整体透明度。

## 适用场景
- 静态图标
- 装饰图片
- 不需要旋转缩放的普通图片显示

## 关键 API
- `we_img_obj_init(...)`
- `we_img_obj_set_opacity(...)`
- `we_img_obj_set_pos(...)`
- `we_img_obj_draw(...)`

## 可调宏
无专属宏。

## 事件与行为
- `img` 本身不处理交互事件
- 更适合做被动显示控件

## 注意事项
- 资源格式由 `image_res.h / image_res.c` 提供
- 如果需要旋转/缩放，请改用 `img_ex`

## 对应 demo
- `Demo/demo_img.c`
