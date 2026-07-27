# btnmatrix（preview）

## 功能
按键矩阵：一个行优先字符串数组渲染整面等分网格按键，单控件承载整块键盘/快捷键面板。`NULL` 或 `""` 元素为空位（不绘制、不响应）。

## 适用场景
- 数字键盘 / 拨号盘 / 计算器键面
- 快捷按钮面板（一次布置一批按键，省下逐个 btn 对象的 RAM 和布局代码）

## 关键 API
- `we_btnmatrix_obj_init(obj, lcd, x, y, w, h, labels, rows, cols, font)`
- `we_btnmatrix_set_clicked_cb(obj, cb)` —— `void cb(void *bm, uint8_t key_idx, const char *label)`
- `we_btnmatrix_set_colors(obj, 普通底色, 按压底色, 文字色)`
- `we_btnmatrix_set_opacity(obj, opacity)`
- `we_btnmatrix_obj_delete(obj)`

## 可调宏
包含头文件前可覆盖：
- `WE_BTNMATRIX_GAP`（格间距，默认 5px）
- `WE_BTNMATRIX_RADIUS`（按键圆角，默认 8px，按格宽高自动钳制）

## 事件与行为
- `PRESSED` 命中非空格：记录格序号并切按压底色；命中空位/间距带返回 0 穿透
- `STAY` 拖出原格：取消按压态，本次触摸不再产生点击
- `CLICKED` 在原格内释放：触发 `clicked_cb(bm, key_idx, label)`，`key_idx` 为行优先序号
- 无动画节点，删除无需 `we_anim_stop`

## 注意事项
- `labels` 数组与文本全部由调用方持有（控件只存 `const` 指针，零拷贝）
- `rows*cols` 必须 <= 255（回调 `key_idx` 为 `uint8_t`）
- 格宽 = `(w - (cols-1)*gap) / cols` 整除，余数像素堆在矩阵右/下边缘

## 毕业前需优化
- 文字未按格子裁剪：过长键名会溢出按键区域（参考 btn 的 `_btn_draw_text_clipped` 收窄 PFB 做裁剪）
- 重绘遍历全部格子，依赖 PFB 原语裁剪丢弃格外写入；应按脏区求交只画相关格子
- 网格余数像素未摊分，最右列/最下行与边缘的留白比其它格间距略大
- 无 disabled 态、无每键独立配色/字体

## 对应 demo
- `Demo/preview/demo_btnmatrix.c`（DEMO_ID 104：4x3 数字键盘 + 顶部回显）
