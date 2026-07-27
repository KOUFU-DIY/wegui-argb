# joystick（preview 孵化区）

## 功能
虚拟摇杆：内切于 size×size 正方形的圆形托盘（暗色底盘 + 中心十字准线）+ 直径 ≈ size/2.5 的实心摇杆头。手指在底盘圆内按下后拖动摇杆头（可拖出包围盒，方向仍正确），偏移限幅在行程圆内；输出二维矢量 (dx, dy)，每轴 -127..127（屏幕坐标系，右/下为正），死区内输出 0。松手后摇杆头经中央动画弹性回中（we_ease_out_back 轻微过冲，默认 220ms），回中过程持续输出衰减矢量。

## 适用场景
- 遥控小车 / 云台 / 无人机的方向速度控制面板
- 游戏 demo 的移动输入（配合 tick 里 pos += vector 积分）
- 菜单/地图的连续平移导航

## 关键 API
- `we_joystick_obj_init(obj, lcd, x, y, size)` —— size = 底盘圆直径（最小 40，包围盒 size×size）
- `we_joystick_get_vector(obj, &dx, &dy)` —— 当前矢量（-127..127，中心 0）
- `we_joystick_set_changed_cb(obj, cb)` —— 矢量变化即回调（拖动 + 回中全程）
- `we_joystick_set_colors(obj, base, knob, cross)` —— 底盘 / 摇杆头 / 准线三色
- `we_joystick_set_deadzone(obj, pct)` —— 死区百分比（相对行程，默认 8，钳 0~90）
- `we_joystick_set_opacity(obj, opacity)`
- `we_joystick_obj_delete(obj)` —— 内部先 we_anim_stop 再摘链

## 可调宏
包含头文件前可覆盖：
- `WE_JOYSTICK_RETURN_MS`（回中动画时长，默认 220ms）
- `WE_JOYSTICK_DEF_DEADZONE`（默认死区百分比，默认 8）

## 渲染模型与成本
- 底盘 / 摇杆头 = `we_draw_round_rect_analytic_fill` 退化实心抗锯齿圆（w=h=直径、radius=半径），十字准线 = 两条 1px `we_draw_line_round`；控件自身无逐像素扫描代码
- 摇杆头半径 = size/5（钳 ≥6），行程半径 travel = size/2 - 头半径，最大偏移时摇杆头恰好贴底盘边缘（不出包围盒）
- 触点限幅：平方比较判越界（圆内快速路径零开方）；越界时一次整数 isqrt（逐位逼近，无除法）求模长后整数缩放回圆周
- 零 malloc、控件层零浮点；原语内部自带 PFB 裁剪与 opa_scale 级联

## 事件与行为
- PRESSED：触点在底盘圆内（平方比较）才接管并打断回中动画，返回 1；圆内即按即跳（摇杆头直接吸附触点）；圆外（包围盒四角）返回 0
- STAY：接管态下摇杆头实时跟随触点（允许拖出包围盒）
- RELEASED/CLICKED：转入弹性回中，`we_ease_out_back` 过冲插值（we_lerp 支持 t>256），到中心后动画节点自行摘链
- 矢量 = 偏移 × 127 / travel（四舍五入）；死区判定平方比较，死区内 (0,0)
- changed_cb 仅在矢量整数值真正变化时触发（拖动与回中全程一致）；程序侧 setter 不触发
- 无 set_pos_cb：几何全部由 base.x/y 推导，`we_obj_set_pos` 默认移动逻辑即正确

## 注意事项
- size < 40 时 init 直接拒绝；建议 ≥ 90 保证触控手感
- 圆外 PRESSED 返回 0 只是语义声明：driver 命中检测按矩形包围盒且不消费 PRESSED 返回值，触点仍会被锁定到本控件（不会真正穿透给下层控件），只是本控件不进入拖动态
- 回中过程中再次按住可即时打断动画重新接管
- deadzone 修改即时生效但不重绘（摇杆头位置不受死区影响）

## 毕业前需优化
- 脏矩形：目前任何偏移变化都整包围盒标脏；应改为"旧摇杆头 + 新摇杆头"两个小圆矩形差分标脏（底盘静态无需重刷）
- 死区外矢量未重映射：过死区边界时输出有一个小台阶，应改为 (len - dead) / (travel - dead) 的平滑起坡映射
- 圆外按下真正穿透：需要 driver 支持"PRESSED 返回 0 时改派下层对象"或控件提供自定义命中测试回调
- 可选输出口径：极坐标（角度 512 步制 + 模长）与 8 方向量化输出
- 可选自锁模式（松手不回中，适合油门杆）与仅单轴模式（水平/垂直滑杆退化）
- 十字准线在摇杆头下方被整体重绘，可预烘焙进底盘减少一次原语调用

## 对应 demo
- `Demo/preview/demo_joystick.c`（DEMO_ID 128：左侧 110px 摇杆，右侧 120×120 围栏内小圆点按矢量速度移动（碰壁夹紧），顶部 label 实时显示 "dx,dy"，changed_cb 驱动）
