/**
 * @file  we_widget_animimg.c
 * @brief 帧动画控件（animimg）实现 —— preview 孵化区
 *
 * 帧推进：单个 we_anim_t 节点挂中央动画引擎（不占 GUI timer 槽），
 * step_cb 累计 elapsed_ms、跨过 interval_ms 才换帧、帧号变化才标脏，
 * 停播时节点摘链（空闲零开销）。
 *
 * 渲染：控件自写的带 PFB 条带裁剪逐像素 blit（cx0/cy0/cx1/cy1 与
 * pfb_area/pfb_y_start/pfb_y_end 求交 + row 指针 stride 步进，照 box
 * 角落合成函数的标准套路）；帧数据为本机字节序 uint16_t RGB565 裸数组，
 * we_color_from_rgb565 转设备色后 we_store_color 直写，半透明走混色。
 */

#include "we_widget_animimg.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _animimg_draw_cb(void *ptr);
static uint8_t _animimg_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void    _animimg_set_pos_cb(void *ptr, int16_t x, int16_t y);

static const we_class_t _animimg_class = {
    .draw_cb    = _animimg_draw_cb,
    .event_cb   = _animimg_event_cb,
    .set_pos_cb = _animimg_set_pos_cb
};

/**
 * @brief 控件绘制回调：当前帧的 PFB 裁剪逐像素 blit。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _animimg_draw_cb(void *ptr)
{
    we_animimg_obj_t *o = (we_animimg_obj_t *)ptr;
    we_lcd_t *lcd;
    const uint16_t *frame;
    const uint16_t *src_line;
    colour_t *dst_line;
    int16_t cx0, cy0, cx1, cy1;
    uint16_t stride;
    uint8_t eff_op;
    int16_t px, py;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL || o->base.w <= 0 || o->base.h <= 0)
        return;
    if (o->frames == NULL || o->frame_cnt == 0U || o->cur >= o->frame_cnt)
        return;
    frame = o->frames[o->cur];
    if (frame == NULL)
        return;
    eff_op = we_opa_apply(lcd, o->opacity); /* 容器透明度级联，入口消费一次 */
    if (eff_op == 0U)
        return;

    /* 裁剪到当前 PFB 切片（照 box 角落合成的标准套路） */
    cx0 = o->base.x;
    cy0 = o->base.y;
    cx1 = (int16_t)(o->base.x + o->base.w - 1);
    cy1 = (int16_t)(o->base.y + o->base.h - 1);
    if (cx0 < (int16_t)lcd->pfb_area.x0) cx0 = (int16_t)lcd->pfb_area.x0;
    if (cy0 < (int16_t)lcd->pfb_y_start) cy0 = (int16_t)lcd->pfb_y_start;
    if (cx1 > (int16_t)lcd->pfb_area.x1) cx1 = (int16_t)lcd->pfb_area.x1;
    if (cy1 > (int16_t)lcd->pfb_y_end) cy1 = (int16_t)lcd->pfb_y_end;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    stride   = lcd->pfb_width;
    src_line = frame + (uint32_t)(cy0 - o->base.y) * (uint32_t)o->base.w
                     + (uint32_t)(cx0 - o->base.x);
    dst_line = lcd->pfb_gram
             + (uint32_t)(cy0 - (int16_t)lcd->pfb_y_start) * stride
             + (uint32_t)(cx0 - (int16_t)lcd->pfb_area.x0);

    /* opacity 整次绘制内是常量，循环外分类：不透明直写 / 半透明混色 */
    if (eff_op >= 250U)
    {
        for (py = cy0; py <= cy1; py++)
        {
            const uint16_t *s = src_line;
            colour_t *p = dst_line;

            for (px = cx0; px <= cx1; px++, p++, s++)
            {
                we_store_color(p, we_color_from_rgb565(*s));
            }
            src_line += o->base.w;
            dst_line += stride;
        }
    }
    else
    {
        for (py = cy0; py <= cy1; py++)
        {
            const uint16_t *s = src_line;
            colour_t *p = dst_line;

            for (px = cx0; px <= cx1; px++, p++, s++)
            {
                we_store_blended_color(p, we_color_from_rgb565(*s), eff_op);
            }
            src_line += o->base.w;
            dst_line += stride;
        }
    }
}

/**
 * @brief 控件事件回调：装饰性穿透（不消费输入）。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒返回 0，事件穿透给背后控件。
 */
static uint8_t _animimg_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

/**
 * @brief 容器/框架重定位回调。
 * @param ptr 回调透传对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
static void _animimg_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_animimg_set_pos((we_animimg_obj_t *)ptr, x, y);
}

/**
 * @brief 中央动画引擎推进回调：累计毫秒，跨过间隔才换帧、帧变才标脏。
 * @param owner 回调透传控件实例指针。
 * @param elapsed_ms 本调度周期经过的毫秒数。
 * @return 无。
 */
static void _animimg_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_animimg_obj_t *o = (we_animimg_obj_t *)owner;
    uint8_t advanced = 0U;

    if (o == NULL || elapsed_ms == 0U)
        return;
    if (!o->playing || o->frames == NULL || o->frame_cnt == 0U)
        return;

    o->acc_ms = (uint16_t)(o->acc_ms + elapsed_ms);
    while (o->acc_ms >= o->interval_ms)
    {
        o->acc_ms = (uint16_t)(o->acc_ms - o->interval_ms);
        o->cur = (uint8_t)((o->cur + 1U) % o->frame_cnt);
        advanced = 1U;
    }

    if (advanced)
    {
        we_obj_invalidate((we_obj_t *)o);
    }
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_animimg_obj_init(we_animimg_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (obj == NULL || lcd == NULL || w <= 0 || h <= 0)
        return;

    obj->base.lcd     = lcd;
    obj->base.class_p = &_animimg_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = w;
    obj->base.h       = h;

    obj->frames      = NULL;
    obj->frame_cnt   = 0U;
    obj->cur         = 0U;
    obj->playing     = 0U;
    obj->opacity     = 255U;
    obj->interval_ms = WE_ANIMIMG_DEF_INTERVAL_MS;
    obj->acc_ms      = 0U;

    obj->anim.next    = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner   = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_animimg_set_frames(we_animimg_obj_t *obj, const uint16_t *const *frames,
                           uint8_t frame_cnt, uint16_t interval_ms)
{
    if (obj == NULL)
        return;

    if (frames == NULL)
        frame_cnt = 0U;
    if (interval_ms == 0U)
        interval_ms = WE_ANIMIMG_DEF_INTERVAL_MS;

    obj->frames      = frames;
    obj->frame_cnt   = frame_cnt;
    obj->cur         = 0U;
    obj->acc_ms      = 0U;
    obj->interval_ms = interval_ms;

    /* 同一指针下帧内容可能已被调用方重新生成，这里总是标脏一次 */
    we_obj_invalidate((we_obj_t *)obj);
}

void we_animimg_start(we_animimg_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->frames == NULL || obj->frame_cnt == 0U)
        return;
    if (obj->playing)
        return;

    obj->playing = 1U;
    we_anim_start(obj->base.lcd, &obj->anim, _animimg_step_cb, obj);
}

void we_animimg_stop(we_animimg_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (!obj->playing)
        return;

    obj->playing = 0U;
    we_anim_stop(obj->base.lcd, &obj->anim); /* 摘链后空闲零开销 */
}

void we_animimg_set_interval(we_animimg_obj_t *obj, uint16_t interval_ms)
{
    if (interval_ms == 0U)
        interval_ms = 1U;
    if (obj == NULL || obj->interval_ms == interval_ms)
        return;
    obj->interval_ms = interval_ms;
}

void we_animimg_set_opacity(we_animimg_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_animimg_set_pos(we_animimg_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || (obj->base.x == x && obj->base.y == y))
        return;
    we_obj_invalidate((we_obj_t *)obj); /* 旧位置 */
    obj->base.x = x;
    obj->base.y = y;
    we_obj_invalidate((we_obj_t *)obj); /* 新位置 */
}

void we_animimg_obj_delete(we_animimg_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* 动画节点归控件所有：必须先摘链再删对象（同 pressed_obj 教训） */
    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->playing = 0U;
    we_obj_delete((we_obj_t *)obj);
}
