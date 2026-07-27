# roller

## 功能
滚轮选值器：选项沿垂直方向排布，中心行为选中行（圆角高亮条 + 主色文字），
上下行按距中心的行距做透明度递减（255/160/90/55/40，档间按像素线性插值）。
拖拽跟手滚动；慢速松手就近吸附，快速甩动松手惯性继承拖拽速度、滑过多行
再减速吸附（中央动画引擎，拉力 + 阻尼整数缓动）；轻点上/下可见行直达该行。

## 适用场景
- 时间/日期选择（小时、分钟、年月日）
- 数值/档位选取（音量、温度、模式）
- 任何"从固定候选集中选一项"的场合

## 关键 API
- `we_roller_obj_init(obj, lcd, x, y, w, visible_rows, font)`
  —— visible_rows 奇数（0 = 默认 5，偶数自动 +1）；控件高度 =
  visible_rows × 行高，行高 = 字体行高 + 2 × `WE_ROLLER_ROW_PAD`
- `we_roller_set_options(obj, options, count)`
  —— 数据驱动：`const char *const *` 字符串数组由**调用方持有**，
  控件只存指针绝不拷贝；绑定时刷新文字测量缓存
- `we_roller_set_selected(obj, idx)` —— 立即定位（无动画，不触发回调）
- `we_roller_get_selected(obj)` —— 当前选中索引（无选项返回 -1）
- `we_roller_set_changed_cb(obj, cb)` —— 吸附完成且索引变化时回调新 index
- `we_roller_set_font(obj, font)` —— 换字体：标脏旧区 → 重推导行高/控件高 →
  scroll 按行高比例换算保持选中行 → 刷新测量缓存 → 标脏新区
- `we_roller_obj_delete(obj)` —— 内部先 `we_anim_stop` 再 `we_obj_delete`

## 可调宏
包含头文件前可覆盖：
- `WE_ROLLER_DEF_VISIBLE_ROWS`（默认 5）
- `WE_ROLLER_ROW_PAD`（行内上下边距，默认 6）
- `WE_ROLLER_PANEL_RADIUS` / `WE_ROLLER_BAR_RADIUS` / `WE_ROLLER_BAR_INSET`
- `WE_ROLLER_DRAG_THRESHOLD`（拖拽判定阈值，默认 3px）
- `WE_ROLLER_SNAP_PULL_DIV` / `WE_ROLLER_SNAP_DAMP_NUM` /
  `WE_ROLLER_SNAP_DAMP_DEN` / `WE_ROLLER_SNAP_MAX_STEP`
  （吸附缓动参数，默认对齐 slideshow COMPLEX 模式）
- `WE_ROLLER_FLING_MIN_V`（惯性触发速度阈值 px/16ms 周期，默认 4；
  低于阈值松手就近吸附）
- `WE_ROLLER_FLING_PROJ_NUM` / `WE_ROLLER_FLING_PROJ_DEN`
  （惯性落点外推系数，默认 6/1 = 速度按 6/7 几何衰减的级数和）
- `WE_ROLLER_WCACHE_SIZE`（行宽缓存槽数，2 的幂，默认 16；
  RAM 开销 4 字节/槽）
- `WE_ROLLER_BBOX_SCAN_MAX`（y bbox 常量化扫描的选项数上限，默认 32）
- `WE_ROLLER_DIRTY_PAD`（滚动标脏文本列带左右安全余量，默认 4px）

## 事件与行为
- PRESSED 打断进行中的吸附并记录起点；STAY 位移超阈值进入跟手拖拽
  （滚动范围硬夹紧到首尾项，无回弹），同时按"上一触点 − 当前触点"
  步进测速（scroll_panel 同款约定，一次 STAY ≈ 一个 16ms 调度周期）；
  指尖停驻时速度每周期减半归零，防止"拖快→停稳→松手"仍被甩飞
- RELEASED：|速度| ≥ `WE_ROLLER_FLING_MIN_V` 进入惯性甩动——落点 =
  scroll + v × PROJ_NUM / PROJ_DEN（等比级数和的纯整数近似，见头文件
  推导），夹紧量程后取整到行作为吸附目标，松手速度作为吸附动画初速
  种子（速度曲线在松手瞬间连续）；低于阈值就近吸附最近行
- 轻点（未进入拖拽的 CLICKED）中心行上/下方可见行：吸附动画滚到该行；
  点中心行不动作；拖拽后回落原位的"假点击"不触发
- 快速轻扫（无 STAY 的 SWIPE_UP/DOWN）向对应方向翻 1 行
- 所有事件返回 1（消费，不穿透）
- 滚动为像素级 `scroll_px`（int32 累计），选中第 i 项时 = i × 行高
- 吸附动画走中央动画引擎（单 `we_anim_t` 节点，不占 timer 槽），
  到位自行摘链；changed_cb 仅在吸附完成且索引变化时触发一次

## 渲染说明
- 行文字经 PFB 窗口收窄（save/restore `pfb_area`/`pfb_y_start`/
  `pfb_y_end`/`pfb_gram`，scroll_panel 同款套路）裁剪在控件矩形内
- 距中心半行以内的行按"中心行"绘制（主色 + 满透明度）
- 精细标脏：滚动位移只标"文本列带"（可见窗最大行宽 + 2×DIRTY_PAD、
  水平居中、全控件高，取新旧带宽较大者覆盖移出文字）；列带窄于控件宽
  时面板圆角所在的左右边缘列不进脏区（即排除圆角外区域、只标内容裁剪
  矩形），文本逼近控件宽时退回整件标脏；吸附完成 commit 只标中心行条带
- 文字测量缓存：y 方向 bbox 对同一字体 + 选项集是常量（set_options /
  set_font 时扫描前 `WE_ROLLER_BBOX_SCAN_MAX` 项取纵向并集缓存到结构体，
  各行共用同一垂直基准，顺带消除逐行独立 bbox 的基线抖动）；行宽走
  直接映射缓存（槽位 = 索引 & (SIZE-1)，零除法零 malloc，选项数无上限；
  槽数 ≥ 可见行数 + 2 时可见窗内零互逐，LRU 的维护开销在此顺序访问
  模式下买不到命中率，故不采用），绘制内环与标脏路径零
  `we_get_text_width` / `we_get_text_bbox` 调用，每行至多重测一次/进窗
- 零 malloc、渲染内环零浮点

## 注意事项
- 删除前必须走 `we_roller_obj_delete`（动画节点归控件所有，内核无法代摘）
- 选项数组生命周期须覆盖控件生命周期（推荐 `static const char *const []`）
- 字体经 init 传入，可用 `we_roller_set_font` 更换
  （控件高度随行高变化，注意周边布局预留）
- 就地修改选项字符串内容不会自动刷新行宽缓存，需重新 `set_options`
  （先绑 NULL 再绑回可强制刷新）

## 已完成的毕业优化
- [x] 惯性甩动：STAY 步进测速 + 松手速度外推落点取整到行 +
  速度作吸附动画初速种子（快速甩动滑过多行再减速吸附）
- [x] 点击直达：轻点上/下方可见行经吸附动画滚到该行，点中心行不动作
- [x] 精细标脏：滚动位移标文本列带（排除面板圆角外区域），
  吸附 commit 只标中心行条带
- [x] 文字测量缓存：y bbox 字体常量化 + 行宽直接映射缓存，
  绘制内环零测量调用
- [x] 字体参数化：`we_roller_set_font`（旧区标脏 → 几何重推导 →
  scroll 行高比例换算保持选中行 → 新区标脏）

## 毕业前需优化（保留项）
- [ ] 循环滚动 `we_roller_set_loop(0/1)`（首尾相接、取模索引绘制与吸附）：
  本轮未做。原因：循环模式与现有"scroll_px 硬夹紧 [0, max]"量程模型冲突
  面大——夹紧、惯性落点、吸附取整、SWIPE 翻行、set_selected 全部要改成
  取模语义，且精细标脏的可见窗计算需按环形索引重写；作为可选加分项
  留给毕业后迭代，避免本轮把已验证的量程路径改出回归
- [ ] 越界橡皮筋回弹（当前硬夹紧）：非本轮清单必做项；若做需引入
  过冲区间 + 回弹动画（scroll_panel REBOUND 同款），与惯性落点夹紧的
  交互（甩到边界时的过冲手感）需要一并调参

## 对应 demo
- `Demo/demo_roller.c`（DEMO_ID 26：双 roller 选时间 HH:MM，
  底部提示行 "Fling to spin, tap to jump"）
