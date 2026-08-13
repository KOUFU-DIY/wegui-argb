#include "we_widget_img.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 图片控件绘制回调
 *
 * 图片格式分发统一由渲染层的 we_img_render_auto / we_img_format_supported
 * 负责（we_render.c），img 与 imgbtn 等所有图片控件共用同一份分发表：
 * - 支持哪些格式、当前工程裁掉了哪些，只看 we_render.c 一处
 * - 控件层只做"init 阶段拦下不支持的资源、绘制时把图和参数递进去"
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

    /* 格式分发由渲染层统一负责（we_img_render_auto），控件只提供
     * "画哪张图 + 前景色 + 透明度"；前景色仅 A1/A2/A4/A8 会用到。 */
    we_img_render_auto(obj->base.lcd, obj->base.x, obj->base.y, obj->img_src,
                       obj->color, obj->opacity);
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
    static const we_class_t _img_class = {.draw_cb = _img_draw_cb, .event_cb = NULL, .set_pos_cb = NULL};

    if (obj == NULL || lcd == NULL || img == NULL)
    {
        return;
    }

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = IMG_DAT_WIDTH(img);
    obj->base.h = IMG_DAT_HEIGHT(img);
    obj->base.class_p = we_img_format_supported(IMG_DAT_FORMAT(img)) ? &_img_class : NULL;
    obj->base.next = NULL;

    obj->img_src = img;
    obj->opacity = opacity;
    obj->color = RGB888TODEV(255, 255, 255); // A 位图默认白色前景，可用 we_img_obj_set_color 修改

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0 && obj->base.class_p != NULL)
    {
        we_obj_invalidate((we_obj_t *)obj);
    }
}
