# img_ex

## 功能
支持旋转、缩放、偏心轴心的高级图片控件，适合指针、仪表、动态旋转图元。

## 适用场景
- 仪表盘指针
- 偏心摆动图像
- 缩放呼吸动画
- 需要绕指定中心旋转的图片

## 关键 API
- `we_img_ex_obj_init(...)`
- `we_img_ex_obj_set_center(...)`
- `we_img_ex_obj_set_pivot_offset(...)`
- `we_img_ex_obj_set_transform(...)`
- `we_img_ex_obj_set_opacity(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_IMG_EX_SAMPLE_MODE`
- `WE_IMG_EX_ENABLE_EDGE_AA`
- `WE_IMG_EX_ASSUME_OPAQUE`
- `WE_IMG_EX_USE_TIGHT_BBOX`
- `WE_IMG_EX_ENABLE_FAST_RGB565_BLEND` 为自动推导宏，一般不手改

## 事件与行为
- 不以交互事件为主
- 会在移动/滚动类场景里依赖重绘与位置更新

## 注意事项
- 当前只支持未压缩 `IMG_RGB565` 原图
- 不支持 `RLE / QOI / 索引QOI` 作为 `img_ex` 源图
- 角度为 512 步制，缩放以 `256 = 1.0x`
- `cx/cy` 是屏幕中心点，`pivot_ofs_x/y` 是图片局部轴心偏移，两者不是一个概念

## 对应 demo
- `Demo/demo_img_ex.c`
