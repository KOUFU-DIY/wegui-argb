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

不支持的格式在 `we_img_obj_init` 阶段即拦截（`class_p` 置 NULL，不进入绘制）。

## 关键 API
- `we_img_obj_init(...)`
- `we_img_obj_set_opacity(...)`
- `we_img_obj_set_color(...)`（仅 A1/A2/A4/A8 生效，默认白色）
- `we_img_obj_set_pos(...)`
- `we_img_obj_draw(...)`

## 可调宏
- `WE_CFG_ENABLE_INDEXED_QOI`（we_user_config.h）：裁掉索引 QOI 解码与分发路径

## 事件与行为
- `img` 本身不处理交互事件
- 更适合做被动显示控件
- A 位图的 `set_color` 带同色去重：颜色未变化不触发重绘

## 注意事项
- 资源格式契约见 `Core/image_res.h`（v2 信息头），素材由 `tool/2.img2c` 例程生成
- 如果需要旋转/缩放，请改用 `img_ex`（仅接受无压缩 RGB565）
- A 位图不含颜色信息，同一份取模可反复换色复用，位深越低体积越小（A1 每像素 1 bit）

## 对应 demo
- `Demo/demo_img.c`（RGB565/ARGB8565 显示 + 透明度/位置动画）
- `Demo/demo_img_alpha.c`（A1/A2/A4/A8 位深阶梯对比 + 前景色上色/循环变色）
