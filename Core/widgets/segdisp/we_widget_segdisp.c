/**
 * @file  we_widget_segdisp.c
 * @brief 段码数码管控件（segdisp）实现
 *
 * 渲染思路：
 *   1. 每个字符占一个等宽单元格，经典 a~g 七段布局 + 右下角 dp 点；
 *   2. 段形两种风格：直角矩形（每段一次 we_fill_rect）/ 45° 斜切收尖
 *      （逐行/逐列 1px 扫描、向段中线收缩，段厚 < 3 自动退化为矩形）；
 *   3. 冒号位画上下两个 t×t 小方点（熄灭时 ghost 画暗点）；
 *   4. 内容统一存为"每位一个段码字节"（bit0~6=a~g，bit7=dp）+ 冒号位
 *      标记位图；set_text 在设置时解码，绘制端零解码零查表。
 *
 * 标脏：所有内容 setter 汇聚到 _segdisp_cell_write() 逐位 diff，
 * 只把段码/冒号发生变化的单元格提交给脏矩形管理器。
 */

#include "we_widget_segdisp.h"
#include "we_render.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _segdisp_draw_cb(void *ptr);
static uint8_t _segdisp_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);
static void    _segdisp_set_pos_cb(void *ptr, int16_t x, int16_t y);

static const we_class_t _segdisp_class = {
    .draw_cb    = _segdisp_draw_cb,
    .event_cb   = _segdisp_event_cb,
    .set_pos_cb = _segdisp_set_pos_cb
};

/* '0'~'9' + 'A'~'F' 段码表（bit0~bit6 = a~g，经典数码管编码） */
static const uint8_t _segdisp_digit_map[16] = {
    0x3FU, /* 0 */
    0x06U, /* 1 */
    0x5BU, /* 2 */
    0x4FU, /* 3 */
    0x66U, /* 4 */
    0x6DU, /* 5 */
    0x7DU, /* 6 */
    0x07U, /* 7 */
    0x7FU, /* 8 */
    0x6FU, /* 9 */
    0x77U, /* A */
    0x7CU, /* b */
    0x39U, /* C */
    0x5EU, /* d */
    0x79U, /* E */
    0x71U  /* F */
};

/**
 * @brief 颜色相等比较（RGB565/RGB888），供 setter 的"值未变则跳过"守卫使用。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 = 相等，0 = 不等。
 */
static __inline uint8_t _segdisp_colour_eq(colour_t a, colour_t b)
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
 * @return 段码位图（bit0~bit6 = a~g，bit7 = dp），不支持的字符返回 0（全灭）。
 * @note ':' 不走段码路径，由 set_text 置冒号位标记。
 */
static uint8_t _segdisp_decode(char ch)
{
    if (ch >= '0' && ch <= '9')
        return _segdisp_digit_map[ch - '0'];
    if (ch >= 'A' && ch <= 'F')
        return _segdisp_digit_map[ch - 'A' + 10];
    if (ch >= 'a' && ch <= 'f')
        return _segdisp_digit_map[ch - 'a' + 10];
    if (ch == '-')
        return WE_SEGDISP_SEG_G;
    if (ch == '.')
        return WE_SEGDISP_SEG_DP;
    return 0x00U; /* ' ' 与其余字符：全灭 */
}

/**
 * @brief 读取某位的冒号标记。
 * @param o 传入：控件对象指针。
 * @param pos 传入：位索引（调用方保证 < digit_cnt）。
 * @return 1 = 该位是冒号位，0 = 数字位。
 */
static __inline uint8_t _segdisp_colon_get(const we_segdisp_obj_t *o, uint8_t pos)
{
    return (uint8_t)((o->colon_mask[pos >> 3] >> (pos & 0x07U)) & 0x01U);
}

/**
 * @brief 写入某位的冒号标记。
 * @param o 传入：控件对象指针。
 * @param pos 传入：位索引（调用方保证 < digit_cnt）。
 * @param on 传入：1 = 置为冒号位，0 = 数字位。
 * @return 无。
 */
static __inline void _segdisp_colon_put(we_segdisp_obj_t *o, uint8_t pos, uint8_t on)
{
    uint8_t bit = (uint8_t)(1U << (pos & 0x07U));

    if (on)
        o->colon_mask[pos >> 3] |= bit;
    else
        o->colon_mask[pos >> 3] &= (uint8_t)~bit;
}

/**
 * @brief 计算某位单元格左上角的屏幕绝对 X。
 * @param o 传入：控件对象指针。
 * @param pos 传入：位索引。
 * @return 单元格左上角 X。
 */
static __inline int16_t _segdisp_cell_x(const we_segdisp_obj_t *o, uint8_t pos)
{
    return (int16_t)(o->base.x + (int16_t)pos * (int16_t)(o->digit_w + o->gap));
}

/**
 * @brief 内容写入汇聚点：逐位 diff，只有段码/冒号变化的位才标脏。
 * @param o 传入：控件对象指针。
 * @param pos 传入：位索引（调用方保证 < digit_cnt）。
 * @param code 传入：新段码。
 * @param colon 传入：新冒号标记（0/1）。
 * @return 无。
 */
static void _segdisp_cell_write(we_segdisp_obj_t *o, uint8_t pos, uint8_t code, uint8_t colon)
{
    if (o->segs[pos] == code && _segdisp_colon_get(o, pos) == colon)
        return;

    o->segs[pos] = code;
    _segdisp_colon_put(o, pos, colon);
    we_obj_invalidate_area((we_obj_t *)o, _segdisp_cell_x(o, pos), o->base.y,
                           (int16_t)o->digit_w, (int16_t)o->digit_h);
}

/**
 * @brief 绘制一条段（水平/垂直），可选 45° 斜切收尖。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param x 传入：段包围盒左上角 X（屏幕绝对坐标）。
 * @param y 传入：段包围盒左上角 Y。
 * @param len 传入：段长（沿段方向）。
 * @param t 传入：段厚。
 * @param vertical 传入：1 = 垂直段，0 = 水平段。
 * @param bevel 传入：1 = 两端向中线 45° 斜切收尖（六边形段），0 = 直角矩形。
 * @param color 传入：填充颜色。
 * @param opa 传入：不透明度。
 * @return 无。
 * @note 斜切用逐行/逐列 1px 填充实现：第 k 行（列）距段中线越远收缩
 *       越多（inset = |2k-(t-1)|/2），六边形内切于原矩形，纯整数运算。
 *       段厚 < 3 时无可见斜切，直接走整块矩形。
 */
static void _segdisp_draw_seg(we_lcd_t *lcd, int16_t x, int16_t y,
                               uint16_t len, uint16_t t, uint8_t vertical,
                               uint8_t bevel, colour_t color, uint8_t opa)
{
    uint16_t k;
    uint16_t d;
    uint16_t inset;

    if (!bevel || t < 3U)
    {
        if (vertical)
            we_fill_rect(lcd, x, y, t, len, color, opa);
        else
            we_fill_rect(lcd, x, y, len, t, color, opa);
        return;
    }

    for (k = 0U; k < t; k++)
    {
        d = (uint16_t)((2U * k >= (uint16_t)(t - 1U)) ? (2U * k - (t - 1U))
                                                      : ((t - 1U) - 2U * k));
        inset = (uint16_t)(d / 2U);
        if ((uint16_t)(2U * inset) >= len)
            continue; /* 极端窄段保护 */

        if (vertical)
            we_fill_rect(lcd, (int16_t)(x + (int16_t)k), (int16_t)(y + (int16_t)inset),
                         1U, (uint16_t)(len - 2U * inset), color, opa);
        else
            we_fill_rect(lcd, (int16_t)(x + (int16_t)inset), (int16_t)(y + (int16_t)k),
                         (uint16_t)(len - 2U * inset), 1U, color, opa);
    }
}

/**
 * @brief 绘制一个数字单元格（a~g 亮段 + 可选鬼影灭段 + dp 点）。
 * @param o 传入：控件对象指针。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param dx 传入：单元格左上角 X（屏幕绝对坐标）。
 * @param dy 传入：单元格左上角 Y。
 * @param segs 传入：段码（bit0~6 = a~g，bit7 = dp）。
 * @return 无。
 */
static void _segdisp_draw_digit(const we_segdisp_obj_t *o, we_lcd_t *lcd,
                                 int16_t dx, int16_t dy, uint8_t segs)
{
    uint16_t t   = o->seg_t;
    uint16_t w   = o->digit_w;
    uint16_t h   = o->digit_h;
    uint16_t mid = (uint16_t)((h - t) / 2U);          /* g 段上沿的局部 Y */
    uint16_t hl  = (uint16_t)(w - 2U * t);            /* 横段长度 */
    uint16_t vt  = (uint16_t)(mid - t);               /* 上半竖段长度 */
    uint16_t vb  = (uint16_t)(h - mid - 2U * t);      /* 下半竖段长度 */
    uint8_t  bevel = (uint8_t)(o->style == WE_SEGDISP_STYLE_BEVEL);
    uint8_t  ghost_opa;
    uint8_t  i;

    /* 七段几何表：与段码 bit0~bit6 一一对应（len 沿段长方向） */
    struct
    {
        int16_t  sx, sy;
        uint16_t len;
        uint8_t  vert;
    } sg[7];

    sg[0].sx = (int16_t)(dx + (int16_t)t);       sg[0].sy = dy;                                 sg[0].len = hl; sg[0].vert = 0U; /* a */
    sg[1].sx = (int16_t)(dx + (int16_t)(w - t)); sg[1].sy = (int16_t)(dy + (int16_t)t);         sg[1].len = vt; sg[1].vert = 1U; /* b */
    sg[2].sx = (int16_t)(dx + (int16_t)(w - t)); sg[2].sy = (int16_t)(dy + (int16_t)(mid + t)); sg[2].len = vb; sg[2].vert = 1U; /* c */
    sg[3].sx = (int16_t)(dx + (int16_t)t);       sg[3].sy = (int16_t)(dy + (int16_t)(h - t));   sg[3].len = hl; sg[3].vert = 0U; /* d */
    sg[4].sx = dx;                               sg[4].sy = (int16_t)(dy + (int16_t)(mid + t)); sg[4].len = vb; sg[4].vert = 1U; /* e */
    sg[5].sx = dx;                               sg[5].sy = (int16_t)(dy + (int16_t)t);         sg[5].len = vt; sg[5].vert = 1U; /* f */
    sg[6].sx = (int16_t)(dx + (int16_t)t);       sg[6].sy = (int16_t)(dy + (int16_t)mid);       sg[6].len = hl; sg[6].vert = 0U; /* g */

    ghost_opa = we_div255((uint32_t)o->opacity * WE_SEGDISP_GHOST_OPA);

    for (i = 0U; i < 7U; i++)
    {
        if ((segs >> i) & 0x01U)
        {
            _segdisp_draw_seg(lcd, sg[i].sx, sg[i].sy, sg[i].len, t, sg[i].vert,
                               bevel, o->on_color, o->opacity);
        }
        else if (o->ghost && ghost_opa > 0U)
        {
            _segdisp_draw_seg(lcd, sg[i].sx, sg[i].sy, sg[i].len, t, sg[i].vert,
                               bevel, o->off_color, ghost_opa);
        }
    }

    /* dp：本位右下角空白角区内的小方点（上/左各留 1px 与 d/c 段分离）；
     * 仅在 dp_show 打开时绘制（bit7 可能被硬件段码表挪作它用） */
    if (o->dp_show && (segs & WE_SEGDISP_SEG_DP) != 0U)
    {
        we_fill_rect(lcd, (int16_t)(dx + (int16_t)(w - t) + 1),
                     (int16_t)(dy + (int16_t)(h - t) + 1),
                     (uint16_t)(t - 1U), (uint16_t)(t - 1U),
                     o->on_color, o->opacity);
    }
}

/**
 * @brief 绘制一个冒号单元格（上下两个 t×t 小方点）。
 * @param o 传入：控件对象指针。
 * @param lcd 传入：GUI 屏幕上下文指针。
 * @param dx 传入：单元格左上角 X（屏幕绝对坐标）。
 * @param dy 传入：单元格左上角 Y。
 * @param lit 传入：1 = 点亮（亮色），0 = 熄灭（ghost 开启时画暗点，否则不画）。
 * @return 无。
 */
static void _segdisp_draw_colon(const we_segdisp_obj_t *o, we_lcd_t *lcd,
                                 int16_t dx, int16_t dy, uint8_t lit)
{
    uint16_t t  = o->seg_t;
    int16_t  cx = (int16_t)(dx + (int16_t)((o->digit_w - t) / 2U));
    int16_t  y1 = (int16_t)(dy + (int16_t)(o->digit_h / 3U - t / 2U));
    int16_t  y2 = (int16_t)(dy + (int16_t)((o->digit_h * 2U) / 3U - t / 2U));
    colour_t col;
    uint8_t  opa;

    if (lit)
    {
        col = o->on_color;
        opa = o->opacity;
    }
    else
    {
        if (!o->ghost)
            return;
        col = o->off_color;
        opa = we_div255((uint32_t)o->opacity * WE_SEGDISP_GHOST_OPA);
        if (opa == 0U)
            return;
    }

    we_fill_rect(lcd, cx, y1, t, t, col, opa);
    we_fill_rect(lcd, cx, y2, t, t, col, opa);
}

/**
 * @brief 控件绘制回调：逐单元格按已解码段码/冒号标记绘制。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _segdisp_draw_cb(void *ptr)
{
    we_segdisp_obj_t *o = (we_segdisp_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t cell_x;
    uint8_t i;

    if (o == NULL || o->opacity == 0U)
        return;
    lcd = o->base.lcd;
    if (lcd == NULL)
        return;

    cell_x = o->base.x;

    for (i = 0U; i < o->digit_cnt; i++)
    {
        if (_segdisp_colon_get(o, i))
        {
            _segdisp_draw_colon(o, lcd, cell_x, o->base.y,
                                 (uint8_t)(o->segs[i] & 0x01U));
        }
        else
        {
            _segdisp_draw_digit(o, lcd, cell_x, o->base.y, o->segs[i]);
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
static uint8_t _segdisp_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
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
static void _segdisp_set_pos_cb(void *ptr, int16_t x, int16_t y)
{
    we_segdisp_set_pos((we_segdisp_obj_t *)ptr, x, y);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_segdisp_obj_init(we_segdisp_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y,
                          uint16_t digit_w, uint16_t digit_h, uint16_t gap,
                          uint8_t digit_cnt, uint8_t seg_t)
{
    uint16_t t;
    uint16_t t_max;
    uint16_t w;

    if (obj == NULL || lcd == NULL)
        return;

    if (digit_h < 16U)
        digit_h = 16U;
    if (digit_cnt < 1U)
        digit_cnt = 1U;
    if (digit_cnt > (uint8_t)WE_SEGDISP_MAX_CHARS)
        digit_cnt = (uint8_t)WE_SEGDISP_MAX_CHARS;

    /* 几何推导（digit_w/gap/seg_t 传 0 均走自动）：
     * 段厚   = 字高/8，钳到 [2, (字高-2)/3]，保证竖段长度 (字高-3t)/2 >= 1
     * 单字宽 = 字高/2，自定义与自动都钳到最小 3t，保证横段长度 >= t
     * 字间距 = 段厚 */
    t = (seg_t != 0U) ? (uint16_t)seg_t : (uint16_t)(digit_h / 8U);
    if (t < 2U)
        t = 2U;
    t_max = (uint16_t)((digit_h - 2U) / 3U);
    if (t > t_max)
        t = t_max;
    w = (digit_w != 0U) ? digit_w : (uint16_t)(digit_h / 2U);
    if (w < (uint16_t)(3U * t))
        w = (uint16_t)(3U * t);
    if (gap == 0U)
        gap = t;

    obj->base.lcd     = lcd;
    obj->base.class_p = &_segdisp_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)((uint16_t)digit_cnt * w + (uint16_t)(digit_cnt - 1U) * gap);
    obj->base.h       = (int16_t)digit_h;

    memset(obj->segs, 0, sizeof(obj->segs));
    memset(obj->colon_mask, 0, sizeof(obj->colon_mask));
    obj->digit_h   = digit_h;
    obj->digit_w   = w;
    obj->gap       = gap;
    obj->seg_t     = (uint8_t)t;
    obj->digit_cnt = digit_cnt;
    obj->ghost     = 0U;
    obj->dp_show   = 0U;
    obj->style     = WE_SEGDISP_STYLE_BEVEL;
    obj->opacity   = 255U;
    {
        colour_t on  = RGB888_CONST(WE_SEGDISP_DEF_ON_R, WE_SEGDISP_DEF_ON_G, WE_SEGDISP_DEF_ON_B);
        colour_t off = RGB888_CONST(WE_SEGDISP_DEF_OFF_R, WE_SEGDISP_DEF_OFF_G, WE_SEGDISP_DEF_OFF_B);
        obj->on_color  = on;
        obj->off_color = off;
    }

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_segdisp_set_text(we_segdisp_obj_t *obj, const char *str)
{
    const char *s;
    uint8_t cell;

    if (obj == NULL)
        return;

    s = (str != NULL) ? str : "";

    for (cell = 0U; cell < obj->digit_cnt; cell++)
    {
        char    ch    = '\0';
        uint8_t code  = 0U;
        uint8_t colon = 0U;

        if (*s != '\0')
        {
            ch = *s;
            s++;
        }

        if (ch == ':')
        {
            colon = 1U;
            code  = 0x01U; /* 冒号位：bit0 = 两点点亮 */
        }
        else
        {
            code = _segdisp_decode(ch);
            /* '.' 紧跟可显示字符时合并为前一位的 dp（"12.5" 占 3 位） */
            if (ch != '\0' && ch != '.' && *s == '.')
            {
                code |= WE_SEGDISP_SEG_DP;
                s++;
            }
        }

        _segdisp_cell_write(obj, cell, code, colon);
    }
}

void we_segdisp_set_segs(we_segdisp_obj_t *obj, const uint8_t *codes, uint8_t count)
{
    uint8_t i;

    if (obj == NULL)
        return;
    if (codes == NULL)
        count = 0U;

    for (i = 0U; i < obj->digit_cnt; i++)
        _segdisp_cell_write(obj, i, (i < count) ? codes[i] : 0U, 0U);
}

void we_segdisp_set_seg(we_segdisp_obj_t *obj, uint8_t pos, uint8_t code)
{
    if (obj == NULL || pos >= obj->digit_cnt)
        return;
    _segdisp_cell_write(obj, pos, code, 0U);
}

void we_segdisp_set_colon(we_segdisp_obj_t *obj, uint8_t pos, uint8_t on)
{
    if (obj == NULL || pos >= obj->digit_cnt)
        return;
    _segdisp_cell_write(obj, pos, (uint8_t)(on ? 0x01U : 0x00U), 1U);
}

void we_segdisp_set_style(we_segdisp_obj_t *obj, uint8_t style)
{
    if (obj == NULL || obj->style == style)
        return;
    obj->style = style;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_segdisp_set_colors(we_segdisp_obj_t *obj, colour_t on_color, colour_t off_color)
{
    if (obj == NULL)
        return;
    if (_segdisp_colour_eq(obj->on_color, on_color) &&
        _segdisp_colour_eq(obj->off_color, off_color))
        return;
    obj->on_color  = on_color;
    obj->off_color = off_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_segdisp_set_ghost(we_segdisp_obj_t *obj, uint8_t enable)
{
    uint8_t en = (uint8_t)(enable ? 1U : 0U);

    if (obj == NULL || obj->ghost == en)
        return;
    obj->ghost = en;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_segdisp_set_dp(we_segdisp_obj_t *obj, uint8_t enable)
{
    uint8_t en = (uint8_t)(enable ? 1U : 0U);

    if (obj == NULL || obj->dp_show == en)
        return;
    obj->dp_show = en;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_segdisp_set_opacity(we_segdisp_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_segdisp_set_pos(we_segdisp_obj_t *obj, int16_t x, int16_t y)
{
    if (obj == NULL || (obj->base.x == x && obj->base.y == y))
        return;
    we_obj_invalidate((we_obj_t *)obj); /* 旧位置 */
    obj->base.x = x;
    obj->base.y = y;
    we_obj_invalidate((we_obj_t *)obj); /* 新位置 */
}

void we_segdisp_obj_delete(we_segdisp_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
