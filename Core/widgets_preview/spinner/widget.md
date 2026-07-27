# spinner（preview 孵化区）

## 功能
加载指示器：12 个小圆点沿内切圆环均布，以旋转头索引为最亮点、沿环逐点 alpha 递减形成拖尾，周期性前进一格产生旋转效果。用于"加载中 / 等待"状态提示。

## 适用场景
- 网络请求、外部 flash 读取等耗时操作的等待提示
- 开机 / 页面切换过渡动画
- 多个不同尺寸/颜色实例并存（每实例一个动画节点，互不干扰）

## 关键 API
- `we_spinner_obj_init(obj, lcd, x, y, diameter)` —— 包围盒 = diameter × diameter，init 后立即旋转
- `we_spinner_set_colors(obj, color)` —— 主色（圆点同色，拖尾仅按 alpha 衰减）
- `we_spinner_start(obj)` / `we_spinner_stop(obj)` —— 启停（stop 摘链定格，空闲期零开销）
- `we_spinner_is_running(obj)`
- `we_spinner_set_speed(obj, step_ms)` —— 步进周期（毫秒/格，最小 16ms）
- `we_spinner_set_opacity(obj, opacity)`
- `we_spinner_obj_delete(obj)` —— 内部先 `we_anim_stop` 再 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_SPINNER_DOT_CNT`（默认 12，圆点个数，同时决定拖尾衰减梯度）
- `WE_SPINNER_DEF_STEP_MS`（默认 80，一圈 = DOT_CNT × step_ms ≈ 0.96s）
- `WE_SPINNER_TAIL_MIN`（默认 18，拖尾末端最低亮度）
- `WE_SPINNER_DEF_R/G/B`（默认青蓝主色）

## 渲染模型与成本
- 每个圆点 = `we_draw_round_rect_analytic_fill` 退化实心抗锯齿圆（w = h = 直径，radius = 半径）；点径 ≈ 直径/5，环半径贴外沿留 1px
- 点位由 `we_cos/we_sin`（Q15，512 步制均布）算出；每帧固定 12 次解析圆填充，渲染路径零浮点、零 malloc
- 旋转推进走单个中央动画节点：节点内累计 elapsed_ms，每满 step_ms 头索引 +1 并标脏；慢主循环下自动补进多格不丢拍

## 事件与行为
- 装饰性控件：event_cb 返回 0，输入穿透给背后控件
- stop 后画面定格在当前相位（不清除圆点），start 恢复推进
- 所有 setter 值未变时直接返回；set_speed 只改推进速度不触发重绘
- 无 set_pos_cb：几何全部由 base.x/y 推导，`we_obj_set_pos` 默认移动逻辑即正确

## 注意事项
- 直径 < 12px 时圆点挤在一起，建议 diameter ≥ 32
- 删除必须走 `we_spinner_obj_delete`（先摘动画链）

## 毕业前需优化
- 脏矩形：目前每格转动按整包围盒标脏，环中心的大片空腔被无谓重绘；应改为"仅标脏 12 个点的外接小矩形"或"环带 bbox 排除内切空洞"（参考 arc 的 `_arc_invalidate_bbox_exclude_hole`）
- 点位缓存：12 个点的圆周坐标每帧重复计算 we_cos/we_sin，可在 init/尺寸确定时缓存（12×4 字节）
- 可选样式位：弧线式 spinner（旋转的缺口圆弧，参考 arc 渲染）、点径/环径比可调
- alpha 衰减曲线可换缓动（当前线性），头部可加 1 格"亮尾"提升方向感

## 对应 demo
- `Demo/preview/demo_spinner.c`（DEMO_ID 101：三个不同直径/颜色/速度的 spinner 并排，中间那个每 1.6s 自动 stop/start 演示启停，下方 label 显示其运行状态）
