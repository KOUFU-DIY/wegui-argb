#include "we_widget_indicator.h"
#include "we_render.h"

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _indicator_draw_cb(void *ptr);

/**
 * @brief 控件事件回调，处理按压/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 未处理）。
 */
static uint8_t _indicator_event_cb(void *ptr, we_event_t event,
                                   we_indev_data_t *data);

/**
 * @brief 每对象周期任务，按时间片推进亮灭过渡动画。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param user_data 任务回调用户数据指针（指向控件对象）。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _indicator_anim_step_cb(void *owner, uint16_t elapsed_ms);

static const we_class_t _indicator_class = {
    .draw_cb = _indicator_draw_cb,
    .event_cb = _indicator_event_cb,
    .set_pos_cb = NULL
};

static const colour_t _c_black = RGB888_CONST(0, 0, 0);

/* 按下时向黑色额外混合的 alpha，产生轻微暗化按压反馈 */
#define _IND_PRESS_DARKEN 40U

/**
 * @brief 以 (w,h) 内接圆方式绘制一枚抗锯齿圆（round_rect 退化）。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param cx 圆心 X 坐标。
 * @param cy 圆心 Y 坐标。
 * @param r 半径（像素）。
 * @param color 填充颜色。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
static void _indicator_fill_circle(we_lcd_t *lcd, int16_t cx, int16_t cy,
                                   int16_t r, colour_t color, uint8_t opacity)
{
    int16_t d;

    if (lcd == NULL || r <= 0 || opacity == 0U)
        return;

    d = (int16_t)(r * 2);
    we_draw_round_rect_analytic_fill(lcd, (int16_t)(cx - r), (int16_t)(cy - r),
                                     (uint16_t)d, (uint16_t)d, (uint16_t)r,
                                     color, opacity);
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _indicator_draw_cb(void *ptr)
{
    we_indicator_obj_t *obj = (we_indicator_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t cx;
    int16_t cy;
    int16_t r_max;
    int16_t core_r;
    uint16_t eased;
    uint8_t lit_alpha;
    colour_t core_color;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->base.w <= 0 || obj->base.h <= 0)
        return;

    /* 圆心与最大半径（取短边的一半，保证正圆且落在 box 内） */
    cx = (int16_t)(obj->base.x + obj->base.w / 2);
    cy = (int16_t)(obj->base.y + obj->base.h / 2);
    r_max = (obj->base.w < obj->base.h) ? (int16_t)(obj->base.w / 2)
                                        : (int16_t)(obj->base.h / 2);
    if (r_max <= 0)
        return;

    /* 视觉进度经缓动后映射为点亮混合 alpha（0=熄灭色，255=点亮色） */
    eased = obj->ease ? obj->ease(obj->progress) : obj->progress;
    if (eased > 256U)
        eased = 256U;
    lit_alpha = (uint8_t)((uint32_t)eased * 255U / 256U);

    /* 开启光晕时核心圆收缩，外圈留给光晕；否则核心圆占满 box */
    if (obj->glow)
        core_r = (int16_t)((int32_t)r_max * (int32_t)(256U - WE_INDICATOR_GLOW_RATIO) / 256);
    else
        core_r = r_max;
    if (core_r < 1)
        core_r = 1;

    /* ---- 光晕：点亮程度越高越明显，由数圈同心半透明圆叠加出径向衰减 ---- */
    if (obj->glow && lit_alpha > 0U)
    {
        uint8_t i;
        uint8_t rings = 3U;
        for (i = 0U; i < rings; i++)
        {
            /* 由外到内：半径从 r_max 递减到 core_r，alpha 由弱到强累积 */
            int16_t gr = (int16_t)(core_r
                        + (int32_t)(r_max - core_r) * (int32_t)(rings - i) / (int32_t)rings);
            uint32_t a = (uint32_t)WE_INDICATOR_GLOW_ALPHA * lit_alpha / 255U;
            a = a * (uint32_t)(i + 1U) / (uint32_t)rings;        /* 内圈更实 */
            a = a * (uint32_t)obj->opacity / 255U;
            if (gr > core_r && a > 0U)
                _indicator_fill_circle(lcd, cx, cy, gr, obj->on_color, (uint8_t)a);
        }
    }

    /* ---- 核心圆：熄灭色与点亮色按 lit_alpha 混合，按下时整体压暗 ---- */
    core_color = we_colour_blend(obj->on_color, obj->off_color, lit_alpha);
    if (obj->pressed)
        core_color = we_colour_blend(_c_black, core_color, (uint8_t)_IND_PRESS_DARKEN);

    _indicator_fill_circle(lcd, cx, cy, core_r, core_color, obj->opacity);
}

/**
 * @brief 控件事件回调，处理按压/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 未处理）。
 */
static uint8_t _indicator_event_cb(void *ptr, we_event_t event,
                                   we_indev_data_t *data)
{
    we_indicator_obj_t *obj = (we_indicator_obj_t *)ptr;

    if (obj == NULL)
        return 0U;

    /* 用户自定义回调优先，接管全部事件 */
    if (obj->user_event_cb != NULL)
        return obj->user_event_cb(ptr, event, data);

    (void)data;

    /* 只读：不消费事件，让事件可穿透给背后控件 */
    if (!obj->clickable)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed = 1U;
        we_obj_invalidate((we_obj_t *)obj);
        break;
    case WE_EVENT_RELEASED:
        obj->pressed = 0U;
        we_obj_invalidate((we_obj_t *)obj);
        break;
    case WE_EVENT_CLICKED:
        obj->pressed = 0U;
        we_indicator_toggle(obj);
        break;
    default:
        break;
    }
    return 1U;
}

/**
 * @brief 推进一次亮灭过渡动画。
 * @param obj 指示灯控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _indicator_anim_step(we_indicator_obj_t *obj, uint16_t elapsed_ms)
{
    uint16_t target;
    uint32_t delta;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    target = obj->state ? 256U : 0U;
    if (obj->progress == target)
    {
        /* 已就位：摘链停表，空闲期零开销 */
        we_anim_stop(obj->base.lcd, &obj->anim);
        return;
    }

    /* 本帧线性推进量（Q8）。anim_ms 为 0 时直接到位，避免除零 */
    if (obj->anim_ms == 0U)
    {
        obj->progress = target;
    }
    else
    {
        delta = (uint32_t)elapsed_ms * 256U / (uint32_t)obj->anim_ms;
        if (delta == 0U)
            delta = 1U; /* 保证慢主循环下也能缓慢前进 */

        if (obj->progress < target)
            obj->progress = ((uint32_t)obj->progress + delta >= target)
                          ? target : (uint16_t)(obj->progress + delta);
        else
            obj->progress = ((uint32_t)delta >= obj->progress - target)
                          ? target : (uint16_t)(obj->progress - delta);
    }

    if (obj->progress == target)
        we_anim_stop(obj->base.lcd, &obj->anim); /* 本步到位，摘链停表 */

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 中央动画引擎回调，按时间片推进亮灭过渡动画。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _indicator_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    _indicator_anim_step((we_indicator_obj_t *)owner, elapsed_ms);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化圆形指示灯并挂载到 LCD 对象链表。
 * @param obj 指示灯控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @return 无。
 */
void we_indicator_obj_init(we_indicator_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = w;
    obj->base.h       = h;
    obj->base.class_p = &_indicator_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    {
        colour_t on  = RGB888_CONST(WE_INDICATOR_ON_R, WE_INDICATOR_ON_G, WE_INDICATOR_ON_B);
        colour_t off = RGB888_CONST(WE_INDICATOR_OFF_R, WE_INDICATOR_OFF_G, WE_INDICATOR_OFF_B);
        obj->on_color  = on;
        obj->off_color = off;
    }

    obj->user_event_cb = NULL;
    obj->ease          = we_ease_in_out_sine;
    obj->anim_ms       = WE_INDICATOR_ANIM_MS;
    obj->anim_acc_ms   = 0U;
    obj->progress      = 0U;     /* 初始熄灭 */
    obj->anim.next     = NULL;
    obj->anim.step_cb  = NULL;
    obj->anim.owner    = NULL;
    obj->state         = 0U;
    obj->opacity       = 255U;
    obj->pressed       = 0U;
#if WE_INDICATOR_USE_ANIM
    obj->anim_enabled  = 1U;
#else
    obj->anim_enabled  = 0U;
#endif
    obj->glow          = 1U;     /* 光晕默认开 */
    obj->clickable     = 0U;     /* 默认只读 */

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置目标状态（按当前动画配置过渡或瞬切）。
 * @param obj 指示灯控件对象指针。
 * @param on 目标状态，0=灭，非0=亮。
 * @return 无。
 */
void we_indicator_set_state(we_indicator_obj_t *obj, uint8_t on)
{
    uint8_t val;

    if (obj == NULL)
        return;

    val = on ? 1U : 0U;
    if (obj->state == val)
        return;

    obj->state = val;
    obj->anim_acc_ms = 0U;

    if (!obj->anim_enabled)
    {
        /* 无动画：直接把视觉进度拉到目标位 */
        obj->progress = val ? 256U : 0U;
    }
    else
    {
        /* 挂入中央动画链表（不占 task 槽、不会失败） */
        we_anim_start(obj->base.lcd, &obj->anim, _indicator_anim_step_cb, obj);
    }

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 翻转状态（灭/亮互换）。
 * @param obj 指示灯控件对象指针。
 * @return 无。
 */
void we_indicator_toggle(we_indicator_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_indicator_set_state(obj, obj->state ? 0U : 1U);
}

/**
 * @brief 查询当前目标状态。
 * @param obj 指示灯控件对象指针。
 * @return 1=亮，0=灭或 obj 为 NULL。
 */
uint8_t we_indicator_get_state(const we_indicator_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->state;
}

/**
 * @brief 设置点亮色与熄灭色并刷新。
 * @param obj 指示灯控件对象指针。
 * @param on_color 点亮颜色。
 * @param off_color 熄灭颜色。
 * @return 无。
 */
void we_indicator_set_colors(we_indicator_obj_t *obj, colour_t on_color,
                             colour_t off_color)
{
    if (obj == NULL)
        return;
    obj->on_color  = on_color;
    obj->off_color = off_color;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 配置动画开关与时长（速度）。
 * @param obj 指示灯控件对象指针。
 * @param enabled 0=瞬切无动画，非0=平滑过渡。
 * @param duration_ms 过渡时长（毫秒），enabled 为 0 时忽略。
 * @return 无。
 */
void we_indicator_set_anim(we_indicator_obj_t *obj, uint8_t enabled,
                           uint16_t duration_ms)
{
    if (obj == NULL)
        return;

    obj->anim_enabled = enabled ? 1U : 0U;
    if (enabled && duration_ms > 0U)
        obj->anim_ms = duration_ms;

    /* 关闭动画时立即让视觉就位到当前目标态，并摘除进行中的动画 */
    if (!obj->anim_enabled)
    {
        we_anim_stop(obj->base.lcd, &obj->anim);
        obj->progress = obj->state ? 256U : 0U;
        we_obj_invalidate((we_obj_t *)obj);
    }
}

/**
 * @brief 设置缓动函数（来自 we_motion.h）。
 * @param obj 指示灯控件对象指针。
 * @param ease 缓动函数指针，NULL 时退回线性。
 * @return 无。
 */
void we_indicator_set_ease(we_indicator_obj_t *obj, we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    obj->ease = (ease != NULL) ? ease : we_ease_linear;
}

/**
 * @brief 开关外发光晕。
 * @param obj 指示灯控件对象指针。
 * @param enable 0=纯圆，非0=点亮时显示光晕。
 * @return 无。
 */
void we_indicator_set_glow(we_indicator_obj_t *obj, uint8_t enable)
{
    uint8_t val;

    if (obj == NULL)
        return;
    val = enable ? 1U : 0U;
    if (obj->glow == val)
        return;
    obj->glow = val;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置是否允许点击翻转状态。
 * @param obj 指示灯控件对象指针。
 * @param clickable 0=只读，非0=点击翻转。
 * @return 无。
 */
void we_indicator_set_clickable(we_indicator_obj_t *obj, uint8_t clickable)
{
    if (obj == NULL)
        return;
    obj->clickable = clickable ? 1U : 0U;
}

/**
 * @brief 设置自定义事件回调（非 NULL 时接管全部输入事件）。
 * @param obj 指示灯控件对象指针。
 * @param cb 事件回调，NULL 时恢复内建行为。
 * @return 无。
 */
void we_indicator_set_event_cb(we_indicator_obj_t *obj,
                               we_indicator_event_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->user_event_cb = cb;
}

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 指示灯控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_indicator_set_opacity(we_indicator_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除指示灯控件并从对象链表/任务系统移除。
 * @param obj 指示灯控件对象指针。
 * @return 无。
 */
void we_indicator_obj_delete(we_indicator_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* 节点归控件所有，删除前必须摘链，否则动画链表留悬空指针 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    we_obj_delete((we_obj_t *)obj);
}









