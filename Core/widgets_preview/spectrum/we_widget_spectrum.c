#include "we_widget_spectrum.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * spectrum —— 频谱柱（preview 孵化区）
 *
 * 显示模型：target（目标电平，push 写入）/ shown（显示电平）/ peak（峰值帽）。
 * 上升：push 时 shown 立即取 max（快速上冲）；
 * 回落：单个中央动画节点按 WE_SPECTRUM_STEP_MS 时基统一推进全部柱，
 *       每步 shown 向 target 比例衰减（差值 >> WE_SPECTRUM_FALL_SHIFT），
 *       peak 以更慢的匀速（WE_SPECTRUM_PEAK_FALL/步）下坠贴向柱顶；
 * 全部柱就位后动画自行摘链，空闲期零开销。
 *
 * 渲染全部 we_fill_rect：柱体按全量程高度分 WE_SPECTRUM_GRAD_STEPS 段
 * 预混渐变色（段色只算一次，颜色随绝对高度固定，柱长变化不闪色），
 * 柱底 1px 基线横贯控件，峰值帽为柱宽 2px 横线。
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色值是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 表示相等，0 表示不等。
 */
static uint8_t _spec_color_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#else
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 计算电平可用像素高度（总高扣除 1px 基线与 2px 峰值帽余量）。
 * @param obj 传入：控件对象指针。
 * @return 可用高度（像素，最小 1）。
 */
static int16_t _spec_usable_h(const we_spectrum_obj_t *obj)
{
    int16_t u = (int16_t)(obj->base.h - 3);
    return (u < 1) ? 1 : u;
}

/**
 * @brief 停止衰减动画（摘链停表）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _spec_stop_anim(we_spectrum_obj_t *obj)
{
    obj->anim_busy = 0U;
    obj->fall_acc_ms = 0U;
    we_anim_stop(obj->base.lcd, &obj->anim);
}

/**
 * @brief 执行一个衰减量子步：柱体比例回落 + 峰值帽匀速下坠。
 * @param obj 传入：控件对象指针。
 * @return 1 表示本步有电平变化，0 表示全部静止。
 */
static uint8_t _spec_fall_one_step(we_spectrum_obj_t *obj)
{
    uint8_t i;
    uint8_t changed = 0U;

    for (i = 0U; i < obj->bar_cnt; i++)
    {
        /* 柱体：显示值高于目标时按差值比例衰减（最小 1 防停滞） */
        if (obj->shown[i] > obj->target[i])
        {
            uint8_t delta = (uint8_t)((obj->shown[i] - obj->target[i]) >> WE_SPECTRUM_FALL_SHIFT);
            if (delta == 0U)
                delta = 1U;
            obj->shown[i] = (uint8_t)(obj->shown[i] - delta);
            changed = 1U;
        }

        /* 峰值帽：高于柱顶时匀速下坠，落到柱顶即停 */
        if (obj->peak[i] > obj->shown[i])
        {
            uint8_t fall = WE_SPECTRUM_PEAK_FALL;
            if (fall > (uint8_t)(obj->peak[i] - obj->shown[i]))
                fall = (uint8_t)(obj->peak[i] - obj->shown[i]);
            if (fall != 0U)
            {
                obj->peak[i] = (uint8_t)(obj->peak[i] - fall);
                changed = 1U;
            }
        }
    }
    return changed;
}

/**
 * @brief 中央动画引擎回调：按时基量子推进全部柱的回落/下坠。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次调度经过的毫秒数。
 * @return 无。
 * @note 全部柱就位（shown==target 且 peak==shown）后自行摘链。
 */
static void _spec_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_spectrum_obj_t *obj = (we_spectrum_obj_t *)owner;
    uint16_t steps;
    uint8_t changed = 0U;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    obj->fall_acc_ms = (uint16_t)(obj->fall_acc_ms + elapsed_ms);
    steps = obj->fall_acc_ms / WE_SPECTRUM_STEP_MS;
    obj->fall_acc_ms = (uint16_t)(obj->fall_acc_ms % WE_SPECTRUM_STEP_MS);

    if (steps == 0U)
        return; /* 时基未满一个量子，保持挂链等待下轮 */

    if (steps > 4U)
        steps = 4U; /* 主循环长时间卡顿时限幅，避免一帧跳空全部电平 */

    while (steps != 0U)
    {
        if (_spec_fall_one_step(obj))
            changed = 1U;
        steps--;
    }

    if (changed)
    {
        we_obj_invalidate((we_obj_t *)obj); /* preview：整控件包围盒标脏 */
    }
    else
    {
        _spec_stop_anim(obj); /* 全部静止，摘链停表 */
    }
}

/**
 * @brief 控件绘制回调：基线 + 分段渐变柱体 + 峰值帽，全部 we_fill_rect。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 */
static void _spec_draw_cb(void *ptr)
{
    we_spectrum_obj_t *obj = (we_spectrum_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t usable;
    int16_t base_y;
    int16_t slot_w;
    int16_t bar_w;
    int16_t gap;
    int16_t inset;
    uint8_t i;
    uint8_t seg;
    colour_t seg_color[WE_SPECTRUM_GRAD_STEPS];

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->base.w <= 0 || obj->base.h <= 0 || obj->bar_cnt == 0U)
        return;

    usable = _spec_usable_h(obj);
    base_y = (int16_t)(obj->base.y + obj->base.h - 1); /* 基线所在行 */

    /* 柱位布局：控件宽度等分为 bar_cnt 个槽，槽内留间隙，余数居中吸收 */
    slot_w = (int16_t)(obj->base.w / obj->bar_cnt);
    if (slot_w < 1)
        slot_w = 1;
    gap = (int16_t)(slot_w / 4);
    if (gap < 1 && slot_w >= 2)
        gap = 1;
    bar_w = (int16_t)(slot_w - gap);
    if (bar_w < 1)
    {
        bar_w = 1;
        gap = (int16_t)(slot_w - 1);
    }
    inset = (int16_t)((obj->base.w - slot_w * (int16_t)obj->bar_cnt) / 2);

    /* 分段渐变色只按段预混一次（低端 -> 高端），内环零混色计算 */
    for (seg = 0U; seg < WE_SPECTRUM_GRAD_STEPS; seg++)
    {
        uint8_t t = (uint8_t)((uint32_t)seg * 255U / (WE_SPECTRUM_GRAD_STEPS - 1U));
        seg_color[seg] = we_colour_blend(obj->color_high, obj->color_low, t);
    }

    /* 1. 柱底基线：1px 横线横贯整个控件（低色调暗显示） */
    we_fill_rect(lcd, obj->base.x, base_y, (uint16_t)obj->base.w, 1U,
                 obj->color_low, (uint8_t)(((uint16_t)obj->opacity * 140U) >> 8));

    /* 2. 柱体 + 峰值帽 */
    for (i = 0U; i < obj->bar_cnt; i++)
    {
        int16_t bx = (int16_t)(obj->base.x + inset + (int16_t)i * slot_w + gap / 2);
        int16_t bar_h = (int16_t)(((int32_t)obj->shown[i] * usable) / 255);

        /* 柱体按段填充：段区间 [seg_lo, seg_hi) 为距基线的像素高度 */
        if (bar_h > 0)
        {
            for (seg = 0U; seg < WE_SPECTRUM_GRAD_STEPS; seg++)
            {
                int16_t seg_lo = (int16_t)(((int32_t)usable * seg) / WE_SPECTRUM_GRAD_STEPS);
                int16_t seg_hi = (int16_t)(((int32_t)usable * (seg + 1U)) / WE_SPECTRUM_GRAD_STEPS);

                if (seg_lo >= bar_h)
                    break; /* 该段已在柱顶之上 */
                if (seg_hi > bar_h)
                    seg_hi = bar_h;
                if (seg_hi <= seg_lo)
                    continue;

                we_fill_rect(lcd, bx, (int16_t)(base_y - seg_hi),
                             (uint16_t)bar_w, (uint16_t)(seg_hi - seg_lo),
                             seg_color[seg], obj->opacity);
            }
        }

        /* 峰值帽：柱宽 2px 横线，跟随峰值电平（帽底紧贴峰值高度） */
        if (obj->peak_hold && obj->peak[i] > 0U)
        {
            int16_t peak_px = (int16_t)(((int32_t)obj->peak[i] * usable) / 255);
            if (peak_px > 0)
            {
                we_fill_rect(lcd, bx, (int16_t)(base_y - peak_px - 2),
                             (uint16_t)bar_w, 2U, obj->color_peak, obj->opacity);
            }
        }
    }
}

/**
 * @brief 控件事件回调：装饰性控件，不消费任何输入。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 恒返回 0（穿透给背后控件）。
 */
static uint8_t _spec_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

static const we_class_t _spectrum_class = {
    .draw_cb = _spec_draw_cb,
    .event_cb = _spec_event_cb,
    .set_pos_cb = NULL, /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化频谱柱控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param h 传入：控件高度（像素）。
 * @param bar_cnt 传入：柱数（钳制到 1..WE_SPECTRUM_BAR_MAX）。
 * @return 无。
 */
void we_spectrum_obj_init(we_spectrum_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                          uint8_t bar_cnt)
{
    uint8_t i;

    if (obj == NULL || lcd == NULL)
        return;

    if (bar_cnt == 0U)
        bar_cnt = 1U;
    if (bar_cnt > WE_SPECTRUM_BAR_MAX)
        bar_cnt = WE_SPECTRUM_BAR_MAX;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_spectrum_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->bar_cnt = bar_cnt;
    obj->peak_hold = 1U;
    obj->opacity = 255U;
    obj->anim_busy = 0U;
    obj->fall_acc_ms = 0U;

    for (i = 0U; i < WE_SPECTRUM_BAR_MAX; i++)
    {
        obj->target[i] = 0U;
        obj->shown[i] = 0U;
        obj->peak[i] = 0U;
    }

    obj->color_low = RGB888TODEV(48, 200, 220);   /* 低端青蓝 */
    obj->color_high = RGB888TODEV(236, 92, 176);  /* 高端品红 */
    obj->color_peak = RGB888TODEV(244, 246, 250); /* 峰值帽近白 */

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 推入一帧目标电平（bar_cnt 个 0~255 值，拷入控件内部数组）。
 * @param obj 传入：控件对象指针。
 * @param levels 传入：电平数组指针，长度至少 bar_cnt 字节。
 * @return 无。
 */
void we_spectrum_push(we_spectrum_obj_t *obj, const uint8_t *levels)
{
    uint8_t i;
    uint8_t rose = 0U;
    uint8_t need_anim = 0U;

    if (obj == NULL || levels == NULL || obj->base.lcd == NULL)
        return;

    for (i = 0U; i < obj->bar_cnt; i++)
    {
        obj->target[i] = levels[i];

        /* 上升立即取 max（快速上冲）；峰值帽同步抬到新柱顶 */
        if (levels[i] > obj->shown[i])
        {
            obj->shown[i] = levels[i];
            rose = 1U;
        }
        if (obj->shown[i] > obj->peak[i])
        {
            obj->peak[i] = obj->shown[i];
            rose = 1U;
        }

        if (obj->shown[i] > obj->target[i] || obj->peak[i] > obj->shown[i])
            need_anim = 1U;
    }

    if (rose)
        we_obj_invalidate((we_obj_t *)obj);

    if (need_anim && !obj->anim_busy)
    {
        obj->anim_busy = 1U;
        obj->fall_acc_ms = 0U;
        we_anim_start(obj->base.lcd, &obj->anim, _spec_anim_step_cb, obj);
    }
}

/**
 * @brief 设置柱体渐变双色与峰值帽颜色。
 * @param obj 传入：控件对象指针。
 * @param low 传入：柱体低端颜色（靠近基线）。
 * @param high 传入：柱体高端颜色（靠近顶部）。
 * @param peak 传入：峰值帽颜色。
 * @return 无。
 */
void we_spectrum_set_colors(we_spectrum_obj_t *obj,
                            colour_t low, colour_t high, colour_t peak)
{
    if (obj == NULL)
        return;
    if (_spec_color_eq(obj->color_low, low) &&
        _spec_color_eq(obj->color_high, high) &&
        _spec_color_eq(obj->color_peak, peak))
        return;

    obj->color_low = low;
    obj->color_high = high;
    obj->color_peak = peak;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 开关峰值保持帽（切换时峰值帽复位到当前柱顶）。
 * @param obj 传入：控件对象指针。
 * @param enable 传入：0=关闭，非0=开启。
 * @return 无。
 */
void we_spectrum_set_peak_hold(we_spectrum_obj_t *obj, uint8_t enable)
{
    uint8_t val;
    uint8_t i;

    if (obj == NULL)
        return;
    val = enable ? 1U : 0U;
    if (obj->peak_hold == val)
        return;

    obj->peak_hold = val;
    for (i = 0U; i < obj->bar_cnt; i++)
        obj->peak[i] = obj->shown[i];
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置控件整体透明度并按需重绘。
 * @param obj 传入：控件对象指针。
 * @param opacity 传入：不透明度（0~255）。
 * @return 无。
 */
void we_spectrum_set_opacity(we_spectrum_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除频谱柱控件：先摘除衰减动画节点（we_anim_stop）再摘链。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_spectrum_obj_delete(we_spectrum_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    _spec_stop_anim(obj); /* 节点归控件所有，删除前必须摘链 */
    we_obj_delete((we_obj_t *)obj);
}
