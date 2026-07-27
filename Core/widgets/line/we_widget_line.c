/**
 * @file  we_widget_line.c
 * @brief 线段控件（line）实现
 *
 * 圆头线走单遍胶囊填充 we_draw_line_round（线身+圆头一次成形，半透明下不叠色），
 * 平头线用 Wu 抗锯齿线 we_draw_line。端点几何 / 颜色 / 透明度各占一个独立的中央
 * 动画引擎节点，可同时进行；三通道共用一个缓动函数。动画整体可由 WE_LINE_USE_ANIM
 * 编译期关闭（退化为立即生效的兼容桩）。
 */

#include "we_widget_line.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _line_draw_cb(void *ptr);
static uint8_t _line_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void    _line_set_pos_cb(void *ptr, int16_t x, int16_t y);

static const we_class_t _line_class = {
    .draw_cb    = _line_draw_cb,
    .event_cb   = _line_event_cb,
    .set_pos_cb = _line_set_pos_cb
};

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 按端点与线宽重算包围盒（base.x/y/w/h）。
 * @note 外扩 半线宽 + 2px，覆盖 AA 羽化与圆头 cap（半径=线宽/2）。
 */
static void _line_recompute_bbox(we_line_obj_t *o)
{
    int16_t minx = (o->x0 < o->x1) ? o->x0 : o->x1;
    int16_t miny = (o->y0 < o->y1) ? o->y0 : o->y1;
    int16_t maxx = (o->x0 > o->x1) ? o->x0 : o->x1;
    int16_t maxy = (o->y0 > o->y1) ? o->y0 : o->y1;
    int16_t margin = (int16_t)(o->width / 2U + 2U);

    o->base.x = (int16_t)(minx - margin);
    o->base.y = (int16_t)(miny - margin);
    o->base.w = (int16_t)((maxx - minx) + 2 * margin);
    o->base.h = (int16_t)((maxy - miny) + 2 * margin);
}

/**
 * @brief 更新端点：先标脏旧包围盒，再改端点+重算盒，再标脏新包围盒。
 */
static void _line_apply_points(we_line_obj_t *o, int16_t x0, int16_t y0,
                               int16_t x1, int16_t y1)
{
    we_obj_invalidate((we_obj_t *)o);   /* 旧包围盒 */
    o->x0 = x0;
    o->y0 = y0;
    o->x1 = x1;
    o->y1 = y1;
    _line_recompute_bbox(o);
    we_obj_invalidate((we_obj_t *)o);   /* 新包围盒 */
}

/**
 * @brief 更新颜色并标脏（包围盒不变）。
 */
static void _line_apply_color(we_line_obj_t *o, colour_t c)
{
    o->color = c;
    we_obj_invalidate((we_obj_t *)o);
}

/* --------------------------------------------------------------------------
 * 绘制 / 事件 / 位置
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：画线身 + 可选圆头。
 */
static void _line_draw_cb(void *ptr)
{
    we_line_obj_t *o = (we_line_obj_t *)ptr;
    we_lcd_t *lcd;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL || o->width == 0U)
        return;

    /* 圆头走单遍胶囊填充（线身+圆头一次成形，半透明下不叠色）；
     * 平头无端帽叠加，直接用现成 Wu 抗锯齿线即可。 */
    if (o->cap == (uint8_t)WE_LINE_CAP_ROUND)
        we_draw_line_round(lcd, o->x0, o->y0, o->x1, o->y1, o->width, o->color, o->opacity);
    else
        we_draw_line(lcd, o->x0, o->y0, o->x1, o->y1, o->width, o->color, o->opacity);
}

/**
 * @brief 控件事件回调：有自定义回调则接管，否则装饰性穿透。
 */
static uint8_t _line_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_line_obj_t *o = (we_line_obj_t *)ptr;

    if (o == NULL)
        return 0U;
    if (o->user_event_cb != NULL)
        return o->user_event_cb(ptr, event, data);

    (void)event;
    (void)data;
    return 0U; /* 默认装饰性，不消费事件，让其穿透给背后控件 */
}

/**
 * @brief 容器/框架重定位回调：把包围盒左上角移到 (x,y)，等价两端同偏移。
 */
static void _line_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_line_obj_t *o = (we_line_obj_t *)ptr;

    if (o == NULL)
        return;
    we_line_move(o, (int16_t)(x - o->base.x), (int16_t)(y - o->base.y));
}

/* --------------------------------------------------------------------------
 * 动画推进（编译期可关）
 * -------------------------------------------------------------------------- */
#if WE_LINE_USE_ANIM

/**
 * @brief 推进一个 Q8 进度并经缓动输出（三通道共用此样板）。
 * @return 1=需要更新（*eased 有效）；0=已就位（已 we_anim_stop 摘链）。
 */
static uint8_t _line_advance(we_line_obj_t *o, we_anim_t *node, uint16_t *t,
                             uint16_t ms, uint16_t elapsed_ms, uint16_t *eased)
{
    uint32_t delta;
    uint16_t e;

    if (*t >= 256U)
    {
        we_anim_stop(o->base.lcd, node); /* 已就位：摘链停表 */
        return 0U;
    }

    if (ms == 0U)
    {
        *t = 256U;
    }
    else
    {
        delta = (uint32_t)elapsed_ms * 256U / (uint32_t)ms;
        if (delta == 0U)
            delta = 1U;
        *t = ((uint32_t)*t + delta >= 256U) ? 256U : (uint16_t)(*t + delta);
    }

    e = o->ease ? o->ease(*t) : *t;
    if (e > 256U)
        e = 256U;
    *eased = e;
    return 1U;
}

/**
 * @brief 推进一步几何动画（端点 from→to）。
 */
static void _line_anim_geo_step(we_line_obj_t *o, uint16_t elapsed_ms)
{
    uint16_t e;

    if (o == NULL || elapsed_ms == 0U)
        return;
    if (!_line_advance(o, &o->anim_geo, &o->geo_t, o->geo_ms, elapsed_ms, &e))
        return;

    _line_apply_points(o,
                       (int16_t)we_lerp(o->g_from[0], o->g_to[0], e),
                       (int16_t)we_lerp(o->g_from[1], o->g_to[1], e),
                       (int16_t)we_lerp(o->g_from[2], o->g_to[2], e),
                       (int16_t)we_lerp(o->g_from[3], o->g_to[3], e));

    if (o->geo_t >= 256U)
        we_anim_stop(o->base.lcd, &o->anim_geo);
}

static void _line_anim_geo_step_cb(void *owner, uint16_t elapsed_ms)
{
    _line_anim_geo_step((we_line_obj_t *)owner, elapsed_ms);
}

/**
 * @brief 推进一步颜色动画（color from→to）。
 */
static void _line_anim_col_step(we_line_obj_t *o, uint16_t elapsed_ms)
{
    uint16_t e;
    uint8_t  a;

    if (o == NULL || elapsed_ms == 0U)
        return;
    if (!_line_advance(o, &o->anim_col, &o->col_t, o->col_ms, elapsed_ms, &e))
        return;

    a = (uint8_t)((uint32_t)e * 255U / 256U); /* 0=起点色，255=终点色 */
    _line_apply_color(o, we_colour_blend(o->c_to, o->c_from, a));

    if (o->col_t >= 256U)
        we_anim_stop(o->base.lcd, &o->anim_col);
}

static void _line_anim_col_step_cb(void *owner, uint16_t elapsed_ms)
{
    _line_anim_col_step((we_line_obj_t *)owner, elapsed_ms);
}

/**
 * @brief 推进一步透明度动画（opacity from→to）。
 */
static void _line_anim_opa_step(we_line_obj_t *o, uint16_t elapsed_ms)
{
    uint16_t e;

    if (o == NULL || elapsed_ms == 0U)
        return;
    if (!_line_advance(o, &o->anim_opa, &o->opa_t, o->opa_ms, elapsed_ms, &e))
        return;

    o->opacity = (uint8_t)we_lerp(o->opa_from, o->opa_to, e);
    we_obj_invalidate((we_obj_t *)o);

    if (o->opa_t >= 256U)
        we_anim_stop(o->base.lcd, &o->anim_opa);
}

static void _line_anim_opa_step_cb(void *owner, uint16_t elapsed_ms)
{
    _line_anim_opa_step((we_line_obj_t *)owner, elapsed_ms);
}

#endif /* WE_LINE_USE_ANIM */

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_line_obj_init(we_line_obj_t *obj, we_lcd_t *lcd,
                      int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd     = lcd;
    obj->base.class_p = &_line_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    obj->x0    = x0;
    obj->y0    = y0;
    obj->x1    = x1;
    obj->y1    = y1;
    obj->width = WE_LINE_DEF_WIDTH;
    obj->cap   = (uint8_t)WE_LINE_CAP_ROUND;
    {
        colour_t c = RGB888_CONST(WE_LINE_DEF_R, WE_LINE_DEF_G, WE_LINE_DEF_B);
        obj->color = c;
    }
    obj->opacity       = 255U;
    obj->user_event_cb = NULL;

#if WE_LINE_USE_ANIM
    obj->anim_geo.next    = NULL;
    obj->anim_geo.step_cb = NULL;
    obj->anim_geo.owner   = NULL;
    obj->anim_col.next    = NULL;
    obj->anim_col.step_cb = NULL;
    obj->anim_col.owner   = NULL;
    obj->anim_opa.next    = NULL;
    obj->anim_opa.step_cb = NULL;
    obj->anim_opa.owner   = NULL;
    obj->ease   = we_ease_in_out_sine; /* 三通道共用缓动 */
    obj->geo_ms = WE_LINE_ANIM_MS;
    obj->col_ms = WE_LINE_ANIM_MS;
    obj->opa_ms = WE_LINE_ANIM_MS;
    obj->geo_t  = 256U; /* 空闲（无动画在跑） */
    obj->col_t  = 256U;
    obj->opa_t  = 256U;
    obj->g_from[0] = x0; obj->g_from[1] = y0; obj->g_from[2] = x1; obj->g_from[3] = y1;
    obj->g_to[0]   = x0; obj->g_to[1]   = y0; obj->g_to[2]   = x1; obj->g_to[3]   = y1;
    obj->c_from = obj->color;
    obj->c_to   = obj->color;
    obj->opa_from = obj->opacity;
    obj->opa_to   = obj->opacity;
#endif

    _line_recompute_bbox(obj);
    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_line_set_points(we_line_obj_t *obj, int16_t x0, int16_t y0,
                        int16_t x1, int16_t y1)
{
    if (obj == NULL)
        return;
    _line_apply_points(obj, x0, y0, x1, y1);
}

void we_line_set_width(we_line_obj_t *obj, uint8_t width)
{
    if (obj == NULL || width == 0U)
        return;
    we_obj_invalidate((we_obj_t *)obj);
    obj->width = width;
    _line_recompute_bbox(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_line_set_cap(we_line_obj_t *obj, we_line_cap_t cap)
{
    if (obj == NULL)
        return;
    obj->cap = (uint8_t)cap;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_line_set_color(we_line_obj_t *obj, colour_t color)
{
    if (obj == NULL)
        return;
    _line_apply_color(obj, color);
}

void we_line_set_opacity(we_line_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_line_move(we_line_obj_t *obj, int16_t dx, int16_t dy)
{
    if (obj == NULL)
        return;
    _line_apply_points(obj, (int16_t)(obj->x0 + dx), (int16_t)(obj->y0 + dy),
                       (int16_t)(obj->x1 + dx), (int16_t)(obj->y1 + dy));
}

void we_line_set_event_cb(we_line_obj_t *obj, we_line_event_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->user_event_cb = cb;
}

#if WE_LINE_USE_ANIM

void we_line_anim_points(we_line_obj_t *obj, int16_t x0, int16_t y0,
                         int16_t x1, int16_t y1, uint16_t dur_ms,
                         we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    obj->g_from[0] = obj->x0; obj->g_from[1] = obj->y0;
    obj->g_from[2] = obj->x1; obj->g_from[3] = obj->y1;
    obj->g_to[0] = x0; obj->g_to[1] = y0; obj->g_to[2] = x1; obj->g_to[3] = y1;
    obj->geo_ms = dur_ms;
    obj->ease   = ease ? ease : we_ease_in_out_sine;
    obj->geo_t  = 0U;
    we_anim_start(obj->base.lcd, &obj->anim_geo, _line_anim_geo_step_cb, obj);
}

void we_line_anim_move(we_line_obj_t *obj, int16_t dx, int16_t dy,
                       uint16_t dur_ms, we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    we_line_anim_points(obj, (int16_t)(obj->x0 + dx), (int16_t)(obj->y0 + dy),
                        (int16_t)(obj->x1 + dx), (int16_t)(obj->y1 + dy),
                        dur_ms, ease);
}

void we_line_anim_color(we_line_obj_t *obj, colour_t target, uint16_t dur_ms,
                        we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    obj->c_from = obj->color;
    obj->c_to   = target;
    obj->col_ms = dur_ms;
    obj->ease   = ease ? ease : we_ease_in_out_sine;
    obj->col_t  = 0U;
    we_anim_start(obj->base.lcd, &obj->anim_col, _line_anim_col_step_cb, obj);
}

void we_line_anim_opacity(we_line_obj_t *obj, uint8_t target, uint16_t dur_ms,
                          we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    obj->opa_from = obj->opacity;
    obj->opa_to   = target;
    obj->opa_ms   = dur_ms;
    obj->ease     = ease ? ease : we_ease_in_out_sine;
    obj->opa_t    = 0U;
    we_anim_start(obj->base.lcd, &obj->anim_opa, _line_anim_opa_step_cb, obj);
}

#else /* WE_LINE_USE_ANIM == 0：兼容桩，立即生效，调用方代码无需改动 */

void we_line_anim_points(we_line_obj_t *obj, int16_t x0, int16_t y0,
                         int16_t x1, int16_t y1, uint16_t dur_ms,
                         we_ease_fn_t ease)
{
    (void)dur_ms;
    (void)ease;
    we_line_set_points(obj, x0, y0, x1, y1);
}

void we_line_anim_move(we_line_obj_t *obj, int16_t dx, int16_t dy,
                       uint16_t dur_ms, we_ease_fn_t ease)
{
    (void)dur_ms;
    (void)ease;
    we_line_move(obj, dx, dy);
}

void we_line_anim_color(we_line_obj_t *obj, colour_t target, uint16_t dur_ms,
                        we_ease_fn_t ease)
{
    (void)dur_ms;
    (void)ease;
    we_line_set_color(obj, target);
}

void we_line_anim_opacity(we_line_obj_t *obj, uint8_t target, uint16_t dur_ms,
                          we_ease_fn_t ease)
{
    (void)dur_ms;
    (void)ease;
    we_line_set_opacity(obj, target);
}

#endif /* WE_LINE_USE_ANIM */

void we_line_obj_delete(we_line_obj_t *obj)
{
    if (obj == NULL)
        return;
#if WE_LINE_USE_ANIM
    we_anim_stop(obj->base.lcd, &obj->anim_geo);
    we_anim_stop(obj->base.lcd, &obj->anim_col);
    we_anim_stop(obj->base.lcd, &obj->anim_opa);
#endif
    we_obj_delete((we_obj_t *)obj);
}
