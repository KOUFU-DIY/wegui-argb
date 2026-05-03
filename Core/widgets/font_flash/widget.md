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

## 事件与行为
- `font_flash` 本身不处理交互事件
- 多个对象可以共享同一个 `we_flash_font_face_t`

## 注意事项
- 使用前必须给 `lcd` 绑定 `storage_read_cb`
- `face` 只绑定元信息、外挂地址和 scratch buffer，本身不拥有外部资源
- 运行时读取的是外挂 blob，不是片内整字库镜像
- 当前推荐走零 malloc 路径

## 对应 demo
- `Demo/demo_flash_font.c`
