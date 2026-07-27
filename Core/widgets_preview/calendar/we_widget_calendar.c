#include "we_widget_calendar.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * calendar —— 月视图日历（preview 孵化区）
 *
 * 结构：标题行（"YYYY-MM" + "<" ">" 翻月热区）+ 星期表头 + 6x7 日期网格。
 * 万年历为纯整数算法：闰年判断（四年一闰百年不闰四百年再闰）+
 * 基姆拉尔森公式求当月 1 日星期几。布局由 w/h 等分推导：
 * 列宽 = w/7，行高 = h/8，余数像素均分为四周留白。
 *
 * 热区编码（press_zone）：
 *   -1 无 / 0 上月 "<" / 1 下月 ">" / (2 + day) 日期格。
 * -------------------------------------------------------------------------- */

#define _CAL_ZONE_NONE (-1)
#define _CAL_ZONE_PREV 0
#define _CAL_ZONE_NEXT 1
#define _CAL_ZONE_DAY_BASE 2

/* 星期表头文本（列 0 = 周日，与 first_wday 的 0=周日 对齐） */
static const char *const _cal_wday_names[7] = {
    "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"
};

/* 平年每月天数表 */
static const uint8_t _cal_mdays_tab[12] = {
    31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
};

/* --------------------------------------------------------------------------
 * 万年历纯整数算法
 * -------------------------------------------------------------------------- */

/**
 * @brief 判断公历闰年（四年一闰、百年不闰、四百年再闰）。
 * @param year 传入：公历年份。
 * @return 1 闰年，0 平年。
 */
static uint8_t _cal_is_leap(uint16_t year)
{
    if ((year % 4U) != 0U)
        return 0U;
    if ((year % 100U) != 0U)
        return 1U;
    return ((year % 400U) == 0U) ? 1U : 0U;
}

/**
 * @brief 求指定年月的天数。
 * @param year 传入：公历年份。
 * @param month 传入：月份（1~12）。
 * @return 当月天数（28~31）。
 */
static uint8_t _cal_days_in_month(uint16_t year, uint8_t month)
{
    if (month == 2U && _cal_is_leap(year))
        return 29U;
    return _cal_mdays_tab[month - 1U];
}

/**
 * @brief 基姆拉尔森公式求当月 1 日星期几。
 * @param year 传入：公历年份（>= 1583）。
 * @param month 传入：月份（1~12）。
 * @return 星期序号（0=周日..6=周六，与表头列对齐）。
 * @note 公式原生输出 0=周一..6=周日，这里 +1 取模换算成 0=周日。
 */
static uint8_t _cal_first_wday(uint16_t year, uint8_t month)
{
    uint32_t y = year;
    uint32_t m = month;
    uint32_t w;

    if (m < 3U)
    {
        m += 12U;
        y -= 1U;
    }
    /* Kim-Larsen: W = (d + 2m + 3(m+1)/5 + y + y/4 - y/100 + y/400) % 7 */
    w = (1U + 2U * m + (3U * (m + 1U)) / 5U + y + y / 4U - y / 100U + y / 400U) % 7U;
    return (uint8_t)((w + 1U) % 7U);
}

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（按当前色深比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _cal_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 将透明度按控件整体不透明度缩放。
 * @param a 传入：原始透明度（0~255）。
 * @param opacity 传入：控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _cal_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/**
 * @brief 重新格式化标题文本缓存（"YYYY-MM"，纯整数拆位）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _cal_format_title(we_calendar_obj_t *obj)
{
    uint16_t y = obj->year;

    obj->title_buf[0] = (char)('0' + (y / 1000U) % 10U);
    obj->title_buf[1] = (char)('0' + (y / 100U) % 10U);
    obj->title_buf[2] = (char)('0' + (y / 10U) % 10U);
    obj->title_buf[3] = (char)('0' + y % 10U);
    obj->title_buf[4] = '-';
    obj->title_buf[5] = (char)('0' + obj->month / 10U);
    obj->title_buf[6] = (char)('0' + obj->month % 10U);
    obj->title_buf[7] = '\0';
}

/**
 * @brief 年月变化后刷新派生缓存：1 日星期几、当月天数、选中日钳制、标题。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _cal_refresh_month(we_calendar_obj_t *obj)
{
    obj->first_wday = _cal_first_wday(obj->year, obj->month);
    obj->month_days = _cal_days_in_month(obj->year, obj->month);
    if (obj->sel_day < 1U)
        obj->sel_day = 1U;
    if (obj->sel_day > obj->month_days)
        obj->sel_day = obj->month_days;
    _cal_format_title(obj);
}

/**
 * @brief 计算布局参数：列宽/行高与内容区左上留白偏移。
 * @param obj 传入：控件对象指针。
 * @param out_cw 传出：列宽（= w/7）。
 * @param out_rh 传出：行高（= h/8）。
 * @param out_ox 传出：内容区相对控件左缘偏移（余数像素居中）。
 * @param out_oy 传出：内容区相对控件上缘偏移。
 * @return 1 布局有效，0 控件尺寸退化。
 */
static uint8_t _cal_layout(const we_calendar_obj_t *obj, int16_t *out_cw, int16_t *out_rh,
                           int16_t *out_ox, int16_t *out_oy)
{
    int16_t cw = (int16_t)(obj->base.w / 7);
    int16_t rh = (int16_t)(obj->base.h / 8);

    if (cw <= 0 || rh <= 0)
        return 0U;

    *out_cw = cw;
    *out_rh = rh;
    *out_ox = (int16_t)((obj->base.w - 7 * cw) / 2);
    *out_oy = (int16_t)((obj->base.h - 8 * rh) / 2);
    return 1U;
}

/**
 * @brief 计算某日日期格外接矩形（屏幕绝对坐标）。
 * @param obj 传入：控件对象指针。
 * @param day 传入：日（1~当月天数）。
 * @param out_x 传出：格左上角 X。
 * @param out_y 传出：格左上角 Y。
 * @param out_w 传出：格宽。
 * @param out_h 传出：格高。
 * @return 1 成功，0 布局退化或日无效。
 */
static uint8_t _cal_day_rect(const we_calendar_obj_t *obj, uint8_t day,
                             int16_t *out_x, int16_t *out_y, int16_t *out_w, int16_t *out_h)
{
    int16_t cw;
    int16_t rh;
    int16_t ox;
    int16_t oy;
    int16_t idx;

    if (day < 1U || day > obj->month_days)
        return 0U;
    if (!_cal_layout(obj, &cw, &rh, &ox, &oy))
        return 0U;

    idx = (int16_t)((int16_t)obj->first_wday + (int16_t)day - 1);
    *out_x = (int16_t)(obj->base.x + ox + (idx % 7) * cw);
    *out_y = (int16_t)(obj->base.y + oy + (2 + idx / 7) * rh);
    *out_w = cw;
    *out_h = rh;
    return 1U;
}

/**
 * @brief 命中测试：把触摸点映射为热区编码。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触摸 X（屏幕绝对坐标）。
 * @param py 传入：触摸 Y。
 * @return 热区编码（_CAL_ZONE_xx / _CAL_ZONE_DAY_BASE+day），未命中返回 -1。
 * @note 翻月热区 = 标题行左/右各 2 列宽；日期热区 = 有效日所在整格。
 */
static int16_t _cal_hit_zone(const we_calendar_obj_t *obj, int16_t px, int16_t py)
{
    int16_t cw;
    int16_t rh;
    int16_t ox;
    int16_t oy;
    int16_t lx;
    int16_t ly;
    int16_t col;
    int16_t row;
    int16_t day;

    if (!_cal_layout(obj, &cw, &rh, &ox, &oy))
        return _CAL_ZONE_NONE;

    lx = (int16_t)(px - obj->base.x - ox);
    ly = (int16_t)(py - obj->base.y - oy);
    if (lx < 0 || ly < 0 || lx >= 7 * cw || ly >= 8 * rh)
        return _CAL_ZONE_NONE;

    row = (int16_t)(ly / rh);
    col = (int16_t)(lx / cw);

    if (row == 0)
    {
        /* 标题行：左 2 列 = 上月，右 2 列 = 下月，中间不响应 */
        if (col <= 1)
            return _CAL_ZONE_PREV;
        if (col >= 5)
            return _CAL_ZONE_NEXT;
        return _CAL_ZONE_NONE;
    }
    if (row == 1)
        return _CAL_ZONE_NONE; /* 星期表头不响应 */

    day = (int16_t)((row - 2) * 7 + col - (int16_t)obj->first_wday + 1);
    if (day < 1 || day > (int16_t)obj->month_days)
        return _CAL_ZONE_NONE; /* 当月外留空格 */
    return (int16_t)(_CAL_ZONE_DAY_BASE + day);
}

/**
 * @brief 在指定矩形内水平/垂直居中绘制一段文本。
 * @param obj 传入：控件对象指针。
 * @param str 传入：UTF-8 文本。
 * @param cx 传入：矩形左上角 X。
 * @param cy 传入：矩形左上角 Y。
 * @param cw 传入：矩形宽。
 * @param ch 传入：矩形高。
 * @param color 传入：文字颜色。
 * @return 无。
 */
static void _cal_draw_text_center(we_calendar_obj_t *obj, const char *str,
                                  int16_t cx, int16_t cy, int16_t cw, int16_t ch, colour_t color)
{
    uint16_t tw = we_get_text_width(obj->font, str);
    int8_t y_top;
    int8_t y_bot;
    int16_t tx;
    int16_t ty;

    we_get_text_bbox(obj->font, str, &y_top, &y_bot);
    tx = (int16_t)(cx + cw / 2 - (int16_t)(tw / 2U));
    ty = (int16_t)(cy + ch / 2 - (y_top + y_bot) / 2);
    we_draw_string(obj->base.lcd, tx, ty, obj->font, str, color, obj->opacity);
}

/**
 * @brief 绘制圆角描边环（外轮廓减内轮廓的逐像素合成，用于今日环）。
 * @param obj 传入：控件对象指针。
 * @param bx 传入：环外接矩形左上角 X。
 * @param by 传入：环外接矩形左上角 Y。
 * @param bw 传入：环外接矩形宽。
 * @param bh 传入：环外接矩形高。
 * @param radius 传入：外轮廓圆角半径。
 * @param color 传入：环颜色。
 * @return 无。
 * @note 逐像素调用 we_mask_round_rect_alpha 两次（外/内），仅覆盖单个
 *       日期格 K×K 区域，纯整数；退化尺寸（比环厚还小）退为整块填充。
 */
static void _cal_draw_ring(we_calendar_obj_t *obj, int16_t bx, int16_t by,
                           int16_t bw, int16_t bh, uint16_t radius, colour_t color)
{
    we_lcd_t *lcd = obj->base.lcd;
    const int16_t t = (int16_t)WE_CALENDAR_RING_W;
    uint16_t r_in;
    int16_t px;
    int16_t py;

    if (bw <= 2 * t || bh <= 2 * t)
    {
        we_draw_round_rect_analytic_fill(lcd, bx, by, (uint16_t)bw, (uint16_t)bh,
                                         radius, color, obj->opacity);
        return;
    }

    r_in = (radius > (uint16_t)t) ? (uint16_t)(radius - (uint16_t)t) : 0U;

    for (py = by; py < (int16_t)(by + bh); py++)
    {
        for (px = bx; px < (int16_t)(bx + bw); px++)
        {
            uint8_t a_out = we_mask_round_rect_alpha(bx, by, (uint16_t)bw, (uint16_t)bh,
                                                     radius, px, py);
            uint8_t a_in = we_mask_round_rect_alpha((int16_t)(bx + t), (int16_t)(by + t),
                                                    (uint16_t)(bw - 2 * t), (uint16_t)(bh - 2 * t),
                                                    r_in, px, py);
            uint8_t ring = (a_out > a_in) ? (uint8_t)(a_out - a_in) : 0U;

            if (ring != 0U)
                we_draw_pixel(lcd, px, py, color, _cal_scale_opa(ring, obj->opacity));
        }
    }
}

/**
 * @brief 触发选中变化回调（仅用户交互路径调用）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _cal_fire_changed(we_calendar_obj_t *obj)
{
    if (obj->changed_cb != NULL)
        obj->changed_cb(obj, obj->year, obj->month, obj->sel_day);
}

/**
 * @brief 内部翻月：dir = +1 下月 / -1 上月，跨年自动进退并钳制年限。
 * @param obj 传入：控件对象指针。
 * @param dir 传入：翻月方向（+1 / -1）。
 * @return 1 年月发生变化，0 已到边界未变化。
 */
static uint8_t _cal_flip_month(we_calendar_obj_t *obj, int8_t dir)
{
    uint16_t y = obj->year;
    uint8_t m = obj->month;

    if (dir > 0)
    {
        if (m == 12U)
        {
            if (y >= (uint16_t)WE_CALENDAR_MAX_YEAR)
                return 0U; /* 上边界：9999-12 不再下翻 */
            y++;
            m = 1U;
        }
        else
        {
            m++;
        }
    }
    else
    {
        if (m == 1U)
        {
            if (y <= (uint16_t)WE_CALENDAR_MIN_YEAR)
                return 0U; /* 下边界：1583-01 不再上翻 */
            y--;
            m = 12U;
        }
        else
        {
            m--;
        }
    }

    obj->year = y;
    obj->month = m;
    _cal_refresh_month(obj);
    we_obj_invalidate((we_obj_t *)obj); /* preview：整控件包围盒标脏 */
    return 1U;
}

/* --------------------------------------------------------------------------
 * 绘制回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：标题行 + 星期表头 + 6x7 日期网格。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 * @note preview 放宽：每次重绘遍历全部内容，越出 PFB 的写入由原语裁剪丢弃。
 */
static void _calendar_draw_cb(void *ptr)
{
    we_calendar_obj_t *obj = (we_calendar_obj_t *)ptr;
    int16_t cw;
    int16_t rh;
    int16_t ox;
    int16_t oy;
    int16_t x0;
    int16_t y0;
    int16_t col;
    uint8_t day;
    uint8_t today_here;

    if (obj == NULL || obj->opacity == 0U || obj->base.lcd == NULL || obj->font == NULL)
        return;
    if (!_cal_layout(obj, &cw, &rh, &ox, &oy))
        return;

    x0 = (int16_t)(obj->base.x + ox);
    y0 = (int16_t)(obj->base.y + oy);

    /* 1. 标题行：翻月热区按压反馈块 + "<" ">" 箭头 + 居中年月 */
    if (obj->pressed && (obj->press_zone == _CAL_ZONE_PREV || obj->press_zone == _CAL_ZONE_NEXT))
    {
        int16_t zx = (obj->press_zone == _CAL_ZONE_PREV) ? x0 : (int16_t)(x0 + 5 * cw);

        we_draw_round_rect_analytic_fill(obj->base.lcd, (int16_t)(zx + 2), (int16_t)(y0 + 2),
                                         (uint16_t)(2 * cw - 4), (uint16_t)(rh - 4),
                                         (uint16_t)((rh - 4) / 2), obj->sel_bg_color,
                                         _cal_scale_opa(WE_CALENDAR_PRESS_OPA, obj->opacity));
    }
    _cal_draw_text_center(obj, "<", x0, y0, (int16_t)(2 * cw), rh, obj->title_color);
    _cal_draw_text_center(obj, ">", (int16_t)(x0 + 5 * cw), y0, (int16_t)(2 * cw), rh,
                          obj->title_color);
    _cal_draw_text_center(obj, obj->title_buf, x0, y0, (int16_t)(7 * cw), rh, obj->title_color);

    /* 2. 星期表头 Su Mo Tu We Th Fr Sa */
    for (col = 0; col < 7; col++)
    {
        _cal_draw_text_center(obj, _cal_wday_names[col], (int16_t)(x0 + col * cw),
                              (int16_t)(y0 + rh), cw, rh, obj->weekday_color);
    }

    /* 3. 日期网格：当月外留空，选中块 / 按压块 / 今日环 / 日期数字 */
    today_here = (uint8_t)(obj->today_year != 0U &&
                           obj->today_year == obj->year && obj->today_month == obj->month);

    for (day = 1U; day <= obj->month_days; day++)
    {
        int16_t dx;
        int16_t dy;
        int16_t dw;
        int16_t dh;
        int16_t bw;
        int16_t bh;
        uint16_t br;
        char num_buf[3];

        if (!_cal_day_rect(obj, day, &dx, &dy, &dw, &dh))
            continue;

        /* 高亮块几何：格内缩 2px 的圆角块 */
        bw = (int16_t)(dw - 4);
        bh = (int16_t)(dh - 4);
        if (bw < 4 || bh < 4)
        {
            bw = dw;
            bh = dh;
        }
        br = (uint16_t)(((bw < bh) ? bw : bh) / 3);

        if (day == obj->sel_day)
        {
            /* 选中日：实心圆角高亮块 */
            we_draw_round_rect_analytic_fill(obj->base.lcd,
                                             (int16_t)(dx + (dw - bw) / 2),
                                             (int16_t)(dy + (dh - bh) / 2),
                                             (uint16_t)bw, (uint16_t)bh, br,
                                             obj->sel_bg_color, obj->opacity);
        }
        else if (obj->pressed &&
                 obj->press_zone == (int16_t)(_CAL_ZONE_DAY_BASE + (int16_t)day))
        {
            /* 按压中的日期格：半透明预览块 */
            we_draw_round_rect_analytic_fill(obj->base.lcd,
                                             (int16_t)(dx + (dw - bw) / 2),
                                             (int16_t)(dy + (dh - bh) / 2),
                                             (uint16_t)bw, (uint16_t)bh, br,
                                             obj->sel_bg_color,
                                             _cal_scale_opa(WE_CALENDAR_PRESS_OPA, obj->opacity));
        }

        if (today_here && day == obj->today_day)
        {
            /* 今日：圆角描边环（叠加在选中块之上仍可辨识） */
            _cal_draw_ring(obj, (int16_t)(dx + (dw - bw) / 2), (int16_t)(dy + (dh - bh) / 2),
                           bw, bh, br, obj->today_ring_color);
        }

        /* 日期数字（1~31，无前导零） */
        if (day >= 10U)
        {
            num_buf[0] = (char)('0' + day / 10U);
            num_buf[1] = (char)('0' + day % 10U);
            num_buf[2] = '\0';
        }
        else
        {
            num_buf[0] = (char)('0' + day);
            num_buf[1] = '\0';
        }
        _cal_draw_text_center(obj, num_buf, dx, dy, dw, dh, obj->day_color);
    }
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调：热区按压反馈 / 点击提交 / 滑动翻月。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 1 表示消费事件，0 表示穿透。
 */
static uint8_t _calendar_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_calendar_obj_t *obj = (we_calendar_obj_t *)ptr;
    int16_t zone;

    if (obj == NULL || data == NULL)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        zone = _cal_hit_zone(obj, data->x, data->y);
        obj->press_zone = zone;
        if (zone != _CAL_ZONE_NONE)
        {
            obj->pressed = 1U;
            we_obj_invalidate((we_obj_t *)obj); /* 显示按压反馈 */
        }
        return 1U; /* 控件矩形内一律消费（保证后续 SWIPE 派发到本控件） */

    case WE_EVENT_STAY:
        /* 拖出原热区：撤销按压反馈，本次触摸不再产生点击提交 */
        if (obj->pressed &&
            _cal_hit_zone(obj, data->x, data->y) != obj->press_zone)
        {
            obj->pressed = 0U;
            obj->press_zone = _CAL_ZONE_NONE;
            we_obj_invalidate((we_obj_t *)obj);
        }
        return 1U;

    case WE_EVENT_RELEASED:
        if (obj->pressed)
        {
            obj->pressed = 0U;
            we_obj_invalidate((we_obj_t *)obj); /* 清除按压反馈 */
        }
        /* press_zone 留给紧随其后的 CLICKED 复核 */
        return 1U;

    case WE_EVENT_CLICKED:
        zone = _cal_hit_zone(obj, data->x, data->y);
        if (zone != _CAL_ZONE_NONE && zone == obj->press_zone)
        {
            if (zone == _CAL_ZONE_PREV)
            {
                if (_cal_flip_month(obj, -1))
                    _cal_fire_changed(obj);
            }
            else if (zone == _CAL_ZONE_NEXT)
            {
                if (_cal_flip_month(obj, 1))
                    _cal_fire_changed(obj);
            }
            else
            {
                uint8_t day = (uint8_t)(zone - _CAL_ZONE_DAY_BASE);

                if (day != obj->sel_day)
                {
                    obj->sel_day = day;
                    we_obj_invalidate((we_obj_t *)obj);
                    _cal_fire_changed(obj);
                }
            }
        }
        obj->press_zone = _CAL_ZONE_NONE;
        return 1U;

    case WE_EVENT_SWIPE_LEFT: /* 内容左滑 = 翻下月 */
        obj->press_zone = _CAL_ZONE_NONE;
        if (_cal_flip_month(obj, 1))
            _cal_fire_changed(obj);
        return 1U;

    case WE_EVENT_SWIPE_RIGHT: /* 内容右滑 = 翻上月 */
        obj->press_zone = _CAL_ZONE_NONE;
        if (_cal_flip_month(obj, -1))
            _cal_fire_changed(obj);
        return 1U;

    default:
        break;
    }
    return 0U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_CALENDAR_USE_KEY == 1)
/**
 * @brief 键控移动选中日：日内移动直接选中，越过月首/月尾自动翻月落位。
 * @param obj 传入：控件对象指针。
 * @param delta 传入：日偏移（左右 ±1，上下 ±7）。
 * @return 无。
 * @note 与触摸路径同语义：任何选中/翻月变化都触发 changed_cb；
 *       到达 WE_CALENDAR_MIN/MAX_YEAR 边界时翻月失败，保持原选中不动。
 */
static void _cal_key_move(we_calendar_obj_t *obj, int8_t delta)
{
    int16_t target = (int16_t)obj->sel_day + (int16_t)delta;
    uint8_t old = obj->sel_day;

    if (target >= 1 && target <= (int16_t)obj->month_days)
    {
        obj->sel_day = (uint8_t)target;
        we_obj_invalidate((we_obj_t *)obj);
        _cal_fire_changed(obj);
    }
    else if (target < 1)
    {
        /* 越过月首：翻上月，落到"再往前 |delta| 天"的对应日（月尾方向） */
        if (!_cal_flip_month(obj, -1))
            return;
        obj->sel_day = (uint8_t)((int16_t)old + (int16_t)delta + (int16_t)obj->month_days);
        _cal_fire_changed(obj);
    }
    else
    {
        /* 越过月尾：翻下月，落到"再往后 delta 天"的对应日（月首方向） */
        int16_t old_days = (int16_t)obj->month_days;

        if (!_cal_flip_month(obj, 1))
            return;
        obj->sel_day = (uint8_t)((int16_t)old + (int16_t)delta - old_days);
        _cal_fire_changed(obj);
    }
}

/**
 * @brief 按键/焦点回调：OK 进出编辑态，编辑态方向键在日期网格内移动选中日。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note BACK 不处理，交焦点管理器退出编辑态。
 */
static uint8_t _calendar_key_cb(void *ptr, uint8_t key_evt)
{
    we_calendar_obj_t *obj = (we_calendar_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (we_focus_edit_active(lcd))
            we_focus_edit_exit(lcd);
        else
            we_focus_edit_enter(lcd);
        return 1U;
    case WE_KEY_LEFT:
    case WE_KEY_RIGHT:
    case WE_KEY_UP:
    case WE_KEY_DOWN:
        if (!we_focus_edit_active(lcd))
            return 0U; /* 导航态：方向键交焦点管理器移动焦点 */
        _cal_key_move(obj, (key_evt == WE_KEY_LEFT)  ? (int8_t)-1
                         : (key_evt == WE_KEY_RIGHT) ? (int8_t)1
                         : (key_evt == WE_KEY_UP)    ? (int8_t)-7
                                                     : (int8_t)7);
        return 1U;
    default:
        return 0U;
    }
}
#endif

static const we_class_t _calendar_class = {
    .draw_cb = _calendar_draw_cb,
    .event_cb = _calendar_event_cb,
    .set_pos_cb = NULL, /* 通用移动逻辑（旧区标脏 + 新区标脏）已足够 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_CALENDAR_USE_KEY == 1)
    .key_cb = _calendar_key_cb,
#endif
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化日历控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param h 传入：控件高度（像素）。
 * @return 无。
 */
void we_calendar_obj_init(we_calendar_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_calendar_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->year = 2000U;
    obj->month = 1U;
    obj->sel_day = 1U;

    obj->today_year = 0U; /* 默认不显示今日环 */
    obj->today_month = 0U;
    obj->today_day = 0U;

    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;
    obj->title_color = RGB888TODEV(236, 241, 248);
    obj->weekday_color = RGB888TODEV(130, 148, 176);
    obj->day_color = RGB888TODEV(214, 221, 233);
    obj->sel_bg_color = RGB888TODEV(56, 128, 220);
    obj->today_ring_color = RGB888TODEV(120, 230, 205);
    obj->opacity = 255U;

    obj->changed_cb = NULL;
    obj->press_zone = _CAL_ZONE_NONE;
    obj->pressed = 0U;

    _cal_refresh_month(obj);

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置显示的年月（程序设置，不触发回调）。
 * @param obj 传入：控件对象指针。
 * @param year 传入：年份，钳制到 [WE_CALENDAR_MIN_YEAR, WE_CALENDAR_MAX_YEAR]。
 * @param month 传入：月份，钳制到 [1, 12]。
 * @return 无。
 */
void we_calendar_set_month(we_calendar_obj_t *obj, uint16_t year, uint8_t month)
{
    if (obj == NULL)
        return;

    if (year < (uint16_t)WE_CALENDAR_MIN_YEAR)
        year = (uint16_t)WE_CALENDAR_MIN_YEAR;
    if (year > (uint16_t)WE_CALENDAR_MAX_YEAR)
        year = (uint16_t)WE_CALENDAR_MAX_YEAR;
    if (month < 1U)
        month = 1U;
    if (month > 12U)
        month = 12U;

    if (obj->year == year && obj->month == month)
        return;

    obj->year = year;
    obj->month = month;
    _cal_refresh_month(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置选中日（越界钳制到当月范围，程序设置不触发回调）。
 * @param obj 传入：控件对象指针。
 * @param day 传入：目标日。
 * @return 无。
 */
void we_calendar_set_selected(we_calendar_obj_t *obj, uint8_t day)
{
    if (obj == NULL)
        return;

    if (day < 1U)
        day = 1U;
    if (day > obj->month_days)
        day = obj->month_days;

    if (obj->sel_day == day)
        return;

    obj->sel_day = day;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 读取当前选中的完整日期。
 * @param obj 传入：控件对象指针。
 * @param out_year 传出：选中年份（可传 NULL）。
 * @param out_month 传出：选中月份（可传 NULL）。
 * @param out_day 传出：选中日（可传 NULL）。
 * @return 无。
 */
void we_calendar_get_selected(const we_calendar_obj_t *obj,
                              uint16_t *out_year, uint8_t *out_month, uint8_t *out_day)
{
    if (obj == NULL)
        return;
    if (out_year != NULL)
        *out_year = obj->year;
    if (out_month != NULL)
        *out_month = obj->month;
    if (out_day != NULL)
        *out_day = obj->sel_day;
}

/**
 * @brief 翻到上一个月（跨年自动退位，程序设置不触发回调）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_calendar_prev_month(we_calendar_obj_t *obj)
{
    if (obj == NULL)
        return;
    (void)_cal_flip_month(obj, -1);
}

/**
 * @brief 翻到下一个月（跨年自动进位，程序设置不触发回调）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_calendar_next_month(we_calendar_obj_t *obj)
{
    if (obj == NULL)
        return;
    (void)_cal_flip_month(obj, 1);
}

/**
 * @brief 设置选中日期变化回调。
 * @param obj 传入：控件对象指针。
 * @param cb 传入：回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_calendar_set_changed_cb(we_calendar_obj_t *obj, we_calendar_changed_cb_t cb)
{
    if (obj == NULL || obj->changed_cb == cb)
        return;
    obj->changed_cb = cb;
}

/**
 * @brief 设置五项配色（全部未变时直接返回）。
 * @param obj 传入：控件对象指针。
 * @param title 传入：标题文字色。
 * @param weekday 传入：星期表头文字色。
 * @param day_text 传入：日期数字文字色。
 * @param sel_bg 传入：选中块色。
 * @param today_ring 传入：今日环色。
 * @return 无。
 */
void we_calendar_set_colors(we_calendar_obj_t *obj, colour_t title, colour_t weekday,
                            colour_t day_text, colour_t sel_bg, colour_t today_ring)
{
    if (obj == NULL)
        return;
    if (_cal_colour_eq(obj->title_color, title) &&
        _cal_colour_eq(obj->weekday_color, weekday) &&
        _cal_colour_eq(obj->day_color, day_text) &&
        _cal_colour_eq(obj->sel_bg_color, sel_bg) &&
        _cal_colour_eq(obj->today_ring_color, today_ring))
        return;

    obj->title_color = title;
    obj->weekday_color = weekday;
    obj->day_color = day_text;
    obj->sel_bg_color = sel_bg;
    obj->today_ring_color = today_ring;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置"今日"标记日期（值未变时直接返回）。
 * @param obj 传入：控件对象指针。
 * @param year 传入：今日年份，0 = 关闭今日环。
 * @param month 传入：今日月份。
 * @param day 传入：今日日。
 * @return 无。
 */
void we_calendar_set_today(we_calendar_obj_t *obj, uint16_t year, uint8_t month, uint8_t day)
{
    if (obj == NULL)
        return;

    /* 非法日期一律视为关闭今日环 */
    if (year < (uint16_t)WE_CALENDAR_MIN_YEAR || month < 1U || month > 12U ||
        day < 1U || day > _cal_days_in_month(year, month))
    {
        year = 0U;
        month = 0U;
        day = 0U;
    }

    if (obj->today_year == year && obj->today_month == month && obj->today_day == day)
        return;

    obj->today_year = year;
    obj->today_month = month;
    obj->today_day = day;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 传入：控件对象指针。
 * @param opacity 传入：不透明度（0~255）。
 * @return 无。
 */
void we_calendar_set_opacity(we_calendar_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除日历控件并从对象链表移除（无动画节点，无需摘链）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_calendar_obj_delete(we_calendar_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
