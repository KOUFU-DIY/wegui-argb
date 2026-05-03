#ifndef WE_WIDGET_IMG_H
#define WE_WIDGET_IMG_H

#include "image_res.h"
#include "we_gui_driver.h"

typedef struct {
    we_obj_t base;
    const uint8_t *img_src;
    uint8_t opacity;
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
 * @brief 释放控件运行时状态并从任务系统注销。
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
 * 图片控件透明度修改
 *
 * 这里改成头文件内联，原因是：
 * 1. 逻辑非常短
 * 2. 当前工程里调用点很少
 * 3. 比单独保留一个外部函数实体更有机会省 ROM
 * -------------------------------------------------------------------------- */

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param new_opacity 新的不透明度值。
 * @return 无。
 */
static inline void we_img_obj_set_opacity(we_img_obj_t *obj, uint8_t new_opacity)
{
    if (obj == NULL || obj->base.lcd == NULL)
    {
        return;
    }
    if (obj->opacity == new_opacity)
    {
        return;
    }

    obj->opacity = new_opacity;
we_obj_invalidate((we_obj_t *)obj);
}

/* --------------------------------------------------------------------------
 * 主动触发图片控件绘制
 *
 * 当前仓库里没有实际调用这个接口，因此也收成头文件内联。
 * 如果后面确实有模块要主动调 draw_cb，这里仍然可以继续保留兼容。
 * -------------------------------------------------------------------------- */

/**
 * @brief 执行该控件的绘制流程。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static inline void we_img_obj_draw(we_img_obj_t *obj)
{
    if (obj == NULL || obj->base.class_p == NULL || obj->base.class_p->draw_cb == NULL)
    {
        return;
    }

    obj->base.class_p->draw_cb(obj);
}

#endif
