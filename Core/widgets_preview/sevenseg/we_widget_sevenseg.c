/**
 * @file  we_widget_sevenseg.c
 * @brief 七段数码管控件（sevenseg）实现 —— preview 孵化区
 *
 * 渲染思路：
 *   1. 每个字符占一个等宽单元格，经典 a~g 七段布局，全部为直角矩形段；
 *   2. 段 = we_fill_rect 整块填充（自带 PFB 裁剪 + 容器透明度级联）；
 *   3. ':' 画上下两个 t×t 小方点；'-' 只亮 g 段；空位可选画鬼影骨架；
 *   4. 每位数字查 static const 段码表，零查表以外的分支计算。
 *
 * set_text 用控件内部快照做"内容变才重绘"判定，适合 demo 每帧 sprintf
 * 进同一块静态缓冲再无脑调用 set_text 的写法。
 */

#include "we_widget_sevenseg.h"
#include "we_render.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _sevenseg_draw_cb(void *ptr);
static uint8_t _sevenseg_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void    _sevenseg_set_pos_cb(void *ptr, int16_t x, int16_t y);

static const we_class_t _sevenseg_class = {
    .draw_cb    = _sevenseg_draw_cb,
    .event_cb   = _sevenseg_event_cb,
    .set_pos_cb = _sevenseg_set_pos_cb
};

/* 段位定义：bit0=a(上横) bit1=b(右上竖) bit2=c(右下竖)
 *           bit3=d(下横) bit4=e(左下竖) bit5=f(左上竖) bit6=g(中横) */
#define _SEG_A 0x01U
#define _SEG_B 0x02U
#define _SEG_C 0x04U
#define _SEG_D 0x08U
#define _SEG_E 0x10U
#define _SEG_F 0x20U
#define _SEG_G 0x40U

/* '0'~'9' 段码表 */
static const uint8_t _sevenseg_digit_map[10] = {
    /* 0 */ _SEG_A | _SEG_B | _SEG_C | _SEG_D | _SEG_E | _SEG_F,
    /* 1 */ _SEG_B | _SEG_C,
    /* 2 */ _SEG_A | _SEG_B | _SEG_G | _SEG_E | _SEG_D,
    /* 3 */ _SEG_A | _SEG_B | _SEG_G | _SEG_C | _SEG_D,
    /* 4 */ _SEG_F | _SEG_G | _SEG_B | _SEG_C,
    /* 5 */ _SEG_A | _SEG_F | _SEG_G | _SEG_C | _SEG_D,
    /* 6 */ _SEG_A | _SEG_F | _SEG_G | _SEG_E | _SEG_C | _SEG_D,
    /* 7 */ _SEG_A | _SEG_B | _SEG_C,
    /* 8 */ _SEG_A | _SEG_B | _SEG_C | _SEG_D | _SEG_E | _SEG_F | _SEG_G,
    /* 9 */ _SEG_A | _SEG_B | _SEG_C | _SEG_D | _SEG_F | _SEG_G
};

/**
 * @brief 颜色相等比较（RGB565/RGB888），供 setter 的"值未变则跳过"守卫使用。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 = 相等，0 = 不等。
 */
static __inline uint8_t _sevenseg_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#elif (LCD_DEEP == DEEP_RGB888)
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 字符解码为段码。
 * @param ch 传入：待解码字符。
 * @return 段码位图（bit0~bit6 = a~g），不支持的字符返回 0（全灭）。
 * @note ':' 不走段码路径，由绘制端特判画点。
 */
static uint8_t _sevenseg_decode(char ch)
{
    if (ch >= '0' && ch <= '9')
        return _sevenseg_digit_map[ch - '0'];
    if (ch == '-')
        return _SEG_G;
    return 0x00U; /* ' ' 与其余字符：全灭 */
}

/**
 * @brief 绘制一个数字单元格的七段（亮段 + 可选鬼影灭段）。
 * @param o 传入：控件对象指针。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param dx 传入：单元格左上角 X（屏幕绝对坐标）。
 * @param dy 传入：单元格左上角 Y。
 * @param segs 传入：亮段位图。
 * @return 无。
 */
static void _sevenseg_draw_digit(const we_sevenseg_obj_t *o, we_lcd_t *lcd,
                                 int16_t dx, int16_t dy, uint8_t segs)
{
    uint16_t t   = o->seg_t;
    uint16_t w   = o->digit_w;
    uint16_t h   = o->digit_h;
    uint16_t mid = (uint16_t)((h - t) / 2U);          /* g 段上沿的局部 Y */
    uint16_t hl  = (uint16_t)(w - 2U * t);            /* 横段长度 */
    uint16_t vt  = (uint16_t)(mid - t);               /* 上半竖段长度 */
    uint16_t vb  = (uint16_t)(h - mid - 2U * t);      /* 下半竖段长度 */
    uint8_t  ghost_opa;
    uint8_t  i;

    /* 七段的矩形几何表：与 _SEG_x 位序一一对应 */
    struct
    {
        int16_t  rx, ry;
        uint16_t rw, rh;
    } rc[7];

    rc[0].rx = (int16_t)(dx + (int16_t)t);           rc[0].ry = dy;                                    rc[0].rw = hl; rc[0].rh = t; /* a */
    rc[1].rx = (int16_t)(dx + (int16_t)(w - t));     rc[1].ry = (int16_t)(dy + (int16_t)t);            rc[1].rw = t;  rc[1].rh = vt; /* b */
    rc[2].rx = (int16_t)(dx + (int16_t)(w - t));     rc[2].ry = (int16_t)(dy + (int16_t)(mid + t));    rc[2].rw = t;  rc[2].rh = vb; /* c */
    rc[3].rx = (int16_t)(dx + (int16_t)t);           rc[3].ry = (int16_t)(dy + (int16_t)(h - t));      rc[3].rw = hl; rc[3].rh = t; /* d */
    rc[4].rx = dx;                                   rc[4].ry = (int16_t)(dy + (int16_t)(mid + t));    rc[4].rw = t;  rc[4].rh = vb; /* e */
    rc[5].rx = dx;                                   rc[5].ry = (int16_t)(dy + (int16_t)t);            rc[5].rw = t;  rc[5].rh = vt; /* f */
    rc[6].rx = (int16_t)(dx + (int16_t)t);           rc[6].ry = (int16_t)(dy + (int16_t)mid);          rc[6].rw = hl; rc[6].rh = t; /* g */

    ghost_opa = we_div255((uint32_t)o->opacity * WE_SEVENSEG_GHOST_OPA);

    for (i = 0U; i < 7U; i++)
    {
        if ((segs >> i) & 0x01U)
        {
            we_fill_rect(lcd, rc[i].rx, rc[i].ry, rc[i].rw, rc[i].rh,
                         o->on_color, o->opacity);
        }
        else if (o->ghost && ghost_opa > 0U)
        {
            we_fill_rect(lcd, rc[i].rx, rc[i].ry, rc[i].rw, rc[i].rh,
                         o->off_color, ghost_opa);
        }
    }
}

/**
 * @brief 绘制一个冒号单元格（上下两个 t×t 小方点，恒为亮色）。
 * @param o 传入：控件对象指针。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param dx 传入：单元格左上角 X（屏幕绝对坐标）。
 * @param dy 传入：单元格左上角 Y。
 * @return 无。
 */
static void _sevenseg_draw_colon(const we_sevenseg_obj_t *o, we_lcd_t *lcd,
                                 int16_t dx, int16_t dy)
{
    uint16_t t  = o->seg_t;
    int16_t  cx = (int16_t)(dx + (int16_t)((o->digit_w - t) / 2U));
    int16_t  y1 = (int16_t)(dy + (int16_t)(o->digit_h / 3U - t / 2U));
    int16_t  y2 = (int16_t)(dy + (int16_t)((o->digit_h * 2U) / 3U - t / 2U));

    we_fill_rect(lcd, cx, y1, t, t, o->on_color, o->opacity);
    we_fill_rect(lcd, cx, y2, t, t, o->on_color, o->opacity);
}

/**
 * @brief 控件绘制回调：逐单元格解码段码并整块填充。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _sevenseg_draw_cb(void *ptr)
{
    we_sevenseg_obj_t *o = (we_sevenseg_obj_t *)ptr;
    we_lcd_t *lcd;
    const char *s;
    int16_t cell_x;
    uint8_t i;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL)
        return;

    s      = o->text;
    cell_x = o->base.x;

    for (i = 0U; i < o->digit_cnt; i++)
    {
        char ch = '\0';

        if (s != NULL && *s != '\0')
        {
            ch = *s;
            s++;
        }

        if (ch == ':')
        {
            _sevenseg_draw_colon(o, lcd, cell_x, o->base.y);
        }
        else
        {
            _sevenseg_draw_digit(o, lcd, cell_x, o->base.y, _sevenseg_decode(ch));
        }

        cell_x = (int16_t)(cell_x + (int16_t)(o->digit_w + o->gap));
    }
}

/**
 * @brief 控件事件回调：装饰性穿透（不消费输入）。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒返回 0，事件穿透给背后控件。
 */
static uint8_t _sevenseg_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
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
static void _sevenseg_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_sevenseg_set_pos((we_sevenseg_obj_t *)ptr, x, y);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_sevenseg_obj_init(we_sevenseg_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t digit_h, uint8_t digit_cnt)
{
    uint16_t t;
    uint16_t w;

    if (obj == NULL || lcd == NULL)
        return;

    if (digit_h < 16U)
        digit_h = 16U;
    if (digit_cnt < 1U)
        digit_cnt = 1U;
    if (digit_cnt > (uint8_t)WE_SEVENSEG_MAX_CHARS)
        digit_cnt = (uint8_t)WE_SEVENSEG_MAX_CHARS;

    /* 几何推导：段厚 = 字高/8（最小 2），字宽 = 字高/2，字间距 = 段厚 */
    t = (uint16_t)(digit_h / 8U);
    if (t < 2U)
        t = 2U;
    w = (uint16_t)(digit_h / 2U);
    if (w < (uint16_t)(3U * t))
        w = (uint16_t)(3U * t);

    obj->base.lcd     = lcd;
    obj->base.class_p = &_sevenseg_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)((uint16_t)digit_cnt * w + (uint16_t)(digit_cnt - 1U) * t);
    obj->base.h       = (int16_t)digit_h;

    obj->text      = NULL;
    obj->shadow[0] = '\0';
    obj->digit_h   = digit_h;
    obj->digit_w   = w;
    obj->gap       = t;
    obj->seg_t     = (uint8_t)t;
    obj->digit_cnt = digit_cnt;
    obj->ghost     = 0U;
    obj->opacity   = 255U;
    {
        colour_t on  = RGB888_CONST(WE_SEVENSEG_DEF_ON_R, WE_SEVENSEG_DEF_ON_G, WE_SEVENSEG_DEF_ON_B);
        colour_t off = RGB888_CONST(WE_SEVENSEG_DEF_OFF_R, WE_SEVENSEG_DEF_OFF_G, WE_SEVENSEG_DEF_OFF_B);
        obj->on_color  = on;
        obj->off_color = off;
    }

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_sevenseg_set_text(we_sevenseg_obj_t *obj, const char *str)
{
    const char *s;

    if (obj == NULL)
        return;

    s = (str != NULL) ? str : "";

    /* 指针始终更新（调用方可能换了缓冲区），内容未变则不重绘 */
    obj->text = str;
    if (strncmp(obj->shadow, s, obj->digit_cnt) == 0)
        return;

    strncpy(obj->shadow, s, obj->digit_cnt);
    obj->shadow[obj->digit_cnt] = '\0';
    we_obj_invalidate((we_obj_t *)obj);
}

void we_sevenseg_set_colors(we_sevenseg_obj_t *obj, colour_t on_color, colour_t off_color)
{
    if (obj == NULL)
        return;
    if (_sevenseg_colour_eq(obj->on_color, on_color) &&
        _sevenseg_colour_eq(obj->off_color, off_color))
        return;
    obj->on_color  = on_color;
    obj->off_color = off_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_sevenseg_set_ghost(we_sevenseg_obj_t *obj, uint8_t enable)
{
    uint8_t en = (uint8_t)(enable ? 1U : 0U);

    if (obj == NULL || obj->ghost == en)
        return;
    obj->ghost = en;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_sevenseg_set_opacity(we_sevenseg_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_sevenseg_set_pos(we_sevenseg_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || (obj->base.x == x && obj->base.y == y))
        return;
    we_obj_invalidate((we_obj_t *)obj); /* 旧位置 */
    obj->base.x = x;
    obj->base.y = y;
    we_obj_invalidate((we_obj_t *)obj); /* 新位置 */
}

void we_sevenseg_obj_delete(we_sevenseg_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
