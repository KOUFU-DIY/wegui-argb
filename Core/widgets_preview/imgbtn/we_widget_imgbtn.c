/**
 * @file  we_widget_imgbtn.c
 * @brief 图片按钮控件（imgbtn）实现 —— preview 孵化区
 *
 * 渲染：按当前按压状态选图，走 we_img_render_rgb565（PFB 裁剪 + 容器透明度
 * 级联都由渲染内核处理）；无按压态图时在常态图上叠半透明黑矩形变暗。
 *
 * 交互沿用 btn 的事件状态机语义：PRESSED 换按压视觉、RELEASED 恢复、
 * CLICKED（按下并在框内释放，由内核 pressed_obj == target 保证）触发回调。
 */

#include "we_widget_imgbtn.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _imgbtn_draw_cb(void *ptr);
static uint8_t _imgbtn_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void    _imgbtn_set_pos_cb(void *ptr, int16_t x, int16_t y);
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_IMGBTN_USE_KEY == 1)
static uint8_t _imgbtn_key_cb(void *ptr, uint8_t key_evt);
#endif

static const we_class_t _imgbtn_class = {
    .draw_cb    = _imgbtn_draw_cb,
    .event_cb   = _imgbtn_event_cb,
    .set_pos_cb = _imgbtn_set_pos_cb,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_IMGBTN_USE_KEY == 1)
    .key_cb     = _imgbtn_key_cb,
#endif
};

/**
 * @brief 控件绘制回调：按状态选图渲染，可选叠加按压变暗遮罩。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _imgbtn_draw_cb(void *ptr)
{
    we_imgbtn_obj_t *o = (we_imgbtn_obj_t *)ptr;
    we_lcd_t *lcd;
    const uint8_t *img;

    if (o == NULL || o->opacity == 0U || o->img_normal == NULL)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL)
        return;

    img = (o->pressed && o->img_pressed != NULL) ? o->img_pressed : o->img_normal;

    /* preview 版只支持 RGB565 未压缩资源，其他格式静默跳过 */
    if (IMG_DAT_FORMAT(img) == IMG_RGB565)
    {
        we_img_render_rgb565(lcd, o->base.x, o->base.y, img, o->opacity);
    }

    /* 无按压态图：按压时整块叠半透明黑变暗（遮罩透明度再与整体 opacity 相乘） */
    if (o->pressed && o->img_pressed == NULL)
    {
        static const colour_t dim = RGB888_CONST(0, 0, 0);
        we_fill_rect(lcd, o->base.x, o->base.y,
                     (uint16_t)o->base.w, (uint16_t)o->base.h,
                     dim, we_div255((uint32_t)o->opacity * WE_IMGBTN_DIM_OPA));
    }
}

/**
 * @brief 控件事件回调：按压/释放切视觉，框内释放触发点击回调。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒返回 1（交互控件消费事件，容器据此锁定并转发后续事件）。
 */
static uint8_t _imgbtn_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_imgbtn_obj_t *o = (we_imgbtn_obj_t *)ptr;

    (void)data;
    if (o == NULL)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        if (!o->pressed)
        {
            o->pressed = 1U;
            we_obj_invalidate((we_obj_t *)o);
        }
        break;

    case WE_EVENT_RELEASED:
        if (o->pressed)
        {
            o->pressed = 0U;
            we_obj_invalidate((we_obj_t *)o);
        }
        break;

    case WE_EVENT_CLICKED:
        /* 内核只在"按下并于原控件框内释放"时派发 CLICKED，无需重复判框 */
        if (o->clicked_cb != NULL)
        {
            o->clicked_cb(o);
        }
        break;

    default:
        break;
    }
    return 1U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_IMGBTN_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 按下沿进入按压视觉，松开沿回弹并触发点击回调。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 与 btn 同款 OK 双沿；FLASH_END（焦点切走等取消）仅回弹不点击。
 */
static uint8_t _imgbtn_key_cb(void *ptr, uint8_t key_evt)
{
    we_imgbtn_obj_t *o = (we_imgbtn_obj_t *)ptr;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (o->opacity != 0U && o->img_normal != NULL) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK: /* 按下沿：进入按压视觉，等待松开沿 */
        if (!o->pressed)
        {
            o->pressed = 1U;
            we_obj_invalidate((we_obj_t *)o);
        }
        return 1U;
    case WE_KEY_EVT_OK_RELEASE: /* 松开沿：回弹 + 触发点击 */
        if (o->pressed)
        {
            o->pressed = 0U;
            we_obj_invalidate((we_obj_t *)o);
        }
        if (o->clicked_cb != NULL)
            o->clicked_cb(o);
        return 1U;
    case WE_KEY_EVT_FLASH_END: /* 取消（焦点切走等）：仅回弹不点击 */
        if (o->pressed)
        {
            o->pressed = 0U;
            we_obj_invalidate((we_obj_t *)o);
        }
        return 1U;
    default:
        return 0U;
    }
}
#endif

/**
 * @brief 容器/框架重定位回调。
 * @param ptr 回调透传对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
static void _imgbtn_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_imgbtn_set_pos((we_imgbtn_obj_t *)ptr, x, y);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_imgbtn_obj_init(we_imgbtn_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y,
                        const uint8_t *img_normal, const uint8_t *img_pressed)
{
    if (obj == NULL || lcd == NULL || img_normal == NULL)
        return;

    obj->base.lcd     = lcd;
    obj->base.class_p = &_imgbtn_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)IMG_DAT_WIDTH(img_normal);   /* 宽高取自资源头 */
    obj->base.h       = (int16_t)IMG_DAT_HEIGHT(img_normal);

    obj->img_normal  = img_normal;
    obj->img_pressed = img_pressed;
    obj->clicked_cb  = NULL;
    obj->pressed     = 0U;
    obj->opacity     = 255U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_imgbtn_set_clicked_cb(we_imgbtn_obj_t *obj, we_imgbtn_clicked_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->clicked_cb = cb;
}

void we_imgbtn_set_opacity(we_imgbtn_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_imgbtn_set_pos(we_imgbtn_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || (obj->base.x == x && obj->base.y == y))
        return;
    we_obj_invalidate((we_obj_t *)obj); /* 旧位置 */
    obj->base.x = x;
    obj->base.y = y;
    we_obj_invalidate((we_obj_t *)obj); /* 新位置 */
}

void we_imgbtn_obj_delete(we_imgbtn_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
