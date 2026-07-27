# scale（preview 孵化区）

## 功能
直线刻度尺：基线 + 主刻度长线 + 小刻度短线 + 主刻度整数数字 + 指向当前值的三角指针。两种朝向：`WE_SCALE_H`（水平，刻度朝下，数字居中于刻度下方）与 `WE_SCALE_V`（垂直，刻度朝右，数字靠右并垂直对齐刻度线）。int32 量程线性映射到轴向像素，指针支持立即设值与中央动画平滑滑动（单 `we_anim_t` 节点，不占 GUI timer 槽）。

## 适用场景
- 温度计 / 液位 / 音量 / 进度等带读数的线性标尺
- 与 slider、chart 配合做坐标轴 / 参考标尺
- 需要"指针平滑滑到新值"的示值动画

## 关键 API
- `we_scale_obj_init(obj, lcd, x, y, len, orientation, font)` —— len 为轴向长度，厚度由宏推导（H 尺 `WE_SCALE_H_THICKNESS`，V 尺 `WE_SCALE_V_THICKNESS`）
- `we_scale_set_range(obj, v_min, v_max)` —— int32 量程（max 必须 > min）
- `we_scale_set_ticks(obj, major_step, minor_div)` —— 主刻度值间隔 + 每主刻度间小刻度条数
- `we_scale_set_colors(obj, line, text, pointer)` —— 线色 / 数字色 / 指针色
- `we_scale_set_value(obj, v)` —— 立即就位（打断进行中的滑动）
- `we_scale_anim_value(obj, v, dur_ms)` —— 中央动画平滑滑动（缓入缓出正弦）
- `we_scale_set_show_pointer(obj, 0/1)` —— 指针显隐
- `we_scale_obj_delete(obj)` —— 内部先 `we_anim_stop` 再 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_SCALE_PTR_LEN`（指针三角高/宽，默认 4）
- `WE_SCALE_LINE_W`（基线厚度，默认 2）
- `WE_SCALE_MAJOR_LEN` / `WE_SCALE_MINOR_LEN`（主/小刻度线长，默认 10 / 5）
- `WE_SCALE_TEXT_GAP`（刻度区与数字区间隙，默认 2）
- `WE_SCALE_TEXT_H`（H 尺数字区高度，默认 18 = ASCII16 字库行高）
- `WE_SCALE_V_TEXT_W`（V 尺数字区宽度，默认 34，容纳 "-20"/"100" 级别数字）

## 渲染模型与成本
- 刻度全部为 `we_fill_rect` 1px 竖/横条（无 AA，横平竖直），指针 = `WE_SCALE_PTR_LEN` 条递减宽度 fill_rect 拼的实心小三角
- 数字由内部栈缓冲整数格式化（自带 i32 转十进制 helper，不依赖 stdio）+ `we_draw_string`，字体经 init 传入
- 值→像素映射 `pos = (v - min) * (len - 1) / (max - min)`，纯 int32 乘除；渲染路径零浮点、零 malloc
- 每帧成本 ≈ 1 条基线 + N 主刻度 (线+数字) + N×minor_div 小刻度 + 4 条指针横条

## 事件与行为
- 装饰性控件：event_cb 恒返回 0，输入穿透给背后控件
- `we_scale_anim_value` 滑动中再次调用会以当前显示值为新起点无缝改道
- 所有 setter 值未变时直接返回，不触发重绘
- 指针三角在两端会被横向裁剪进包围盒（尖端始终精确指在值位置，侧翼贴边收平）
- 主刻度密度护栏：主刻度数量超过 len（比像素还密）时整组刻度与数字跳过，只画基线与指针

## 注意事项
- 量程跨度 |v_max - v_min| 需小于 2^22（len <= 320 时映射乘法与 we_lerp 的 int32 中间量防溢出）
- 垂直尺数值方向：v_min 在顶端，数值向下递增（与像素 Y 同向）
- V 尺数字宽超过 `WE_SCALE_V_TEXT_W` 时该数字直接跳过不画（保证不越出包围盒）
- H 尺两端数字会横向钳制进包围盒（首末数字不再严格居中于刻度）
- 删除带动画的控件必须走 `we_scale_obj_delete`（先摘动画链）

## 毕业前需优化
- 脏矩形：指针移动按整控件包围盒标脏；应改为"旧指针条带 + 新指针条带"两小块差分标脏（刻度与数字静态，无需重绘）
- 刻度像素位置每帧重复计算乘除；主刻度/小刻度位置可在 set_range/set_ticks 时缓存（或预除 Q16 斜率去掉每帧除法）
- 数字文本每帧重复格式化 + 测宽；可缓存字符串表或至少缓存首末数字宽度
- V 尺数字方向可选项（v_min 在底端向上递增，更符合温度计直觉）
- 数字区尺寸随字体行高自动推导（当前 `WE_SCALE_TEXT_H` 固定 18，换字体需手动同步）
- 可选功能位：刻度标签自定义格式回调、量程色带（警戒区）、指针带数值气泡
- 小刻度像素等分在主刻度间隔很窄（< minor_div+1 px）时会重叠在同一像素，可加密度护栏

## 对应 demo
- `Demo/preview/demo_scale.c`（DEMO_ID 126：一根水平尺 0~100 主步 20 小分 4 + 一根垂直尺 -20~60，指针每 2 秒向新目标平滑摆动，顶部 label 显示当前目标）
