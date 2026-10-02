# img

## 功能
基础图片控件，直接显示片内图片资源，支持整体透明度；A1/A2/A4/A8 透明位图额外支持前景色上色。

## 适用场景
- 静态图标
- 装饰图片
- 不需要旋转缩放的普通图片显示
- 单色可换色图标（A1/A2/A4/A8 透明位图 + 前景色）

## 支持的图片格式
| 格式 | 说明 |
| --- | --- |
| `IMG_RGB565` | 无压缩 RGB565 |
| `IMG_ARGB8565` | 无压缩，逐像素 [alpha][RGB565 大端] 3 字节 |
| `IMG_RGB565_INDEXQOI` / `IMG_ARGB8565_INDEXQOI` | 索引 QOI 压缩（`WE_CFG_ENABLE_INDEXED_QOI` 裁剪） |
| `IMG_A1 / IMG_A2 / IMG_A4 / IMG_A8` | 纯 alpha 透明位图，绘制时以控件前景色混合；取模数据每行按字节对齐、位序高位在前 |
| `IMG_A8_INDEXQOIMASK` | 索引QOI_MASK 压缩 A8 透明蒙版（alpha 推荐格式，`WE_CFG_ENABLE_INDEXQOI_MASK` 裁剪）：行独立编码 + 行字节偏移索引，PFB 切片只解可见行、流式零额外 RAM，可选 8/7/6/5bit 量化，48×48 图标实测约为裸 A8 的 21%~40% |

不支持的格式在 `we_img_obj_init` 阶段即拦截（`class_p` 置 NULL，不进入绘制）。

格式分发表在渲染层（`Core/we_image.c` 的 `we_img_render_auto` /
`we_img_format_supported`），`img` 与 `imgbtn` 共用同一份：
新增像素格式只改渲染层一处，控件层不必跟着改。

## 关键 API
- `we_img_obj_init(...)`
- `we_img_obj_set_opacity(...)`
- `we_img_obj_set_color(...)`（仅 A1/A2/A4/A8 生效，默认白色）
- `we_img_obj_set_pos(...)`
- `we_img_obj_delete(...)`

## 可调宏
- `WE_CFG_ENABLE_INDEXED_QOI`（we_user_config.h）：裁掉索引 QOI 解码与分发路径
- `WE_CFG_ENABLE_INDEXQOI_MASK`（we_user_config.h）：裁掉索引QOI_MASK（A8 蒙版压缩）解码与分发路径

## 事件与行为
- 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 更适合做被动显示控件
- A 位图的 `set_color` 带同色去重：颜色未变化不触发重绘

## 注意事项
- 资源格式契约见 `Core/image_res.h`（v2 信息头），素材由 `tool/2.img2c` 例程生成
- 如果需要旋转/缩放，请改用 `img_ex`（仅接受无压缩 RGB565）
- A 位图不含颜色信息，同一份取模可反复换色复用，位深越低体积越小（A1 每像素 1 bit）
- alpha 蒙版当前推荐用索引QOI_MASK 压缩取模（`tool/2.img2c/input/alpha/A8_indexqoimask_*` 桶）；
  A8 raw 桶保留作后续旋转图支持的口径，A1/A2/A4 raw 取模桶已默认移除（解码能力仍在）

## 已完成的毕业优化
- 索引 QOI 的 RGB565 / ARGB8565 两个解码器合并为一个（按每像素字节数区分调色盘项长与 0xFF 载荷），RUN 先跳过裁剪左缘以外的部分再连续填到右缘
- 各格式渲染入口在容器透明度级联后 <= 5 时直接返回（什么也画不出，连解码也省掉）；控件绘制回调同样提前返回，不再逐切片检查 init 已保证的指针
- 删掉 `we_img_obj_draw`：直接调 draw_cb 会绕过内核把窗口收窄到包围盒的步骤，全仓也没有调用点
- init 清 `parent`（未清零的静态对象标脏时不会沿垃圾父链走）；颜色比较用 `we_colour_eq`（GRAY8 下也能去重）

## 对应 demo
- `Demo/demo_img.c`（RGB565/ARGB8565 显示 + 透明度/位置动画）
- `Demo/demo_img_alpha.c`（A8 raw 与索引QOI_MASK 解码一致性对比 + 前景色上色/循环变色）
