# statusbar（preview 孵化区）

## 功能
高度固定（默认 22px）的深色状态栏横条：左侧显示 "HH:MM" 时间文本，右侧右对齐依次排列 信号（4 柱）→ WiFi（3 层）→ 电池（贴最右）三个矢量图标（高约 12px），全部用 `we_fill_rect` / `we_draw_round_rect_analytic_fill` 拼装，零图片资产。电池支持百分比填充、低电变色与充电小闪电；WiFi/信号支持分级点亮（未点亮层低透明度）与 -1 隐藏（布局自动收拢）。

## 适用场景
- 表盘 / 手持设备 / 面板类 UI 的顶部系统状态条
- 需要电量/连接状态常驻显示但不想引入图片资产的项目
- 演示"纯矢量图标拼装"的参考实现

## 关键 API
- `we_statusbar_obj_init(obj, lcd, x, y, w, font)` —— 高度固定 WE_STATUSBAR_HEIGHT，w ≥ 64
- `we_statusbar_set_time(obj, "12:34")` —— 调用方持有字符串（仅存指针，NULL 隐藏）
- `we_statusbar_set_battery(obj, pct)` —— 0~100，< 20% 填充用低电色
- `we_statusbar_set_charging(obj, 0/1)` —— 充电时填充变充电绿 + 叠小闪电
- `we_statusbar_set_wifi(obj, level)` —— -1 隐藏，0~3 自下而上点亮层数
- `we_statusbar_set_signal(obj, level)` —— -1 隐藏，0~4 自左点亮柱数
- `we_statusbar_set_colors(obj, bg, fg, low)` —— 底色 / 前景色 / 低电色
- `we_statusbar_set_opacity(obj, opacity)`
- `we_statusbar_obj_delete(obj)` —— 无动画节点，无需 we_anim_stop

## 可调宏
包含头文件前可覆盖：
- `WE_STATUSBAR_HEIGHT`（底条高度，默认 22）
- `WE_STATUSBAR_LOW_PCT`（低电阈值百分比，默认 20）
- `WE_STATUSBAR_DIM_OPA`（未点亮层/柱透明度，默认 64）

## 渲染模型与成本
- 底条 = 1 次 `we_fill_rect`；时间 = `we_draw_string`（bbox 垂直居中）
- 电池 = 外层前景色圆角矩形 − 内层底色圆角矩形（1px 壳）+ 电极凸块矩形 + 百分比填充矩形（14px 内腔四舍五入映射）+ 充电时三段前景色矩形拼小闪电（绿色填充与暗色空腔上都可见）
- WiFi = 3 层逐级加宽圆角短条（12/8/4 px 宽，radius 1）叠扇形近似——刻意避开昂贵的同心弧扫描
- 信号 = 4 根 2px 宽、3/6/9/12px 递增高、底对齐小柱
- 图标自右向左流式布局：电池贴右缘，WiFi/信号隐藏（-1）时后续图标自动右移收拢
- 零 malloc、渲染零浮点；所有原语内部自带 PFB 裁剪与 opa_scale 级联

## 事件与行为
- 装饰性控件：event_cb 恒返回 0，输入事件穿透给下层
- 所有数值 setter 值未变直接返回；set_time 例外（见下）
- 无 set_pos_cb：几何全部由 base.x/y 推导，`we_obj_set_pos` 默认移动逻辑即正确

## 注意事项
- `set_time` 每次调用都重绘：调用方通常在原缓冲上覆写内容后重传同一指针，指针等值判断不可靠，preview 阶段不做内容比较
- 时间字符串由调用方持有，控件只存指针；缓冲不可为栈上临时变量
- 图标几何为 12px 高固定像素画，不随底条高度/宽度缩放
- w < 64 时 init 直接拒绝（右侧图标区 + 时间最小占位）

## 毕业前需优化
- 脏矩形：任何值变化都整条标脏；应改为每图标独立小矩形差分标脏（电池 20x12、WiFi 12x11、信号 11x12），时间区单独标脏
- set_time 可缓存上次内容（定长 8 字节本地副本）做内容级幂等判断
- 图标几何按 WE_STATUSBAR_HEIGHT 参数化缩放（当前 12px 固定，高度改大后图标偏小）
- 电池百分比可选数字显示模式（"87%" 文本替代/并列图标）
- WiFi 扇形层与信号柱可加"无服务"叉号/斜杠形态（level 0 与隐藏之间的语义档）
- 低电阈值处可加闪烁提醒（接中央动画引擎）

## 对应 demo
- `Demo/preview/demo_statusbar.c`（DEMO_ID 129：状态栏置顶，下方大 label 联动回显各值；tick 驱动时间每 2 秒 +1 分钟、电池 100→5 递减循环（过 20% 变红、到 5% 切充电态回满）、WiFi/信号各自周期变格）
