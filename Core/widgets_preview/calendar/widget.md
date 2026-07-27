# calendar（preview 孵化区）

## 功能
月视图日历：标题行（"YYYY-MM" 年月 + 左右 "<" ">" 翻月热区）+ 星期表头
（Su Mo Tu We Th Fr Sa）+ 6 行 7 列日期网格（当月外留空）。选中日绘制
圆角高亮块，"今日" 绘制圆角描边环（叠加在选中块之上仍可辨识）。
内置纯整数万年历（1583+ 公历）：闰年四年一闰百年不闰四百年再闰，
当月 1 日星期几用基姆拉尔森（Kim-Larsen）公式求出后换算为周日起始。

## 适用场景
- 日期选择（闹钟 / 日程 / 打卡类界面）
- 只读月历展示（配合 set_today 高亮当天）
- 需要程序驱动翻月轮播的展示页

## 关键 API
- `we_calendar_obj_init(obj, lcd, x, y, w, h, font)`
- `we_calendar_set_month(obj, year, month)`
  —— 年份钳制 [1583, 9999]、月份钳制 [1, 12]；选中日自动钳到新月天数
- `we_calendar_set_selected(obj, day)` —— 越界钳制；程序设置不触发回调
- `we_calendar_get_selected(obj, &y, &m, &d)` —— 传出参数均可 NULL
- `we_calendar_prev_month(obj)` / `we_calendar_next_month(obj)`
  —— 跨年自动进退（12→1 进年 / 1→12 退年），选中日钳到新月天数
- `we_calendar_set_changed_cb(obj, cb)`
  —— `void (*cb)(void *cal, uint16_t y, uint8_t m, uint8_t d)`，
  仅用户交互（点日期格 / 点翻月热区 / 快速横滑）触发
- `we_calendar_set_colors(obj, title, weekday, day_text, sel_bg, today_ring)`
- `we_calendar_set_today(obj, y, m, d)` —— 今日环标记，传 0 关闭
- `we_calendar_set_opacity(obj, opacity)`
- `we_calendar_obj_delete(obj)` —— 无动画节点，直接摘链

## 可调宏
包含头文件前可覆盖：
- `WE_CALENDAR_MIN_YEAR`（下限，默认 1583）
- `WE_CALENDAR_MAX_YEAR`（上限，默认 9999）
- `WE_CALENDAR_RING_W`（今日环描边厚度，默认 2px）
- `WE_CALENDAR_PRESS_OPA`（按压反馈块透明度，默认 90）

## 布局与渲染
- 列宽 = w/7，行高 = h/8（1 标题 + 1 表头 + 6 日期行），
  除法余数像素均分为四周留白
- 高亮块 / 按压块 / 今日环共用同一几何：格内缩 2px、
  圆角 = min(块宽, 块高)/3
- 今日环 = 外圆角轮廓 alpha − 内缩 `WE_CALENDAR_RING_W` 的内轮廓 alpha，
  逐像素 `we_mask_round_rect_alpha` 合成，仅覆盖单个日期格，纯整数
- 标题 "YYYY-MM" 由内部 8 字节缓存整数拆位格式化（无 sprintf 依赖）
- 零 malloc、渲染内环零浮点

## 事件与行为
- PRESSED 命中热区（翻月箭头 2 列宽 / 有效日期格）显示按压反馈；
  控件矩形内一律消费（保证 SWIPE 能派发到本控件）
- STAY 拖出原热区撤销按压反馈，本次触摸不再产生点击
- CLICKED 释放点复核与按压热区一致才提交：
  - 翻月热区 → prev/next + 回调
  - 日期格 → 选中该日 + 回调（点已选中日不回调）
- SWIPE_LEFT = 翻下月、SWIPE_RIGHT = 翻上月（快速横滑，均触发回调）
- 程序 set 接口（set_month / set_selected / prev / next_month）不触发回调
- 到达年限边界（1583-01 / 9999-12）后翻月静默不动作

## 万年历算法自测
- 2026-07-01 → Kim-Larsen W=2（0=周一制）→ 周三 ✓（表头列 We）
- 2024-02 → 2024 闰年 → 29 天 ✓
- 2000-02 → 400 整除闰年 → 29 天；1900-02 → 百年不闰 → 28 天

## 注意事项
- 星期表头固定周日起始（Su 在第 0 列），暂无周一起始配置
- 当月外日期留空（不显示上/下月灰色补位日期）
- "今日" 概念由调用方通过 `we_calendar_set_today` 注入
  （控件无 RTC 依赖），显示年月切走后环自动消失
- 无动画节点，删除前不需要 `we_anim_stop`

## 毕业前需优化
- [ ] 标脏目前按整控件包围盒；应改为按变化格（旧选中格 + 新选中格 /
  按压格）精细标脏，翻月才整控件标脏
- [ ] 当月外日期留空；可补上/下月灰色补位日期（点击直接跳月选日）
- [ ] 周起始日不可配置（周一起始需求常见）；表头文本不可本地化
- [ ] 翻月无过渡动画；可经中央动画引擎加水平滑动过渡
- [ ] 今日环逐像素双 mask 调用可优化为 quarter-ring 单遍子采样
  （对齐 box 边框环的做法）
- [ ] 小尺寸（格宽 < 字宽）
  下未做降级处理
- [ ] 长按翻月热区无连发（stepper 的 STAY 连发套路可平移过来）

## 对应 demo
- `Demo/preview/demo_calendar.c`（DEMO_ID 122：2026-07-20 初始 +
  回调回显 SEL 日期 + 定时自动翻月循环演示）
