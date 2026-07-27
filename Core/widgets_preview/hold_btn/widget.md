# hold_btn（preview 孵化区）

## 功能
长按确认按钮：圆形核心 + 外圈分段充能环。按住期间充能进度随真实时间增长（计时在中央动画节点里推进，不依赖 STAY 事件派发频率）；松手未满则进度以 2 倍速回退到 0；充满触发回调一次并闪亮反馈，随后进入已触发锁定态（进度环保持全亮、核心常亮），需 `we_hold_btn_reset` 复位后才能再次充能。适合"误触代价高"的确认类操作（删除/关机/恢复出厂）。

## 适用场景
- 删除、复位、关机等需要防误触的危险操作确认
- "按住 N 秒解锁"类交互
- 与状态 label 联动的充能进度演示

## 关键 API
- `we_hold_btn_obj_init(obj, lcd, x, y, size, label, font)` —— 包围盒 = size × size（建议 >= 60）；label 为调用方持有的 UTF-8 指针（可 NULL）
- `we_hold_btn_set_hold_ms(obj, ms)` —— 充满时长（默认 1200ms；充能中修改会按新时长重折算进度）
- `we_hold_btn_set_triggered_cb(obj, cb)` —— `void (*)(void *hb)`，每轮充满只回调一次
- `we_hold_btn_set_colors(obj, bg, ring, text)` —— 核心底色 / 环亮色 / 文字色
- `we_hold_btn_reset(obj)` —— 清锁定态与进度；复位瞬间仍被按住则立即开始新一轮充能
- `we_hold_btn_get_progress(obj)` —— 当前进度 Q8（0..256）
- `we_hold_btn_is_triggered(obj)` —— 是否处于锁定态
- `we_hold_btn_set_opacity(obj, opacity)`
- `we_hold_btn_obj_delete(obj)` —— 内部先 `we_anim_stop` 再摘链

## 可调宏
包含头文件前可覆盖：
- `WE_HOLD_BTN_DEF_HOLD_MS`（默认 1200）
- `WE_HOLD_BTN_RING_SEGS`（默认 48，充能环辐条段数）
- `WE_HOLD_BTN_FLASH_MS`（默认 320，触发闪亮反馈时长）
- `WE_HOLD_BTN_DECAY_MUL`（默认 2，松手回退速度倍率）
- `WE_HOLD_BTN_TRACK_OPA`（默认 64，未充能轨道辐条透明度）

## 渲染模型与成本
- 充能环 = 512 步制逐段近似：整圈按 `WE_HOLD_BTN_RING_SEGS` 等分，每段一条 `we_draw_line_round` 径向短粗辐条（内半径指向外半径，圆头）；每根辐条只画一次，亮/暗由透明度区分（已充能段全亮，未充能段 `WE_HOLD_BTN_TRACK_OPA` 暗轨道）
- 核心圆 = `we_draw_round_rect_analytic_fill` 退化抗锯齿圆；颜色链：底色 → 已触发向环色混合常亮 → 闪亮期向白色混合（强度随 flash Q8 衰减）→ 按压期轻微增亮
- 标签文字经 PFB 收窄裁剪居中绘制（btn 同款 save/restore 套路），字体经 init 传入
- 角度查 `we_cos/we_sin` Q15 表，内环零浮点、零 malloc

## 动画与计时模型
- 单个 `we_anim_t` 节点承担充能 / 回退 / 闪亮三种推进（由 pressed/triggered/flash 标志区分），不占 GUI task 槽
- **计时不依赖 STAY**：STAY 派发频率取决于输入轮询周期，控件仅在 PRESSED/RELEASED 维护 pressed 标志，充能量 = 动画回调的 elapsed_ms 累计（限幅 128ms/步防长卡顿跳变）
- 充满路径：progress 达 256 → `_hb_fire`（锁定 + 回调一次 + flash=256）→ 节点继续跑闪亮衰减 → flash 归零摘链
- 空闲（待机/锁定稳态）零动画开销

## 事件与行为
- event_cb 恒返回 1（交互控件；容器据此锁定并转发后续事件）
- PRESSED：置按压态；未锁定则挂动画链开始充能
- RELEASED：清按压态；未满进度由动画节点自动转 2 倍速回退
- 已触发锁定态下按压/点击不再引发充能（仍消费事件），直到 reset
- 所有 setter 值未变时直接返回；无 set_pos_cb（几何全由 base.x/y 推导）

## 注意事项
- 按住期间手指移出控件范围充能**不会中断**（内核持续向 pressed_obj 派发事件，与 btn 语义一致）；如需"移出即取消"要自行在 STAY 里判坐标
- `hold_ms` 为 uint16，上限 65535ms
- size < 40 时环带与核心圆偏小，不利于触控

## 毕业前需优化
- 脏矩形：整控件包围盒标脏；应改为"变化角度扇区 + 核心圆"差分标脏（参考 arc 的 tight bbox + 中心空洞排除）
- 充能环观感：分段辐条在段数低/环带厚时有锯齿感，毕业时应换 arc 同款距离场连续弧带（含端帽圆角）
- 每帧重画全部 48 根辐条（含暗轨道），可缓存轨道到 snapshot 或只画进度增量扇区
- 闪亮反馈可换 we_ease_out_quad 曲线（当前线性衰减）
- 可选"移出取消"模式与触发时的震动/声音钩子

## 对应 demo
- `Demo/preview/demo_hold_btn.c`（DEMO_ID 119：中央 120px "HOLD" 长按钮 + 顶部状态 label（idle / charging % / TRIGGERED! 变色）+ 右下 RESET 小按钮复位）
