/**
 * @file  we_widget_imgbtn.c
 * @brief 图片按钮控件（imgbtn）实现
 *
 * 渲染：按当前按压状态选图，走渲染层统一分发 we_img_render_auto（支持格式
 * 与 img 控件一致；PFB 裁剪 + 容器透明度级联都由渲染内核处理）。无按压态图
 * 时自动变暗：带透明通道的图压低整体透明度，不透明图叠半透明黑矩形。
 *
 * 资源格式在 init / set_imgs 一次性校验（img 控件同口径：不支持的格式
 * class_p 置 NULL，控件不画也不可命中），绘制路径因此无需每切片再判格式。
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
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_IMGBTN_USE_KEY == 1)
static uint8_t _imgbtn_key_cb(void *ptr, uint8_t key_evt);
#endif

/* set_pos_cb 留 NULL：几何全部由 base.x/y 推导，内核默认移动逻辑即正确，
 * 且移动焦点对象时的光标环标脏由 we_obj_set_pos 统一负责。 */
static const we_class_t _imgbtn_class = {
    .draw_cb    = _imgbtn_draw_cb,
    .event_cb   = _imgbtn_event_cb,
    .set_pos_cb = NULL,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_IMGBTN_USE_KEY == 1)
    .class_flags = WE_CLASS_FLAG_FOCUSABLE, /* 键/焦点走统一 event_cb 通道 */
#endif
};

/**
 * @brief 判断一组图片资源是否可用（常态图必需，两张都须为渲染层支持的格式）。
 * @param img_normal 常态图资源指针。
 * @param img_pressed 按压态图资源指针（允许 NULL）。
 * @return 1 表示可用，0 表示不支持。
 */
static uint8_t _imgbtn_src_ok(const uint8_t *img_normal, const uint8_t *img_pressed)
{
    if (img_normal == NULL || !we_img_format_supported(IMG_DAT_FORMAT(img_normal)))
    {
        return 0U;
    }
    if (img_pressed != NULL && !we_img_format_supported(IMG_DAT_FORMAT(img_pressed)))
    {
        return 0U;
    }
    return 1U;
}

/**
 * @brief 判断图片格式是否自带逐像素透明度（决定按压变暗用哪种做法）。
 * @param fmt 资源信息头里的格式码。
 * @return 1 表示带透明通道，0 表示整块不透明。
 */
static uint8_t _imgbtn_fmt_has_alpha(imgarry_type_t fmt)
{
    switch (fmt)
    {
    case IMG_ARGB8565:
    case IMG_A1:
    case IMG_A2:
    case IMG_A4:
    case IMG_A8:
        return 1U;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    case IMG_ARGB8565_INDEXQOI:
        return 1U;
#endif

#if (WE_CFG_ENABLE_INDEXQOI_MASK == 1)
    case IMG_A8_INDEXQOIMASK:
        return 1U;
#endif

    default:
        return 0U;
    }
}

/**
 * @brief 控件绘制回调：按状态选图渲染，可选叠加按压变暗遮罩。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 资源格式已在 init / set_imgs 校验，这里不再逐切片重复判定。
 */
static void _imgbtn_draw_cb(void *ptr)
{
    we_imgbtn_obj_t *o = (we_imgbtn_obj_t *)ptr;
    we_lcd_t *lcd;
    const uint8_t *img;
    uint8_t opa;
    uint8_t dim_self; /* 需要"自变暗"：按压中且没有专门的按压态图 */
    uint8_t has_alpha;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL)
        return;

    img = (o->pressed && o->img_pressed != NULL) ? o->img_pressed : o->img_normal;
    dim_self = (uint8_t)(o->pressed && o->img_pressed == NULL);
    has_alpha = _imgbtn_fmt_has_alpha(IMG_DAT_FORMAT(img));
    opa = o->opacity;

    /* 按压变暗按格式分两路：
     * - 带透明通道的图（ARGB8565 / A1~A8）：整体透明度乘系数变暗，
     *   避免叠矩形在透明区留下一块方形黑影；
     * - 不透明图（RGB565 等）：绘制后叠一层半透明黑，按压感更实。 */
    if (dim_self && has_alpha)
    {
        opa = we_div255((uint32_t)opa * WE_IMGBTN_DIM_SCALE);
    }

    /* 格式分发由渲染层统一负责；前景色仅 A1/A2/A4/A8 透明位图会用到 */
    we_img_render_auto(lcd, o->base.x, o->base.y, img, o->color, opa);

    if (dim_self && !has_alpha)
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
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_IMGBTN_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _imgbtn_key_cb(ptr, (uint8_t)event);
#endif

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
        /* 资源合法性已由 init / set_imgs 保证（不合法时 class_p 为 NULL，
         * 根本收不到按键事件），这里只按"全透明不接受聚焦"拒绝。 */
        return (o->opacity != 0U) ? 1U : 0U;
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

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_imgbtn_obj_init(we_imgbtn_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y,
                        const uint8_t *img_normal, const uint8_t *img_pressed,
                        we_imgbtn_clicked_cb_t clicked_cb)
{
    WE_ASSERT(obj != NULL && lcd != NULL && img_normal != NULL);
    if (obj == NULL || lcd == NULL || img_normal == NULL)
        return;

    obj->base.lcd = lcd;
    /* 格式不支持时置空类描述符：不画、不命中、不可聚焦（img 控件同口径） */
    obj->base.class_p = _imgbtn_src_ok(img_normal, img_pressed) ? &_imgbtn_class : NULL;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)IMG_DAT_WIDTH(img_normal);   /* 宽高取自资源头 */
    obj->base.h       = (int16_t)IMG_DAT_HEIGHT(img_normal);

    obj->img_normal  = img_normal;
    obj->img_pressed = img_pressed;
    obj->clicked_cb  = clicked_cb;
    obj->color       = RGB888TODEV(255, 255, 255); /* A 位图默认白色前景 */
    obj->pressed     = 0U;
    obj->opacity     = 255U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    if (obj->base.class_p != NULL)
    {
        we_obj_invalidate((we_obj_t *)obj);
    }
}

void we_imgbtn_set_imgs(we_imgbtn_obj_t *obj, const uint8_t *img_normal,
                        const uint8_t *img_pressed)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    /* 校验不通过整体不改，避免出现"换了一半"的状态 */
    if (!_imgbtn_src_ok(img_normal, img_pressed))
        return;
    if (obj->img_normal == img_normal && obj->img_pressed == img_pressed)
        return;

    /* 1. 图变了就得重画：先标旧区域。尺寸不变时这一次同时就是新区域，
     *    尺寸变了则由下面的 set_size 补标新区域与焦点光标环。 */
    we_obj_invalidate((we_obj_t *)obj);

    obj->img_normal   = img_normal;
    obj->img_pressed  = img_pressed;
    obj->base.class_p = &_imgbtn_class; /* init 曾因格式不符置空时在此恢复可用 */

    /* 2. 尺寸随常态图更新：尺寸真变了时，新旧区域与光标环的标脏都归内核；
     *    尺寸没变则直接返回，不产生多余脏矩形。 */
    we_obj_set_size((we_obj_t *)obj,
                    (int16_t)IMG_DAT_WIDTH(img_normal),
                    (int16_t)IMG_DAT_HEIGHT(img_normal));
}
