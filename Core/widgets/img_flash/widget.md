# img_flash

## 功能
外挂 Flash 图片控件，从外部存储流式读取图片资源并显示，当前支持 `IMG_RGB565` 和 `IMG_RGB565_INDEXQOI`。

## 适用场景
- 大量图片放外部 Flash
- 片内空间不足但需要展示图片资源
- 图片资源与内置资源格式保持一致的场景

## 关键 API
- `we_flash_img_obj_init(...)`
- `we_flash_img_obj_set_opacity(...)`
- `we_flash_img_obj_set_pos(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_FLASH_IMG_CHUNK`：流式读取块大小

## 事件与行为
- `img_flash` 本身不处理交互事件
- 主要通过外部 Flash 读取 + 普通重绘完成显示

## 注意事项
- 使用前必须给 `lcd` 绑定 `storage_read_cb`
- 图片头格式与片内图片资源一致
- 当前支持格式：`IMG_RGB565 / IMG_RGB565_INDEXQOI`

## 对应 demo
- `Demo/demo_flash_img.c`
