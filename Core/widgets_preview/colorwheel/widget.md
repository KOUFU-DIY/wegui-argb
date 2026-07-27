# colorwheel（preview 孵化区）

## 功能
HSV 色轮取色控件：在 size×size 包围盒内画一圈内切色环（S=V=255 固定，
只选 Hue），环带宽约 size/5，选中角度处画白芯深边圆点标记；
按下 / 拖动实时改变选中色相并触发回调。

## 适用场景
- RGB 灯带 / 氛围灯颜色选择
- 主题色、画笔颜色等单色相取色
- 需要"转一圈选颜色"直觉交互的设置页

## 关键 API
- `we_colorwheel_obj_init(obj, lcd, x, y, size)` —— 色环内切 size×size
- `we_colorwheel_get_color(obj)` —— 当前颜色（colour_t，设备色格式）
- `we_colorwheel_get_rgb(obj, &r, &g, &b)` —— RGB888 三通道（与屏幕色深无关，适合喂 LED）
- `we_colorwheel_get_hue(obj)` / `we_colorwheel_set_hue(obj, 0..511)` —— 512 步制色相
- `we_colorwheel_set_changed_cb(obj, cb)` —— `void cb(void *cw, colour_t c)`，用户交互改值时触发
- `we_colorwheel_set_opacity(...)` / `we_colorwheel_set_pos(...)` / `we_colorwheel_obj_delete(...)`

## 可调宏
- `WE_COLORWHEEL_MARK_MIN_R`（标记点最小半径，默认 3）

## 事件与行为
- 交互控件：`event_cb` 恒返回 1（消费事件）；PRESSED / STAY 把触点向量转
  角度更新 hue，按住拖出控件仍持续跟随（内核按 pressed_obj 路由）
- 触点距中心 < r_in/2 的死区忽略（角度不稳定）
- `set_hue` 程序化设置不触发 changed_cb（回调只响应用户交互）
- 角度 512 步制：0 = +X，128 = 正下方（屏幕 Y 向下），顺时针增
- 渲染逐像素直写 pfb_gram（PFB 条带裁剪照 box 角落套路），环带内外边缘
  1px 用 d² 线性渐隐做简易 AA；全程整数（八分区多项式 atan2 近似误差
  < 1/512 圈，六段整数 Hue→RGB），零 malloc、零浮点

## 注意事项
- 命中检测为包围盒粒度：bbox 四角空白区也会吃掉 PRESSED（挡住下层控件）
- 环中心空洞不属于控件绘制范围，显示的是背景色
- S/V 固定 255：这是"纯色相环"，不是完整 HSV 拾色器

## 毕业前需优化
- **每次 hue 变化按整控件包围盒标脏 → 整环全部重绘，且内环逐像素做
  atan2 近似 + Hue→RGB，代价 O(size²)**；毕业版必须改为：
  1. 只标脏"旧标记点 + 新标记点"两个小矩形（色环本体静态，可不重绘）；
  2. 或把色环渲染成一次性离屏缓存 / 预渲染资源，运行期只贴图 + 画标记
- 边缘 AA 用 d² 线性近似 d，环极细（band < 4px）时渐隐略生硬
- 命中检测应改为环带命中（d² 判定），让 bbox 四角穿透
- 可选增补：S/V 二维选择（中心三角/方块）、双击回默认色

## 对应 demo
- `Demo/preview/demo_colorwheel.c`（DEMO_ID 109）：色轮 + 右侧大色块 box
  实时显示选中色 + 三行 label 显示 R/G/B 数值（整数 sprintf 静态缓冲）
