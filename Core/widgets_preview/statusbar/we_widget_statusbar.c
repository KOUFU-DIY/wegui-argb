/**
 * @file  we_widget_statusbar.c
 * @brief 状态栏控件（statusbar）实现 —— preview 孵化区
 *
 * 深色底条 + 左侧时间文本 + 右侧右对齐排列的 信号/WiFi/电池 矢量图标。
 * 图标全部用 we_fill_rect 与 we_draw_round_rect_analytic_fill 拼装
 * （零图片资产、零浮点）：电池 = 双层圆角矩形抠出 1px 外壳 + 电极凸块 +
 * 百分比填充（充电叠三段矩形小闪电）；WiFi = 3 层逐级加宽的圆角短条
 * 叠扇形近似；信号 = 4 根递增高度小柱。未点亮层/柱低透明度呈现。
 */

#include "we_widget_statusbar.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 图标几何常量（图标高统一 12px，垂直居中于底条）
 * -------------------------------------------------------------------------- */
#define _SB_ICON_H     12  /* 图标统一高度 */
#define _SB_MARGIN_R   6   /* 图标区距右边缘 */
#define _SB_MARGIN_L   8   /* 时间文本距左边缘 */
#define _SB_ICON_GAP   5   /* 相邻图标间距 */

#define _SB_BAT_BODY_W 18  /* 电池外壳宽（不含电极凸块） */
#define _SB_BAT_NUB_W  2   /* 电极凸块宽 */
#define _SB_BAT_W      (_SB_BAT_BODY_W + _SB_BAT_NUB_W)
#define _SB_WIFI_W     12  /* WiFi 图标宽（顶层弧条宽） */
#define _SB_SIG_W      11  /* 信号图标宽（4 柱 x2px + 3 缝 x1px） */

/* 充电态电池填充色（经典充电绿）与小闪电三段矩形几何 */
static const colour_t _sb_charge_green = RGB888_CONST(90, 200, 110);

/* --------------------------------------------------------------------------
 * 内部回调声明
 * -------------------------------------------------------------------------- */
static void    _sb_draw_cb(void *ptr);
static uint8_t _sb_event_cb(void *ptr, we_event_t event, we_indev_data_t *data);

static const we_class_t _statusbar_class = {
    .draw_cb    = _sb_draw_cb,
    .event_cb   = _sb_event_cb,
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
static uint8_t _sb_col_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#else
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 将图层透明度与控件整体不透明度相乘。
 * @param a 图层透明度（0~255）。
 * @param opacity 控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _sb_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/* --------------------------------------------------------------------------
 * 图标绘制（bx/wx/sx = 图标左上角 X，iy = 图标顶 Y，均为屏幕绝对坐标）
 * -------------------------------------------------------------------------- */

/**
 * @brief 绘制电池图标：外壳 + 电极凸块 + 百分比填充 + 充电小闪电。
 * @param obj 控件对象指针。
 * @param bx 图标左上角 X。
 * @param iy 图标顶 Y。
 * @return 无。
 * @note 外壳 = 前景色圆角矩形再抠回底色内腔（留 1px 轮廓）；填充色：
 *       充电 = 充电绿，低电（< WE_STATUSBAR_LOW_PCT）= 低电色，否则前景色；
 *       小闪电以底色三段矩形叠在填充上（充电绿上的暗色镂空观感）。
 */
static void _sb_draw_battery(const we_statusbar_obj_t *obj, int16_t bx, int16_t iy)
{
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t opa = obj->opacity;
    colour_t fill_c;
    int16_t fw;

    /* 外壳轮廓：外层前景色圆角矩形 - 内层底色圆角矩形 = 1px 壳 */
    we_draw_round_rect_analytic_fill(lcd, bx, iy,
                                     (uint16_t)_SB_BAT_BODY_W, (uint16_t)_SB_ICON_H,
                                     3U, obj->fg_color, opa);
    we_draw_round_rect_analytic_fill(lcd, (int16_t)(bx + 1), (int16_t)(iy + 1),
                                     (uint16_t)(_SB_BAT_BODY_W - 2), (uint16_t)(_SB_ICON_H - 2),
                                     2U, obj->bg_color, opa);

    /* 右侧电极凸块（4px 高，垂直居中） */
    we_fill_rect(lcd, (int16_t)(bx + _SB_BAT_BODY_W), (int16_t)(iy + 4),
                 (uint16_t)_SB_BAT_NUB_W, 4U, obj->fg_color, opa);

    /* 内部填充：内腔 14x8（壳 1px + 间隙 1px），宽度按百分比四舍五入 */
    fill_c = obj->charging ? _sb_charge_green
             : ((obj->battery_pct < WE_STATUSBAR_LOW_PCT) ? obj->low_color
                                                          : obj->fg_color);
    fw = (int16_t)(((uint16_t)obj->battery_pct * 14U + 50U) / 100U);
    if (fw > 0)
        we_fill_rect(lcd, (int16_t)(bx + 2), (int16_t)(iy + 2),
                     (uint16_t)fw, 8U, fill_c, opa);

    /* 充电小闪电：6x8 框内三段矩形拼近似（右上块 / 中横杠 / 左下块），
     * 用前景色绘制——在绿色填充与暗色空腔上都保持可见（充电中
     * 电量低时填充尚未推进到闪电区域，底色画会隐形） */
    if (obj->charging)
    {
        int16_t lx = (int16_t)(bx + 6);
        int16_t ly = (int16_t)(iy + 2);
        we_fill_rect(lcd, (int16_t)(lx + 3), ly, 2U, 3U, obj->fg_color, opa);
        we_fill_rect(lcd, (int16_t)(lx + 1), (int16_t)(ly + 3), 4U, 2U, obj->fg_color, opa);
        we_fill_rect(lcd, (int16_t)(lx + 1), (int16_t)(ly + 5), 2U, 3U, obj->fg_color, opa);
    }
}

/**
 * @brief 绘制 WiFi 图标：3 层逐级加宽的圆角短条叠扇形近似。
 * @param obj 控件对象指针。
 * @param wx 图标左上角 X。
 * @param iy 图标顶 Y。
 * @return 无。
 * @note 自下而上点亮：level>=1 亮底点、>=2 亮中层、>=3 亮顶层；
 *       未点亮层以 WE_STATUSBAR_DIM_OPA 低透明度呈现。
 */
static void _sb_draw_wifi(const we_statusbar_obj_t *obj, int16_t wx, int16_t iy)
{
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t dim = _sb_scale_opa(WE_STATUSBAR_DIM_OPA, obj->opacity);
    uint8_t opa;

    /* 顶层弧条 12x3（level >= 3 点亮） */
    opa = (obj->wifi_level >= 3) ? obj->opacity : dim;
    we_draw_round_rect_analytic_fill(lcd, wx, iy, 12U, 3U, 1U, obj->fg_color, opa);

    /* 中层弧条 8x3（level >= 2 点亮） */
    opa = (obj->wifi_level >= 2) ? obj->opacity : dim;
    we_draw_round_rect_analytic_fill(lcd, (int16_t)(wx + 2), (int16_t)(iy + 4),
                                     8U, 3U, 1U, obj->fg_color, opa);

    /* 底部一点 4x3（level >= 1 点亮） */
    opa = (obj->wifi_level >= 1) ? obj->opacity : dim;
    we_draw_round_rect_analytic_fill(lcd, (int16_t)(wx + 4), (int16_t)(iy + 8),
                                     4U, 3U, 1U, obj->fg_color, opa);
}

/**
 * @brief 绘制蜂窝信号图标：4 根递增高度小柱，自左点亮 level 根。
 * @param obj 控件对象指针。
 * @param sx 图标左上角 X。
 * @param iy 图标顶 Y。
 * @return 无。
 * @note 柱宽 2px、缝 1px，高度 3/6/9/12，底边对齐；未点亮柱低透明度。
 */
static void _sb_draw_signal(const we_statusbar_obj_t *obj, int16_t sx, int16_t iy)
{
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t dim = _sb_scale_opa(WE_STATUSBAR_DIM_OPA, obj->opacity);
    int16_t i;

    for (i = 0; i < 4; i++)
    {
        int16_t bh = (int16_t)(3 * (i + 1));
        uint8_t opa = (obj->signal_level > i) ? obj->opacity : dim;
        we_fill_rect(lcd, (int16_t)(sx + i * 3),
                     (int16_t)(iy + _SB_ICON_H - bh),
                     2U, (uint16_t)bh, obj->fg_color, opa);
    }
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：底条 + 左侧时间 + 右对齐 信号/WiFi/电池 图标。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 图标自右向左布局：电池贴右缘，隐藏的图标自动收拢不占位；
 *       所有原语内部自带 PFB 裁剪与 opa_scale 级联消费。
 */
static void _sb_draw_cb(void *ptr)
{
    we_statusbar_obj_t *obj = (we_statusbar_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t iy;
    int16_t cur_x;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->base.w <= 0 || obj->base.h <= 0)
        return;

    /* 1. 深色底条 */
    we_fill_rect(lcd, obj->base.x, obj->base.y,
                 (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                 obj->bg_color, obj->opacity);

    iy = (int16_t)(obj->base.y + (obj->base.h - _SB_ICON_H) / 2);

    /* 2. 左侧时间文本（bbox 垂直居中） */
    if (obj->time_str != NULL)
    {
        int8_t y_top;
        int8_t y_bot;
        int16_t cy = (int16_t)(obj->base.y + obj->base.h / 2);
        we_get_text_bbox(obj->font, obj->time_str, &y_top, &y_bot);
        we_draw_string(lcd, (int16_t)(obj->base.x + _SB_MARGIN_L),
                       (int16_t)(cy - (y_top + y_bot) / 2),
                       obj->font, obj->time_str,
                       obj->fg_color, obj->opacity);
    }

    /* 3. 右侧图标自右向左排列：电池 → WiFi → 信号（隐藏项收拢补位） */
    cur_x = (int16_t)(obj->base.x + obj->base.w - _SB_MARGIN_R - _SB_BAT_W);
    _sb_draw_battery(obj, cur_x, iy);

    if (obj->wifi_level >= 0)
    {
        cur_x = (int16_t)(cur_x - _SB_ICON_GAP - _SB_WIFI_W);
        _sb_draw_wifi(obj, cur_x, iy);
    }
    if (obj->signal_level >= 0)
    {
        cur_x = (int16_t)(cur_x - _SB_ICON_GAP - _SB_SIG_W);
        _sb_draw_signal(obj, cur_x, iy);
    }
}

/* --------------------------------------------------------------------------
 * 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调：装饰性控件，不消费任何事件。
 * @param ptr 回调透传对象指针（未使用）。
 * @param event 输入事件类型（未使用）。
 * @param data 输入设备事件数据指针（未使用）。
 * @return 恒返回 0（事件穿透语义）。
 */
static uint8_t _sb_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_statusbar_obj_init(we_statusbar_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, uint16_t w, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL || font == NULL || w < 64U)
        return;

    obj->font = font; /* 字体必传（上方守卫已拦 NULL） */
    obj->base.lcd     = lcd;
    obj->base.x       = x;
    obj->base.y       = y;
    obj->base.w       = (int16_t)w;
    obj->base.h       = WE_STATUSBAR_HEIGHT;
    obj->base.class_p = &_statusbar_class;
    obj->base.parent  = NULL;
    obj->base.next    = NULL;

    obj->time_str     = NULL;
    obj->battery_pct  = 100U;
    obj->charging     = 0U;
    obj->wifi_level   = 3;
    obj->signal_level = 4;

    {
        colour_t bg  = RGB888_CONST(24, 30, 42);    /* 深蓝灰底 */
        colour_t fg  = RGB888_CONST(230, 236, 245); /* 近白前景 */
        colour_t low = RGB888_CONST(240, 90, 60);   /* 橙红低电 */
        obj->bg_color  = bg;
        obj->fg_color  = fg;
        obj->low_color = low;
    }
    obj->opacity = 255U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_set_time(we_statusbar_obj_t *obj, const char *hhmm)
{
    if (obj == NULL)
        return;
    /* 调用方常在原缓冲上覆写后重传同一指针，指针等值不代表内容未变，
     * preview 阶段每次调用都重绘（见 widget.md 毕业清单） */
    obj->time_str = hhmm;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_set_battery(we_statusbar_obj_t *obj, uint8_t pct)
{
    if (obj == NULL)
        return;
    if (pct > 100U)
        pct = 100U;
    if (obj->battery_pct == pct)
        return;
    obj->battery_pct = pct;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_set_charging(we_statusbar_obj_t *obj, uint8_t charging)
{
    uint8_t v;

    if (obj == NULL)
        return;
    v = (charging != 0U) ? 1U : 0U;
    if (obj->charging == v)
        return;
    obj->charging = v;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_set_wifi(we_statusbar_obj_t *obj, int8_t level)
{
    if (obj == NULL)
        return;
    if (level < -1)
        level = -1;
    if (level > 3)
        level = 3;
    if (obj->wifi_level == level)
        return;
    obj->wifi_level = level;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_set_signal(we_statusbar_obj_t *obj, int8_t level)
{
    if (obj == NULL)
        return;
    if (level < -1)
        level = -1;
    if (level > 4)
        level = 4;
    if (obj->signal_level == level)
        return;
    obj->signal_level = level;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_set_colors(we_statusbar_obj_t *obj, colour_t bg,
                             colour_t fg, colour_t low)
{
    if (obj == NULL)
        return;
    if (_sb_col_eq(obj->bg_color, bg) &&
        _sb_col_eq(obj->fg_color, fg) &&
        _sb_col_eq(obj->low_color, low))
        return;

    obj->bg_color  = bg;
    obj->fg_color  = fg;
    obj->low_color = low;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_set_opacity(we_statusbar_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_statusbar_obj_delete(we_statusbar_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* statusbar 无动画节点，直接摘除对象即可 */
    we_obj_delete((we_obj_t *)obj);
}
