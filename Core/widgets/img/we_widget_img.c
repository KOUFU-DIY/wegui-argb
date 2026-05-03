#include "we_widget_img.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 图片控件绘制回调
 *
 * 这里把“图片类型分发”收口到 img 控件内部。
 *
 * 实现思路：
 * 1. 控件层只关心当前资源属于哪一种图片格式
 * 2. 根据 dat_type 选择对应的渲染内核
 * 3. we_gui_driver.c 不再承担“图片控件该怎么分发”的职责
 *
 * 这样后面查看图片控件时，只看 we_widget_img.c 就能知道：
 * - 当前支持哪些图片类型
 * - 当前工程是否裁掉了某些格式
 * - 控件最终会落到哪条渲染路径
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _img_draw_cb(void *ptr)
{
    we_img_obj_t *obj = (we_img_obj_t *)ptr;

    if (obj == NULL || obj->opacity == 0 || obj->img_src == NULL || obj->base.lcd == NULL)
    {
        return;
    }

    switch (IMG_DAT_FORMAT(obj->img_src))
    {
    case IMG_RGB565:
        we_img_render_rgb565(obj->base.lcd, obj->base.x, obj->base.y, obj->img_src, obj->opacity);
        break;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    case IMG_RGB565_INDEXQOI:
        we_img_render_indexed_qoi_rgb565(obj->base.lcd, obj->base.x, obj->base.y, obj->img_src, obj->opacity);
        break;

    case IMG_ARGB8565_INDEXQOI:
        we_img_render_indexed_qoi_argb8565(obj->base.lcd, obj->base.x, obj->base.y, obj->img_src, obj->opacity);
        break;
#endif

    default:
        break;
    }
}

/* --------------------------------------------------------------------------
 * 判断当前图片格式是否受支持
 *
 * 这一步放在控件层的目的，是在初始化阶段就把不支持的资源挡住，
 * 避免后面 draw_cb 每一帧还要继续判断这个对象能不能画。
 * -------------------------------------------------------------------------- */

/**
 * @brief 判断图片数据格式是否为当前编译配置支持。
 * @param dat_type 图片资源的数据格式枚举值。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _img_type_supported(imgarry_type_t dat_type)
{
    switch (dat_type)
    {
    case IMG_RGB565:
        return 1U;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    case IMG_RGB565_INDEXQOI:
        return 1U;

    case IMG_ARGB8565_INDEXQOI:
        return 1U;
#endif

    default:
        return 0U;
    }
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param img 图像资源数据指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_img_obj_init(we_img_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y, const uint8_t *img, uint8_t opacity)
{
    static const we_class_t _img_class = {.draw_cb = _img_draw_cb, .event_cb = NULL};

    if (obj == NULL || lcd == NULL || img == NULL)
    {
        return;
    }

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = IMG_DAT_WIDTH(img);
    obj->base.h = IMG_DAT_HEIGHT(img);
    obj->base.class_p = _img_type_supported(IMG_DAT_FORMAT(img)) ? &_img_class : NULL;
    obj->base.next = NULL;

    obj->img_src = img;
    obj->opacity = opacity;

    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = lcd->obj_list_head;
        while (tail->next != NULL)
        {
            tail = tail->next;
        }
        tail->next = (we_obj_t *)obj;
    }

    if (opacity > 0 && obj->base.class_p != NULL)
    {
we_obj_invalidate((we_obj_t *)obj);
    }
}
