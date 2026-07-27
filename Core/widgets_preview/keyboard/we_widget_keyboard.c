/**
 * @file  we_widget_keyboard.c
 * @brief 软键盘控件（preview）：3 页内置布局 + 份数网格自绘 + 键值回调
 *
 * 实现方式为自绘网格（方案 B，理由见 widget.md）：
 * 每行总宽固定划分为 WE_KEYBOARD_UNITS 份，键位表给出每键占用份数，
 * 键矩形边缘按 "inner_x + units * inner_w / UNITS" 整数求值，
 * 无累计误差；行内份数不足整行时自动居中（如字母页第二行 9 键）。
 * 绘制（圆角键底 + 居中键名）与命中/按压状态机结构借鉴 btnmatrix。
 */

#include "we_widget_keyboard.h"
#include "../textarea/we_widget_textarea.h" /* 弹层模式绑定目标输入框直接注入 */
#include "we_render.h"
#include <string.h>

/* 固定 4 行布局：3 行字符键 + 底行功能键 */
#define WE_KEYBOARD_ROWS  4
/* 每行总宽度份数（所有页共用） */
#define WE_KEYBOARD_UNITS 20

/* --------------------------------------------------------------------------
 * 内置页面键位表（static const 持有，控件零拷贝引用）
 *
 * labels 与 spans 为行优先平铺的平行数组；row_cnt 给出每行键数。
 * 特殊标签约定："SH" shift、"123"/"abc" 页面切换、"<-" 退格、" " 空格。
 * -------------------------------------------------------------------------- */
typedef struct
{
    const char *const *labels; /* 键名（行优先平铺） */
    const uint8_t *spans;      /* 每键宽度份数（与 labels 平行） */
    const uint8_t *row_cnt;    /* 每行键数（WE_KEYBOARD_ROWS 项） */
    uint8_t total;             /* 全页键数 */
} _kb_page_t;

/* ---- 页面 0：小写字母 ---- */
static const char *const _kb_lower_labels[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    "a", "s", "d", "f", "g", "h", "j", "k", "l",
    "SH", "z", "x", "c", "v", "b", "n", "m", "<-",
    "123", ",", " ", ".", "OK",
};
static const uint8_t _kb_lower_spans[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 2, 2, 2, 2, 2, 2, 2, 3,
    3, 2, 9, 2, 4,
};
static const uint8_t _kb_lower_rowcnt[WE_KEYBOARD_ROWS] = { 10, 9, 9, 5 };

/* ---- 页面 1：大写字母（SH 切回小写；敲一个字母也自动回小写） ---- */
static const char *const _kb_upper_labels[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
    "A", "S", "D", "F", "G", "H", "J", "K", "L",
    "SH", "Z", "X", "C", "V", "B", "N", "M", "<-",
    "123", ",", " ", ".", "OK",
};
/* 大写页与小写页键位形状完全一致，共用份数/行数表 */

/* ---- 页面 2：数字符号（abc 切回小写） ---- */
static const char *const _kb_symbol_labels[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "-", "/", ":", ";", "(", ")", "$", "&", "@", "\"",
    "#", "+", "=", "%", "*", "'", "!", "?", "<-",
    "abc", ",", " ", ".", "OK",
};
static const uint8_t _kb_symbol_spans[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 4,
    3, 2, 9, 2, 4,
};
static const uint8_t _kb_symbol_rowcnt[WE_KEYBOARD_ROWS] = { 10, 10, 9, 5 };

static const _kb_page_t _kb_pages[3] = {
    [WE_KEYBOARD_PAGE_LOWER]  = { _kb_lower_labels,  _kb_lower_spans,  _kb_lower_rowcnt,  33U },
    [WE_KEYBOARD_PAGE_UPPER]  = { _kb_upper_labels,  _kb_lower_spans,  _kb_lower_rowcnt,  33U },
    [WE_KEYBOARD_PAGE_SYMBOL] = { _kb_symbol_labels, _kb_symbol_spans, _kb_symbol_rowcnt, 34U },
};

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _kb_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 判断键名是否为功能键（SH / 页面切换 / 退格）。
 * @param label 传入：键面字符串。
 * @return 1 功能键，0 普通字符键。
 */
static uint8_t _kb_label_is_fn(const char *label)
{
    return (strcmp(label, "SH") == 0 || strcmp(label, "123") == 0 ||
            strcmp(label, "abc") == 0 || strcmp(label, "<-") == 0 ||
            strcmp(label, "OK") == 0) ? 1U : 0U;
}

/**
 * @brief 取当前页键位表指针。
 * @param obj 传入：控件对象指针。
 * @return 页面表指针（page 越界时退回小写页，防御性）。
 */
static const _kb_page_t *_kb_cur_page(const we_keyboard_obj_t *obj)
{
    uint8_t page = obj->page;
    if (page > WE_KEYBOARD_PAGE_SYMBOL)
        page = WE_KEYBOARD_PAGE_LOWER;
    return &_kb_pages[page];
}

/**
 * @brief 重新派生功能键底色（键色向面板色混合 3/8，比普通键略暗）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _kb_update_fn_color(we_keyboard_obj_t *obj)
{
    obj->fn_color = we_colour_blend(obj->bg_color, obj->key_color, 96U);
}

/**
 * @brief 计算指定键的外接矩形（屏幕绝对坐标）。
 * @param obj 传入：控件对象指针。
 * @param page 传入：页面表指针。
 * @param idx 传入：页内行优先键序号。
 * @param out_x 传出：键左上角 X。
 * @param out_y 传出：键左上角 Y。
 * @param out_w 传出：键宽度。
 * @param out_h 传出：键高度。
 * @return 1 成功，0 序号越界或几何退化。
 * @note 行高按 "inner_y + r * inner_h / ROWS" 边缘求值，键 X 边缘按份数同法
 *       求值，间距通过两侧各让 GAP/2 实现，整数运算无累计漂移。
 */
static uint8_t _kb_key_rect(const we_keyboard_obj_t *obj, const _kb_page_t *page, int16_t idx,
                            int16_t *out_x, int16_t *out_y, int16_t *out_w, int16_t *out_h)
{
    /* 弹层模式顶部让位给回显条，键区从回显条下方开始 */
    int16_t echo_h = obj->popup_mode ? (int16_t)WE_KEYBOARD_ECHO_H : 0;
    int16_t inner_x = (int16_t)(obj->base.x + WE_KEYBOARD_PAD);
    int16_t inner_y = (int16_t)(obj->base.y + echo_h + WE_KEYBOARD_PAD);
    int16_t inner_w = (int16_t)(obj->base.w - 2 * WE_KEYBOARD_PAD);
    int16_t inner_h = (int16_t)(obj->base.h - echo_h - 2 * WE_KEYBOARD_PAD);
    int16_t base_idx = 0;
    int16_t row;

    if (idx < 0 || idx >= (int16_t)page->total || inner_w <= 0 || inner_h <= 0)
        return 0U;

    for (row = 0; row < WE_KEYBOARD_ROWS; row++)
    {
        int16_t n = (int16_t)page->row_cnt[row];

        if (idx < base_idx + n)
        {
            int16_t k;
            int16_t units_sum = 0;
            int16_t units_before = 0;
            int16_t lead;
            int16_t ry0;
            int16_t ry1;
            int16_t kx0;
            int16_t kx1;

            /* 行内份数合计与目标键之前的份数 */
            for (k = 0; k < n; k++)
            {
                if (base_idx + k < idx)
                    units_before = (int16_t)(units_before + page->spans[base_idx + k]);
                units_sum = (int16_t)(units_sum + page->spans[base_idx + k]);
            }
            lead = (int16_t)((WE_KEYBOARD_UNITS - units_sum) / 2); /* 份数不足整行时居中 */
            if (lead < 0)
                lead = 0;

            ry0 = (int16_t)(inner_y + row * inner_h / WE_KEYBOARD_ROWS);
            ry1 = (int16_t)(inner_y + (row + 1) * inner_h / WE_KEYBOARD_ROWS);
            kx0 = (int16_t)(inner_x + (lead + units_before) * inner_w / WE_KEYBOARD_UNITS);
            kx1 = (int16_t)(inner_x +
                            (lead + units_before + (int16_t)page->spans[idx]) * inner_w / WE_KEYBOARD_UNITS);

            *out_x = (int16_t)(kx0 + WE_KEYBOARD_GAP / 2);
            *out_y = (int16_t)(ry0 + WE_KEYBOARD_GAP / 2);
            *out_w = (int16_t)((kx1 - kx0) - WE_KEYBOARD_GAP);
            *out_h = (int16_t)((ry1 - ry0) - WE_KEYBOARD_GAP);
            return (*out_w > 0 && *out_h > 0) ? 1U : 0U;
        }
        base_idx = (int16_t)(base_idx + n);
    }
    return 0U;
}

/**
 * @brief 触点命中检测：返回命中的键序号（落在键间距/面板边距上视为未命中）。
 * @param obj 传入：控件对象指针。
 * @param page 传入：页面表指针。
 * @param px 传入：触点 X（屏幕绝对坐标）。
 * @param py 传入：触点 Y。
 * @return 命中的页内行优先键序号，未命中返回 -1。
 * @note 全页键数 <= 33，逐键矩形复测足够快（仅触摸事件路径调用）。
 */
static int16_t _kb_hit_key(const we_keyboard_obj_t *obj, const _kb_page_t *page,
                           int16_t px, int16_t py)
{
    int16_t idx;

    for (idx = 0; idx < (int16_t)page->total; idx++)
    {
        int16_t kx;
        int16_t ky;
        int16_t kw;
        int16_t kh;

        if (!_kb_key_rect(obj, page, idx, &kx, &ky, &kw, &kh))
            continue;
        if (px >= kx && px < (int16_t)(kx + kw) && py >= ky && py < (int16_t)(ky + kh))
            return idx;
    }
    return -1;
}

/**
 * @brief 按键序号标脏对应键区域（沿父链裁剪）。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：页内行优先键序号。
 * @return 无。
 */
static void _kb_invalidate_key(we_keyboard_obj_t *obj, int16_t idx)
{
    int16_t kx;
    int16_t ky;
    int16_t kw;
    int16_t kh;

    if (_kb_key_rect(obj, _kb_cur_page(obj), idx, &kx, &ky, &kw, &kh))
        we_obj_invalidate_area((we_obj_t *)obj, kx, ky, kw, kh);
}

/**
 * @brief 标脏弹层模式的顶部回显条（注入目标后镜像文本变化）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _kb_invalidate_echo(we_keyboard_obj_t *obj)
{
    if (obj->popup_mode && WE_KEYBOARD_ECHO_H > 0)
        we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y,
                               obj->base.w, WE_KEYBOARD_ECHO_H);
}

/**
 * @brief 处理一次已确认的按键点击：功能键内部消化，普通键注入/回调。
 * @param obj 传入：控件对象指针。
 * @param label 传入：键面字符串。
 * @return 无。
 * @note 大写页敲出一个字母后自动切回小写页（非 sticky shift）。
 *       弹层模式绑定目标输入框时，普通键/退格直接 we_textarea_input 注入；
 *       "OK" 确定键先启动收回再触发 done_cb（回调内可再 show 阻止关闭）。
 */
static void _kb_handle_key(we_keyboard_obj_t *obj, const char *label)
{
    if (strcmp(label, "SH") == 0)
    {
        we_keyboard_set_page(obj, (obj->page == WE_KEYBOARD_PAGE_LOWER)
                                      ? WE_KEYBOARD_PAGE_UPPER
                                      : WE_KEYBOARD_PAGE_LOWER);
        return;
    }
    if (strcmp(label, "123") == 0)
    {
        we_keyboard_set_page(obj, WE_KEYBOARD_PAGE_SYMBOL);
        return;
    }
    if (strcmp(label, "abc") == 0)
    {
        we_keyboard_set_page(obj, WE_KEYBOARD_PAGE_LOWER);
        return;
    }
    if (strcmp(label, "OK") == 0)
    {
        void *tgt = obj->target;

        if (obj->popup_mode)
            we_keyboard_popup_hide(obj);
        if (obj->done_cb != NULL)
            obj->done_cb(obj, tgt);
        return;
    }
    if (strcmp(label, "<-") == 0)
    {
        if (obj->popup_mode && obj->target != NULL)
        {
            we_textarea_input((we_textarea_obj_t *)obj->target, "\b");
            _kb_invalidate_echo(obj); /* 回显条同步刷新 */
        }
        if (obj->key_cb != NULL)
            obj->key_cb(obj, "\b"); /* 退格统一回传 "\b" */
        return;
    }

    if (obj->popup_mode && obj->target != NULL)
    {
        we_textarea_input((we_textarea_obj_t *)obj->target, label);
        _kb_invalidate_echo(obj); /* 回显条同步刷新 */
    }
    if (obj->key_cb != NULL)
        obj->key_cb(obj, label); /* 普通键（含空格 " "）原样回传键面字符串 */

    if (obj->page == WE_KEYBOARD_PAGE_UPPER)
        we_keyboard_set_page(obj, WE_KEYBOARD_PAGE_LOWER); /* shift 一次性生效 */
}

/* --------------------------------------------------------------------------
 * 绘图回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note preview 放宽：每次重绘遍历当前页全部键，越出 PFB 的写入由原语裁剪丢弃。
 */
static void _keyboard_draw_cb(void *ptr)
{
    we_keyboard_obj_t *obj = (we_keyboard_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    const _kb_page_t *page = _kb_cur_page(obj);
    int16_t idx;

    if (obj->opacity == 0U)
        return;

    /* 1. 面板底：整块纯色填充（键盘停靠面板，方角即可） */
    we_fill_rect(lcd, obj->base.x, obj->base.y,
                 (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                 obj->bg_color, obj->opacity);

    /* 1.5 弹层模式回显条：实时镜像目标输入框内容（尾部对齐 + 末尾光标），
     *     键盘遮住输入框时也能看到正在输入的文本（textarea 同款 PFB 收窄裁剪） */
    if (obj->popup_mode && WE_KEYBOARD_ECHO_H > 0)
    {
        int16_t eb_y = obj->base.y;
        int16_t pad_x = 8;
        int16_t inner_x = (int16_t)(obj->base.x + pad_x);
        int16_t inner_w = (int16_t)(obj->base.w - 2 * pad_x);
        int16_t avail_w = (int16_t)(inner_w - 2); /* 末尾光标留 2px */

        we_fill_rect(lcd, obj->base.x, eb_y, (uint16_t)obj->base.w,
                     (uint16_t)WE_KEYBOARD_ECHO_H, obj->fn_color, obj->opacity);

        if (obj->target != NULL && obj->font != NULL && avail_w > 0)
        {
            const char *text = we_textarea_get_text((const we_textarea_obj_t *)obj->target);
            we_area_t old_pfb_area = lcd->pfb_area;
            uint16_t old_y_start = lcd->pfb_y_start;
            uint16_t old_y_end = lcd->pfb_y_end;
            colour_t *old_gram = lcd->pfb_gram;
            int16_t new_x0 = WE_MAX(old_pfb_area.x0, inner_x);
            int16_t new_y0 = WE_MAX((int16_t)old_y_start, eb_y);
            int16_t new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(inner_x + inner_w - 1));
            int16_t new_y1 = WE_MIN((int16_t)old_y_end,
                                    (int16_t)(eb_y + WE_KEYBOARD_ECHO_H - 1));

            if (new_x0 <= new_x1 && new_y0 <= new_y1)
            {
                uint16_t line_h = we_font_get_line_height(obj->font);
                int16_t text_y = (int16_t)(eb_y + ((int16_t)WE_KEYBOARD_ECHO_H -
                                                   (int16_t)line_h) / 2);
                int16_t cursor_x = inner_x;

                lcd->pfb_area.x0 = (uint16_t)new_x0;
                lcd->pfb_area.x1 = (uint16_t)new_x1;
                lcd->pfb_y_start = (uint16_t)new_y0;
                lcd->pfb_y_end = (uint16_t)new_y1;
                lcd->pfb_gram = old_gram + (new_y0 - (int16_t)old_y_start) * lcd->pfb_width +
                                (new_x0 - (int16_t)old_pfb_area.x0);

                if (text != NULL && text[0] != '\0')
                {
                    uint16_t text_w = we_get_text_width(obj->font, text);
                    int16_t tx = inner_x;

                    if ((int32_t)text_w > (int32_t)avail_w)
                        tx = (int16_t)(inner_x + avail_w - (int16_t)text_w);
                    we_draw_string(lcd, tx, text_y, obj->font, text,
                                   obj->text_color, obj->opacity);
                    cursor_x = (int16_t)(tx + (int16_t)text_w);
                }
                /* 末尾 2px 光标（常亮，闪烁交给目标输入框本体） */
                we_fill_rect(lcd, cursor_x, (int16_t)(eb_y + 4), 2U,
                             (uint16_t)(WE_KEYBOARD_ECHO_H - 8), obj->key_press_color,
                             obj->opacity);
            }

            lcd->pfb_area = old_pfb_area;
            lcd->pfb_y_start = old_y_start;
            lcd->pfb_y_end = old_y_end;
            lcd->pfb_gram = old_gram;
        }
    }

    /* 2. 逐键绘制：圆角键底 + 居中键名 */
    for (idx = 0; idx < (int16_t)page->total; idx++)
    {
        int16_t kx;
        int16_t ky;
        int16_t kw;
        int16_t kh;
        uint16_t draw_r;
        colour_t bg;
        const char *label;

        if (!_kb_key_rect(obj, page, idx, &kx, &ky, &kw, &kh))
            continue;

        label = page->labels[idx];
        if (obj->pressed && idx == obj->press_idx)
            bg = obj->key_press_color;
        else
            bg = _kb_label_is_fn(label) ? obj->fn_color : obj->key_color;

        draw_r = WE_KEYBOARD_RADIUS;
        if (draw_r > (uint16_t)(kw / 2))
            draw_r = (uint16_t)(kw / 2);
        if (draw_r > (uint16_t)(kh / 2))
            draw_r = (uint16_t)(kh / 2);
        we_draw_round_rect_analytic_fill(lcd, kx, ky, (uint16_t)kw, (uint16_t)kh,
                                         draw_r, bg, obj->opacity);

        /* 键名文字（水平按测宽、垂直按墨迹 bbox 居中）；空格键面为空白 */
        if (obj->font != NULL && label[0] != ' ')
        {
            uint16_t txt_w = we_get_text_width(obj->font, label);
            int8_t y_top;
            int8_t y_bot;
            int16_t txt_x;
            int16_t txt_y;

            we_get_text_bbox(obj->font, label, &y_top, &y_bot);
            txt_x = (int16_t)(kx + kw / 2 - (int16_t)(txt_w / 2U));
            txt_y = (int16_t)(ky + kh / 2 - (y_top + y_bot) / 2);
            we_draw_string(lcd, txt_x, txt_y, obj->font, label,
                           obj->text_color, obj->opacity);
        }
    }

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
    /* 3. 键光标环：聚焦键四周 2px 描边（画在键间距带里，聚焦导航色）；
     *    作为 ime 等宿主的内嵌键盘时 popup_mode 为 0，同样按 focus_idx 绘制 */
    if (obj->focus_idx >= 0)
    {
        int16_t kx;
        int16_t ky;
        int16_t kw;
        int16_t kh;

        if (_kb_key_rect(obj, page, obj->focus_idx, &kx, &ky, &kw, &kh))
        {
            colour_t rc = RGB888TODEV(WE_CFG_FOCUS_CURSOR_R, WE_CFG_FOCUS_CURSOR_G,
                                      WE_CFG_FOCUS_CURSOR_B);

            we_fill_rect(lcd, (int16_t)(kx - 2), (int16_t)(ky - 2),
                         (uint16_t)(kw + 4), 2U, rc, obj->opacity);
            we_fill_rect(lcd, (int16_t)(kx - 2), (int16_t)(ky + kh),
                         (uint16_t)(kw + 4), 2U, rc, obj->opacity);
            we_fill_rect(lcd, (int16_t)(kx - 2), ky, 2U, (uint16_t)kh, rc, obj->opacity);
            we_fill_rect(lcd, (int16_t)(kx + kw), ky, 2U, (uint16_t)kh, rc, obj->opacity);
        }
    }
#endif
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调，处理按压/拖出/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 = 事件已消费，0 = 穿透。
 * @note 键盘是不透明停靠面板：面板矩形内的触摸（含键间距）一律消费，
 *       避免误触穿透到下层控件；完全透明（opacity==0）时不拦截。
 */
static uint8_t _keyboard_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_keyboard_obj_t *obj = (we_keyboard_obj_t *)ptr;
    const _kb_page_t *page = _kb_cur_page(obj);
    int16_t idx;

    if (obj->opacity == 0U)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        idx = _kb_hit_key(obj, page, data->x, data->y);
        if (idx >= 0)
        {
            obj->press_idx = idx;
            obj->pressed = 1U;
            _kb_invalidate_key(obj, idx);
        }
        else
        {
            obj->press_idx = -1;
            obj->pressed = 0U;
        }
        return 1U; /* 面板内按压（含间距带）一律消费 */

    case WE_EVENT_STAY:
        if (obj->press_idx >= 0)
        {
            /* 拖出原键：取消按压态，本次触摸序列不再产生点击 */
            if (_kb_hit_key(obj, page, data->x, data->y) != obj->press_idx)
            {
                if (obj->pressed)
                {
                    obj->pressed = 0U;
                    _kb_invalidate_key(obj, obj->press_idx);
                }
                obj->press_idx = -1;
            }
        }
        return 1U;

    case WE_EVENT_RELEASED:
        if (obj->pressed)
        {
            obj->pressed = 0U;
            _kb_invalidate_key(obj, obj->press_idx);
        }
        /* press_idx 留给紧随其后的 CLICKED 判定使用 */
        return 1U;

    case WE_EVENT_CLICKED:
        idx = _kb_hit_key(obj, page, data->x, data->y);
        if (idx >= 0 && idx == obj->press_idx)
        {
            obj->press_idx = -1;
            _kb_handle_key(obj, page->labels[idx]);
        }
        else
        {
            obj->press_idx = -1;
        }
        return 1U;

    default:
        break;
    }
    return 1U; /* 面板矩形内的其余事件（SWIPE 等）也不穿透 */
}

static const we_class_t _keyboard_class = {
    .draw_cb = _keyboard_draw_cb,
    .event_cb = _keyboard_event_cb,
    .set_pos_cb = NULL
};

/* --------------------------------------------------------------------------
 * 弹层模式：滑入/收回状态机 + 弹层回调
 *
 * 键盘对象不挂普通对象链表，由 LCD 弹层承载绘制与输入；位置用 slide_q8
 * （0=全隐藏在屏底外，256=完全展开）经一个中央动画节点推进，toast 同款
 * 可中途反向。每步把弹层 area 与 base.y 同步（set_area 负责新旧区标脏）。
 * -------------------------------------------------------------------------- */

#define _KB_SLIDE_HIDDEN 0U /* 完全隐藏（弹层已释放） */
#define _KB_SLIDE_IN     1U /* 滑入中 */
#define _KB_SLIDE_SHOWN  2U /* 完全展开停留 */
#define _KB_SLIDE_OUT    3U /* 滑出中 */

/**
 * @brief 按当前 base 矩形同步弹层 area（set_area 内部标脏旧/新区域）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _kb_popup_sync_area(we_keyboard_obj_t *obj)
{
    we_area_t a;

    a.x0 = obj->base.x;
    a.y0 = obj->base.y;
    a.x1 = (int16_t)(obj->base.x + obj->base.w - 1);
    a.y1 = (int16_t)(obj->base.y + obj->base.h - 1);
    we_popup_layer_set_area(obj->base.lcd, obj, &a);
}

/**
 * @brief 按 slide_q8 计算 base.y（自屏底升起）并同步弹层区域。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _kb_slide_apply(we_keyboard_obj_t *obj)
{
    we_lcd_t *lcd = obj->base.lcd;

    obj->base.y = (int16_t)((int32_t)lcd->height -
                            ((int32_t)obj->base.h * (int32_t)obj->slide_q8) / 256);
    _kb_popup_sync_area(obj);
}

/**
 * @brief 滑动动画步进（中央动画节点回调）：推进 Q8 进度并落位。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次步进经过的毫秒数。
 * @return 无。
 * @note 滑出到底后释放弹层（close_cb 里统一复位状态）。
 */
static void _kb_slide_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_keyboard_obj_t *obj = (we_keyboard_obj_t *)owner;
    int32_t step;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    step = ((int32_t)elapsed_ms * 256) / (int32_t)WE_KEYBOARD_ANIM_MS;
    if (step < 1)
        step = 1;

    if (obj->slide_state == _KB_SLIDE_IN)
    {
        int32_t q = (int32_t)obj->slide_q8 + step;

        if (q >= 256)
        {
            q = 256;
            obj->slide_state = _KB_SLIDE_SHOWN;
            we_anim_stop(obj->base.lcd, &obj->anim);
        }
        obj->slide_q8 = (uint16_t)q;
        _kb_slide_apply(obj);
    }
    else if (obj->slide_state == _KB_SLIDE_OUT)
    {
        int32_t q = (int32_t)obj->slide_q8 - step;

        if (q <= 0)
        {
            obj->slide_q8 = 0U;
            _kb_slide_apply(obj); /* 最后一条可见带标脏 */
            we_anim_stop(obj->base.lcd, &obj->anim);
            we_popup_layer_close(obj->base.lcd, obj); /* close_cb 复位为 HIDDEN */
            return;
        }
        obj->slide_q8 = (uint16_t)q;
        _kb_slide_apply(obj);
    }
    else
    {
        we_anim_stop(obj->base.lcd, &obj->anim); /* 防御：异常状态摘链 */
    }
}

/**
 * @brief 弹层关闭回调：无论正常滑出收尾还是被其他弹层顶掉，都复位状态。
 * @param owner 传入：控件对象指针。
 * @return 无。
 */
static void _kb_popup_close_cb(void *owner)
{
    we_keyboard_obj_t *obj = (we_keyboard_obj_t *)owner;

    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->slide_state = _KB_SLIDE_HIDDEN;
    obj->slide_q8 = 0U;
    obj->base.y = (int16_t)obj->base.lcd->height; /* 归位屏外 */
    obj->pressed = 0U;
    obj->press_idx = -1;
    if (obj->target != NULL) /* 弹层关闭 = 目标退出编辑态（光标熄灭停表） */
        we_textarea_set_editing((we_textarea_obj_t *)obj->target, 0U);
}

/**
 * @brief 弹层事件回调：键盘上方区域按下 = 收回；面板内转交控件事件机。
 * @param owner 传入：控件对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒为 1（模态弹层吞掉全部输入）。
 */
static uint8_t _kb_popup_event(void *owner, we_event_t event, we_indev_data_t *data)
{
    we_keyboard_obj_t *obj = (we_keyboard_obj_t *)owner;

    if (obj->slide_state == _KB_SLIDE_OUT || obj->slide_state == _KB_SLIDE_HIDDEN)
        return 1U; /* 收回中不再接受输入 */
    if (event == WE_EVENT_PRESSED && data != NULL && data->y < obj->base.y)
    {
        we_keyboard_popup_hide(obj); /* 点击键盘外部 = 收回 */
        return 1U;
    }
    return _keyboard_event_cb(obj, event, data);
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
/**
 * @brief 取键序号所在行及该行的起始序号/键数。
 * @param page 传入：页面表指针。
 * @param idx 传入：页内行优先键序号。
 * @param row_start 传出：行首键序号。
 * @param row_n 传出：行内键数。
 * @return 行号（0..WE_KEYBOARD_ROWS-1）；idx 非法时返回 -1。
 */
static int16_t _kb_key_row(const _kb_page_t *page, int16_t idx,
                           int16_t *row_start, int16_t *row_n)
{
    int16_t base = 0;
    int16_t row;

    for (row = 0; row < WE_KEYBOARD_ROWS; row++)
    {
        int16_t n = (int16_t)page->row_cnt[row];

        if (idx < base + n)
        {
            *row_start = base;
            *row_n = n;
            return row;
        }
        base = (int16_t)(base + n);
    }
    return -1;
}

/**
 * @brief 标脏聚焦键连同 2px 光标环的外扩矩形。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：键序号（<0 忽略）。
 * @return 无。
 */
static void _kb_focus_invalidate(we_keyboard_obj_t *obj, int16_t idx)
{
    int16_t kx;
    int16_t ky;
    int16_t kw;
    int16_t kh;

    if (idx < 0)
        return;
    if (_kb_key_rect(obj, _kb_cur_page(obj), idx, &kx, &ky, &kw, &kh))
        we_obj_invalidate_area((we_obj_t *)obj, (int16_t)(kx - 2), (int16_t)(ky - 2),
                               (int16_t)(kw + 4), (int16_t)(kh + 4));
}

/**
 * @brief 把键光标移到指定序号（新旧键环形区各标脏一次）。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：目标键序号。
 * @return 无。
 */
static void _kb_focus_to(we_keyboard_obj_t *obj, int16_t idx)
{
    if (idx == obj->focus_idx)
        return;
    _kb_focus_invalidate(obj, obj->focus_idx);
    obj->focus_idx = idx;
    _kb_focus_invalidate(obj, idx);
}

/**
 * @brief 键光标网格移动：行内回绕，跨行按 x 中心就近落键（行环回绕）。
 * @param obj 传入：控件对象指针。
 * @param dx 传入：水平步（-1/0/+1）。
 * @param dy 传入：垂直步（-1/0/+1）。
 * @return 无。
 * @note 光标未落位时首个方向键落到第一个键，不产生位移。
 */
static void _kb_focus_move(we_keyboard_obj_t *obj, int16_t dx, int16_t dy)
{
    const _kb_page_t *page = _kb_cur_page(obj);
    int16_t row_start;
    int16_t row_n;
    int16_t row;

    if (obj->focus_idx < 0 || obj->focus_idx >= (int16_t)page->total)
    {
        _kb_focus_to(obj, 0); /* 首键落位 */
        return;
    }

    row = _kb_key_row(page, obj->focus_idx, &row_start, &row_n);
    if (row < 0)
        return;

    if (dx != 0)
    {
        int16_t col = (int16_t)(obj->focus_idx - row_start);

        col = (int16_t)((col + dx + row_n) % row_n); /* 行内回绕 */
        _kb_focus_to(obj, (int16_t)(row_start + col));
    }
    else if (dy != 0)
    {
        int16_t kx;
        int16_t ky;
        int16_t kw;
        int16_t kh;
        int16_t cx;
        int16_t trow = (int16_t)((row + dy + WE_KEYBOARD_ROWS) % WE_KEYBOARD_ROWS);
        int16_t tstart = 0;
        int16_t tn = 0;
        int16_t k;
        int16_t best = -1;
        int16_t best_d = 0x7FFF;

        if (!_kb_key_rect(obj, page, obj->focus_idx, &kx, &ky, &kw, &kh))
            return;
        cx = (int16_t)(kx + kw / 2);

        /* 目标行起始序号 */
        for (k = 0; k < trow; k++)
            tstart = (int16_t)(tstart + page->row_cnt[k]);
        tn = (int16_t)page->row_cnt[trow];

        for (k = 0; k < tn; k++)
        {
            int16_t d;

            if (!_kb_key_rect(obj, page, (int16_t)(tstart + k), &kx, &ky, &kw, &kh))
                continue;
            d = (int16_t)(kx + kw / 2 - cx);
            if (d < 0)
                d = (int16_t)-d;
            if (d < best_d)
            {
                best_d = d;
                best = (int16_t)(tstart + k);
            }
        }
        if (best >= 0)
            _kb_focus_to(obj, best);
    }
}

/**
 * @brief 键光标导航核心（公开：供 ime_pinyin 等组合宿主转发弹层键值）。
 * @param obj 传入：控件对象指针。
 * @param code 传入：语义键编码（松开沿带 WE_KEY_RELEASE_FLAG）。
 * @return 1 = 已消费；0 = BACK 键未消费（由宿主决定收回动作）。
 */
uint8_t we_keyboard_key_nav(we_keyboard_obj_t *obj, uint8_t code)
{
    uint8_t key = (uint8_t)(code & (uint8_t)~WE_KEY_RELEASE_FLAG);

    if (obj == NULL)
        return 1U;

    if ((code & WE_KEY_RELEASE_FLAG) != 0U)
    {
        /* OK 松开沿：确认击键（无按下沿配对时忽略，防弹出瞬间的孤立松开） */
        if (key == WE_KEY_OK && obj->pressed && obj->press_idx >= 0 &&
            obj->press_idx == obj->focus_idx)
        {
            const _kb_page_t *page = _kb_cur_page(obj);
            int16_t idx = obj->press_idx;

            obj->pressed = 0U;
            obj->press_idx = -1;
            _kb_invalidate_key(obj, idx);
            _kb_handle_key(obj, page->labels[idx]);
        }
        return 1U;
    }

    switch (key)
    {
    case WE_KEY_UP:
        _kb_focus_move(obj, 0, -1);
        break;
    case WE_KEY_DOWN:
        _kb_focus_move(obj, 0, 1);
        break;
    case WE_KEY_LEFT:
    case WE_KEY_PREV:
        _kb_focus_move(obj, -1, 0);
        break;
    case WE_KEY_RIGHT:
    case WE_KEY_NEXT:
        _kb_focus_move(obj, 1, 0);
        break;
    case WE_KEY_OK: /* 按下沿：聚焦键进入按压高亮，松开沿触发 */
        if (obj->focus_idx < 0)
        {
            _kb_focus_to(obj, 0);
            break;
        }
        obj->press_idx = obj->focus_idx;
        obj->pressed = 1U;
        _kb_invalidate_key(obj, obj->focus_idx);
        break;
    case WE_KEY_BACK:
        return 0U; /* 交宿主处理（弹层模式 = 收回） */
    default:
        break;
    }
    return 1U;
}

/**
 * @brief 查询键光标当前所在行号（0 = 顶行）。
 * @param obj 传入：控件对象指针。
 * @return 行号（0..WE_KEYBOARD_ROWS-1）；光标未落位返回 -1。
 */
int16_t we_keyboard_focus_row(we_keyboard_obj_t *obj)
{
    int16_t row_start;
    int16_t row_n;

    if (obj == NULL || obj->focus_idx < 0)
        return -1;
    return _kb_key_row(_kb_cur_page(obj), obj->focus_idx, &row_start, &row_n);
}

/**
 * @brief 弹层键通道回调：方向键移键光标，OK 双沿击键，BACK 收回。
 * @param owner 传入：控件对象指针。
 * @param code 传入：语义键编码（松开沿带 WE_KEY_RELEASE_FLAG）。
 * @return 恒为 1（模态弹层吞掉全部按键）。
 */
static uint8_t _kb_popup_key_cb(void *owner, uint8_t code)
{
    we_keyboard_obj_t *obj = (we_keyboard_obj_t *)owner;

    if (obj->slide_state == _KB_SLIDE_OUT || obj->slide_state == _KB_SLIDE_HIDDEN)
        return 1U;
    if (!we_keyboard_key_nav(obj, code))
        we_keyboard_popup_hide(obj); /* BACK = 收回 */
    return 1U;
}
#endif /* WE_CFG_ENABLE_KEY_INPUT && WE_KEYBOARD_USE_KEY */

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化软键盘控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 面板左上角 X（屏幕绝对坐标）。
 * @param y 面板左上角 Y。
 * @param w 面板宽度（像素）。
 * @param h 面板高度（像素）。
 * @return 无。
 */
void we_keyboard_obj_init(we_keyboard_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_keyboard_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->bg_color = RGB888TODEV(26, 32, 44);
    obj->key_color = RGB888TODEV(58, 66, 82);
    obj->key_press_color = RGB888TODEV(64, 152, 231);
    obj->text_color = RGB888TODEV(239, 243, 250);
    _kb_update_fn_color(obj);
    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;

    obj->key_cb = NULL;
    obj->done_cb = NULL;
    obj->page = WE_KEYBOARD_PAGE_LOWER;
    obj->press_idx = -1;
    obj->pressed = 0U;
    obj->opacity = 255U;

    obj->target = NULL;
    obj->popup_mode = 0U;
    obj->slide_state = _KB_SLIDE_HIDDEN;
    obj->slide_q8 = 0U;
    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
    obj->focus_idx = -1;
#endif

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 初始化弹层模式软键盘（不挂普通对象链表，show 时经 LCD 弹层滑入）。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param h 面板高度（像素）；宽度固定 = 屏宽，停靠屏底。
 * @param font 字体资源指针（必传；NULL 时不执行初始化）。
 * @return 无。
 */
void we_keyboard_popup_init(we_keyboard_obj_t *obj, we_lcd_t *lcd,
                            int16_t h, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL || font == NULL || h <= 0)
        return;

    obj->base.lcd = lcd;
    obj->base.x = 0;
    obj->base.y = (int16_t)lcd->height; /* 初始完全停在屏外 */
    obj->base.w = (int16_t)lcd->width;
    obj->base.h = h;
    obj->base.class_p = &_keyboard_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->bg_color = RGB888TODEV(26, 32, 44);
    obj->key_color = RGB888TODEV(58, 66, 82);
    obj->key_press_color = RGB888TODEV(64, 152, 231);
    obj->text_color = RGB888TODEV(239, 243, 250);
    _kb_update_fn_color(obj);
    obj->font = font;

    obj->key_cb = NULL;
    obj->done_cb = NULL;
    obj->page = WE_KEYBOARD_PAGE_LOWER;
    obj->press_idx = -1;
    obj->pressed = 0U;
    obj->opacity = 255U;

    obj->target = NULL;
    obj->popup_mode = 1U;
    obj->slide_state = _KB_SLIDE_HIDDEN;
    obj->slide_q8 = 0U;
    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
    obj->focus_idx = -1;
#endif
    /* 弹层承载：不 attach 普通对象链表，不预标脏（屏外不可见） */
}

/**
 * @brief 弹出软键盘：占用 LCD 弹层并从屏底滑入。
 * @param obj 控件对象指针（须为 popup_init 创建）。
 * @param target_textarea 绑定的目标输入框（we_textarea_obj_t*，可 NULL）。
 * @return 无。
 */
void we_keyboard_popup_show(we_keyboard_obj_t *obj, void *target_textarea)
{
    we_lcd_t *lcd;

    if (obj == NULL || obj->base.lcd == NULL || obj->popup_mode == 0U)
        return;
    lcd = obj->base.lcd;

    /* 编辑态交接：旧目标熄灭光标，新目标进入编辑态开始闪烁 */
    if (obj->target != NULL && obj->target != target_textarea)
        we_textarea_set_editing((we_textarea_obj_t *)obj->target, 0U);
    obj->target = target_textarea;
    if (target_textarea != NULL)
        we_textarea_set_editing((we_textarea_obj_t *)target_textarea, 1U);

    if (obj->slide_state == _KB_SLIDE_SHOWN)
        return; /* 已完全展开：仅更新绑定目标 */

    if (!we_popup_layer_is_owner(lcd, obj))
    {
        we_area_t a;

        a.x0 = obj->base.x;
        a.y0 = obj->base.y;
        a.x1 = (int16_t)(obj->base.x + obj->base.w - 1);
        a.y1 = (int16_t)(obj->base.y + obj->base.h - 1);
        we_popup_layer_open(lcd, WE_POPUP_TYPE_KEYBOARD, obj, &a,
                            _keyboard_draw_cb, _kb_popup_event, _kb_popup_close_cb);
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
        we_popup_layer_set_key_cb(lcd, obj, _kb_popup_key_cb);
#endif
    }

    obj->slide_state = _KB_SLIDE_IN; /* 滑出中调用则自当前位置反向 */
    we_anim_start(lcd, &obj->anim, _kb_slide_step_cb, obj);
}

/**
 * @brief 收回软键盘：滑出到屏外后释放 LCD 弹层。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_keyboard_popup_hide(we_keyboard_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->popup_mode == 0U)
        return;
    if (obj->slide_state == _KB_SLIDE_HIDDEN || obj->slide_state == _KB_SLIDE_OUT)
        return;
    if (obj->target != NULL) /* 收回即退出编辑态（光标立即熄灭） */
        we_textarea_set_editing((we_textarea_obj_t *)obj->target, 0U);
    if (!we_popup_layer_is_owner(obj->base.lcd, obj))
    {
        _kb_popup_close_cb(obj); /* 弹层已被顶掉：直接复位状态兜底 */
        return;
    }

    obj->slide_state = _KB_SLIDE_OUT;
    we_anim_start(obj->base.lcd, &obj->anim, _kb_slide_step_cb, obj);
}

/**
 * @brief 注册 "OK" 确定键回调（触发后弹层模式自动收回）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针（kb, target），NULL 表示取消。
 * @return 无。
 */
void we_keyboard_set_done_cb(we_keyboard_obj_t *obj, we_keyboard_done_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->done_cb = cb;
}

/**
 * @brief 注册键值回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_keyboard_set_key_cb(we_keyboard_obj_t *obj, we_keyboard_key_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->key_cb = cb;
}

/**
 * @brief 切换键盘页面并重绘（值未变或编号非法时直接返回）。
 * @param obj 控件对象指针。
 * @param page 目标页面（WE_KEYBOARD_PAGE_LOWER/UPPER/SYMBOL）。
 * @return 无。
 */
void we_keyboard_set_page(we_keyboard_obj_t *obj, uint8_t page)
{
    if (obj == NULL || page > WE_KEYBOARD_PAGE_SYMBOL || obj->page == page)
        return;

    obj->page = page;
    obj->press_idx = -1; /* 切页后旧键序号失效，同步取消按压态 */
    obj->pressed = 0U;
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
    /* 键光标钳制到新页范围内（各页布局相近，行列基本对位） */
    if (obj->focus_idx >= (int16_t)_kb_cur_page(obj)->total)
        obj->focus_idx = (int16_t)(_kb_cur_page(obj)->total - 1U);
#endif
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置四项配色：面板底色 / 普通键底色 / 按压键底色 / 文字色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param panel 面板底色。
 * @param key 普通键底色。
 * @param key_press 按压键底色。
 * @param text 键面文字色。
 * @return 无。
 */
void we_keyboard_set_colors(we_keyboard_obj_t *obj, colour_t panel,
                            colour_t key, colour_t key_press, colour_t text)
{
    if (obj == NULL)
        return;
    if (_kb_colour_eq(obj->bg_color, panel) &&
        _kb_colour_eq(obj->key_color, key) &&
        _kb_colour_eq(obj->key_press_color, key_press) &&
        _kb_colour_eq(obj->text_color, text))
        return;

    obj->bg_color = panel;
    obj->key_color = key;
    obj->key_press_color = key_press;
    obj->text_color = text;
    _kb_update_fn_color(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_keyboard_set_opacity(we_keyboard_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除控件（弹层模式先收弹层摘动画节点；静态模式从对象链表移除）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_keyboard_obj_delete(we_keyboard_obj_t *obj)
{
    if (obj == NULL)
        return;
    if (obj->base.lcd != NULL)
    {
        we_anim_stop(obj->base.lcd, &obj->anim);
        if (obj->popup_mode)
            we_popup_layer_close(obj->base.lcd, obj); /* 非拥有者时为空操作 */
    }
    we_obj_delete((we_obj_t *)obj); /* 弹层键盘不在链表上，摘链自然落空 */
}
