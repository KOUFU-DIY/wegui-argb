/**
 * @file  we_widget_joystick.c
 * @brief 虚拟摇杆控件（joystick）实现 —— preview 孵化区
 *
 * 渲染全部复用既有原语：底盘大圆 / 摇杆头 =
 * we_draw_round_rect_analytic_fill 的退化实心圆用法（w=h=直径、
 * radius=半径），十字准线 = 两条 we_draw_line_round 细线。
 * 触点限幅用平方比较判越界，越界时以整数 sqrt 求模长后按比例缩回
 * 行程圆周；松手回中由中央动画节点驱动（we_ease_out_back 轻微过冲），
 * 全程整数运算，控件自身零浮点、零 malloc。
 */

#include "we_widget_joystick.h"
#include "we_render.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _js_draw_cb(void *ptr);
static uint8_t _js_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _joystick_class = {
    .draw_cb    = _js_draw_cb,
    .event_cb   = _js_event_cb,
    .set_pos_cb = NULL /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

static const colour_t _js_white = RGB888_CONST(255, 255, 255);

/* 按压时摇杆头向白色增亮的混合 alpha */
#define _JS_PRESS_LIGHTEN 60U

/* 十字准线距底盘边缘的内缩量（像素） */
#define _JS_CROSS_INSET 4

/* --------------------------------------------------------------------------
 * 内部工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（setter 幂等判断用）。
 * @param a 颜色 A。
 * @param b 颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _js_col_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#else
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 整数平方根（向下取整），触点越界缩放求模长专用。
 * @param v 被开方数。
 * @return floor(sqrt(v))。
 * @note 逐位逼近法，32 位输入最多 16 轮迭代，无除法无浮点。
 */
static uint32_t _js_isqrt32(uint32_t v)
{
    uint32_t res = 0U;
    uint32_t bit = 1UL << 30;

    while (bit > v)
        bit >>= 2;
    while (bit != 0U)
    {
        if (v >= res + bit)
        {
            v -= res + bit;
            res = (res >> 1) + bit;
        }
        else
        {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/**
 * @brief 由当前偏移重算输出矢量（含死区判定），变化时触发 changed_cb。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 矢量 = 偏移 × 127 / travel（四舍五入），死区（平方比较）内输出 0；
 *       偏移已限幅在 travel 内，矢量天然落在 -127..127，仍做保护钳制。
 */
static void _js_update_vector(we_joystick_obj_t *obj)
{
    int32_t dead_r = ((int32_t)obj->travel * obj->deadzone_pct) / 100;
    int32_t len2 = (int32_t)obj->ofs_x * obj->ofs_x +
                   (int32_t)obj->ofs_y * obj->ofs_y;
    int32_t vx = 0;
    int32_t vy = 0;

    if (len2 > dead_r * dead_r && obj->travel > 0U)
    {
        int32_t tr = (int32_t)obj->travel;
        int32_t half = tr / 2;
        /* 四舍五入（加符号同向的半分母），行程端点精确映射到 ±127 */
        vx = ((int32_t)obj->ofs_x * 127 + (obj->ofs_x >= 0 ? half : -half)) / tr;
        vy = ((int32_t)obj->ofs_y * 127 + (obj->ofs_y >= 0 ? half : -half)) / tr;
        if (vx > 127)
            vx = 127;
        if (vx < -127)
            vx = -127;
        if (vy > 127)
            vy = 127;
        if (vy < -127)
            vy = -127;
    }

    if ((int8_t)vx == obj->vec_x && (int8_t)vy == obj->vec_y)
        return;

    obj->vec_x = (int8_t)vx;
    obj->vec_y = (int8_t)vy;
    if (obj->changed_cb != NULL)
        obj->changed_cb(obj, obj->vec_x, obj->vec_y);
}

/**
 * @brief 应用新的摇杆头偏移：幂等判断、更新矢量、整控件标脏。
 * @param obj 控件对象指针。
 * @param nx 新偏移 X（像素，相对底盘中心，调用方保证已限幅）。
 * @param ny 新偏移 Y（像素）。
 * @return 无。
 */
static void _js_apply_offset(we_joystick_obj_t *obj, int16_t nx, int16_t ny)
{
    if (nx == obj->ofs_x && ny == obj->ofs_y)
        return;

    obj->ofs_x = nx;
    obj->ofs_y = ny;
    _js_update_vector(obj);
    we_obj_invalidate((we_obj_t *)obj); /* preview：整包围盒标脏 */
}

/**
 * @brief 由触点屏幕坐标解算限幅后的摇杆头偏移（用户拖动路径）。
 * @param obj 控件对象指针。
 * @param x 触点 X（屏幕绝对坐标，允许拖出控件包围盒）。
 * @param y 触点 Y。
 * @return 无。
 * @note 越界判定用平方比较（无开方快速路径）；确实越界时用整数 sqrt
 *       求模长并按 travel/len 整数缩放回圆周。
 */
static void _js_apply_point(we_joystick_obj_t *obj, int16_t x, int16_t y)
{
    int32_t cx = obj->base.x + obj->base.w / 2;
    int32_t cy = obj->base.y + obj->base.h / 2;
    int32_t dx = (int32_t)x - cx;
    int32_t dy = (int32_t)y - cy;
    int32_t tr = (int32_t)obj->travel;
    int32_t len2 = dx * dx + dy * dy;

    if (len2 > tr * tr)
    {
        /* 越界：缩放回行程圆周（len 非 0 由 len2 > tr^2 >= 0 保证） */
        int32_t len = (int32_t)_js_isqrt32((uint32_t)len2);
        dx = (dx * tr) / len;
        dy = (dy * tr) / len;
        /* isqrt 向下取整会导致缩放结果偶尔溢出 1px，二次夹紧 */
        if (dx > tr)
            dx = tr;
        if (dx < -tr)
            dx = -tr;
        if (dy > tr)
            dy = tr;
        if (dy < -tr)
            dy = -tr;
    }
    _js_apply_offset(obj, (int16_t)dx, (int16_t)dy);
}

/* --------------------------------------------------------------------------
 * 松手回中动画（中央动画引擎）
 * -------------------------------------------------------------------------- */

/**
 * @brief 中央动画引擎回调：推进弹性回中（we_ease_out_back 轻微过冲）。
 * @param owner 控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 * @note 到达中心后自行摘链；回中过程中矢量持续衰减并触发 changed_cb。
 */
static void _js_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_joystick_obj_t *obj = (we_joystick_obj_t *)owner;
    uint16_t t;
    uint16_t e;

    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (elapsed_ms > 128U)
        elapsed_ms = 128U; /* 主循环长卡顿限幅，防一步跳空 */

    obj->ret_acc = (uint16_t)(obj->ret_acc + elapsed_ms);
    t = (uint16_t)(((uint32_t)obj->ret_acc * 256U) / WE_JOYSTICK_RETURN_MS);

    if (t >= 256U)
    {
        /* 动画结束：精确归零 + 摘链停表 */
        _js_apply_offset(obj, 0, 0);
        we_anim_stop(obj->base.lcd, &obj->anim);
        return;
    }

    /* out_back 中途会超过 256（过冲），we_lerp 支持过冲插值 */
    e = we_ease_out_back(t);
    _js_apply_offset(obj,
                     (int16_t)we_lerp(obj->ret_from_x, 0, e),
                     (int16_t)we_lerp(obj->ret_from_y, 0, e));
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：底盘大圆 + 十字准线 + 摇杆头实心圆。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 圆一律用 we_draw_round_rect_analytic_fill 退化用法
 *       （w = h = 直径、radius = 半径），原语内部自带 PFB 裁剪与
 *       opa_scale 级联消费。
 */
static void _js_draw_cb(void *ptr)
{
    we_joystick_obj_t *obj = (we_joystick_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t cx;
    int16_t cy;
    int16_t base_r;
    int16_t cross_len;
    colour_t knob_c;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->base.w <= 0 || obj->base.h <= 0)
        return;

    cx = (int16_t)(obj->base.x + obj->base.w / 2);
    cy = (int16_t)(obj->base.y + obj->base.h / 2);
    base_r = (int16_t)(obj->base.w / 2);

    /* 1. 底盘大圆（暗色托盘） */
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     (uint16_t)base_r, obj->base_color, obj->opacity);

    /* 2. 中心十字准线：过中心的水平/垂直两条 1px 细线（留边缘内缩） */
    cross_len = (int16_t)(base_r - _JS_CROSS_INSET);
    if (cross_len > 0)
    {
        we_draw_line_round(lcd, (int16_t)(cx - cross_len), cy,
                           (int16_t)(cx + cross_len), cy, 1U,
                           obj->cross_color, obj->opacity);
        we_draw_line_round(lcd, cx, (int16_t)(cy - cross_len),
                           cx, (int16_t)(cy + cross_len), 1U,
                           obj->cross_color, obj->opacity);
    }

    /* 3. 摇杆头实心圆：当前偏移位置，按压时向白色增亮 */
    knob_c = obj->pressed
             ? we_colour_blend(_js_white, obj->knob_color, (uint8_t)_JS_PRESS_LIGHTEN)
             : obj->knob_color;
    we_draw_round_rect_analytic_fill(lcd,
                                     (int16_t)(cx + obj->ofs_x - (int16_t)obj->knob_r),
                                     (int16_t)(cy + obj->ofs_y - (int16_t)obj->knob_r),
                                     (uint16_t)(obj->knob_r * 2U),
                                     (uint16_t)(obj->knob_r * 2U),
                                     obj->knob_r, knob_c, obj->opacity);
}

/* --------------------------------------------------------------------------
 * 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调：底盘圆内按下接管拖动，松手弹性回中。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 表示事件已消费，0 表示穿透（圆外按下 / 未接管态）。
 */
static uint8_t _js_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_joystick_obj_t *obj = (we_joystick_obj_t *)ptr;

    if (obj == NULL)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        if (data != NULL)
        {
            /* 命中检测：触点须落在底盘圆内（平方比较，包围盒四角不接管） */
            int32_t cx = obj->base.x + obj->base.w / 2;
            int32_t cy = obj->base.y + obj->base.h / 2;
            int32_t dx = (int32_t)data->x - cx;
            int32_t dy = (int32_t)data->y - cy;
            int32_t r = obj->base.w / 2;

            if (dx * dx + dy * dy > r * r)
                return 0U; /* 圆外：不接管 */

            obj->pressed = 1U;
            we_anim_stop(obj->base.lcd, &obj->anim); /* 打断回中动画 */
            _js_apply_point(obj, data->x, data->y);
            we_obj_invalidate((we_obj_t *)obj); /* 摇杆头增亮反馈 */
        }
        return 1U;

    case WE_EVENT_STAY:
        if (obj->pressed && data != NULL)
            _js_apply_point(obj, data->x, data->y);
        return 1U;

    case WE_EVENT_RELEASED:
    case WE_EVENT_CLICKED:
        if (obj->pressed)
        {
            obj->pressed = 0U;
            /* 弹性回中：记录起点偏移，交给中央动画节点推进 */
            obj->ret_from_x = obj->ofs_x;
            obj->ret_from_y = obj->ofs_y;
            obj->ret_acc = 0U;
            if (obj->ofs_x != 0 || obj->ofs_y != 0)
                we_anim_start(obj->base.lcd, &obj->anim, _js_anim_step_cb, obj);
            we_obj_invalidate((we_obj_t *)obj); /* 取消增亮 */
        }
        return 1U;

    default:
        return 0U; /* 滑动手势等不处理，穿透 */
    }
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_joystick_obj_init(we_joystick_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t size)
{
    if (obj == NULL || lcd == NULL || size < 40U)
        return;

    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)size;
    obj->base.h       = (int16_t)size;
    obj->base.class_p = &_joystick_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    /* 几何：摇杆头直径 ≈ size/2.5（半径 = size/5），
     * 行程半径 = (size - 头径)/2，最大偏移时摇杆头恰好贴底盘边缘 */
    obj->knob_r = (uint16_t)(size / 5U);
    if (obj->knob_r < 6U)
        obj->knob_r = 6U;
    obj->travel = (uint16_t)(size / 2U - obj->knob_r);

    obj->deadzone_pct = WE_JOYSTICK_DEF_DEADZONE;
    obj->ofs_x = 0;
    obj->ofs_y = 0;
    obj->vec_x = 0;
    obj->vec_y = 0;

    {
        colour_t base_c  = RGB888_CONST(40, 48, 64);    /* 暗蓝灰底盘 */
        colour_t knob_c  = RGB888_CONST(86, 170, 255);  /* 亮青蓝摇杆头 */
        colour_t cross_c = RGB888_CONST(88, 100, 122);  /* 灰蓝准线 */
        obj->base_color  = base_c;
        obj->knob_color  = knob_c;
        obj->cross_color = cross_c;
    }

    obj->anim.next    = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner   = NULL;
    obj->ret_from_x   = 0;
    obj->ret_from_y   = 0;
    obj->ret_acc      = 0U;

    obj->changed_cb = NULL;
    obj->opacity    = 255U;
    obj->pressed    = 0U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_joystick_get_vector(const we_joystick_obj_t *obj, int8_t *dx, int8_t *dy)
{
    if (dx != NULL)
        *dx = (obj != NULL) ? obj->vec_x : 0;
    if (dy != NULL)
        *dy = (obj != NULL) ? obj->vec_y : 0;
}

void we_joystick_set_changed_cb(we_joystick_obj_t *obj, we_joystick_changed_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->changed_cb = cb;
}

void we_joystick_set_colors(we_joystick_obj_t *obj, colour_t base_color,
                            colour_t knob_color, colour_t cross_color)
{
    if (obj == NULL)
        return;
    if (_js_col_eq(obj->base_color, base_color) &&
        _js_col_eq(obj->knob_color, knob_color) &&
        _js_col_eq(obj->cross_color, cross_color))
        return;

    obj->base_color  = base_color;
    obj->knob_color  = knob_color;
    obj->cross_color = cross_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_joystick_set_deadzone(we_joystick_obj_t *obj, uint8_t pct)
{
    if (obj == NULL)
        return;
    if (pct > 90U)
        pct = 90U;
    if (obj->deadzone_pct == pct)
        return;
    obj->deadzone_pct = pct;
    /* 死区口径变化只影响输出矢量，摇杆头位置不变，无需重绘 */
    _js_update_vector(obj);
}

void we_joystick_set_opacity(we_joystick_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_joystick_obj_delete(we_joystick_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    we_anim_stop(obj->base.lcd, &obj->anim); /* 节点归控件所有，删除前必须摘链 */
    we_obj_delete((we_obj_t *)obj);
}
