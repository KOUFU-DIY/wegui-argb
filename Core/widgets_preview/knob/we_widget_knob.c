/**
 * @file  we_widget_knob.c
 * @brief 弧形旋钮滑块控件（knob）实现 —— preview 孵化区
 *
 * 弧带渲染为 we_widget_arc.c 距离平方场扫描的简化版：外/内沿各 1px
 * 线性抗锯齿带，角向用起点/终点向量叉积判定，弧带端面平头（无端帽），
 * track 与 value 弧一遍扫描内合成。拖拽端点小圆用
 * we_draw_round_rect_analytic_fill 退化实心圆叠加在弧带上。
 * 触点角度由内置八分区近似整数 atan2 解算（512 步制，纯整数）。
 */

#include "we_widget_knob.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _knob_draw_cb(void *ptr);
static uint8_t _knob_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _knob_class = {
    .draw_cb    = _knob_draw_cb,
    .event_cb   = _knob_event_cb,
    .set_pos_cb = NULL /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

static const colour_t _knob_white = RGB888_CONST(255, 255, 255);

/* 按压时端点小圆向白色增亮的混合 alpha */
#define _KNOB_PRESS_LIGHTEN 70U

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 将任意角度归一化到 512 分度制范围 [0, 511]。
 * @param a 输入角度（允许负值或超过 511）。
 * @return 归一化后的角度值。
 */
static uint16_t _knob_norm_512(int16_t a)
{
    int16_t r = a & 0x1FF;
    return (uint16_t)(r < 0 ? r + 512 : r);
}

/**
 * @brief 计算两阈值区间的反比系数（255<<15 / 区间宽），用于 alpha 线性插值。
 * @param a 区间上界。
 * @param b 区间下界。
 * @return 归一化倒数系数；a <= b 时返回 0。
 */
static uint32_t _knob_get_inv(uint32_t a, uint32_t b)
{
    return (a > b) ? (8355840U / (a - b)) : 0U; /* 8355840 = 255 << 15 */
}

/**
 * @brief 比较两个颜色是否相等（setter 幂等判断用）。
 * @param a 颜色 A。
 * @param b 颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _knob_col_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#else
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 八分区近似整数 atan2，输出 512 步制角度（纯整数）。
 * @param dy 触点相对中心的 Y 偏移（屏幕 Y 向下为正）。
 * @param dx 触点相对中心的 X 偏移。
 * @return 512 步制角度（0..511，0 = +X，128 = +Y，顺时针增大）。
 * @note 分区内用二阶近似 atan(t) ≈ 64t + 22t(1-t)（t ∈ [0,1] Q8，单位为
 *       512 步制），最大误差约 0.3°（≈0.45 步），preview 精度足够；
 *       毕业前可换查表反正切细化（见 widget.md）。
 */
static uint16_t _knob_atan2_512(int32_t dy, int32_t dx)
{
    uint32_t ax = (uint32_t)(dx < 0 ? -dx : dx);
    uint32_t ay = (uint32_t)(dy < 0 ? -dy : dy);
    uint32_t t_q8;
    uint32_t a;

    if (ax == 0U && ay == 0U)
        return 0U; /* 原点无方向：调用方已用死区挡掉，此处仅作保护 */

    /* 先在第一象限的两个八分区内解算 0..128（0°..90°） */
    if (ay <= ax)
    {
        t_q8 = (ay << 8) / ax;
        a = (64U * t_q8 + ((22U * t_q8 * (256U - t_q8)) >> 8)) >> 8;
    }
    else
    {
        t_q8 = (ax << 8) / ay;
        a = (64U * t_q8 + ((22U * t_q8 * (256U - t_q8)) >> 8)) >> 8;
        a = 128U - a;
    }

    /* 按象限符号折叠回 0..511 */
    if (dx >= 0)
    {
        if (dy < 0)
            a = (512U - a) & 0x1FFU; /* 第四象限（右上），a=0 时回绕到 0 */
    }
    else
    {
        a = (dy >= 0) ? (256U - a) : (256U + a); /* 左下 / 左上 */
    }
    return (uint16_t)a;
}

/**
 * @brief 将数值钳制到量程内。
 * @param obj 控件对象指针。
 * @param v 输入值。
 * @return 钳制后的值。
 */
static int32_t _knob_clamp(const we_knob_obj_t *obj, int32_t v)
{
    if (v < obj->v_min)
        return obj->v_min;
    if (v > obj->v_max)
        return obj->v_max;
    return v;
}

/**
 * @brief 应用新值：钳制、幂等判断、标脏，用户来源时触发 changed_cb。
 * @param obj 控件对象指针。
 * @param v 新值。
 * @param from_user 1 = 用户拖动/点击产生（触发回调），0 = 程序设置。
 * @return 无。
 */
static void _knob_apply_value(we_knob_obj_t *obj, int32_t v, uint8_t from_user)
{
    v = _knob_clamp(obj, v);
    if (v == obj->value)
        return;

    obj->value = v;
    we_obj_invalidate((we_obj_t *)obj); /* preview：整包围盒标脏 */
    if (from_user && obj->changed_cb != NULL)
        obj->changed_cb(obj, v);
}

/**
 * @brief 由触点屏幕坐标解算角度并映射为数值（用户交互路径）。
 * @param obj 控件对象指针。
 * @param x 触点 X（屏幕绝对坐标）。
 * @param y 触点 Y。
 * @return 无。
 * @note 中心死区内的触点忽略（防过圆心角度跳变）；角度落在弧跨度外的
 *       开口区时，就近钳制到量程端点。
 */
static void _knob_apply_point(we_knob_obj_t *obj, int16_t x, int16_t y)
{
    int32_t cx = obj->base.x + obj->base.w / 2;
    int32_t cy = obj->base.y + obj->base.h / 2;
    int32_t dx = (int32_t)x - cx;
    int32_t dy = (int32_t)y - cy;
    int32_t dead_r;
    int16_t rel;
    int32_t span;
    int32_t v;

    if (obj->sweep <= 0)
        return;

    /* 中心死区：内半径的一半（最小 4px），过圆心时角度剧烈跳变，直接忽略 */
    dead_r = ((int32_t)obj->radius - (int32_t)obj->thickness) / 2;
    if (dead_r < 4)
        dead_r = 4;
    if (dx * dx + dy * dy < dead_r * dead_r)
        return;

    /* 绝对角 -> 相对起始角的顺时针偏移（0..511） */
    rel = (int16_t)(((int32_t)_knob_atan2_512(dy, dx) - (int32_t)obj->start_angle) & 0x1FF);

    /* 开口区（rel > sweep）：就近吸附到量程端点 */
    if (rel > obj->sweep)
        rel = ((int32_t)rel - (int32_t)obj->sweep <= 512 - (int32_t)rel)
              ? obj->sweep : 0;

    /* 角度 -> 数值：四舍五入线性映射（量程跨度 < 2^22 防溢出） */
    span = obj->v_max - obj->v_min;
    v = obj->v_min + ((int32_t)rel * span + obj->sweep / 2) / obj->sweep;
    _knob_apply_value(obj, v, 1U);
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：一遍扫描合成 track/value 弧带，再叠端点小圆。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _knob_draw_cb(void *ptr)
{
    we_knob_obj_t *obj = (we_knob_obj_t *)ptr;
    we_lcd_t *lcd;
    uint8_t eff_opa;
    int16_t draw_x_start;
    int16_t draw_y_start;
    int16_t draw_x_end;
    int16_t draw_y_end;
    int16_t cx;
    int16_t cy;
    int32_t r;
    int32_t ir;
    int32_t fg_span;
    int32_t v_range;
    colour_t *dst_line;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->radius <= obj->thickness || obj->sweep <= 0)
        return;

    /* 容器透明度级联在入口消费一次（与绘图原语同口径），常态零开销 */
    eff_opa = we_opa_apply(lcd, obj->opacity);
    if (eff_opa == 0U)
        return;

    /* 与当前 PFB 条带求交，不在条带内直接退出 */
    draw_x_start = WE_MAX(obj->base.x, lcd->pfb_area.x0);
    draw_y_start = WE_MAX(obj->base.y, lcd->pfb_y_start);
    draw_x_end   = WE_MIN((int16_t)(obj->base.x + obj->base.w - 1), lcd->pfb_area.x1);
    draw_y_end   = WE_MIN((int16_t)(obj->base.y + obj->base.h - 1), lcd->pfb_y_end);
    if (draw_x_start > draw_x_end || draw_y_start > draw_y_end)
        return;

    cx = (int16_t)(obj->base.x + obj->base.w / 2);
    cy = (int16_t)(obj->base.y + obj->base.h / 2);
    r  = obj->radius;
    ir = r - obj->thickness;

    /* value 弧跨度：量程线性映射（v_range > 0 由 API 保证） */
    v_range = obj->v_max - obj->v_min;
    fg_span = ((int32_t)obj->sweep * (obj->value - obj->v_min)) / v_range;

    {
        /* 半径平方常量（uint32 防 16 位乘法溢出），外/内沿各 1px AA 过渡带 */
        uint32_t r2     = (uint32_t)r * (uint32_t)r;
        uint32_t rmax2  = (uint32_t)(r + 1) * (uint32_t)(r + 1);
        uint32_t ir2    = (uint32_t)ir * (uint32_t)ir;
        uint32_t irmin2 = (ir > 0) ? ((uint32_t)(ir - 1) * (uint32_t)(ir - 1)) : 0U;
        uint32_t inv_out = _knob_get_inv(rmax2, r2);
        uint32_t inv_in  = _knob_get_inv(ir2, irmin2);

        /* 角向判定向量：起点 / track 终点 / value 终点（Q15） */
        uint16_t s_ang  = _knob_norm_512(obj->start_angle);
        uint16_t te_ang = _knob_norm_512((int16_t)(obj->start_angle + obj->sweep));
        uint16_t fe_ang = _knob_norm_512((int16_t)(obj->start_angle + (int16_t)fg_span));
        int32_t v_s_x  = we_cos((int16_t)s_ang),  v_s_y  = we_sin((int16_t)s_ang);
        int32_t v_te_x = we_cos((int16_t)te_ang), v_te_y = we_sin((int16_t)te_ang);
        int32_t v_fe_x = we_cos((int16_t)fe_ang), v_fe_y = we_sin((int16_t)fe_ang);

        uint8_t is_full     = (obj->sweep >= 512) ? 1U : 0U;
        uint8_t track_large = (obj->sweep > 256) ? 1U : 0U;
        uint8_t fg_large    = (fg_span > 256) ? 1U : 0U;
        uint8_t has_fg      = (fg_span > 0) ? 1U : 0U;

        int16_t y;

        dst_line = (colour_t *)lcd->pfb_gram +
                   ((draw_y_start - lcd->pfb_y_start) * lcd->pfb_width) +
                   (draw_x_start - lcd->pfb_area.x0);

        for (y = draw_y_start; y <= draw_y_end; y++)
        {
            int32_t dy   = y - cy;
            uint32_t dy2 = (uint32_t)(dy * dy);
            int32_t dx0  = draw_x_start - cx;

            /* 叉积基值随 x 递增仅需加 v_*_y（与 arc 同口径的增量式判定） */
            int32_t cp_s  = dx0 * v_s_y - dy * v_s_x;
            int32_t cp_te = dx0 * v_te_y - dy * v_te_x;
            int32_t cp_fe = dx0 * v_fe_y - dy * v_fe_x;

            colour_t *p_dst = dst_line;
            int16_t x;

            for (x = draw_x_start; x <= draw_x_end; x++)
            {
                int32_t dx  = x - cx;
                uint32_t d2 = (uint32_t)(dx * dx) + dy2;

                if (d2 < rmax2 && d2 > irmin2)
                {
                    uint32_t ring_a = 255U;
                    uint8_t in_track;
                    uint8_t in_fg;

                    if (d2 > r2)
                        ring_a = (((rmax2 - d2) * inv_out) + 16384U) >> 15;
                    else if (d2 < ir2)
                        ring_a = (((d2 - irmin2) * inv_in) + 16384U) >> 15;

                    in_track = is_full ? 1U
                             : (track_large ? ((cp_s < 0) || (cp_te > 0))
                                            : ((cp_s < 0) && (cp_te > 0)));
                    in_fg = has_fg ? (fg_large ? ((cp_s < 0) || (cp_fe > 0))
                                               : ((cp_s < 0) && (cp_fe > 0)))
                                   : 0U;

                    if (in_track || in_fg)
                    {
                        uint32_t a = (ring_a * eff_opa) >> 8;
                        if (a > 0U)
                            we_store_blended_color(p_dst,
                                                   in_fg ? obj->value_color
                                                         : obj->track_color,
                                                   (uint8_t)a);
                    }
                }

                p_dst++;
                cp_s  += v_s_y;
                cp_te += v_te_y;
                cp_fe += v_fe_y;
            }
            dst_line += lcd->pfb_width;
        }

        /* ---- 拖拽端点小圆：位于弧带中线、value 弧末端角度处 ---- */
        if (obj->dot_r > 0U)
        {
            int32_t rm = ((r + ir) >> 1); /* 弧带中线半径 */
            int32_t vx = rm * we_cos((int16_t)fe_ang);
            int32_t vy = rm * we_sin((int16_t)fe_ang);
            int16_t dcx = (int16_t)(cx + (vx >= 0 ? ((vx + 16384) >> 15)
                                                  : -((-vx + 16384) >> 15)));
            int16_t dcy = (int16_t)(cy + (vy >= 0 ? ((vy + 16384) >> 15)
                                                  : -((-vy + 16384) >> 15)));
            colour_t dc = obj->pressed
                          ? we_colour_blend(_knob_white, obj->dot_color,
                                            (uint8_t)_KNOB_PRESS_LIGHTEN)
                          : obj->dot_color;

            /* round_rect 退化实心抗锯齿圆（原语内部自行消费 opa_scale） */
            we_draw_round_rect_analytic_fill(lcd,
                                             (int16_t)(dcx - obj->dot_r),
                                             (int16_t)(dcy - obj->dot_r),
                                             (uint16_t)(obj->dot_r * 2U),
                                             (uint16_t)(obj->dot_r * 2U),
                                             (uint16_t)obj->dot_r,
                                             dc, obj->opacity);
        }
    }
}

/* --------------------------------------------------------------------------
 * 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调：PRESSED/STAY 拖拽解算角度改值，CLICKED 点击定位。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 表示事件已消费，0 表示穿透。
 */
static uint8_t _knob_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_knob_obj_t *obj = (we_knob_obj_t *)ptr;

    if (obj == NULL)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed = 1U;
        we_obj_invalidate((we_obj_t *)obj); /* 端点增亮反馈 */
        if (data != NULL)
            _knob_apply_point(obj, data->x, data->y);
        return 1U;

    case WE_EVENT_STAY:
        if (obj->pressed && data != NULL)
            _knob_apply_point(obj, data->x, data->y);
        return 1U;

    case WE_EVENT_RELEASED:
        obj->pressed = 0U;
        we_obj_invalidate((we_obj_t *)obj);
        return 1U;

    case WE_EVENT_CLICKED:
        obj->pressed = 0U;
        if (data != NULL)
            _knob_apply_point(obj, data->x, data->y);
        we_obj_invalidate((we_obj_t *)obj);
        return 1U;

    default:
        return 0U; /* 滑动手势等不处理，穿透 */
    }
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_knob_obj_init(we_knob_obj_t *obj, we_lcd_t *lcd,
                      int16_t x, int16_t y, uint16_t size)
{
    uint16_t th;

    if (obj == NULL || lcd == NULL || size < 24U)
        return;

    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)size;
    obj->base.h       = (int16_t)size;
    obj->base.class_p = &_knob_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    obj->start_angle = WE_KNOB_DEF_START;
    obj->sweep       = WE_KNOB_DEF_SWEEP;

    /* 几何：外半径留 3px（端点小圆外凸 + AA），弧厚 ≈ size/8（钳 6..24） */
    obj->radius = (uint16_t)(size / 2U - 3U);
    th = size / 8U;
    if (th < 6U)
        th = 6U;
    if (th > 24U)
        th = 24U;
    if (th >= obj->radius)
        th = (uint16_t)(obj->radius - 1U);
    obj->thickness = (uint8_t)th;
    obj->dot_r     = (uint8_t)(th / 2U + 2U); /* 外凸 2px，仍在包围盒内 */

    obj->v_min = 0;
    obj->v_max = 100;
    obj->value = 0;

    {
        colour_t track = RGB888_CONST(WE_KNOB_TRACK_R, WE_KNOB_TRACK_G, WE_KNOB_TRACK_B);
        colour_t value = RGB888_CONST(WE_KNOB_VALUE_R, WE_KNOB_VALUE_G, WE_KNOB_VALUE_B);
        colour_t dot   = RGB888_CONST(WE_KNOB_DOT_R8, WE_KNOB_DOT_G8, WE_KNOB_DOT_B8);
        obj->track_color = track;
        obj->value_color = value;
        obj->dot_color   = dot;
    }
    obj->opacity    = 255U;
    obj->pressed    = 0U;
    obj->changed_cb = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_knob_set_range(we_knob_obj_t *obj, int32_t v_min, int32_t v_max)
{
    if (obj == NULL || v_max <= v_min)
        return;
    if (obj->v_min == v_min && obj->v_max == v_max)
        return;

    obj->v_min = v_min;
    obj->v_max = v_max;
    obj->value = _knob_clamp(obj, obj->value); /* 程序侧调整，不触发回调 */
    we_obj_invalidate((we_obj_t *)obj);
}

void we_knob_set_value(we_knob_obj_t *obj, int32_t value)
{
    if (obj == NULL)
        return;
    _knob_apply_value(obj, value, 0U); /* 程序设置：不触发 changed_cb */
}

int32_t we_knob_get_value(const we_knob_obj_t *obj)
{
    return (obj == NULL) ? 0 : obj->value;
}

void we_knob_set_changed_cb(we_knob_obj_t *obj, we_knob_changed_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->changed_cb = cb;
}

void we_knob_set_colors(we_knob_obj_t *obj, colour_t track_color,
                        colour_t value_color, colour_t dot_color)
{
    if (obj == NULL)
        return;
    if (_knob_col_eq(obj->track_color, track_color) &&
        _knob_col_eq(obj->value_color, value_color) &&
        _knob_col_eq(obj->dot_color, dot_color))
        return;

    obj->track_color = track_color;
    obj->value_color = value_color;
    obj->dot_color   = dot_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_knob_set_opacity(we_knob_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_knob_obj_delete(we_knob_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* knob 无动画节点（值由用户拖动直接驱动），直接摘除对象即可 */
    we_obj_delete((we_obj_t *)obj);
}
