# stepper

## 功能
数值步进控件（stepper / spinbox）。左 `[-]` / 中数值 / 右 `[+]` 三段式布局，
适合 MCU 设置项（温度、音量、阈值等）。

## 适用场景
- 设置页带加减按钮的数值调节
- 有上下限、固定步进的参数输入
- 需要小数显示但要避开软浮点开销的场景

## 关键 API
- `we_stepper_obj_init(...)`
- `we_stepper_set_value(...)` / `we_stepper_get_value(...)`
- `we_stepper_set_step(...)`
- `we_stepper_set_range(...)`
- `we_stepper_set_wrap(...)`
- `we_stepper_set_enabled(...)`
- `we_stepper_set_changed_cb(...)`
- `we_stepper_obj_delete(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_STEPPER_HOLD_DELAY` — 按住后首次连续步进前的延迟（STAY 次数）
- `WE_STEPPER_HOLD_INTERVAL` — 达到延迟后每隔多少次 STAY 再步进一次
- `WE_STEPPER_MAX_DECIMALS` — 支持的最大小数位数（0~9，超出编译报错）

## 事件与行为
- 点击左/右区按 `step` 减/加
- 按住不放触发连续步进（复用 `STAY` 事件，不用定时器）
- 到边界时：`wrap=0` 禁用对应按钮；`wrap=1` 回绕不禁用
- 数值改变时触发 `changed_cb`

## 注意事项
- 数值统一用**定点 `int32`** 存储：真实值 = `value / 10^decimals`
  - 例：温度 16.0~30.0、步进 0.5、1 位小数 → `decimals=1, min=160, max=300, step=5, init=230`
- 值变化时才拆成整数/小数格式化成文本（同时量好文字宽度），绘制只读缓存，避免 Cortex-M0 软浮点开销与符号歧义
- `step <= 0` 会被强制为 1；`init_value` 自动夹紧到 `[min, max]`

## 已完成的毕业优化
- 修正：10 次幂表只写了 5 项，`WE_STEPPER_MAX_DECIMALS` 设成 5~9 会除零、小于 4 会编译出错；现表覆盖 0~9，超出编译报错
- 数值文字的宽度与竖直中线随格式化缓存（结构体大小不变），每个切片不再逐字测量两遍
- 按钮宽 w/3、符号尺寸 h/6、h/10 改乘移位（与整除逐位相同）；半径为 0 的矩形直接 `we_fill_rect`
- 按下时只标脏一次；删掉恒等于 `active_side != 0` 的 `pressed` 位；删除接口改为内联包装

## 对应 demo
- `Demo/demo_stepper.c`
