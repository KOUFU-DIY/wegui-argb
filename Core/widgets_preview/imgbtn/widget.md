# imgbtn（preview 孵化区）

## 功能
图片按钮：用一张（或两张）RGB565 未压缩图片资源充当按钮皮肤，
支持按压态切图 / 自动变暗、按下并在框内释放触发点击回调。

## 适用场景
- 图标类按钮（播放/暂停、开关机、方向键盘面）
- 皮肤化 UI：设计稿直接切图充当按钮
- 只有一张图也想要按压反馈的场合（自动叠黑变暗）

## 关键 API
- `we_imgbtn_obj_init(obj, lcd, x, y, img_normal, img_pressed)` ——
  `img_pressed` 传 NULL 时按压视觉 = 常态图叠半透明黑变暗；宽高自动取自资源头
- `we_imgbtn_set_clicked_cb(obj, cb)` —— `void cb(void *btn)`，框内释放触发
- `we_imgbtn_set_opacity(obj, opa)` —— 整体透明度（变暗遮罩会随之等比衰减）
- `we_imgbtn_set_pos(...)` / `we_imgbtn_obj_delete(...)`

## 可调宏
- `WE_IMGBTN_DIM_OPA`（按压变暗遮罩透明度，默认 90，`img_pressed == NULL` 时生效）

## 事件与行为
- 交互控件：`event_cb` 恒返回 1（消费事件，容器据此锁定转发）
- 事件状态机与 btn 一致：PRESSED 切按压视觉、RELEASED 恢复、
  CLICKED（内核保证按下并在原控件框内释放才派发）触发回调
- 渲染走 `we_img_render_rgb565`（PFB 裁剪 + 容器透明度级联由内核处理）
- 图片资源为 image_res.h 信息头 + 大端 RGB565 像素流；指针由调用方持有，
  控件不拷贝

## 注意事项
- 仅支持 `IMG_RGB565` 未压缩格式；其他格式（indexed QOI 等）静默跳过不画
- 两张图建议同尺寸：命中与重绘均按 `img_normal` 的包围盒进行
- 按压变暗遮罩是整块矩形，不跟随图片内的透明区/圆角轮廓

## 毕业前需优化
- 支持 indexed QOI（`we_img_render_indexed_qoi_*`）与 ARGB8565 带透明通道
  资源，变暗遮罩改为跟随像素 alpha 的逐像素变暗（当前整块矩形叠黑）
- 双图尺寸不一致时按各自尺寸标脏（当前统一按常态图包围盒）
- 可选增补：disabled 态（灰化）、长按重复触发（复用 STAY，参考 stepper）
- demo 资产受限：仓库当前只有一张未压缩 RGB565 内置图
  （`img_rgb565_64x80`），双图切换路径尚无真实"按压差异图"验证素材

## 对应 demo
- `Demo/preview/demo_imgbtn.c`（DEMO_ID 110）：两个按钮复用同一张内置图
  （`img_rgb565_64x80`，均走 NULL 按压变暗路径），右侧按钮叠加
  `set_opacity(150)` 演示整体透明度；点击分别累加 L/R 计数 label
