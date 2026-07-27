/**
 * @file  we_widget_spinner.c
 * @brief 加载指示器控件（spinner）实现 —— preview 孵化区
 *
 * 12 个小圆点绕环均布，以旋转头为最亮点、沿环逐点 alpha 递减形成拖尾；
 * 每个圆点用 we_draw_round_rect_analytic_fill 退化实心圆绘制。
 * 旋转推进走单个中央动画节点（节点内累计 elapsed_ms，每满 step_ms
 * 头索引前进一格），不占 GUI timer 槽，全程整数运算。
 */

#include "we_widget_spinner.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _spinner_draw_cb(void *ptr);
static uint8_t _spinner_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _spinner_class = {
    .draw_cb    = _spinner_draw_cb,
    .event_cb   = _spinner_event_cb,
    .set_pos_cb = NULL /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（setter 幂等判断用）。
 * @param a 颜色 A。
 * @param b 颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _spinner_col_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#else
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/* --------------------------------------------------------------------------
 * 绘制 / 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：绕环画 WE_SPINNER_DOT_CNT 个 alpha 递减的圆点。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _spinner_draw_cb(void *ptr)
{
    we_spinner_obj_t *obj = (we_spinner_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t cx;
    int16_t cy;
    int16_t half;
    int16_t dot_r;
    int16_t ring_r;
    uint8_t i;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    /* 中心 / 点半径 / 环半径：点径约为直径的 1/5，环贴外沿留 1px 余量 */
    half = (int16_t)(WE_MIN(obj->base.w, obj->base.h) / 2);
    if (half <= 4)
        return;
    cx     = (int16_t)(obj->base.x + obj->base.w / 2);
    cy     = (int16_t)(obj->base.y + obj->base.h / 2);
    dot_r  = (int16_t)(half / 5);
    if (dot_r < 2)
        dot_r = 2;
    ring_r = (int16_t)(half - dot_r - 1);
    if (ring_r < 1)
        ring_r = 1;

    for (i = 0U; i < (uint8_t)WE_SPINNER_DOT_CNT; i++)
    {
        /* 头点最亮（255），沿环向后逐点线性衰减，尾端恰为 WE_SPINNER_TAIL_MIN */
        uint8_t dist = (uint8_t)((obj->head + (uint8_t)WE_SPINNER_DOT_CNT - i)
                                 % (uint8_t)WE_SPINNER_DOT_CNT);
        uint32_t a = 255U - ((255U - (uint32_t)WE_SPINNER_TAIL_MIN) * dist)
                                / ((uint32_t)WE_SPINNER_DOT_CNT - 1U);
        int16_t angle;
        int32_t vx;
        int32_t vy;
        int16_t dcx;
        int16_t dcy;

        a = ((uint32_t)obj->opacity * a) >> 8; /* 叠加整体透明度（>>8 近似 /255） */
        if (a == 0U)
            continue;

        /* 点位：512 步制均布，i * 512 / CNT 累计误差 < 1 步 */
        angle = (int16_t)(((int32_t)i * 512) / (int32_t)WE_SPINNER_DOT_CNT);
        vx = (int32_t)ring_r * (int32_t)we_cos(angle);
        vy = (int32_t)ring_r * (int32_t)we_sin(angle);
        dcx = (int16_t)(cx + (vx >= 0 ? ((vx + 16384) >> 15) : -((-vx + 16384) >> 15)));
        dcy = (int16_t)(cy + (vy >= 0 ? ((vy + 16384) >> 15) : -((-vy + 16384) >> 15)));

        /* round_rect 退化实心抗锯齿圆：w = h = 直径，radius = 半径 */
        we_draw_round_rect_analytic_fill(lcd,
                                         (int16_t)(dcx - dot_r), (int16_t)(dcy - dot_r),
                                         (uint16_t)(dot_r * 2), (uint16_t)(dot_r * 2),
                                         (uint16_t)dot_r, obj->color, (uint8_t)a);
    }
}

/**
 * @brief 控件事件回调：装饰性控件，不消费事件，输入穿透给背后控件。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒为 0（穿透）。
 */
static uint8_t _spinner_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

/* --------------------------------------------------------------------------
 * 旋转推进（中央动画引擎节点）
 * -------------------------------------------------------------------------- */

/**
 * @brief 推进一步旋转：节点内累计 elapsed_ms，每满 step_ms 头索引进一格。
 * @param obj 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _spinner_anim_step(we_spinner_obj_t *obj, uint16_t elapsed_ms)
{
    uint32_t sum;      /* uint32 累加，防极端大时间片下 uint16 回绕丢拍 */
    uint16_t adv = 0U; /* 大时间片一次补进多格 */

    if (obj == NULL || elapsed_ms == 0U)
        return;

    if (!obj->running)
    {
        we_anim_stop(obj->base.lcd, &obj->anim); /* 已停止：摘链保险 */
        return;
    }

    sum = (uint32_t)obj->acc_ms + elapsed_ms;
    while (sum >= obj->step_ms)
    {
        sum -= obj->step_ms;
        adv++;
    }
    obj->acc_ms = (uint16_t)sum;

    if (adv > 0U)
    {
        obj->head = (uint8_t)(((uint16_t)obj->head + adv) % (uint16_t)WE_SPINNER_DOT_CNT);
        we_obj_invalidate((we_obj_t *)obj); /* preview：整包围盒标脏 */
    }
}

/**
 * @brief 中央动画引擎回调壳（owner 透传控件实例）。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _spinner_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    _spinner_anim_step((we_spinner_obj_t *)owner, elapsed_ms);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_spinner_obj_init(we_spinner_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, uint16_t diameter)
{
    if (obj == NULL || lcd == NULL || diameter == 0U)
        return;

    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)diameter;
    obj->base.h       = (int16_t)diameter;
    obj->base.class_p = &_spinner_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    {
        colour_t c = RGB888_CONST(WE_SPINNER_DEF_R, WE_SPINNER_DEF_G, WE_SPINNER_DEF_B);
        obj->color = c;
    }
    obj->opacity = 255U;
    obj->running = 1U; /* init 后立即开始旋转 */
    obj->head    = 0U;
    obj->step_ms = (uint16_t)WE_SPINNER_DEF_STEP_MS;
    if (obj->step_ms < 16U)
        obj->step_ms = 16U; /* 与 set_speed 同口径钳下限，防步进累加死循环 */
    obj->acc_ms  = 0U;

    obj->anim.next    = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner   = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
    we_anim_start(lcd, &obj->anim, _spinner_anim_step_cb, obj);
}

void we_spinner_set_colors(we_spinner_obj_t *obj, colour_t color)
{
    if (obj == NULL || _spinner_col_eq(obj->color, color))
        return;
    obj->color = color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_spinner_start(we_spinner_obj_t *obj)
{
    if (obj == NULL || obj->running)
        return;
    obj->running = 1U;
    obj->acc_ms  = 0U;
    we_anim_start(obj->base.lcd, &obj->anim, _spinner_anim_step_cb, obj);
}

void we_spinner_stop(we_spinner_obj_t *obj)
{
    if (obj == NULL || !obj->running)
        return;
    obj->running = 0U;
    we_anim_stop(obj->base.lcd, &obj->anim); /* 摘链定格，空闲期零开销 */
}

uint8_t we_spinner_is_running(const we_spinner_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->running;
}

void we_spinner_set_speed(we_spinner_obj_t *obj, uint16_t step_ms)
{
    if (obj == NULL)
        return;
    if (step_ms < 16U)
        step_ms = 16U; /* 一帧以内的步进无意义，钳制下限 */
    if (obj->step_ms == step_ms)
        return;
    obj->step_ms = step_ms;
    /* 只改推进速度，不改当前画面，无需标脏 */
}

void we_spinner_set_opacity(we_spinner_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_spinner_obj_delete(we_spinner_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* 动画节点归控件所有，删除前必须摘链，否则中央动画链表留悬空指针 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    we_obj_delete((we_obj_t *)obj);
}
