#include "we_widget_checkbox.h"
#include "we_font_text.h"
#include "we_render.h"

/* 方框与文本之间的间距 (像素) */
#define CB_TEXT_GAP 6

/* ---- 默认样式表 (暗色主题, 存放 Flash) ---- */
static const we_cb_style_t _default_styles[WE_CB_STATE_MAX] = {
    /* OFF:         灰色空心框 */
    [WE_CB_STATE_OFF]         = { .box_color = RGB888_CONST(120, 135, 155),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(220, 228, 238),
                                  .border_w = 2 },
    /* OFF_PRESSED: 亮灰空心框 */
    [WE_CB_STATE_OFF_PRESSED] = { .box_color   = RGB888_CONST(170, 185, 200),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(245, 248, 252),
                                  .border_w = 2 },
    /* ON:          蓝色实心框 */
    [WE_CB_STATE_ON]          = { .box_color   = RGB888_CONST( 50, 130, 240),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(220, 228, 238),
                                  .border_w = 0 },
    /* ON_PRESSED:  深蓝实心框 */
    [WE_CB_STATE_ON_PRESSED]  = { .box_color   = RGB888_CONST( 35, 100, 190),
                                  .check_color = RGB888_CONST(255, 255, 255),
                                  .text_color  = RGB888_CONST(245, 248, 252),
                                  .border_w = 0 },
};

/* ---- 获取当前视觉状态对应的样式 ---- */
static const we_cb_style_t *_cb_get_style(const we_checkbox_obj_t *obj)
{
    we_cb_state_t s;

    if (obj->checked)
        s = obj->pressed ? WE_CB_STATE_ON_PRESSED : WE_CB_STATE_ON;
    else
        s = obj->pressed ? WE_CB_STATE_OFF_PRESSED : WE_CB_STATE_OFF;

    return &obj->styles[s];
}

/* 只标脏方框区域，不涉及文字 */

/**
 * @brief 计算脏区并发起局部重绘请求。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _cb_invalidate_box(we_checkbox_obj_t *obj)
{
    we_obj_invalidate_area((we_obj_t *)obj,
                           obj->base.x, obj->base.y,
                           (int16_t)obj->box_size, (int16_t)obj->box_size);
}

/* ---- 内部：绘制对勾 ---- */

/**
 * @brief 在当前 PFB 裁剪区内执行局部绘制。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param bx 绘制区域起始 X 坐标。
 * @param by 绘制区域起始 Y 坐标。
 * @param size 数据长度（字节）。
 * @param color 目标颜色值。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
static void _cb_draw_checkmark(we_lcd_t *lcd, int16_t bx, int16_t by,
                               uint16_t size, colour_t color, uint8_t opacity)
{
    /* 对勾两笔：(20%,50%)→(40%,72%)→(80%,28%) */
    int16_t ax1 = (int16_t)(bx + size * 20 / 100);
    int16_t ay1 = (int16_t)(by + size * 50 / 100);
    int16_t ax2 = (int16_t)(bx + size * 40 / 100);
    int16_t ay2 = (int16_t)(by + size * 72 / 100);
    int16_t ax3 = (int16_t)(bx + size * 80 / 100);
    int16_t ay3 = (int16_t)(by + size * 28 / 100);

    uint8_t lw = (uint8_t)(size / 8);
    if (lw < 2) lw = 2;

we_draw_line(lcd, ax1, ay1, ax2, ay2, lw, color, opacity);
we_draw_line(lcd, ax2, ay2, ax3, ay3, lw, color, opacity);
}

/* ---- 绘图回调 ---- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _checkbox_draw_cb(void *ptr)
{
    we_checkbox_obj_t *obj = (we_checkbox_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    const we_cb_style_t *st = _cb_get_style(obj);
    int16_t bx = obj->base.x;
    int16_t by = obj->base.y;
    uint16_t bs = obj->box_size;

    if (obj->opacity == 0)
        return;

    /* 1. 方框 */
    if (st->border_w == 0)
    {
        /* 实心填充 */
        we_draw_round_rect_analytic_fill(lcd, bx, by, bs, bs, obj->radius,
                                         st->box_color, obj->opacity);
    }
    else
    {
        /* 空心：先画边框色，再用背景色填充内部 */
        we_draw_round_rect_analytic_fill(lcd, bx, by, bs, bs, obj->radius,
                                         st->box_color, obj->opacity);

        if (st->border_w < bs / 2)
        {
            int16_t iw = (int16_t)(bs - st->border_w * 2);
            int16_t ir = obj->radius - st->border_w;
            if (ir < 0) ir = 0;

            we_draw_round_rect_analytic_fill(lcd,
                                             (int16_t)(bx + st->border_w),
                                             (int16_t)(by + st->border_w),
                                             (uint16_t)iw, (uint16_t)iw,
                                             (uint16_t)ir, lcd->bg_color, obj->opacity);
        }
    }

    /* 2. 勾选标记 */
    if (obj->checked)
        _cb_draw_checkmark(lcd, bx, by, bs, st->check_color, obj->opacity);

    /* 3. 右侧文本 */
    if (obj->text && obj->font)
    {
        int16_t txt_x = (int16_t)(bx + bs + CB_TEXT_GAP);
        int8_t y_top, y_bot;
        we_get_text_bbox(obj->font, obj->text, &y_top, &y_bot);
        int16_t txt_y = (int16_t)(by + bs / 2) - (y_top + y_bot) / 2;

        we_draw_string(lcd, txt_x, txt_y, obj->font, obj->text,
                       st->text_color, obj->opacity);
    }
}

/* ---- 事件回调 ---- */

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 返回状态标志（1 有效，0 无效）。
 */
static uint8_t _checkbox_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_checkbox_obj_t *cb = (we_checkbox_obj_t *)ptr;

    if (cb->user_event_cb != NULL)
        return cb->user_event_cb(ptr, event, data);

    (void)data;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        cb->pressed = 1;
        _cb_invalidate_box(cb);
        break;
    case WE_EVENT_RELEASED:
        cb->pressed = 0;
        _cb_invalidate_box(cb);
        break;
    case WE_EVENT_CLICKED:
        we_checkbox_toggle(cb);
        break;
    default:
        break;
    }
    return 1;
}

/* ---- 公开 API ---- */

/**
 * @brief 内部辅助：cb_recalc_size。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _cb_recalc_size(we_checkbox_obj_t *obj)
{
    int16_t w = (int16_t)obj->box_size;
    int16_t h = (int16_t)obj->box_size;

    if (obj->text && obj->font)
    {
        uint16_t tw = we_get_text_width(obj->font, obj->text);
        w = (int16_t)(obj->box_size + CB_TEXT_GAP + tw);
        if (we_font_get_line_height(obj->font) > (uint16_t)h)
            h = (int16_t)we_font_get_line_height(obj->font);
    }

    obj->base.w = w;
    obj->base.h = h;
}

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param box_size 复选框方框边长（像素）。
 * @param text UTF-8 文本字符串。
 * @param font 字体资源指针。
 * @param event_cb 回调函数指针。
 * @return 无。
 */
void we_checkbox_obj_init(we_checkbox_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y,
                          uint16_t box_size, const char *text, const unsigned char *font,
                          we_checkbox_event_cb_t event_cb)
{
    if (obj == NULL || lcd == NULL)
        return;

    static const we_class_t _cb_class = {
        .draw_cb = _checkbox_draw_cb,
        .event_cb = _checkbox_event_cb
    };

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.class_p = &_cb_class;
    obj->base.parent = NULL;

    obj->text = text;
    obj->font = font;
    obj->user_event_cb = event_cb;
    obj->box_size = box_size;
    obj->radius = (uint16_t)(box_size / 5);
    obj->opacity = 255;
    obj->checked = 0;
    obj->pressed = 0;

    /* 默认指向 Flash 内置样式表 */
    obj->styles = _default_styles;

    _cb_recalc_size(obj);

    /* 加入显示链表 */
    obj->base.next = NULL;
    if (lcd->obj_list_head == NULL)
    {
        lcd->obj_list_head = (we_obj_t *)obj;
    }
    else
    {
        we_obj_t *tail = lcd->obj_list_head;
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = (we_obj_t *)obj;
    }

we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置对象属性并同步刷新状态。
 * @param obj 目标控件对象指针。
 * @param checked 目标勾选状态（0 未选中，非 0 选中）。
 * @return 无。
 */
void we_checkbox_set_checked(we_checkbox_obj_t *obj, uint8_t checked)
{
    if (obj == NULL)
        return;

    uint8_t val = checked ? 1U : 0U;
    if (obj->checked == val)
        return;

    obj->checked = val;
_cb_invalidate_box(obj);
}

/**
 * @brief 执行 we_checkbox_toggle。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_checkbox_toggle(we_checkbox_obj_t *obj)
{
    if (obj == NULL)
        return;
    obj->checked = obj->checked ? 0U : 1U;
_cb_invalidate_box(obj);
}

/**
 * @brief 执行 we_checkbox_is_checked。
 * @param obj 目标控件对象指针。
 * @return 1 表示命中，0 表示未命中。
 */
uint8_t we_checkbox_is_checked(const we_checkbox_obj_t *obj)
{
    if (obj == NULL)
        return 0;
    return obj->checked;
}

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param text UTF-8 文本字符串。
 * @return 无。
 */
void we_checkbox_set_text(we_checkbox_obj_t *obj, const char *text)
{
    if (obj == NULL)
        return;
we_obj_invalidate((we_obj_t *)obj);
    obj->text = text;
_cb_recalc_size(obj);
we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置控件样式参数并刷新显示。
 * @param obj 目标控件对象指针。
 * @param styles 四种状态对应的样式表指针。
 * @return 无。
 */
void we_checkbox_set_styles(we_checkbox_obj_t *obj, const we_cb_style_t *styles)
{
    if (obj == NULL)
        return;
    obj->styles = (styles != NULL) ? styles : _default_styles;
we_obj_invalidate((we_obj_t *)obj);
}
