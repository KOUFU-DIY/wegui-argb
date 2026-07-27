# spectrum（preview 孵化区）

## 功能
N 根竖直电平频谱柱 + 可选峰值保持帽。调用方周期 `we_spectrum_push()` 一帧目标电平（0~255 × bar_cnt），控件内部维护"显示电平"做快上冲/慢回落包络：上升立即取 max，回落经中央动画引擎按差值比例衰减；峰值帽（每柱 2px 横线）跟随柱顶并以更慢的匀速下坠。柱体按全量程高度分段做低色→高色垂直渐变，柱底 1px 基线横贯控件。

## 适用场景
- 音频播放器/收音机的频谱可视化
- 多通道电平表（VU meter 阵列）
- 任意"多路 0~255 数值 + 动态包络"仪表面板

## 关键 API
- `we_spectrum_obj_init(obj, lcd, x, y, w, h, bar_cnt)` —— 柱宽/间隙按 w/bar_cnt 等分推导（间隙 = 槽宽/4，余数居中吸收）
- `we_spectrum_push(obj, levels)` —— 一帧 bar_cnt 个电平，拷入内部数组（此小数组是控件自身状态）
- `we_spectrum_set_colors(obj, low, high, peak)` —— 柱体渐变双色 + 峰值帽色
- `we_spectrum_set_peak_hold(obj, 0/1)` —— 切换时峰值帽复位到当前柱顶
- `we_spectrum_set_opacity(obj, opacity)`
- `we_spectrum_obj_delete(obj)` —— 内部先 `we_anim_stop` 再摘链

## 可调宏
包含头文件前可覆盖：
- `WE_SPECTRUM_BAR_MAX`（默认 32，电平数组按此静态分配，每柱 3 字节）
- `WE_SPECTRUM_STEP_MS`（默认 16，衰减时基量子）
- `WE_SPECTRUM_FALL_SHIFT`（默认 3，柱体每步衰减差值 >> 3，比例回落）
- `WE_SPECTRUM_PEAK_FALL`（默认 2，峰值帽每步匀速下坠电平量）
- `WE_SPECTRUM_GRAD_STEPS`（默认 8，柱体渐变分段数）

## 渲染模型与成本
- 全部 `we_fill_rect`：每柱最多 GRAD_STEPS 段矩形 + 1 条峰值帽 + 全控件 1px 基线
- 分段渐变色每帧只按段预混 `WE_SPECTRUM_GRAD_STEPS` 次（`we_colour_blend`），内环零混色、零浮点
- 段色按绝对高度固定（非按柱长归一化），柱升降不会闪色
- 高度换算 `shown * usable / 255` 为 int32 乘除；顶部预留 2px 峰值帽余量 + 底部 1px 基线（`usable = h - 3`）

## 动画模型
- 单个 `we_anim_t` 节点统一推进全部柱（不占 GUI task 槽）
- 时基量子化：累积 elapsed 按 `WE_SPECTRUM_STEP_MS` 切步，单次限幅 4 步防长卡顿跳空
- 全部柱就位（shown==target 且 peak==shown）后自行摘链，空闲期零开销
- push 仅在有可回落量时才挂链；重复 push 不会重复挂链（`anim_busy` 标志）

## 事件与行为
- 装饰性控件：event_cb 恒返回 0，输入穿透给背后控件
- 所有 setter 值未变时直接返回，不触发重绘
- 无 set_pos_cb：几何全部由 base.x/y 推导，`we_obj_set_pos` 默认移动逻辑即正确

## 注意事项
- `bar_cnt` 超过 `WE_SPECTRUM_BAR_MAX` 时钳制；0 按 1 处理
- 控件高度建议 >= 40px（h-3 为实际量程高度），宽度建议 >= bar_cnt * 4
- push 的 levels 数组由调用方持有，拷贝后即可释放/复用

## 毕业前需优化
- 脏矩形：目前整控件包围盒标脏；应改为逐柱差分标脏（只标电平变化的柱 + 峰值帽扫过区间）
- 高度换算 `/255` 可换 `(v * usable * 257) >> 16` 或 `we_div255` 族近似，消除 M0 软除法
- 渐变段色可在 set_colors 时缓存到结构体（现在每帧重算 GRAD_STEPS 次混色）
- 峰值帽下坠可加"悬停时间"（到顶后停留 N ms 再下坠），观感更接近经典频谱表
- 柱体上冲可选短平滑（当前瞬时取 max，硬上冲），电平剧烈跳变时略生硬

## 对应 demo
- `Demo/preview/demo_spectrum.c`（DEMO_ID 118：24 柱假音频包络，LCG 伪随机 + we_sin 合成，每 50ms push 一帧）
