/**
 * @file  we_widget_colorwheel.c
 * @brief HSV 色轮控件（colorwheel）实现 —— preview 孵化区
 *
 * 渲染：draw_cb 逐像素扫描自身 bbox 与当前 PFB 条带的交集（裁剪套路照
 * box 角落合成函数），d² 整数比较判环带、八分区 atan2 近似求角度、
 * 六段整数插值求 Hue→RGB，直写 pfb_gram；环带内外边缘 1px 用 d² 线性
 * 渐隐做简易 AA。标记点走公共解析圆点原语。
 *
 * 交互：PRESSED/STAY 把触点向量转角度更新 hue，值变触发回调 + 整体标脏。
 * 全程整数，无查表 atan2 的多项式近似误差 < 1 步（1/512 圈），选色无感。
 */

#include "we_widget_colorwheel.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _cw_draw_cb(void *ptr);
static uint8_t _cw_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void    _cw_set_pos_cb(void *ptr, int16_t x, int16_t y);

static const we_class_t _cw_class = {
    .draw_cb    = _cw_draw_cb,
    .event_cb   = _cw_event_cb,
    .set_pos_cb = _cw_set_pos_cb
};

/* --------------------------------------------------------------------------
 * 整数三角/色彩工具
 * -------------------------------------------------------------------------- */

/**
 * @brief 八分区内 atan 近似：Q8 正切比值 → 0..64（512 步制的 0..45°）。
 * @param z 传入：Q8 正切比值（0..256，= 短边<<8 / 长边）。
 * @return 角度（512 步制单位，0..64）。
 * @note 多项式 atan(z) ≈ z·(π/4) + 0.273·z·(1-z)，整数化后误差 < 0.5 步。
 */
static int32_t _cw_atan_oct(int32_t z)
{
    /* 64·z + 22.25·z·(1-z)，其中 22.25 = 89/4，全部 Q8 定点 */
    int32_t t = 64 * z + ((89 * z * (256 - z)) >> 10);
    return (t + 128) >> 8;
}

/**
 * @brief 整数 atan2：屏幕向量 → 512 步制角度。
 * @param dy 传入：Y 分量（屏幕 Y 向下为正）。
 * @param dx 传入：X 分量。
 * @return 角度 0..511（0 = +X，128 = 正下方，顺时针增）。
 * @note 零向量返回 0；先按 |dx|/|dy| 折进八分区求 0..128，再按象限展开。
 */
static uint16_t _cw_atan2_512(int32_t dy, int32_t dx)
{
    int32_t ax = (dx < 0) ? -dx : dx;
    int32_t ay = (dy < 0) ? -dy : dy;
    int32_t base;
    int32_t a;

    if (ax == 0 && ay == 0)
        return 0U;

    if (ax >= ay)
        base = _cw_atan_oct((ay << 8) / ax);         /* 0..64 */
    else
        base = 128 - _cw_atan_oct((ax << 8) / ay);   /* 64..128 */

    if (dx >= 0)
        a = (dy >= 0) ? base : (512 - base);
    else
        a = (dy >= 0) ? (256 - base) : (256 + base);

    return (uint16_t)(a & 0x1FF);
}

/**
 * @brief 整数 Hue→RGB888（S=V=255 固定）。
 * @param hue 传入：色相（512 步制 0..511）。
 * @param r 传出：红色分量。
 * @param g 传出：绿色分量。
 * @param b 传出：蓝色分量。
 * @return 无。
 * @note 六段线性插值：h6 = hue*6，段号 = h6>>9，段内进度 = (h6&511)>>1。
 */
static void _cw_hue_to_rgb(uint16_t hue, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint16_t h6  = (uint16_t)((hue & 0x1FFU) * 6U);
    uint8_t  sec = (uint8_t)(h6 >> 9);
    uint8_t  f   = (uint8_t)((h6 & 0x1FFU) >> 1);  /* 段内进度 0..255 */

    switch (sec)
    {
    case 0:  *r = 255U;              *g = f;                 *b = 0U;                break;
    case 1:  *r = (uint8_t)(255U - f); *g = 255U;            *b = 0U;                break;
    case 2:  *r = 0U;                *g = 255U;              *b = f;                 break;
    case 3:  *r = 0U;                *g = (uint8_t)(255U - f); *b = 255U;            break;
    case 4:  *r = f;                 *g = 0U;                *b = 255U;              break;
    default: *r = 255U;              *g = 0U;                *b = (uint8_t)(255U - f); break;
    }
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：逐像素合成色环 + 标记点。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _cw_draw_cb(void *ptr)
{
    we_colorwheel_obj_t *o = (we_colorwheel_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t cx0, cy0, cx1, cy1;
    int16_t ccx, ccy;
    int32_t r_out2, r_out_f2, r_in2, r_in_f2;
    int32_t div_out, div_in;
    uint16_t stride;
    colour_t *row;
    uint8_t eff_op;
    int16_t px, py;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL || o->base.w <= 0 || o->base.h <= 0)
        return;
    eff_op = we_opa_apply(lcd, o->opacity);
    if (eff_op == 0U)
        return;

    ccx = (int16_t)(o->base.x + o->base.w / 2);
    ccy = (int16_t)(o->base.y + o->base.h / 2);

    /* 环带边界的平方值：外沿/内沿各留 1px 渐隐带 */
    r_out2   = (int32_t)o->r_out * o->r_out;
    r_out_f2 = (int32_t)(o->r_out - 1U) * (o->r_out - 1U);
    r_in2    = (int32_t)o->r_in * o->r_in;
    r_in_f2  = (int32_t)(o->r_in + 1U) * (o->r_in + 1U);
    div_out  = r_out2 - r_out_f2;   /* = 2·r_out - 1，恒 > 0 */
    div_in   = r_in_f2 - r_in2;     /* = 2·r_in + 1，恒 > 0 */

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

    stride = lcd->pfb_width;
    row = lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)lcd->pfb_y_start) * stride
        + (uint32_t)(cx0 - (int16_t)lcd->pfb_area.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        int32_t dy  = (int32_t)py - ccy;
        int32_t dy2 = dy * dy;
        colour_t *p = row;

        for (px = cx0; px <= cx1; px++, p++)
        {
            int32_t dx = (int32_t)px - ccx;
            int32_t d2 = dx * dx + dy2;
            int32_t alpha;
            uint8_t r8, g8, b8;
            uint8_t a;

            if (d2 >= r_out2 || d2 <= r_in2)
                continue; /* 环带之外：保持背景 */

            /* 内外边缘 1px 简易渐隐（用 d² 线性近似 d，1px 内视觉无差） */
            alpha = 255;
            if (d2 > r_out_f2)
                alpha = (255 * (r_out2 - d2)) / div_out;
            else if (d2 < r_in_f2)
                alpha = (255 * (d2 - r_in2)) / div_in;

            _cw_hue_to_rgb(_cw_atan2_512(dy, dx), &r8, &g8, &b8);

            a = (eff_op >= 250U) ? (uint8_t)alpha
                                 : we_div255((uint32_t)alpha * eff_op);
            we_store_blended_color(p, RGB888TODEV(r8, g8, b8), a);
        }
    }

    /* 标记点：环带中线上的白芯深边圆点（解析圆点原语自带 PFB 裁剪） */
    {
        int16_t r_mid = (int16_t)((o->r_out + o->r_in) / 2U);
        int16_t mr    = (int16_t)((o->r_out - o->r_in) / 2U);
        int16_t mx, my;

        mr = (int16_t)(mr - 1);
        if (mr < WE_COLORWHEEL_MARK_MIN_R)
            mr = WE_COLORWHEEL_MARK_MIN_R;
        mx = (int16_t)(ccx + (int16_t)(((int32_t)r_mid * we_cos((int16_t)o->hue)) >> 15));
        my = (int16_t)(ccy + (int16_t)(((int32_t)r_mid * we_sin((int16_t)o->hue)) >> 15));

        we_draw_round_rect_analytic_fill(lcd, (int16_t)(mx - mr), (int16_t)(my - mr),
                                         (uint16_t)(2 * mr + 1), (uint16_t)(2 * mr + 1),
                                         (uint16_t)mr, RGB888TODEV(28, 32, 42), o->opacity);
        we_draw_round_rect_analytic_fill(lcd, (int16_t)(mx - mr + 2), (int16_t)(my - mr + 2),
                                         (uint16_t)(2 * (mr - 2) + 1), (uint16_t)(2 * (mr - 2) + 1),
                                         (uint16_t)(mr - 2), RGB888TODEV(245, 247, 250), o->opacity);
    }
}

/* --------------------------------------------------------------------------
 * 交互
 * -------------------------------------------------------------------------- */

/**
 * @brief 把触点坐标转为色相并更新（值变才回调 + 重绘）。
 * @param o 传入：控件对象指针。
 * @param tx 传入：触点 X（屏幕绝对坐标）。
 * @param ty 传入：触点 Y。
 * @return 无。
 * @note 触点距中心太近（< r_in/2）时角度不稳定，直接忽略。
 */
static void _cw_update_from_touch(we_colorwheel_obj_t *o, int16_t tx, int16_t ty)
{
    int32_t dx = (int32_t)tx - (o->base.x + o->base.w / 2);
    int32_t dy = (int32_t)ty - (o->base.y + o->base.h / 2);
    int32_t dead = (int32_t)(o->r_in / 2U);
    uint16_t hue;

    if (dx * dx + dy * dy < dead * dead)
        return;

    hue = _cw_atan2_512(dy, dx);
    if (hue == o->hue)
        return;

    o->hue = hue;
    we_obj_invalidate((we_obj_t *)o);
    if (o->changed_cb != NULL)
        o->changed_cb(o, we_colorwheel_get_color(o));
}

/**
 * @brief 控件事件回调：按下/拖动实时选色。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒返回 1（交互控件消费事件，容器据此锁定并转发后续事件）。
 */
static uint8_t _cw_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_colorwheel_obj_t *o = (we_colorwheel_obj_t *)ptr;

    if (o == NULL || data == NULL)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
    case WE_EVENT_STAY:
        _cw_update_from_touch(o, data->x, data->y);
        break;
    default:
        break;
    }
    return 1U;
}

/**
 * @brief 容器/框架重定位回调。
 * @param ptr 回调透传对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
static void _cw_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_colorwheel_set_pos((we_colorwheel_obj_t *)ptr, x, y);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_colorwheel_obj_init(we_colorwheel_obj_t *obj, we_lcd_t *lcd,
                            int16_t x, int16_t y, uint16_t size)
{
    uint16_t band;

    if (obj == NULL || lcd == NULL)
        return;

    if (size < 40U)
        size = 40U;

    obj->base.lcd     = lcd;
    obj->base.class_p = &_cw_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)size;
    obj->base.h       = (int16_t)size;

    /* 色环内切包围盒：外半径 = size/2 - 1（AA 渐隐不越界），环带宽约 size/5 */
    obj->r_out = (uint16_t)(size / 2U - 1U);
    band       = (uint16_t)(size / 5U);
    if (band < 8U)
        band = 8U;
    obj->r_in = (obj->r_out > band) ? (uint16_t)(obj->r_out - band) : 2U;

    obj->hue        = 0U;
    obj->opacity    = 255U;
    obj->changed_cb = NULL;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

colour_t we_colorwheel_get_color(const we_colorwheel_obj_t *obj)
{
    uint8_t r = 0U, g = 0U, b = 0U;

    if (obj != NULL)
        _cw_hue_to_rgb(obj->hue, &r, &g, &b);
    return RGB888TODEV(r, g, b);
}

void we_colorwheel_get_rgb(const we_colorwheel_obj_t *obj,
                           uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t rr = 0U, gg = 0U, bb = 0U;

    if (obj != NULL)
        _cw_hue_to_rgb(obj->hue, &rr, &gg, &bb);
    if (r != NULL)
        *r = rr;
    if (g != NULL)
        *g = gg;
    if (b != NULL)
        *b = bb;
}

uint16_t we_colorwheel_get_hue(const we_colorwheel_obj_t *obj)
{
    return (obj != NULL) ? obj->hue : 0U;
}

void we_colorwheel_set_hue(we_colorwheel_obj_t *obj, uint16_t hue)
{
    if (obj == NULL)
        return;
    hue &= 0x1FFU;
    if (obj->hue == hue)
        return;
    obj->hue = hue;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_colorwheel_set_changed_cb(we_colorwheel_obj_t *obj, we_colorwheel_changed_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->changed_cb = cb;
}

void we_colorwheel_set_opacity(we_colorwheel_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_colorwheel_set_pos(we_colorwheel_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || (obj->base.x == x && obj->base.y == y))
        return;
    we_obj_invalidate((we_obj_t *)obj); /* 旧位置 */
    obj->base.x = x;
    obj->base.y = y;
    we_obj_invalidate((we_obj_t *)obj); /* 新位置 */
}

void we_colorwheel_obj_delete(we_colorwheel_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
