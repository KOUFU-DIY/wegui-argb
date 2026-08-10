/**
 * @file  we_widget_gauge.c
 * @brief 仪表盘控件（gauge）实现
 *
 * 刻度与指针全部复用 we_draw_line_round（圆头抗锯齿线），中心帽复用
 * we_draw_round_rect_analytic_fill 退化的实心抗锯齿圆，不新增渲染图元。
 * 数值扫动经单个中央动画节点推进（不占 GUI timer 槽），全程整数运算。
 *
 * 实现要点：
 *   1. 指针差分标脏：数值变化只提交"旧指针位形"与"新指针位形"两块
 *      包围盒（各自并入中心帽），静态刻度区零重绘（_gauge_invalidate_pointer）；
 *   2. 刻度几何缓存：主刻度内外端点偏移在 init/set_tick_count 时一次算好，
 *      draw 内零 we_cos/we_sin、零乘除（_gauge_rebuild_ticks）；
 *   3. Q16 量程斜率：set_range 时预除 slope_q16，value→角度只乘 + 移位
 *      （_gauge_update_slope，大跨度自动回退除法保精度）；
 *   4. 极小表盘护栏：min(w,h) < WE_GAUGE_SMALL_SIZE 时按最外元素 AA 晕圈
 *      钳缩外半径，抗锯齿不越出控件包围盒（_gauge_geometry）。
 */

#include "we_widget_gauge.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _gauge_draw_cb(void *ptr);
static uint8_t _gauge_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _gauge_class = {
    .draw_cb    = _gauge_draw_cb,
    .event_cb   = _gauge_event_cb,
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
static uint8_t _gauge_col_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#else
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 将数值钳制到量程内。
 * @param obj 控件对象指针。
 * @param v 输入值。
 * @return 钳制后的值。
 */
static int32_t _gauge_clamp(const we_gauge_obj_t *obj, int32_t v)
{
    if (v < obj->v_min)
        return obj->v_min;
    if (v > obj->v_max)
        return obj->v_max;
    return v;
}

/**
 * @brief 重算量程映射的 Q16 预除斜率（init / set_range 时调用一次）。
 * @param obj 控件对象指针。
 * @return 无。
 * @note slope_q16 = round((sweep << 16) / span)。仅在 span <= 65536 时启用：
 *       此时求值端量化误差 <= 1 角度步（约 0.7°，不可见），且正向扫角下
 *       端点 v_max 精确落在 start + sweep（证明见 _gauge_value_to_angle）；
 *       更大跨度置 0，_gauge_value_to_angle 自动回退每次除法保精度。
 *       溢出边界：|sweep| <= 512 → |sweep<<16| <= 2^25，加 span/2 <= 2^15
 *       后仍远小于 int32 上限，除法前中间量安全。
 */
static void _gauge_update_slope(we_gauge_obj_t *obj)
{
    int32_t span = obj->v_max - obj->v_min;

    if (span > 0 && span <= 65536L)
        obj->slope_q16 = ((int32_t)obj->sweep * 65536L + span / 2) / span; /* 乘法而非左移：负 sweep 覆盖宏下左移是 UB */
    else
        obj->slope_q16 = 0;
}

/**
 * @brief 将量程内数值线性映射为 512 步制绝对角度。
 * @param obj 控件对象指针。
 * @param v 数值（应已钳制在量程内）。
 * @return 对应的 512 步制角度。
 * @note Q16 快路径（span <= 65536）：只乘 + 移位，无除法。
 *       溢出边界：0 <= (v - v_min) <= span，|(v-v_min)*slope_q16| <=
 *       |sweep<<16| + span/2 < 2^26，加 32768 四舍五入后仍远小于 2^31，
 *       int32 全程安全（无需 int64）。端点精确性：v = v_max 时乘积 =
 *       sweep<<16 + e（|e| <= span/2 <= 32768 且取不到 +32768 的整除临界），
 *       (+32768)>>16 后恰为 sweep，正向扫角满量程不缺步。
 *       大跨度回退除法：|sweep| <= 512、span < 2^22 时
 *       sweep*(v-v_min) < 2^31 不溢出（量程跨度上限见头文件说明）。
 */
static int16_t _gauge_value_to_angle(const we_gauge_obj_t *obj, int32_t v)
{
    int32_t span = obj->v_max - obj->v_min;

    if (span <= 0)
        return obj->start_angle;
    if (obj->slope_q16 != 0)
        return (int16_t)(obj->start_angle +
                         (((v - obj->v_min) * obj->slope_q16 + 32768L) >> 16));
    return (int16_t)(obj->start_angle + ((int32_t)obj->sweep * (v - obj->v_min)) / span);
}

/**
 * @brief 由圆心/半径/角度计算圆周点屏幕坐标（Q15 三角，四舍五入）。
 * @param cx 圆心 X。
 * @param cy 圆心 Y。
 * @param r 半径（像素）。
 * @param angle 512 步制角度。
 * @param out_x 传出：圆周点 X。
 * @param out_y 传出：圆周点 Y。
 * @return 无。
 */
static void _gauge_polar(int16_t cx, int16_t cy, int32_t r, int16_t angle,
                         int16_t *out_x, int16_t *out_y)
{
    int32_t vx = r * (int32_t)we_cos(angle);
    int32_t vy = r * (int32_t)we_sin(angle);

    /* Q15 -> 整数，负数同样四舍五入（与 arc 控件同口径） */
    *out_x = (int16_t)(cx + (vx >= 0 ? ((vx + 16384) >> 15) : -((-vx + 16384) >> 15)));
    *out_y = (int16_t)(cy + (vy >= 0 ? ((vy + 16384) >> 15) : -((-vy + 16384) >> 15)));
}

/**
 * @brief 计算表盘中心与外半径（draw / 刻度缓存 / 差分标脏共用同一口径）。
 * @param obj 控件对象指针。
 * @param out_cx 传出：表盘中心 X（屏幕绝对坐标）。
 * @param out_cy 传出：表盘中心 Y。
 * @return 外半径 R（像素）；<= 4 时调用方应放弃绘制/标脏。
 * @note 常规尺寸：R = min(w,h)/2 - 2（2px 抗锯齿羽化余量，覆盖默认
 *       刻度线宽 2 的有效像素晕圈：半宽 1 + 羽化 1）。极小表盘护栏：
 *       min(w,h) < WE_GAUGE_SMALL_SIZE 时按最外元素（外端点落在 R 上的
 *       圆头刻度线，指针线宽更大时用指针兜底）的有效像素晕圈
 *       线宽/2 + 1px 羽化 + 1px 取整余量钳缩 R，保证 AA 不越出包围盒。
 *       指针端点在 0.72R、中心帽半径 <= R-1（由 R > 4 保证），恒更安全。
 */
static int16_t _gauge_geometry(const we_gauge_obj_t *obj,
                               int16_t *out_cx, int16_t *out_cy)
{
    int16_t size = (int16_t)WE_MIN(obj->base.w, obj->base.h);
    int16_t r = (int16_t)(size / 2 - 2);

    *out_cx = (int16_t)(obj->base.x + obj->base.w / 2);
    *out_cy = (int16_t)(obj->base.y + obj->base.h / 2);

    if (size < (int16_t)WE_GAUGE_SMALL_SIZE)
    {
        int16_t line_w = (int16_t)WE_MAX((int16_t)WE_GAUGE_TICK_W,
                                         (int16_t)obj->pointer_w);
        int16_t r_safe = (int16_t)(size / 2 - (line_w / 2 + 2));

        if (r_safe < r)
            r = r_safe;
    }
    return r;
}

/**
 * @brief 重建主刻度几何缓存：端点相对表盘中心的偏移（init/set 时一次算好）。
 * @param obj 控件对象指针（tick_cnt 已被写入口钳制 <= WE_GAUGE_TICK_MAX）。
 * @return 无。
 * @note draw 内直接 中心 + 偏移，零 we_cos/we_sin、零乘除；
 *       偏移相对中心存储，we_obj_set_pos 移动控件无需重算。
 */
static void _gauge_rebuild_ticks(we_gauge_obj_t *obj)
{
    int16_t cx;
    int16_t cy;
    int16_t r = _gauge_geometry(obj, &cx, &cy);
    int16_t r_in;
    int16_t angle;
    uint8_t i;

    if (r < 1)
        r = 1; /* 退化尺寸也保持缓存有定义（draw 侧另有 r<=4 拦截） */
    r_in = (int16_t)(r - (int16_t)obj->tick_len);
    if (r_in < 1)
        r_in = 1;

    for (i = 0U; i < obj->tick_cnt; i++)
    {
        if (obj->tick_cnt == 1U)
            angle = (int16_t)(obj->start_angle + obj->sweep / 2); /* 单刻度画在扫角中点 */
        else
            angle = (int16_t)(obj->start_angle +
                              ((int32_t)obj->sweep * i) / (int32_t)(obj->tick_cnt - 1U));

        _gauge_polar(0, 0, r, angle, &obj->tick_ox[i], &obj->tick_oy[i]);
        _gauge_polar(0, 0, r_in, angle, &obj->tick_ix[i], &obj->tick_iy[i]);
    }
}

/**
 * @brief 提交一块钳制到控件矩形内的脏区。
 * @param obj 控件对象指针。
 * @param x0 脏区左上角 X（屏幕绝对坐标，闭区间）。
 * @param y0 脏区左上角 Y。
 * @param x1 脏区右下角 X（闭区间）。
 * @param y1 脏区右下角 Y。
 * @return 无。
 * @note 有效像素恒在控件矩形内（_gauge_geometry 护栏保证），钳制不丢像素。
 */
static void _gauge_submit_dirty(we_gauge_obj_t *obj,
                                int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    x0 = (int16_t)WE_MAX(x0, obj->base.x);
    y0 = (int16_t)WE_MAX(y0, obj->base.y);
    x1 = (int16_t)WE_MIN(x1, (int16_t)(obj->base.x + obj->base.w - 1));
    y1 = (int16_t)WE_MIN(y1, (int16_t)(obj->base.y + obj->base.h - 1));
    if (x0 <= x1 && y0 <= y1)
        we_obj_invalidate_area((we_obj_t *)obj, x0, y0,
                               (int16_t)(x1 - x0 + 1), (int16_t)(y1 - y0 + 1));
}

/**
 * @brief 指针差分标脏：只标"旧指针位形"与"新指针位形"两块包围盒。
 * @param obj 控件对象指针。
 * @param old_v 变化前的显示值（已钳制在量程内）。
 * @param new_v 变化后的显示值（已钳制在量程内）。
 * @return 无。
 * @note 单块包围盒 = 圆心与指针端点的 min/max 矩形，向外扩
 *       max(指针半宽 + 2, 帽半径 + 1)：既包住指针胶囊的有效像素与 1px 羽化，
 *       也包住中心帽的 AA 边缘（帽压在指针根部上，指针换向时帽边缘
 *       混色随之变化）。旧/新两块分别提交，不合成大盒——扫动小步进时
 *       两块高度重叠可被脏矩形管理器合并，大步进时避免标脏中间无关区。
 *       新旧角度相同（大量程小步进）时画面无变化，直接零提交。
 */
static void _gauge_invalidate_pointer(we_gauge_obj_t *obj, int32_t old_v, int32_t new_v)
{
    int16_t cx;
    int16_t cy;
    int16_t r;
    int16_t a_old;
    int16_t a_new;
    int32_t r_ptr;
    int16_t pad;
    int16_t tx;
    int16_t ty;

    if (obj->opacity == 0U)
        return;
    r = _gauge_geometry(obj, &cx, &cy);
    if (r <= 4)
        return; /* 与 draw 同口径：不画就不标 */

    a_old = _gauge_value_to_angle(obj, old_v);
    a_new = _gauge_value_to_angle(obj, new_v);
    if (a_old == a_new)
        return; /* 角度量化后指针未动，像素无变化 */

    r_ptr = ((int32_t)r * WE_GAUGE_PTR_LEN_Q8) >> 8;
    {
        int16_t pad_ptr = (int16_t)(obj->pointer_w / 2U + 2U);
        int16_t pad_cap = (int16_t)(obj->cap_w / 2U + 1U);
        pad = (int16_t)WE_MAX(pad_ptr, pad_cap);
    }

    _gauge_polar(cx, cy, r_ptr, a_old, &tx, &ty);
    _gauge_submit_dirty(obj,
                        (int16_t)(WE_MIN(cx, tx) - pad), (int16_t)(WE_MIN(cy, ty) - pad),
                        (int16_t)(WE_MAX(cx, tx) + pad), (int16_t)(WE_MAX(cy, ty) + pad));

    _gauge_polar(cx, cy, r_ptr, a_new, &tx, &ty);
    _gauge_submit_dirty(obj,
                        (int16_t)(WE_MIN(cx, tx) - pad), (int16_t)(WE_MIN(cy, ty) - pad),
                        (int16_t)(WE_MAX(cx, tx) + pad), (int16_t)(WE_MAX(cy, ty) + pad));
}

/* --------------------------------------------------------------------------
 * 绘制 / 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：主刻度 -> 指针 -> 中心圆帽。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 刻度端点直接取缓存偏移（中心 + 偏移），本回调内零三角函数；
 *       仅指针端点按当前显示值实时算一次极坐标。
 */
static void _gauge_draw_cb(void *ptr)
{
    we_gauge_obj_t *obj = (we_gauge_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t cx;
    int16_t cy;
    int16_t r;
    int16_t angle;
    int16_t x0;
    int16_t y0;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    /* 表盘中心与外半径（含极小表盘护栏，与标脏/缓存同口径） */
    r = _gauge_geometry(obj, &cx, &cy);
    if (r <= 4)
        return;

    /* ---- 主刻度：外端在 R、内端向心收 tick_len 的圆头短线（几何走缓存） ---- */
    if (obj->tick_cnt > 0U && obj->tick_len > 0U)
    {
        uint8_t i;

        for (i = 0U; i < obj->tick_cnt; i++)
        {
            we_draw_line_round(lcd,
                               (int16_t)(cx + obj->tick_ox[i]), (int16_t)(cy + obj->tick_oy[i]),
                               (int16_t)(cx + obj->tick_ix[i]), (int16_t)(cy + obj->tick_iy[i]),
                               (uint8_t)WE_GAUGE_TICK_W, obj->tick_color, obj->opacity);
        }
    }

    /* ---- 指针：中心指向 0.72R 的圆头粗线 ---- */
    angle = _gauge_value_to_angle(obj, _gauge_clamp(obj, obj->disp_value));
    _gauge_polar(cx, cy, ((int32_t)r * WE_GAUGE_PTR_LEN_Q8) >> 8, angle, &x0, &y0);
    we_draw_line_round(lcd, cx, cy, x0, y0, obj->pointer_w,
                       obj->pointer_color, obj->opacity);

    /* ---- 中心圆帽：round_rect 退化为实心抗锯齿圆（w=h=直径，radius=半径） ---- */
    if (obj->cap_w >= 2U)
    {
        int16_t half = (int16_t)(obj->cap_w / 2U);
        we_draw_round_rect_analytic_fill(lcd, (int16_t)(cx - half), (int16_t)(cy - half),
                                         obj->cap_w, obj->cap_w, (uint16_t)half,
                                         obj->pointer_color, obj->opacity);
    }
}

/**
 * @brief 控件事件回调：装饰性控件，不消费事件，输入穿透给背后控件。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒为 0（穿透）。
 */
static uint8_t _gauge_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

/* --------------------------------------------------------------------------
 * 扫动动画（中央动画引擎节点）
 * -------------------------------------------------------------------------- */

/**
 * @brief 推进一步扫动动画：Q8 进度 -> 缓动 -> we_lerp 更新显示值。
 * @param obj 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _gauge_anim_step(we_gauge_obj_t *obj, uint16_t elapsed_ms)
{
    uint32_t delta;
    uint16_t eased;
    int32_t new_disp;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    if (obj->anim_t >= 256U)
    {
        we_anim_stop(obj->base.lcd, &obj->anim); /* 已就位：摘链停表 */
        return;
    }

    if (obj->anim_ms == 0U)
    {
        obj->anim_t = 256U; /* 零时长直接到位，避免除零 */
    }
    else
    {
        delta = (uint32_t)elapsed_ms * 256U / (uint32_t)obj->anim_ms;
        if (delta == 0U)
            delta = 1U; /* 保证慢主循环下也能缓慢前进 */
        obj->anim_t = ((uint32_t)obj->anim_t + delta >= 256U)
                      ? 256U : (uint16_t)(obj->anim_t + delta);
    }

    eased = obj->ease ? obj->ease(obj->anim_t) : obj->anim_t;
    if (eased > 256U)
        eased = 256U; /* 表针不过冲出量程（back 类缓动钳制在端点） */

    new_disp = (obj->anim_t >= 256U) ? obj->v_to
                                     : we_lerp(obj->v_from, obj->v_to, eased);

    if (obj->anim_t >= 256U)
        we_anim_stop(obj->base.lcd, &obj->anim); /* 本步到位，摘链停表 */

    if (new_disp != obj->disp_value)
    {
        int32_t old_disp = obj->disp_value;

        obj->disp_value = new_disp;
        _gauge_invalidate_pointer(obj, old_disp, new_disp); /* 差分标脏：刻度静态区零重绘 */
    }
}

/**
 * @brief 中央动画引擎回调壳（owner 透传控件实例）。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 */
static void _gauge_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    _gauge_anim_step((we_gauge_obj_t *)owner, elapsed_ms);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_gauge_obj_init(we_gauge_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = w;
    obj->base.h       = h;
    obj->base.class_p = &_gauge_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    obj->start_angle = WE_GAUGE_DEF_START;
    obj->sweep       = WE_GAUGE_DEF_SWEEP;
    obj->v_min       = 0;
    obj->v_max       = 100;
    obj->value       = 0;
    obj->disp_value  = 0;

    obj->tick_cnt  = (uint8_t)WE_GAUGE_DEF_TICK_CNT;
    if (obj->tick_cnt > (uint8_t)WE_GAUGE_TICK_MAX)
        obj->tick_cnt = (uint8_t)WE_GAUGE_TICK_MAX; /* 缓存上限钳制 */
    obj->tick_len  = (uint8_t)WE_GAUGE_DEF_TICK_LEN;
    obj->pointer_w = (uint8_t)WE_GAUGE_DEF_PTR_W;
    obj->cap_w     = (uint8_t)WE_GAUGE_DEF_CAP_W;
    obj->opacity   = 255U;
    {
        colour_t tick = RGB888_CONST(WE_GAUGE_TICK_R, WE_GAUGE_TICK_G, WE_GAUGE_TICK_B);
        colour_t ptr  = RGB888_CONST(WE_GAUGE_PTR_R, WE_GAUGE_PTR_G, WE_GAUGE_PTR_B);
        obj->tick_color    = tick;
        obj->pointer_color = ptr;
    }

    obj->anim.next    = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner   = NULL;
    obj->ease         = we_ease_in_out_sine;
    obj->anim_ms      = 0U;
    obj->anim_t       = 256U; /* 空闲（无扫动在跑） */
    obj->v_from       = 0;
    obj->v_to         = 0;

    _gauge_update_slope(obj);  /* Q16 量程斜率 */
    _gauge_rebuild_ticks(obj); /* 刻度几何缓存 */

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_gauge_set_range(we_gauge_obj_t *obj, int32_t v_min, int32_t v_max)
{
    if (obj == NULL || v_max <= v_min)
        return;
    if (obj->v_min == v_min && obj->v_max == v_max)
        return;

    obj->v_min = v_min;
    obj->v_max = v_max;
    _gauge_update_slope(obj);  /* 量程变了：重算 Q16 预除斜率 */
    _gauge_rebuild_ticks(obj); /* 刻度几何与量程无关，重建保持缓存口径统一 */

    /* 打断进行中的扫动：目标值钳制到新量程，显示值同步就位，
     * 避免"显示值冻结在扫动中间量"的不一致态 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->anim_t     = 256U;
    obj->value      = _gauge_clamp(obj, obj->value);
    obj->disp_value = obj->value;
    we_obj_invalidate((we_obj_t *)obj); /* 结构性变化：整控件标脏 */
}

void we_gauge_set_value(we_gauge_obj_t *obj, int32_t value)
{
    int32_t old_disp;

    if (obj == NULL)
        return;

    value = _gauge_clamp(obj, value);
    if (obj->value == value && obj->disp_value == value)
        return;

    /* 立即就位：打断进行中的扫动 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->anim_t     = 256U;
    old_disp        = obj->disp_value;
    obj->value      = value;
    obj->disp_value = value;
    if (old_disp != value)
        _gauge_invalidate_pointer(obj, old_disp, value); /* 差分标脏：只刷指针扫过区 */
}

void we_gauge_anim_value(we_gauge_obj_t *obj, int32_t target, uint16_t dur_ms)
{
    if (obj == NULL)
        return;

    target = _gauge_clamp(obj, target);
    if (dur_ms == 0U || target == obj->disp_value)
    {
        we_gauge_set_value(obj, target); /* 零时长或已在目标位：直接就位 */
        return;
    }

    obj->value   = target;
    obj->v_from  = obj->disp_value; /* 以当前显示值为新起点，扫动中可无缝改道 */
    obj->v_to    = target;
    obj->anim_ms = dur_ms;
    obj->anim_t  = 0U;
    we_anim_start(obj->base.lcd, &obj->anim, _gauge_anim_step_cb, obj);
}

int32_t we_gauge_get_value(const we_gauge_obj_t *obj)
{
    return (obj == NULL) ? 0 : obj->value;
}

int32_t we_gauge_get_disp_value(const we_gauge_obj_t *obj)
{
    return (obj == NULL) ? 0 : obj->disp_value;
}

void we_gauge_set_colors(we_gauge_obj_t *obj, colour_t tick_color,
                         colour_t pointer_color)
{
    if (obj == NULL)
        return;
    if (_gauge_col_eq(obj->tick_color, tick_color) &&
        _gauge_col_eq(obj->pointer_color, pointer_color))
        return;

    obj->tick_color    = tick_color;
    obj->pointer_color = pointer_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_gauge_set_tick_count(we_gauge_obj_t *obj, uint8_t count)
{
    if (obj == NULL)
        return;
    if (count > (uint8_t)WE_GAUGE_TICK_MAX)
        count = (uint8_t)WE_GAUGE_TICK_MAX; /* 先钳制再比较，保证幂等短路准确 */
    if (obj->tick_cnt == count)
        return;
    obj->tick_cnt = count;
    _gauge_rebuild_ticks(obj);
    we_obj_invalidate((we_obj_t *)obj); /* 结构性变化：整控件标脏 */
}

void we_gauge_set_ease(we_gauge_obj_t *obj, we_ease_fn_t ease)
{
    if (obj == NULL)
        return;
    obj->ease = (ease != NULL) ? ease : we_ease_linear;
}

void we_gauge_set_opacity(we_gauge_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_gauge_obj_delete(we_gauge_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* 动画节点归控件所有，删除前必须摘链，否则中央动画链表留悬空指针 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    we_obj_delete((we_obj_t *)obj);
}
