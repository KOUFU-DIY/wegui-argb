#include "we_widget_stepper.h"
#include "we_font_text.h"
#include "we_render.h"

/* 内置配色（RGB888TODEV 兼容 RGB565 / RGB888）。 */
#define _SP_COL_BG          RGB888TODEV(46, 52, 64)    /* 中间数值区背景 */
#define _SP_COL_BTN         RGB888TODEV(58, 66, 82)    /* 左右按钮区背景 */
#define _SP_COL_BTN_PRESS   RGB888TODEV(64, 152, 231)  /* 按钮按下高亮 */
#define _SP_COL_TEXT        RGB888TODEV(229, 233, 240) /* 数值文本 */
#define _SP_COL_TEXT_DIS    RGB888TODEV(110, 118, 134) /* 禁用态文本 */
#define _SP_COL_SIGN        RGB888TODEV(210, 218, 232) /* +/- 符号正常色 */
#define _SP_COL_SIGN_PRESS  RGB888TODEV(255, 255, 255) /* +/- 符号按下色 */
#define _SP_COL_SIGN_DIS    RGB888TODEV(90, 98, 114)   /* +/- 符号禁用色 */

/* 10^n 查表（n=0..WE_STEPPER_MAX_DECIMALS），避免运行时循环求幂。 */
static const int32_t _sp_pow10[WE_STEPPER_MAX_DECIMALS + 1U] = {
    1, 10, 100, 1000, 10000
};

/**
 * @brief 取 10^decimals。
 * @param decimals 小数位数。
 * @return 对应的 10 次幂；越界时退化为 1。
 */
static int32_t _stepper_divisor(uint8_t decimals)
{
    if (decimals > WE_STEPPER_MAX_DECIMALS)
        return 1;
    return _sp_pow10[decimals];
}

/**
 * @brief 将定点值夹紧到 [min,max]。
 * @param obj 控件对象指针。
 * @param v 输入定点值。
 * @return 夹紧后的定点值。
 */
static int32_t _stepper_clamp(const we_stepper_obj_t *obj, int32_t v)
{
    if (v < obj->min_value)
        return obj->min_value;
    if (v > obj->max_value)
        return obj->max_value;
    return v;
}

/**
 * @brief 将无符号整数写入缓冲（可选左侧补零到 min_digits 位）。
 * @param buf 输出缓冲。
 * @param pos 当前写入位置（原位更新）。
 * @param cap 缓冲容量（含结尾，需预留 1 字节给 '\0'）。
 * @param val 待写入的无符号值。
 * @param min_digits 最少位数，不足在高位补 '0'。
 * @return 无。
 */
static void _stepper_put_uint(char *buf, uint8_t *pos, uint8_t cap,
                              uint32_t val, uint8_t min_digits)
{
    char tmp[12];
    uint8_t n = 0U;

    do {
        tmp[n++] = (char)('0' + (val % 10U));
        val /= 10U;
    } while (val != 0U && n < sizeof(tmp));

    while (n < min_digits && n < sizeof(tmp))
        tmp[n++] = '0';

    while (n > 0U && *pos < (uint8_t)(cap - 1U))
        buf[(*pos)++] = tmp[--n];
}

/**
 * @brief 把定点值格式化为显示字符串（带负号与小数点）。
 *
 * 真实值 = value / 10^decimals。注意 value 为 -1~-(divisor-1) 时整数部分
 * 为 0、负号会丢失，这里用独立的 neg 标志统一在最前补 '-'。
 * @param obj 控件对象指针（结果写入 obj->buf）。
 * @return 无。
 */
static void _stepper_format(we_stepper_obj_t *obj)
{
    int32_t divisor = _stepper_divisor(obj->decimals);
    int32_t v = obj->value;
    uint8_t neg = (v < 0) ? 1U : 0U;
    uint32_t av = (uint32_t)(neg ? -v : v);
    uint32_t ip = av / (uint32_t)divisor;
    uint32_t fp = av % (uint32_t)divisor;
    uint8_t pos = 0U;
    uint8_t cap = (uint8_t)sizeof(obj->buf);

    if (neg)
        obj->buf[pos++] = '-';
    _stepper_put_uint(obj->buf, &pos, cap, ip, 1U);
    if (obj->decimals > 0U)
    {
        if (pos < (uint8_t)(cap - 1U))
            obj->buf[pos++] = '.';
        _stepper_put_uint(obj->buf, &pos, cap, fp, obj->decimals);
    }
    obj->buf[pos] = '\0';
}

/**
 * @brief 计算左右按钮区宽度（方形，取 h 与 w/3 的较小值）。
 * @param obj 控件对象指针。
 * @return 按钮区宽度（像素）。
 */
static int16_t _stepper_btn_w(const we_stepper_obj_t *obj)
{
    int16_t bw = obj->base.h;
    int16_t third = (int16_t)(obj->base.w / 3);
    if (bw > third)
        bw = third;
    if (bw < 1)
        bw = 1;
    return bw;
}

/**
 * @brief 减按钮是否禁用（非回绕且已到下限）。
 * @param obj 控件对象指针。
 * @return 非 0 表示禁用。
 */
static uint8_t _stepper_dec_disabled(const we_stepper_obj_t *obj)
{
    return (uint8_t)(!obj->wrap && obj->value <= obj->min_value);
}

/**
 * @brief 加按钮是否禁用（非回绕且已到上限）。
 * @param obj 控件对象指针。
 * @return 非 0 表示禁用。
 */
static uint8_t _stepper_inc_disabled(const we_stepper_obj_t *obj)
{
    return (uint8_t)(!obj->wrap && obj->value >= obj->max_value);
}

/**
 * @brief 按方向步进一次（处理回绕/夹紧、触发回调、标脏全控件）。
 * @param obj 控件对象指针。
 * @param dir 方向：-1 减，+1 加。
 * @return 1 表示值发生变化，0 表示未变化（边界）。
 */
static uint8_t _stepper_apply(we_stepper_obj_t *obj, int8_t dir)
{
    int32_t span;
    int32_t nv = obj->value + (int32_t)dir * obj->step;

    if (obj->wrap && obj->max_value > obj->min_value)
    {
        span = obj->max_value - obj->min_value + 1;
        if (nv > obj->max_value)
            nv = obj->min_value + ((nv - obj->min_value) % span);
        else if (nv < obj->min_value)
            nv = obj->max_value - ((obj->max_value - nv) % span);
    }
    else
    {
        nv = _stepper_clamp(obj, nv);
    }

    if (nv == obj->value)
        return 0U;

    obj->value = nv;
    we_obj_invalidate((we_obj_t *)obj);
    if (obj->changed_cb != NULL)
        obj->changed_cb(obj, obj->value);
    return 1U;
}

/**
 * @brief 在按钮区中心绘制 +/- 符号（实心横/竖条，居中）。
 * @param lcd GUI 上下文。
 * @param cx 符号中心 X。
 * @param cy 符号中心 Y。
 * @param half 符号半长（横条半宽 / 竖条半高）。
 * @param thick 笔画粗细（像素）。
 * @param is_plus 非 0 画 '+'，0 画 '-'。
 * @param color 笔画颜色。
 * @return 无。
 */
static void _stepper_draw_sign(we_lcd_t *lcd, int16_t cx, int16_t cy,
                               int16_t half, int16_t thick,
                               uint8_t is_plus, colour_t color)
{
    int16_t t0 = (int16_t)(thick / 2);

    /* 横条 */
    we_draw_round_rect_analytic_fill(lcd, (int16_t)(cx - half), (int16_t)(cy - t0),
                                     (uint16_t)(half * 2 + 1), (uint16_t)thick,
                                     0U, color, 255U);
    /* 竖条（仅 '+'） */
    if (is_plus)
        we_draw_round_rect_analytic_fill(lcd, (int16_t)(cx - t0), (int16_t)(cy - half),
                                         (uint16_t)thick, (uint16_t)(half * 2 + 1),
                                         0U, color, 255U);
}

/**
 * @brief 在裁剪框内垂直/水平居中绘制一行文本。
 * @param lcd GUI 上下文。
 * @param font 字体资源。
 * @param text 文本。
 * @param color 文本颜色。
 * @param box_x 框左上 X。
 * @param box_y 框左上 Y。
 * @param box_w 框宽。
 * @param box_h 框高。
 * @param opacity 透明度。
 * @return 无。
 */
static void _stepper_draw_text_centered(we_lcd_t *lcd, const unsigned char *font,
                                        const char *text, colour_t color,
                                        int16_t box_x, int16_t box_y,
                                        int16_t box_w, int16_t box_h, uint8_t opacity)
{
    we_area_t old_pfb_area;
    uint16_t old_y_start, old_y_end;
    colour_t *old_gram;
    int8_t y_top, y_bot;
    uint16_t tw;
    int16_t txt_x, txt_y;
    int16_t cx0, cy0, cx1, cy1;

    if (text == NULL || font == NULL)
        return;

    tw = we_get_text_width(font, text);
    we_get_text_bbox(font, text, &y_top, &y_bot);
    txt_x = (int16_t)(box_x + (box_w - (int16_t)tw) / 2);
    txt_y = (int16_t)(box_y + box_h / 2 - (y_top + y_bot) / 2);

    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    cx0 = WE_MAX(old_pfb_area.x0, box_x);
    cy0 = WE_MAX((int16_t)old_y_start, box_y);
    cx1 = WE_MIN(old_pfb_area.x1, (int16_t)(box_x + box_w - 1));
    cy1 = WE_MIN((int16_t)old_y_end, (int16_t)(box_y + box_h - 1));

    if (cx0 <= cx1 && cy0 <= cy1)
    {
        lcd->pfb_area.x0 = cx0;
        lcd->pfb_area.x1 = cx1;
        lcd->pfb_y_start = (uint16_t)cy0;
        lcd->pfb_y_end = (uint16_t)cy1;
        lcd->pfb_gram = old_gram + (cy0 - (int16_t)old_y_start) * lcd->pfb_width
                                 + (cx0 - old_pfb_area.x0);

        we_draw_string(lcd, txt_x, txt_y, font, text, color, opacity);
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;
}

/**
 * @brief 绘制回调：底框 + 左减/右加按钮区（含按下高亮、禁用淡化）+ 居中数值。
 * @param ptr 控件对象指针。
 * @return 无。
 */
static void _stepper_draw_cb(void *ptr)
{
    we_stepper_obj_t *obj = (we_stepper_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    int16_t x = obj->base.x;
    int16_t y = obj->base.y;
    int16_t w = obj->base.w;
    int16_t h = obj->base.h;
    int16_t bw = _stepper_btn_w(obj);
    uint16_t r = obj->radius;
    int16_t sign_half = (int16_t)(h / 6);
    int16_t sign_thick = (int16_t)(h / 10);
    uint8_t dec_dis = _stepper_dec_disabled(obj);
    uint8_t inc_dis = _stepper_inc_disabled(obj);
    colour_t num_col;
    colour_t sc;

    if (sign_thick < 2)
        sign_thick = 2;
    if (sign_half < 4)
        sign_half = 4;

    /* 整体底框（中间数值区背景） */
    we_draw_round_rect_analytic_fill(lcd, x, y, (uint16_t)w, (uint16_t)h, r, _SP_COL_BG, 255U);

    /* 左按钮区背景：按下高亮，否则常态色（左侧仅左两角圆角，这里用统一圆角近似） */
    {
        colour_t lbg = (obj->pressed && obj->active_side < 0) ? _SP_COL_BTN_PRESS : _SP_COL_BTN;
        we_draw_round_rect_analytic_fill(lcd, x, y, (uint16_t)bw, (uint16_t)h, r, lbg, 255U);
        /* 用底色补掉按钮右侧的圆角缺口，使其与中间区平直拼接 */
        we_draw_round_rect_analytic_fill(lcd, (int16_t)(x + bw - r), y, r, (uint16_t)h, 0U, lbg, 255U);
    }

    /* 右按钮区背景 */
    {
        int16_t rx = (int16_t)(x + w - bw);
        colour_t rbg = (obj->pressed && obj->active_side > 0) ? _SP_COL_BTN_PRESS : _SP_COL_BTN;
        we_draw_round_rect_analytic_fill(lcd, rx, y, (uint16_t)bw, (uint16_t)h, r, rbg, 255U);
        we_draw_round_rect_analytic_fill(lcd, rx, y, r, (uint16_t)h, 0U, rbg, 255U);
    }

    /* '-' 符号 */
    sc = dec_dis ? _SP_COL_SIGN_DIS
                 : ((obj->pressed && obj->active_side < 0) ? _SP_COL_SIGN_PRESS : _SP_COL_SIGN);
    _stepper_draw_sign(lcd, (int16_t)(x + bw / 2), (int16_t)(y + h / 2),
                       sign_half, sign_thick, 0U, sc);

    /* '+' 符号 */
    sc = inc_dis ? _SP_COL_SIGN_DIS
                 : ((obj->pressed && obj->active_side > 0) ? _SP_COL_SIGN_PRESS : _SP_COL_SIGN);
    _stepper_draw_sign(lcd, (int16_t)(x + w - bw / 2), (int16_t)(y + h / 2),
                       sign_half, sign_thick, 1U, sc);

    /* 中间数值 */
    _stepper_format(obj);
    num_col = obj->enabled ? _SP_COL_TEXT : _SP_COL_TEXT_DIS;
    _stepper_draw_text_centered(lcd, obj->font, obj->buf, num_col,
                                (int16_t)(x + bw), y, (int16_t)(w - 2 * bw), h, 255U);
}

/**
 * @brief 判定触摸点落在哪一侧按钮区。
 * @param obj 控件对象指针。
 * @param px 触摸 X。
 * @return -1=左减区，+1=右加区，0=中间数值区（不步进）。
 */
static int8_t _stepper_hit_side(const we_stepper_obj_t *obj, int16_t px)
{
    int16_t bw = _stepper_btn_w(obj);
    if (px < (int16_t)(obj->base.x + bw))
        return -1;
    if (px >= (int16_t)(obj->base.x + obj->base.w - bw))
        return 1;
    return 0;
}

/**
 * @brief 事件回调：点击左右区步进，按住触发连续步进（复用 STAY，不占 timer）。
 * @param ptr 控件对象指针。
 * @param event 输入事件。
 * @param data 输入数据。
 * @return 1 表示消费事件。
 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_STEPPER_USE_KEY == 1)
static uint8_t _stepper_key_cb(void *ptr, uint8_t key_evt);
#endif
static uint8_t _stepper_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_STEPPER_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _stepper_key_cb(ptr, (uint8_t)event);
#endif

    we_stepper_obj_t *obj = (we_stepper_obj_t *)ptr;
    int8_t side;

    if (!obj->enabled)
        return 1U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        side = (data != NULL) ? _stepper_hit_side(obj, data->x) : 0;
        obj->active_side = side;
        obj->hold_cnt = 0U;
        if (side != 0)
        {
            obj->pressed = 1U;
            (void)_stepper_apply(obj, side); /* 内部已标脏；首次按下立即步进一次 */
            we_obj_invalidate((we_obj_t *)obj); /* 保证按钮高亮也刷新 */
        }
        break;

    case WE_EVENT_STAY:
        if (obj->active_side != 0)
        {
            obj->hold_cnt++;
            if (obj->hold_cnt >= WE_STEPPER_HOLD_DELAY &&
                ((obj->hold_cnt - WE_STEPPER_HOLD_DELAY) % WE_STEPPER_HOLD_INTERVAL) == 0U)
            {
                (void)_stepper_apply(obj, obj->active_side);
            }
        }
        break;

    case WE_EVENT_RELEASED:
    case WE_EVENT_CLICKED:
        if (obj->pressed || obj->active_side != 0)
        {
            obj->pressed = 0U;
            obj->active_side = 0;
            obj->hold_cnt = 0U;
            we_obj_invalidate((we_obj_t *)obj);
        }
        break;

    default:
        break;
    }

    return 1U; /* 始终消费 */
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_STEPPER_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 进/退编辑态，编辑态方向键复用 _stepper_apply 步进。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 步进边界/回绕/标脏/回调全部复用触摸路径的 _stepper_apply；
 *       端口按住连发注入即等效触摸长按连调。
 */
static uint8_t _stepper_key_cb(void *ptr, uint8_t key_evt)
{
    we_stepper_obj_t *obj = (we_stepper_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->enabled != 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (we_focus_edit_active(lcd))
            we_focus_edit_exit(lcd);
        else
            we_focus_edit_enter(lcd);
        return 1U;
    case WE_KEY_LEFT:
    case WE_KEY_DOWN:
    case WE_KEY_RIGHT:
    case WE_KEY_UP:
        if (!we_focus_edit_active(lcd))
            return 0U;
        (void)_stepper_apply(obj, (key_evt == WE_KEY_LEFT || key_evt == WE_KEY_DOWN) ? -1 : 1);
        return 1U;
    default:
        return 0U;
    }
}
#endif

static const we_class_t _stepper_class = {
    .draw_cb = _stepper_draw_cb,
    .event_cb = _stepper_event_cb,
    .set_pos_cb = NULL,
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_STEPPER_USE_KEY == 1)
    .class_flags = WE_CLASS_FLAG_FOCUSABLE, /* 键/焦点走统一 event_cb 通道 */
#endif
};

/**
 * @brief 初始化数值步进控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 上下文。
 * @param x 左上 X。
 * @param y 左上 Y。
 * @param w 总宽度。
 * @param h 高度。
 * @param font 字体资源。
 * @param decimals 小数位数。
 * @param min_value 定点下限。
 * @param max_value 定点上限。
 * @param step 步进增量。
 * @param init_value 初始定点值。
 * @return 无。
 */
void we_stepper_obj_init(we_stepper_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, uint16_t w, uint16_t h,
                         const unsigned char *font, uint8_t decimals,
                         int32_t min_value, int32_t max_value,
                         int32_t step, int32_t init_value)
{
    if (obj == NULL || lcd == NULL || w == 0U || h == 0U)
        return;

    if (min_value > max_value)
    {
        int32_t tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    if (decimals > WE_STEPPER_MAX_DECIMALS)
        decimals = WE_STEPPER_MAX_DECIMALS;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = (int16_t)w;
    obj->base.h = (int16_t)h;
    obj->base.class_p = &_stepper_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->decimals = decimals;
    obj->min_value = min_value;
    obj->max_value = max_value;
    obj->step = (step > 0) ? step : 1;
    obj->wrap = 0U;
    obj->enabled = 1U;
    obj->radius = (uint16_t)(h / 4);
    if (obj->radius < 4U)
        obj->radius = 4U;
    obj->font = font;
    obj->changed_cb = NULL;
    obj->active_side = 0;
    obj->pressed = 0U;
    obj->hold_cnt = 0U;
    obj->value = _stepper_clamp(obj, init_value);
    obj->buf[0] = '\0';

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置当前定点值（夹紧后按需触发回调与重绘）。
 * @param obj 控件对象指针。
 * @param value 新定点值。
 * @return 无。
 */
void we_stepper_set_value(we_stepper_obj_t *obj, int32_t value)
{
    int32_t nv;
    if (obj == NULL)
        return;
    nv = _stepper_clamp(obj, value);
    if (nv == obj->value)
        return;
    obj->value = nv;
    we_obj_invalidate((we_obj_t *)obj);
    if (obj->changed_cb != NULL)
        obj->changed_cb(obj, obj->value);
}

/**
 * @brief 获取当前定点值。
 * @param obj 控件对象指针。
 * @return 定点值；NULL 时为 0。
 */
int32_t we_stepper_get_value(const we_stepper_obj_t *obj)
{
    return (obj != NULL) ? obj->value : 0;
}

/**
 * @brief 设置步进增量。
 * @param obj 控件对象指针。
 * @param step 定点增量。
 * @return 无。
 */
void we_stepper_set_step(we_stepper_obj_t *obj, int32_t step)
{
    if (obj == NULL)
        return;
    obj->step = (step > 0) ? step : 1;
}

/**
 * @brief 设置取值范围并夹紧当前值。
 * @param obj 控件对象指针。
 * @param min_value 定点下限。
 * @param max_value 定点上限。
 * @return 无。
 */
void we_stepper_set_range(we_stepper_obj_t *obj, int32_t min_value, int32_t max_value)
{
    int32_t nv;
    if (obj == NULL)
        return;
    if (min_value > max_value)
    {
        int32_t tmp = min_value;
        min_value = max_value;
        max_value = tmp;
    }
    obj->min_value = min_value;
    obj->max_value = max_value;
    nv = _stepper_clamp(obj, obj->value);
    if (nv != obj->value)
    {
        obj->value = nv;
        if (obj->changed_cb != NULL)
            obj->changed_cb(obj, obj->value);
    }
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置到边界是否回绕。
 * @param obj 控件对象指针。
 * @param wrap 非 0 回绕。
 * @return 无。
 */
void we_stepper_set_wrap(we_stepper_obj_t *obj, uint8_t wrap)
{
    if (obj == NULL || obj->wrap == (uint8_t)(wrap ? 1U : 0U))
        return;
    obj->wrap = wrap ? 1U : 0U;
    we_obj_invalidate((we_obj_t *)obj); /* 边界按钮禁用态可能变化 */
}

/**
 * @brief 设置是否可交互。
 * @param obj 控件对象指针。
 * @param enabled 非 0 可交互。
 * @return 无。
 */
void we_stepper_set_enabled(we_stepper_obj_t *obj, uint8_t enabled)
{
    uint8_t e = enabled ? 1U : 0U;
    if (obj == NULL || obj->enabled == e)
        return;
    obj->enabled = e;
    if (!e)
    {
        obj->pressed = 0U;
        obj->active_side = 0;
        obj->hold_cnt = 0U;
    }
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置数值改变回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针。
 * @return 无。
 */
void we_stepper_set_changed_cb(we_stepper_obj_t *obj, we_stepper_changed_cb_t cb)
{
    if (obj != NULL)
        obj->changed_cb = cb;
}

/**
 * @brief 删除控件并从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_stepper_obj_delete(we_stepper_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}







