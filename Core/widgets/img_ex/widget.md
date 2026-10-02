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
- 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 随容器平移（group / scroll_panel / slideshow）时，`set_pos_cb` 把包围盒和变换中心 `cx/cy` 一起平移

## 注意事项
- 当前只支持未压缩 `IMG_RGB565` 原图
- 不支持 `RLE / QOI / 索引QOI` 作为 `img_ex` 源图：这类对象照常挂链占位，但 `class_p` 置 NULL，不绘制（img 控件同口径）
- 角度为 512 步制，缩放以 `256 = 1.0x`；缩放为 0 时不绘制，包围盒按最大 64 倍计算
- `cx/cy` 是屏幕中心点，`pivot_ofs_x/y` 是图片局部轴心偏移，两者不是一个概念
- 采样点取屏幕像素中心：0°、1.0x 时源图逐像素原样画出，宽高同奇偶时 90°/180°/270° 也逐像素一致；采样点只由屏幕坐标决定，与 PFB 行数、脏矩形切法无关

## 已完成的毕业优化
- 逆映射步进改为四舍五入（原先截断，1.0x 时步进 4095 而非 4096：中心列重复、末列丢失）；偶数宽高的采样点补半个步进落到像素中心
- 最近邻采样边界改为 `[0, 宽)`，不再丢末行末列；双线性以源像素中心为插值基准，首末半像素钳到边缘像素，邻点越界时退回自身
- 边缘羽化按“采样点到边界的距离 + 半像素”估覆盖率：0° 时最外圈不再被羽化成近乎透明
- 步进与外接框抽成 `we_draw` 的 `we_xform_steps` / `we_xform_bbox`（与 label_ex 共用），纯 32 位运算，不再每帧做 64 位乘法；外接框四角按半像素精度向外取整
- 简易包围盒模式改为 `|x| + |y|` 外接正方形（原先按较长边的一半取正方形，旋转 45° 时装不下对角线，四角被裁）
- 绘制窗口直接取内核收窄后的 `lcd->clip`，行指针逐行累加
- 源图格式在 init 时校验一次：非 RGB565 的对象 `class_p` 置 NULL，绘制回调里不再逐帧判格式；删掉两层只做转发的混色 / 转色包装

## 对应 demo
- `Demo/demo_img_ex.c`
