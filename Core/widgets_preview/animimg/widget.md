# animimg（preview 孵化区）

## 功能
帧动画控件：按固定间隔循环播放一组"w×h 个 RGB565 像素（uint16_t 本机
字节序，不带资源头）"的裸像素帧，支持 start / stop 与运行期改帧间隔。

## 适用场景
- 加载动画、待机表情、小图标动效
- 代码运行期生成的帧（波形快照、粒子图案等）直接循环播放
- 传感器/算法输出的小尺寸位图序列可视化

## 关键 API
- `we_animimg_obj_init(obj, lcd, x, y, w, h)` —— 帧尺寸即控件尺寸
- `we_animimg_set_frames(obj, frames, frame_cnt, interval_ms)` ——
  `frames` 为 `const uint16_t *const *` 帧指针数组，指针数组与像素数据均由
  调用方持有；绑定后回到第 0 帧并标脏一次
- `we_animimg_start(obj)` / `we_animimg_stop(obj)` —— 挂上 / 摘下中央动画节点
- `we_animimg_set_interval(obj, ms)` —— 运行期改帧间隔（0 钳为 1）
- `we_animimg_set_opacity(...)` / `we_animimg_set_pos(...)` / `we_animimg_obj_delete(...)`

## 可调宏
- `WE_ANIMIMG_DEF_INTERVAL_MS`（默认帧间隔，默认 100ms；set_frames 传 0 时兜底）

## 事件与行为
- 装饰性控件：`event_cb` 恒返回 0，输入穿透给背后控件
- 帧推进走**中央动画引擎**（单个 `we_anim_t` 节点，不占 GUI timer 槽）：
  step_cb 累计 elapsed_ms、跨过 interval_ms 才换帧、**帧号变化才标脏**；
  停播即摘链，空闲零开销
- 渲染为控件自写的 PFB 条带裁剪逐像素 blit（照 box 角落合成的裁剪套路），
  不透明整行直写、半透明逐像素混色，支持容器透明度级联
- 多个控件实例可共享同一组帧数据（只读）

## 注意事项
- **删除必须走 `we_animimg_obj_delete`**（先 `we_anim_stop` 摘链再删对象，
  节点归控件所有，直接 `we_obj_delete` 会在中央动画链上留悬空指针）
- 帧像素数必须等于 w×h，控件不做缩放、不做越界检查
- 帧数据为本机字节序 uint16_t，与 image_res 资源（大端字节流）不同，
  不能直接把 img 资源像素段喂进来
- `set_frames` 总是标脏（同一指针下内容可能被重新生成），不做"值未变跳过"

## 毕业前需优化
- 标脏按整控件包围盒：换帧即整幅重绘，可做帧间 diff / 行哈希只刷差异区
- 仅支持裸 RGB565：毕业版应支持 image_res 资源头帧（复用 img 渲染内核，
  含 indexed QOI 压缩帧）以及 ARGB 透明帧
- 无播放模式选项：可加单次播放（播完停末帧 + 完成回调）、往返 ping-pong、
  倒放
- interval 抖动补偿目前逐帧追赶（连续跨多帧只重绘一次），低帧率下可考虑
  跳帧策略可配置

## 对应 demo
- `Demo/preview/demo_animimg.c`（DEMO_ID 111）：init 时纯整数代码生成
  4 帧 48×48 图案（渐变背景 + 四角轮转亮点，static 静态帧缓冲），
  左侧实例 120ms 持续循环，右侧实例 60ms 且每 2s 自动 start/stop 切换
  （状态 label 同步显示 RUN/STOP）
