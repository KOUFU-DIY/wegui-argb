# table（preview 孵化区）

## 功能
简易表格：表头行（cells 第 0 行，带底色）固定顶部不随滚动，数据行区
可拖拽垂直滚动（硬夹紧、无惯性无回弹）；单元格文本左对齐，超宽文本
在列边界处硬截断；1px 低透明度网格线（列分隔竖线 + 行分隔横线 +
表头下沿线）；可选斑马纹隔行着色；内容超高时右缘常显细滚动条。

## 适用场景
- 传感器参数表 / 通道状态表等"名称-数值-单位"型数据面板
- 行数超出可见区、需要滚动浏览的只读数据列表
- 配合定时器原地改写数值缓冲，做低成本实时刷新表

## 关键 API
- `we_table_obj_init(obj, lcd, x, y, w, h, col_cnt, font)`
  —— 列数钳制 [1, `WE_TABLE_COL_MAX`]（默认上限 6）
- `we_table_set_cells(obj, cells, row_cnt)`
  —— 行优先一维 `const char *const *` 数组（row_cnt × col_cnt，
  含表头行 = 第 0 行），**调用方持有**，控件只存指针绝不拷贝；
  元素可为 NULL（该格留空）
- `we_table_set_col_weights(obj, weights)`
  —— 按权重占比分列宽（拷入定长数组，元素 0 按 1 处理），NULL = 等分
- `we_table_set_row_h(obj, row_h)`
  —— 表头与数据行同高（默认 = 字体行高 + 2 × `WE_TABLE_ROW_PAD`）
- `we_table_set_colors(obj, head_bg, head_text, cell_text, grid, zebra)`
- `we_table_set_zebra(obj, 0/1)` —— 斑马纹开关（默认开）
- `we_table_refresh(obj)`
  —— 调用方原地改写单元格缓冲内容后手动触发重绘
  （cells 归调用方所有，控件感知不到内容变化）
- `we_table_set_opacity(obj, opacity)`
- `we_table_obj_delete(obj)` —— 无动画节点，直接摘链

## 可调宏
包含头文件前可覆盖：
- `WE_TABLE_COL_MAX`（最大列数，默认 6）
- `WE_TABLE_ROW_PAD`（行内上下边距，默认 6）
- `WE_TABLE_CELL_PAD_X`（单元格文本左内边距，默认 6）
- `WE_TABLE_DRAG_THRESHOLD`（拖拽判定阈值，默认 6px）
- `WE_TABLE_GRID_OPA`（网格线透明度，默认 56）
- `WE_TABLE_SB_WIDTH` / `WE_TABLE_SB_MARGIN` / `WE_TABLE_SB_OPA`（滚动条）

## 渲染说明
- 列边界 `col_edge[]` 由权重前缀和整数比例推得并缓存
  （末列自动吸收除法余数像素），set_col_weights 时重算
- 表头文本与数据单元格文本均按"列边界 ∩ 所在行区"做 PFB 窗口收窄
  （save/restore `pfb_area`/`pfb_y_start`/`pfb_y_end`/`pfb_gram` 套路，
  每次从进入 draw 时保存的原始现场推导，非嵌套）；数据区收窄总是
  从表头行以下开始，半露行/超宽文本都不会渗出边界
- 数据单元格按"每列收窄一次、列内逐可见行绘制"组织，
  收窄切换次数 = 列数 + 1，与行数无关
- 斑马纹取数据行绝对序号奇数行，滚动时条纹跟内容走不闪变
- 零 malloc、渲染内环零浮点

## 事件与行为
- PRESSED 记录起点 Y 与起始滚动值；STAY 位移超阈值（6px）进入拖拽，
  内容跟手滚动（scroll = press_scroll − dy）
- 滚动范围硬夹紧 [0, 内容高 − 数据区高]，无回弹、无惯性
- 表头区按下同样可拖拽滚动（不单独区分热区）
- 所有事件返回 1（消费，不穿透）；无单元格点击回调

## 注意事项
- cells 数组与其中每个字符串的生命周期都须覆盖控件生命周期；
  实时刷新的单元格请指向调用方 static 缓冲，改写后调 `we_table_refresh`
- 更换数组指针或行数用 `we_table_set_cells`（会复位滚动）；
  仅内容变化用 `we_table_refresh`（保持滚动位置）
- 滚动条滑块颜色暂无 setter（默认浅灰，直接改 `sb_color` 字段可调）
- 字体经 init 传入（init 时读取行高推导默认行高）
- 无动画节点，删除前不需要 `we_anim_stop`

## 毕业前需优化
- [ ] 标脏目前按整控件包围盒；滚动应改为数据区条带标脏，
  单元格更新应支持按行/按格局部标脏（refresh 可带行号参数）
- [ ] 拖拽无惯性无回弹；可对齐 list 的松手惯性（中央动画引擎）
  与 scroll_panel 的橡皮筋回弹
- [ ] 超宽文本为硬截断；可加省略号（"…"）截断或列内跑马灯
- [ ] 无单元格/行点击回调；选中行高亮亦未实现
- [ ] 列宽只支持权重比例；固定像素列宽、自动按内容测宽未实现
- [ ] 文本只支持左对齐；数值列常用右对齐需补充（per-column align）
- [ ] 表头单行固定；多级表头、冻结首列均未实现
- [ ] 滚动条常显；可加 dropdown 同款空闲淡出动画
- [ ] 字体不可配置（可加 set_font；需同步重推导默认行高）

## 对应 demo
- `Demo/preview/demo_table.c`（DEMO_ID 123：4 列 × 14 行传感器参数表 +
  两行 VAL 每 400ms 实时刷新 + 拖拽滚动）
