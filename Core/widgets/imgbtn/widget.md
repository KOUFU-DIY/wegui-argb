# imgbtn

## 功能
图片按钮：用一张（或两张）图片资源充当按钮皮肤，支持 img 控件的全部图片
格式，按压态切图 / 自动变暗、按下并在框内释放触发点击回调、运行时换图。

## 适用场景
- 图标类按钮（播放/暂停、开关机、方向键盘面）
- 皮肤化 UI：设计稿直接切图充当按钮
- 单色图标按钮：A1/A2/A4/A8 透明位图 + 前景色上色，同一份取模反复换色复用
- 只有一张图也想要按压反馈的场合（自动变暗）

## 关键 API
- `we_imgbtn_obj_init(obj, lcd, x, y, img_normal, img_pressed, clicked_cb)` ——
  点击回调直接在末参传入（btn 同款范式）；`img_pressed` 传 NULL 时按压视觉 =
  常态图叠半透明黑变暗；宽高自动取自资源头
- `we_imgbtn_set_imgs(obj, img_normal, img_pressed)` —— 运行时换图（播放/暂停
  这类切图按钮）：校验通过才整体生效，同步刷新宽高并标脏新旧区域
- `we_imgbtn_set_color(obj, color)` —— A1/A2/A4/A8 透明位图的前景色（默认白，
  其余格式忽略）
- `we_imgbtn_set_opacity(obj, opa)` —— 整体透明度（变暗效果随之等比衰减）
- `we_imgbtn_set_pos(...)` / `we_imgbtn_obj_delete(...)` ——
  头文件内联薄封装，转调 `we_obj_set_pos` / `we_obj_delete`

点击回调签名：`void cb(we_imgbtn_obj_t *btn)`（强类型，无需在回调里强转）。

## 支持的图片格式
与 `img` 控件完全一致——两者共用渲染层的同一份分发表
（`we_img_render_auto` / `we_img_format_supported`，见 `Core/we_render.c`）：

| 格式 | 说明 |
| --- | --- |
| `IMG_RGB565` | 无压缩 RGB565 |
| `IMG_ARGB8565` | 无压缩，逐像素带 Alpha |
| `IMG_RGB565_INDEXQOI` / `IMG_ARGB8565_INDEXQOI` | 索引 QOI 压缩（`WE_CFG_ENABLE_INDEXED_QOI` 裁剪） |
| `IMG_A1 / A2 / A4 / A8` | 纯 alpha 透明位图，用 `color` 前景色上色 |
| `IMG_A8_INDEXQOIMASK` | 索引QOI_MASK 压缩 A8 蒙版（alpha 推荐格式，`WE_CFG_ENABLE_INDEXQOI_MASK` 裁剪），同样用 `color` 上色，按压变暗走透明度缩放路径 |

## 可调宏
- `WE_IMGBTN_DIM_OPA`（不透明图的按压叠黑透明度，默认 90，`img_pressed == NULL` 时生效）
- `WE_IMGBTN_DIM_SCALE`（带透明通道图的按压透明度缩放，默认 165，同上条件生效）
- `WE_IMGBTN_USE_KEY`（默认跟随 `WE_CFG_ENABLE_KEY_INPUT`，置 0 单独裁掉本控件
  的按键回调与可聚焦性）

## 事件与行为
- 交互控件：`event_cb` 恒返回 1（消费事件，容器据此锁定转发）
- 事件状态机与 btn 一致：PRESSED 切按压视觉、RELEASED 恢复、
  CLICKED（内核保证按下并在原控件框内释放才派发）触发回调
- 按键：btn 同款 OK 双沿——按下沿进按压视觉，松开沿回弹并触发点击，
  FLASH_END（焦点被切走等取消）仅回弹不点击；全透明时拒绝聚焦
- 渲染走渲染层的格式分发 `we_img_render_auto`（PFB 裁剪 + 容器透明度级联
  由内核处理）
- 自动变暗（`img_pressed == NULL`）按格式分两路：带逐像素透明度的图
  （ARGB8565 / A1~A8）压低整体透明度，透明区不会出现方形黑影；不透明图
  绘制后叠一层半透明黑，按压感更实
- 图片资源为 image_res.h 信息头格式；指针由调用方持有，控件不拷贝

## 资源占用（STM32F030 / Cortex-M0 实测）
DEMO_ID 32 单独编译：**ROM 35.8 KB / RAM 5.93 KB**（口径同 README 逐 demo 表）。
相对基准底座（demo 1 label，17.3 KB）的 +18.5 KB 里，约 14 KB 是 demo 自带的
四张图片资产（64×80 RGB565 一张 + 48×48 A8 raw 一张 + 48×48 索引QOI_MASK
两张），控件本体与全部解码器（含索引QOI_MASK）合计约 4.6 KB。

想再瘦：把 `WE_CFG_ENABLE_INDEXED_QOI` / `WE_CFG_ENABLE_INDEXQOI_MASK` 置 0
可分别裁掉索引 QOI / 索引QOI_MASK 解码器——`we_img_render_auto` 引用全部
解码器，只用 RGB565 的工程也会把它们链进来，这是"一个控件吃所有格式"
换来的代价。

## 注意事项
- 格式在 `init` / `set_imgs` **一次性校验**（img 控件同口径）：任一张图不被
  支持 → `class_p` 置 NULL，控件不画、不可点、不可聚焦，不会出现"看不见
  却能点"的隐形按钮；之后用 `set_imgs` 换成合法资源即恢复可用
- 换图只能走 `we_imgbtn_set_imgs`；**不要对已挂链的对象重复调用 `obj_init`**
  （会把对象再次挂链，形成自环导致遍历死循环）
- 两张图建议同尺寸：命中与重绘均按 `img_normal` 的包围盒进行
- 不透明图的按压叠黑遮罩是整块矩形，不跟随图片内的圆角轮廓

## 毕业前需优化
- 双图尺寸不一致时按各自尺寸标脏（当前统一按常态图包围盒）
- 可选增补：disabled 态（灰化）、长按重复触发（复用 STAY，参考 stepper）
- 不透明图的按压叠黑仍是整块矩形，圆角/异形图标边缘会露出方角

## 对应 demo
- `Demo/demo_imgbtn.c`（DEMO_ID 32）：三个按钮分别演示三条路径——
  左 `demo_rgb565_raw_be_64x80`（不透明图，按压叠黑）、中
  `demo_windows_a8_raw_be_48x48`（A8 位图上色，按压走透明度变暗，点击换色）、
  右 `demo_chat_a8_raw_be_48x48` ↔ `demo_picture_a8_raw_be_48x48`
  （点击走 `set_imgs` 运行时换图）；点击分别累加 L/M/R 计数 label
