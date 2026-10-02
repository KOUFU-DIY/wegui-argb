# font_flash

## 功能
外挂 Flash 字体控件，适配新版 `font2c external` 格式，支持从外部存储按需读取字形并渲染文本。

## 适用场景
- 片内空间不足但需要中文/大字库
- 菜单字体、状态字体、外挂字库显示
- 多个文本对象共享同一套外挂字库描述符

## 关键 API
- `we_flash_font_face_init(...)`
- `we_flash_font_obj_init(...)`
- `we_flash_font_obj_set_text(...)`
- `we_flash_font_obj_set_color(...)`
- `we_flash_font_obj_set_opacity(...)`
- `we_flash_font_obj_set_pos(...)`
- `we_flash_font_obj_delete(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_FLASH_FONT_SCRATCH_MAX`：单字形 scratch 缓冲上限
- `WE_FLASH_FONT_DESC_CACHE`：字形描述缓存条目数（0 = 关闭，默认；打开时须为 2 的幂，每条 20 字节，放在 face 里、同一 face 的文本对象共享）。外挂读取慢而 RAM 有余量时打开

## 事件与行为
- 不处理输入：event_cb 为 NULL，命中测试跳过它，触摸落到背后的控件
- 多个对象可以共享同一个 `we_flash_font_face_t`

## 注意事项
- 端口描述（`we_port_t`）必须提供 `storage_read`
- `face` 只绑定元信息、外挂地址和 scratch buffer，本身不拥有外部资源；`face_init` 失败时 face 被置为不可用，拿它初始化文本控件直接失败
- 运行时读取的是外挂 blob，不是片内整字库镜像
- 当前推荐走零 malloc 路径
- 只保存文字指针；`set_text` 不比较指针，原地改写同一缓冲区后再调用也会重新测量并重绘
- 包围盒宽度 = 各行步进宽度与向右伸出的墨迹取最大；行首字形向左伸出（`x_ofs < 0`）的墨迹会被包围盒裁掉

## 已完成的毕业优化
- `set_opacity` 不再重新测量文字（原先每次调用读两遍全部字形描述）；`set_text` 只测量一遍（原先测完又为标脏再测一遍）。demo 16 同款文字、2 行 PFB：透明度动画每帧外挂读取 360 → 306 次
- 可选字形描述缓存 `WE_FLASH_FONT_DESC_CACHE`：同一场景每帧 306 → 203 次（16 条）/ 150 次（32 条）
- 码点查字形序号与内置字库共用 `we_font_code_to_index`；描述项越界在 face 初始化时一次校验，逐字读取不再重复检查
- 切片能一次读完时不做除法（原先每个字每个切片算一次行数分块）
- 容器透明度级联后 <= 5 时整段跳过，不读外挂
- 包围盒计入向右伸出的墨迹；`set_text` 去掉指针相同即返回的判断（原地改写缓冲区后不刷新）
- face 删掉只写不读的字段（每个 face 省 16 字节）；颜色比较用 `we_colour_eq`（GRAY8 下也能去重）；删除接口改为内联包装

## 对应 demo
- `Demo/demo_flash_font.c`
