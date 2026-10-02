#ifndef WE_WIDGET_IMG_H
#define WE_WIDGET_IMG_H

#include "image_res.h"
#include "wegui.h"

typedef struct {
    we_obj_t base;
    const uint8_t *img_src;
    uint8_t opacity;
    colour_t color; // A1/A2/A4/A8 透明位图的前景色（其余格式忽略），初始化默认白色
} we_img_obj_t;

/**
 * @brief 设置图像控件位置并触发对应重绘。
 * @param img_obj 图像控件对象指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @return 无。
 */
static inline void we_img_obj_set_pos(we_img_obj_t *img_obj, int16_t x, int16_t y)
{
    we_obj_set_pos((we_obj_t *)img_obj, x, y);
}

/**
 * @brief 从显示链表摘除该图片控件并清空对象状态字段（转调 we_obj_delete）；不释放内存，对象由调用者持有。
 * @param img_obj 图像控件对象指针。
 * @return 无。
 */
static inline void we_img_obj_delete(we_img_obj_t *img_obj)
{
    we_obj_delete((we_obj_t *)img_obj);
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param img 图像资源数据指针（image_res 格式）。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_img_obj_init(we_img_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, const uint8_t *img, uint8_t opacity);

/* --------------------------------------------------------------------------
 * 平凡 setter 放在头文件内联：逻辑短、调用点少，比单独保留函数实体更省 ROM
 * -------------------------------------------------------------------------- */

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param new_opacity 新的不透明度值。
 * @return 无。
 */
static inline void we_img_obj_set_opacity(we_img_obj_t *obj, uint8_t new_opacity)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == new_opacity)
        return;

    obj->opacity = new_opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置 A1/A2/A4/A8 透明位图的前景色并按需重绘（其余图片格式忽略该颜色）。
 * @param obj 目标控件对象指针。
 * @param new_color 新的前景色。
 * @return 无。
 */
static inline void we_img_obj_set_color(we_img_obj_t *obj, colour_t new_color)
{
    if (obj == NULL || obj->base.lcd == NULL || we_colour_eq(obj->color, new_color))
        return;

    obj->color = new_color;
    if (obj->opacity > 0)
        we_obj_invalidate((we_obj_t *)obj);
}

#endif
