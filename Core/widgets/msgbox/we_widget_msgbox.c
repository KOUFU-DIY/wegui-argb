#include "we_widget_msgbox.h"
#include "wegui.h"
#include "components/we_motion.h"

enum
{
    WE_POPUP_BTN_NONE = 0,
    WE_POPUP_BTN_CONFIRM = 1,
    WE_POPUP_BTN_CANCEL = 2
};

static void _popup_sync_hit_area(we_popup_obj_t *obj);

/**
 * @brief 判断 Unicode 码点是否为空白分隔字符（空格或制表符）。
 * @param code 待判断的 Unicode 码点。
 * @return 1 表示为空白分隔符，0 表示非空白分隔符。
 */
static uint8_t _popup_is_space(uint16_t code)
{
    return (uint8_t)(code == ' ' || code == '\t');
}

/**
 * @brief 查询单个码点的排版步进宽度。
 * @param font 字体资源指针。
 * @param code 待查询的 Unicode 码点。
 * @return 该码点的步进宽度（像素）；字库缺字按半个行高计。
 */
static uint16_t _popup_glyph_width(const unsigned char *font, uint16_t code)
{
    we_glyph_info_t info;

    if (font == NULL)
        return 0U;
    if (we_font_get_glyph_info(font, code, &info))
        return info.adv_w;
    return (uint16_t)(we_font_get_line_height(font) / 2U);
}

/**
 * @brief 统计多行文本中最宽一行的像素宽度。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @return 最宽行的宽度（像素）。
 */
static uint16_t _popup_text_longest_line_width(const unsigned char *font, const char *text)
{
    const char *p = text;
    uint16_t code;
    uint16_t line_width = 0U;
    uint16_t max_width = 0U;

    if (font == NULL || text == NULL)
        return 0U;

    while (we_utf8_next(&p, &code) != 0U)
    {
        if (code == '\n')
        {
            if (line_width > max_width)
                max_width = line_width;
            line_width = 0U;
            continue;
        }
        line_width = (uint16_t)(line_width + _popup_glyph_width(font, code));
    }

    return (line_width > max_width) ? line_width : max_width;
}

/**
 * @brief 找一行的断行位置：遇 '\n' 断；超宽时优先断在行内最后一个空白处，
 *        没有空白则断在最后一个放得下的字符之后。
 * @param font 字体资源指针。
 * @param str 本行起点。
 * @param max_width 行宽上限（像素）。
 * @return 本行结束位置（不含）；指向 '\n'、空白或下一行首字符。
 */
static const char *_popup_find_wrap_point(const unsigned char *font, const char *str, uint16_t max_width)
{
    const char *p = str;
    const char *last_space = NULL;
    const char *last_fit = str;
    uint16_t line_width = 0U;

    if (font == NULL || str == NULL)
        return str;

    for (;;)
    {
        const char *cur = p;
        uint16_t code;
        uint16_t glyph_w;

        if (we_utf8_next(&p, &code) == 0U)
            return cur;
        if (code == '\n')
            return cur;

        glyph_w = _popup_glyph_width(font, code);
        if (line_width > 0U && line_width + glyph_w > max_width)
        {
            if (_popup_is_space(code))
                return cur; /* 溢出的正好是空白：前面的词留在本行 */
            if (last_space != NULL && last_space > str)
                return last_space;
            return last_fit;
        }

        if (_popup_is_space(code))
            last_space = cur;

        line_width = (uint16_t)(line_width + glyph_w);
        last_fit = p;
    }
}

/**
 * @brief 跳过断行处的换行与空白，返回下一行的起点。
 * @param str 断行位置。
 * @return 下一行首个非空白字符的位置。
 */
static const char *_popup_skip_break_chars(const char *str)
{
    for (;;)
    {
        const char *p = str;
        uint16_t code;

        if (we_utf8_next(&p, &code) == 0U || !(code == '\n' || _popup_is_space(code)))
            return str;
        str = p;
    }
}

/**
 * @brief 计算文本在 max_width 下自动折行后的总像素高度。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @param max_width 折行最大宽度（像素）。
 * @return 折行后文本块总高度（像素）：首行按墨迹高，其余每行加一个行高。
 */
static uint16_t _popup_text_block_height_wrapped(const unsigned char *font, const char *text, uint16_t max_width)
{
    uint16_t single_h;
    uint16_t line_count = 0U;
    const char *line = text;
    uint16_t line_height;
    int8_t y_top;
    int8_t y_bottom;

    if (font == NULL || text == NULL || text[0] == '\0')
        return 0U;

    line_height = we_font_get_line_height(font);
    we_get_text_bbox(font, text, &y_top, &y_bottom);
    single_h = (y_bottom > y_top) ? (uint16_t)(y_bottom - y_top) : line_height;

    while (*line != '\0')
    {
        const char *next = _popup_find_wrap_point(font, line, max_width);

        if (next == line)
            break;
        line_count++;
        if (*next == '\0')
            break;
        line = _popup_skip_break_chars(next);
    }

    if (line_count <= 1U)
        return single_h;
    return (uint16_t)(single_h + (uint16_t)(line_count - 1U) * line_height);
}

/**
 * @brief 从 [x,y] 起按 max_width 自动折行，逐行绘制 UTF-8 文本。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param font 字体资源指针。
 * @param text UTF-8 文本字符串。
 * @param x 起始绘制左上角 X 坐标。
 * @param y 起始绘制左上角 Y 坐标。
 * @param max_width 折行最大宽度（像素）。
 * @param color 文本颜色值。
 * @return 无。
 * @note 每行直接从原文按长度绘制，不拷贝行缓冲；行顶已在当前窗口之下即停止。
 */
static void _popup_draw_text_wrapped(we_lcd_t *lcd, const unsigned char *font, const char *text,
                                     int16_t x, int16_t y, uint16_t max_width, colour_t color)
{
    const char *line = text;
    int16_t cursor_y = y;
    uint16_t line_height;

    if (font == NULL || text == NULL)
        return;

    line_height = we_font_get_line_height(font);
    while (*line != '\0' && cursor_y <= lcd->clip.y1)
    {
        const char *next = _popup_find_wrap_point(font, line, max_width);

        if (next == line)
            break;
        we_draw_string_n(lcd, x, cursor_y, font, line, (uint16_t)(next - line), color, 255U);

        if (*next == '\0')
            break;
        line = _popup_skip_break_chars(next);
        cursor_y = (int16_t)(cursor_y + line_height);
    }
}

/* 布局尺度：小屏（SCREEN_HEIGHT<=64）走紧凑档，其余保持原值 */
#if (SCREEN_HEIGHT <= 64)
#define _MB_PAD_TOP 3        /* 顶部留白 */
#define _MB_PAD_SIDE 6       /* 左右内容留白 */
#define _MB_GAP_TITLE_MSG 3  /* 标题-正文间距 */
#define _MB_GAP_ACTION 12    /* 正文-动作区间距（含分隔线空间） */
#define _MB_ACTION_H 15      /* 动作区高度 */
#define _MB_PAD_BOTTOM 3     /* 底部留白 */
#define _MB_SEP_LIFT 6       /* 分隔线相对按钮顶的抬升量 */
#define _MB_BTN_PAD 10       /* 按钮文本两侧留白 */
#define _MB_BTN_MIN_W 28     /* 空文本按钮最小宽 */
#define _MB_BTN_GAP 8        /* 双按钮间距/动作区侧边距 */
#else
#define _MB_PAD_TOP 18
#define _MB_PAD_SIDE 18
#define _MB_GAP_TITLE_MSG 8
#define _MB_GAP_ACTION 28
#define _MB_ACTION_H 28
#define _MB_PAD_BOTTOM 14
#define _MB_SEP_LIFT 10
#define _MB_BTN_PAD 20
#define _MB_BTN_MIN_W 48
#define _MB_BTN_GAP 24
#endif

/**
 * @brief 停靠 Y 夹紧到屏内（面板完整可见）。
 * @param obj 目标控件对象指针。
 * @param y 待夹紧的 Y。
 * @return 夹紧后的 Y。
 */
static int16_t _popup_clamp_y(const we_popup_obj_t *obj, int16_t y)
{
    if (y > (int16_t)(obj->base.lcd->height - obj->panel_h))
        y = (int16_t)(obj->base.lcd->height - obj->panel_h);
    if (y < 0)
        y = 0;
    return y;
}

/**
 * @brief 根据文本与按钮内容重新计算弹窗布局，并缓存绘制要用的文字度量。
 * @param obj 目标控件对象指针。
 * @return 无。
 * @note 绘制回调每个 PFB 切片都会调用，布局与文字度量在这里算一次，
 *       切片内不再测量文字。文字、按钮文案变化时经 set_text / set_buttons / show 重算。
 */
static void _popup_refresh_layout(we_popup_obj_t *obj)
{
    uint16_t max_panel_w;
    uint16_t btn_need_w;
    uint16_t content_need_w;
    uint16_t inner_w;
    uint16_t msg_h;
    uint16_t desired_h;
    uint8_t i;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    /* 按钮宽 = 文字宽 + 两侧留白（空文字取最小宽）；绘制居中用的文字宽与墨迹中线另存 */
    for (i = 0U; i < 2U; i++)
    {
        const char *text = (i == 0U) ? obj->confirm_text : obj->cancel_text;
        uint16_t w = _popup_text_longest_line_width(obj->button_font, text);

        obj->btn_w[i] = (w > 0U) ? (uint16_t)(w + _MB_BTN_PAD) : (uint16_t)_MB_BTN_MIN_W;
        obj->btn_txt_w[i] = 0U;
        obj->btn_ymid[i] = 0;
        if (text != NULL && obj->button_font != NULL)
        {
            int8_t y_top;
            int8_t y_bottom;

            obj->btn_txt_w[i] = we_get_text_width(obj->button_font, text);
            we_get_text_bbox(obj->button_font, text, &y_top, &y_bottom);
            obj->btn_ymid[i] = (int8_t)((y_top + y_bottom) / 2);
        }
    }
    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
        btn_need_w = (uint16_t)(obj->btn_w[1] + _MB_BTN_GAP + obj->btn_w[0] + _MB_BTN_GAP);
    else
        btn_need_w = (uint16_t)(obj->btn_w[0] + _MB_BTN_GAP);

    max_panel_w = (uint16_t)(obj->base.lcd->width - WE_MSGBOX_EDGE_MARGIN * 2);
    content_need_w = _popup_text_longest_line_width(obj->title_font, obj->title);
    {
        uint16_t msg_w = _popup_text_longest_line_width(obj->message_font, obj->message);

        if (msg_w > content_need_w)
            content_need_w = msg_w;
    }
    content_need_w = (uint16_t)(content_need_w + 2U * _MB_PAD_SIDE);
    obj->panel_w = content_need_w;
    if (btn_need_w > obj->panel_w)
        obj->panel_w = btn_need_w;
    if (obj->panel_w < obj->panel_min_w)
        obj->panel_w = obj->panel_min_w;
    if (obj->panel_w > max_panel_w)
        obj->panel_w = max_panel_w;

    inner_w = (obj->panel_w > 2U * _MB_PAD_SIDE) ? (uint16_t)(obj->panel_w - 2U * _MB_PAD_SIDE) : obj->panel_w;
    obj->title_h = _popup_text_block_height_wrapped(obj->title_font, obj->title, inner_w);
    msg_h = _popup_text_block_height_wrapped(obj->message_font, obj->message, inner_w);

    /* 标题/正文首行墨迹上沿：绘制时让墨迹贴齐版心 */
    obj->title_top = 0;
    obj->msg_top = 0;
    if (obj->title != NULL && obj->title_font != NULL)
    {
        int8_t y_bottom;

        we_get_text_bbox(obj->title_font, obj->title, &obj->title_top, &y_bottom);
    }
    if (obj->message != NULL && obj->message_font != NULL)
    {
        int8_t y_bottom;

        we_get_text_bbox(obj->message_font, obj->message, &obj->msg_top, &y_bottom);
    }

    /* 扁平版垂直预算：顶部留白 + 标题区 + 标题-正文间距（正文存在时）
     * + 正文区 + 正文-动作区留白（含分隔线）+ 动作区高 + 底部留白，
     * 各尺度见 _MB_* 宏（小屏自动紧凑档）。高度双向跟随内容。 */
    desired_h = (uint16_t)(_MB_PAD_TOP + _MB_GAP_ACTION + _MB_ACTION_H + _MB_PAD_BOTTOM +
                           obj->title_h + msg_h);
    if (obj->title_h > 0U && msg_h > 0U)
        desired_h = (uint16_t)(desired_h + _MB_GAP_TITLE_MSG);
    obj->panel_h = desired_h;

#if (WE_CFG_MONO_STYLE == 1)
    /* 单色平移动画：panel_y 是动画中的位置，夹紧作用于停靠位 slide_target_y */
    obj->slide_target_y = _popup_clamp_y(obj, obj->slide_target_y);
    if (!obj->animating)
        obj->panel_y = obj->slide_target_y;
#else
    obj->panel_y = _popup_clamp_y(obj, obj->panel_y);
#endif

    _popup_sync_hit_area(obj); /* 面板尺寸/位置可能变化：命中区跟随 */
}

/**
 * @brief 计算面板水平居中后的左上角 X 坐标（(屏宽-panel_w)/2）。
 * @param obj 目标控件对象指针。
 * @return 面板左上角 X 坐标（像素）。
 */
static int16_t _popup_panel_x(const we_popup_obj_t *obj)
{
    return (int16_t)((obj->base.lcd->width - obj->panel_w) / 2);
}

/**
 * @brief 标脏整个面板区域。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_invalidate_panel(const we_popup_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    we_obj_invalidate_area((we_obj_t *)obj, _popup_panel_x(obj), obj->panel_y,
                           (int16_t)obj->panel_w, (int16_t)obj->panel_h);
}

/**
 * @brief 停止淡入/淡出动画：摘除中央动画链节点并清 animating、anim_hiding 标志。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_stop_anim(we_popup_obj_t *obj)
{
    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->animating = 0U;
    obj->anim_hiding = 0U;
}

/**
 * @brief 同步命中区域与可见状态：显示时覆盖面板，隐藏时收零。
 * @param obj 目标控件对象指针。
 * @return 无。
 * @note 显示期间是模态，面板外的触摸由内核交给本对象，命中区只需覆盖面板；
 *       命中区同时是内核的绘制范围（窗口按它收窄），只有碰到面板的 PFB 切片才会调 draw_cb。
 *       核心命中测试只看 bbox + event_cb 非空，不检查 visible，所以隐藏时
 *       必须收零（隐藏的弹窗仍挂在顶层链上）。
 *       WE_CFG_ENABLE_TOP_LAYER=0 裁剪档没有模态，显示时改为铺满全屏，
 *       至少拦住背后控件的触摸（按键拦不住）。
 */
static void _popup_sync_hit_area(we_popup_obj_t *obj)
{
    if (obj->visible)
    {
#if (WE_CFG_ENABLE_TOP_LAYER == 1)
        obj->base.x = _popup_panel_x(obj);
        obj->base.y = obj->panel_y;
        obj->base.w = (int16_t)obj->panel_w;
        obj->base.h = (int16_t)obj->panel_h;
#else
        obj->base.x = 0;
        obj->base.y = 0;
        obj->base.w = (int16_t)obj->base.lcd->width;
        obj->base.h = (int16_t)obj->base.lcd->height;
#endif
    }
    else
    {
        obj->base.w = 0;
        obj->base.h = 0;
    }
}

/**
 * @brief 重定位面板 Y，并重绘旧/新两处区域。
 * @param obj 目标控件对象指针。
 * @param new_y 新的 Y 坐标。
 * @return 无。
 */
static void _popup_set_panel_y(we_popup_obj_t *obj, int16_t new_y)
{
    if (obj->panel_y == new_y)
        return;

    _popup_invalidate_panel(obj);
    obj->panel_y = new_y;
    _popup_sync_hit_area(obj); /* 命中区/绘制范围跟随面板 */
    _popup_invalidate_panel(obj);
}

/**
 * @brief 按按钮编号取按钮矩形（动作区底部：单按钮居中；双按钮时取消在左、
 *        确认在右，整组居中）。
 * @param obj 目标控件对象指针。
 * @param btn 按钮编号（WE_POPUP_BTN_*）。
 * @param[out] x/y/w/h 按钮矩形。
 * @return 1 表示当前布局有该按钮，0 表示没有（含 NONE）。
 */
static uint8_t _popup_get_btn_rect(const we_popup_obj_t *obj, uint8_t btn,
                                   int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    int16_t panel_x = _popup_panel_x(obj);
    int16_t confirm_w = (int16_t)obj->btn_w[0];

    if (btn == WE_POPUP_BTN_NONE)
        return 0U;

    *y = (int16_t)(obj->panel_y + obj->panel_h - _MB_ACTION_H - _MB_PAD_BOTTOM);
    *h = _MB_ACTION_H;
    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
    {
        int16_t cancel_w = (int16_t)obj->btn_w[1];
        int16_t group_x = (int16_t)(panel_x + (obj->panel_w - (cancel_w + _MB_BTN_GAP + confirm_w)) / 2);

        if (btn == WE_POPUP_BTN_CANCEL)
        {
            *x = group_x;
            *w = cancel_w;
        }
        else
        {
            *x = (int16_t)(group_x + cancel_w + _MB_BTN_GAP);
            *w = confirm_w;
        }
        return 1U;
    }
    if (btn != WE_POPUP_BTN_CONFIRM)
        return 0U;
    *x = (int16_t)(panel_x + (obj->panel_w - confirm_w) / 2);
    *w = confirm_w;
    return 1U;
}

/**
 * @brief 命中测试：返回点所在的按钮编号。
 * @param obj 目标控件对象指针。
 * @param px 待检测点 X 坐标。
 * @param py 待检测点 Y 坐标。
 * @return WE_POPUP_BTN_*，不在任何按钮上返回 WE_POPUP_BTN_NONE。
 */
static uint8_t _popup_hit_button(const we_popup_obj_t *obj, int16_t px, int16_t py)
{
    uint8_t btn;

    for (btn = WE_POPUP_BTN_CONFIRM; btn <= WE_POPUP_BTN_CANCEL; btn++)
    {
        int16_t x, y, w, h;

        if (_popup_get_btn_rect(obj, btn, &x, &y, &w, &h) &&
            px >= x && px < (x + w) && py >= y && py < (y + h))
            return btn;
    }
    return WE_POPUP_BTN_NONE;
}

/**
 * @brief 标脏全部按钮区域（按压态变化时重绘按钮）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_invalidate_buttons(we_popup_obj_t *obj)
{
    uint8_t btn;

    for (btn = WE_POPUP_BTN_CONFIRM; btn <= WE_POPUP_BTN_CANCEL; btn++)
    {
        int16_t x, y, w, h;

        if (_popup_get_btn_rect(obj, btn, &x, &y, &w, &h))
            we_obj_invalidate_area((we_obj_t *)obj, x, y, w, h);
    }
}

/**
 * @brief 触发按钮动作（触摸点击与按键共用；回调里通常调用 we_popup_hide 收起）。
 * @param obj 目标控件对象指针。
 * @param btn 按钮编号（WE_POPUP_BTN_*）。
 * @return 无。
 */
static void _popup_fire(we_popup_obj_t *obj, uint8_t btn)
{
    if (btn == WE_POPUP_BTN_CONFIRM && obj->confirm_cb != NULL)
        obj->confirm_cb(obj);
    else if (btn == WE_POPUP_BTN_CANCEL && obj->cancel_cb != NULL)
        obj->cancel_cb(obj);
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_MSGBOX_USE_KEY == 1)
/**
 * @brief 标脏某个按钮的键光标环区域（按钮四周外扩 2px）。
 * @param obj 目标控件对象指针。
 * @param btn 按钮编号（WE_POPUP_BTN_NONE 时空操作）。
 * @return 无。
 */
static void _popup_invalidate_key_ring(we_popup_obj_t *obj, uint8_t btn)
{
    int16_t x, y, w, h;

    if (_popup_get_btn_rect(obj, btn, &x, &y, &w, &h))
        we_obj_invalidate_area((we_obj_t *)obj, (int16_t)(x - 2), (int16_t)(y - 2),
                               (int16_t)(w + 4), (int16_t)(h + 4));
}

/**
 * @brief 把键光标移到指定按钮（新旧环形区各标脏一次）。
 * @param obj 目标控件对象指针。
 * @param btn 目标按钮编号。
 * @return 无。
 */
static void _popup_key_to(we_popup_obj_t *obj, uint8_t btn)
{
    if (btn == obj->key_btn)
        return;
    _popup_invalidate_key_ring(obj, obj->key_btn);
    obj->key_btn = btn;
    _popup_invalidate_key_ring(obj, btn);
}
#endif /* WE_CFG_ENABLE_KEY_INPUT && WE_MSGBOX_USE_KEY */

/**
 * @brief 绘制单个弹窗按钮：居中文本 + 按下/次级态配色 + 按下时显示下划线。
 * @param obj 目标控件对象指针。
 * @param btn 按钮编号（WE_POPUP_BTN_CONFIRM / WE_POPUP_BTN_CANCEL）。
 * @param secondary 是否按次级按钮配色绘制。
 * @return 无。
 */
static void _popup_draw_button(we_popup_obj_t *obj, uint8_t btn, uint8_t secondary)
{
    const char *text = (btn == WE_POPUP_BTN_CONFIRM) ? obj->confirm_text : obj->cancel_text;
    uint8_t idx = (uint8_t)(btn - 1U);
    int16_t x, y, w, h;
    int16_t txt_x;
    colour_t text_color;
    uint8_t show_underline;

    if (text == NULL || obj->button_font == NULL || !_popup_get_btn_rect(obj, btn, &x, &y, &w, &h))
        return;

    if (obj->pressed_btn == btn)
    {
        text_color = RGB888TODEV(245, 248, 252);
        show_underline = 1U;
    }
    else if (secondary)
    {
        text_color = RGB888TODEV(176, 183, 194);
        show_underline = 0U;
    }
    else
    {
        text_color = RGB888TODEV(236, 241, 248);
        show_underline = 1U;
    }

    txt_x = (int16_t)(x + (w - (int16_t)obj->btn_txt_w[idx]) / 2);
    we_draw_string(obj->base.lcd, txt_x, (int16_t)(y + h / 2 - obj->btn_ymid[idx] - 2),
                   obj->button_font, text, text_color, 255);

    if (show_underline)
    {
        int16_t underline_y = (int16_t)(y + h - 4);

        we_draw_line(obj->base.lcd, (int16_t)(txt_x - 2), underline_y,
                     (int16_t)(txt_x + (int16_t)obj->btn_txt_w[idx] + 1), underline_y, 1, text_color, 255);
    }
}

/**
 * @brief 控件绘制回调：面板 + 标题 + 正文 + 分隔线 + 按钮 + 键光标环。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _popup_draw_cb(void *ptr)
{
    we_popup_obj_t *obj = (we_popup_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t saved_opa;
    uint8_t fade;
    int16_t panel_x;
    int16_t content_x;
    int16_t content_y;
    uint16_t inner_w;

    if (obj->visible == 0U)
        return;
    fade = we_opa_apply(lcd, obj->fade_opa);
    if (fade == 0U)
        return; /* 完全透明（淡入首帧 / 淡出末帧） */

    panel_x = _popup_panel_x(obj);
#if (WE_CFG_ENABLE_TOP_LAYER == 0)
    /* 裁剪档包围盒铺满全屏：面板不在当前窗口里就不必画 */
    if (panel_x > lcd->clip.x1 || (int16_t)(panel_x + obj->panel_w - 1) < lcd->clip.x0 ||
        obj->panel_y > lcd->clip.y1 || (int16_t)(obj->panel_y + obj->panel_h - 1) < lcd->clip.y0)
        return;
#endif

    /* 淡入淡出：把当前不透明度压入级联乘子，整框统一混合 */
    saved_opa = lcd->opa_scale;
    lcd->opa_scale = fade;

    inner_w = (uint16_t)((int16_t)obj->panel_w - 2 * _MB_PAD_SIDE);
    content_x = (int16_t)(panel_x + _MB_PAD_SIDE);

    /* 面板圆角填充：仅 4 个 r×r 角落做抗锯齿，主体/直边走快速矩形填充。
     * 透明度由入口的 opa_scale 级联统一作用，主体与四角同一份，淡入淡出无亮度断层。 */
#if (WE_CFG_MONO_STYLE == 1)
    /* 单色样式：黑底面板 + 1px 白描边（深色底二值化后不可见，改描边圈定弹窗） */
    we_draw_round_rect_outline_fill(lcd, panel_x, obj->panel_y, obj->panel_w, obj->panel_h,
                                    WE_MSGBOX_RADIUS, RGB888TODEV(255, 255, 255),
                                    RGB888TODEV(0, 0, 0), 255);
#else
    we_draw_round_rect_analytic_fill(lcd, panel_x, obj->panel_y, obj->panel_w, obj->panel_h,
                                     WE_MSGBOX_RADIUS, RGB888TODEV(46, 52, 64), 255);
#endif

    content_y = (int16_t)(obj->panel_y + _MB_PAD_TOP);
    if (obj->title != NULL && obj->title_font != NULL)
    {
        _popup_draw_text_wrapped(lcd, obj->title_font, obj->title, content_x,
                                 (int16_t)(content_y - obj->title_top), inner_w,
                                 RGB888TODEV(244, 247, 252));
        content_y = (int16_t)(content_y + obj->title_h);
    }

    if (obj->message != NULL && obj->message_font != NULL)
    {
        if (obj->title != NULL && obj->title[0] != '\0')
            content_y = (int16_t)(content_y + _MB_GAP_TITLE_MSG);
        _popup_draw_text_wrapped(lcd, obj->message_font, obj->message, content_x,
                                 (int16_t)(content_y - obj->msg_top), inner_w,
                                 RGB888TODEV(204, 214, 228));
    }

    {
        int16_t sep_y = (int16_t)(obj->panel_y + obj->panel_h - _MB_ACTION_H - _MB_PAD_BOTTOM - _MB_SEP_LIFT);

        we_draw_line(lcd, content_x, sep_y, (int16_t)(panel_x + obj->panel_w - _MB_PAD_SIDE), sep_y,
                     1, RGB888TODEV(78, 178, 255), 255);
    }

    _popup_draw_button(obj, WE_POPUP_BTN_CONFIRM, 0U);
    if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
        _popup_draw_button(obj, WE_POPUP_BTN_CANCEL, 1U);

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_MSGBOX_USE_KEY == 1)
    /* 键光标环：键光标按钮四周 2px 描边（画在按钮外侧留白里，聚焦导航色），
     * 与面板一起受淡入淡出的 opa_scale 作用 */
    {
        int16_t x, y, w, h;

        if (_popup_get_btn_rect(obj, obj->key_btn, &x, &y, &w, &h))
        {
            colour_t rc = RGB888TODEV(WE_CFG_FOCUS_CURSOR_R, WE_CFG_FOCUS_CURSOR_G,
                                      WE_CFG_FOCUS_CURSOR_B);

            we_fill_rect(lcd, (int16_t)(x - 2), (int16_t)(y - 2), (uint16_t)(w + 4), 2U, rc, 255);
            we_fill_rect(lcd, (int16_t)(x - 2), (int16_t)(y + h), (uint16_t)(w + 4), 2U, rc, 255);
            we_fill_rect(lcd, (int16_t)(x - 2), y, 2U, (uint16_t)h, rc, 255);
            we_fill_rect(lcd, (int16_t)(x + w), y, 2U, (uint16_t)h, rc, 255);
        }
    }
#endif

    lcd->opa_scale = saved_opa;
}

/**
 * @brief 清空按压与键光标状态（显示、收起时调用）。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
static void _popup_reset_input(we_popup_obj_t *obj)
{
    obj->pressed_btn = WE_POPUP_BTN_NONE;
    obj->armed_btn = WE_POPUP_BTN_NONE;
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_MSGBOX_USE_KEY == 1)
    obj->key_btn = WE_POPUP_BTN_NONE;
    obj->key_armed = 0U;
#endif
}

/**
 * @brief 立即收起：不播退场动画、不触发按钮回调，并解除模态。
 * @param obj 目标控件对象指针。
 * @return 无。
 * @note 用于被新模态替换（WE_EVENT_MODAL_CLOSE）。
 */
static void _popup_hide_now(we_popup_obj_t *obj)
{
    _popup_invalidate_panel(obj); /* 擦除当前面板 */
#if (WE_CFG_MONO_STYLE == 1)
    if (obj->animating)
        obj->panel_y = obj->slide_target_y; /* 平移途中收起：面板回到停靠位，供下次 show 使用 */
#endif
    _popup_stop_anim(obj);
    obj->visible = 0U;
    obj->fade_opa = 0U;
    _popup_reset_input(obj);
    _popup_sync_hit_area(obj);
    we_modal_close(obj->base.lcd, (we_obj_t *)obj);
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_MSGBOX_USE_KEY == 1)
/**
 * @brief 模态键通道：方向键在按钮间移动键光标，OK 双沿触发，BACK 取消。
 * @param obj 目标控件对象指针。
 * @param code 语义键编码（松开沿带 WE_KEY_RELEASE_FLAG）。
 * @return 恒为 1（模态弹窗吞掉全部按键）。
 * @note 键光标未落位时，首个方向键或 OK 只把光标落到确认按钮，不触发；
 *       OK 松开沿只在配对的按下沿之后、且光标仍在同一按钮上时触发，
 *       弹出瞬间收到的孤立松开沿被忽略。BACK 走取消按钮，单按钮布局走确认。
 */
static uint8_t _popup_key_cb(we_popup_obj_t *obj, uint8_t code)
{
    uint8_t key = (uint8_t)(code & (uint8_t)~WE_KEY_RELEASE_FLAG);
    uint8_t btn;

    if ((code & WE_KEY_RELEASE_FLAG) != 0U)
    {
        if (key == WE_KEY_OK && obj->key_armed)
        {
            btn = obj->armed_btn;
            obj->key_armed = 0U;
            obj->pressed_btn = WE_POPUP_BTN_NONE;
            obj->armed_btn = WE_POPUP_BTN_NONE;
            _popup_invalidate_buttons(obj);
            if (btn == obj->key_btn)
                _popup_fire(obj, btn);
        }
        return 1U;
    }

    switch (key)
    {
    case WE_KEY_OK: /* 按下沿：键光标按钮进入按压态，松开沿触发 */
        if (obj->key_btn == WE_POPUP_BTN_NONE)
        {
            _popup_key_to(obj, WE_POPUP_BTN_CONFIRM); /* 首键落位 */
            break;
        }
        obj->pressed_btn = obj->key_btn;
        obj->armed_btn = obj->key_btn;
        obj->key_armed = 1U;
        _popup_invalidate_buttons(obj);
        break;
    case WE_KEY_BACK:
        btn = (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL) ? WE_POPUP_BTN_CANCEL
                                                              : WE_POPUP_BTN_CONFIRM;
        if (obj->pressed_btn != WE_POPUP_BTN_NONE)
            _popup_invalidate_buttons(obj);
        obj->key_armed = 0U;
        obj->pressed_btn = WE_POPUP_BTN_NONE;
        obj->armed_btn = WE_POPUP_BTN_NONE;
        _popup_fire(obj, btn);
        break;
    case WE_KEY_UP:
    case WE_KEY_DOWN:
    case WE_KEY_LEFT:
    case WE_KEY_RIGHT:
    case WE_KEY_PREV:
    case WE_KEY_NEXT:
        if (obj->key_btn == WE_POPUP_BTN_NONE)
        {
            _popup_key_to(obj, WE_POPUP_BTN_CONFIRM); /* 首键落位，不位移 */
        }
        else if (obj->layout == WE_POPUP_LAYOUT_CONFIRM_CANCEL)
        {
            if (key == WE_KEY_LEFT)
                btn = WE_POPUP_BTN_CANCEL; /* 取消按钮在左 */
            else if (key == WE_KEY_RIGHT)
                btn = WE_POPUP_BTN_CONFIRM; /* 确认按钮在右 */
            else /* 上/下/前/后：两个按钮间轮换（单键板只有 NEXT 也能选到取消） */
                btn = (obj->key_btn == WE_POPUP_BTN_CONFIRM) ? WE_POPUP_BTN_CANCEL
                                                             : WE_POPUP_BTN_CONFIRM;
            _popup_key_to(obj, btn);
        }
        break;
    default:
        break;
    }
    return 1U;
}
#endif /* WE_CFG_ENABLE_KEY_INPUT && WE_MSGBOX_USE_KEY */

/**
 * @brief 控件事件回调，处理按压/滑动/点击输入与模态通知、按键。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 显示期间恒为 1（模态吞掉全部输入）；隐藏时为 0。
 */
static uint8_t _popup_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_popup_obj_t *obj = (we_popup_obj_t *)ptr;
    uint8_t hit_btn;
    uint8_t old_pressed;

    if (obj->visible == 0U || data == NULL)
        return 0U;

    if (event == WE_EVENT_MODAL_CLOSE)
    {
        _popup_hide_now(obj); /* 被新模态替换：立即收起，不触发按钮回调 */
        return 1U;
    }
    if (obj->anim_hiding)
        return 1U; /* 淡出中：仍拦截输入，但不再响应按钮（防重复触发） */
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    if ((uint8_t)event >= WE_KEY_UP)
    {
#if (WE_MSGBOX_USE_KEY == 1)
        return _popup_key_cb(obj, (uint8_t)event);
#else
        return 1U; /* 裁掉按键操作：模态照常吞键 */
#endif
    }
#endif

    old_pressed = obj->pressed_btn;
    hit_btn = _popup_hit_button(obj, data->x, data->y);

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->pressed_btn = hit_btn;
        obj->armed_btn = hit_btn;
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_MSGBOX_USE_KEY == 1)
        obj->key_armed = 0U; /* 触摸接管：作废尚未松开的 OK 按压 */
#endif
        break;
    case WE_EVENT_RELEASED:
        obj->pressed_btn = WE_POPUP_BTN_NONE;
        break;
    case WE_EVENT_CLICKED:
        obj->pressed_btn = WE_POPUP_BTN_NONE;
        if (obj->armed_btn == hit_btn)
            _popup_fire(obj, hit_btn);
        obj->armed_btn = WE_POPUP_BTN_NONE;
        break;
    default:
        break;
    }

    if (old_pressed != obj->pressed_btn)
        _popup_invalidate_buttons(obj);

    return 1U;
}

/**
 * @brief 中央动画引擎回调，按时间片推进弹窗淡入/淡出（单色为平移）动画。
 * @param owner 弹窗对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 * @note 起点取 show/hide 调用那一刻的状态（anim_from），中途反向不跳变。
 */
static void _popup_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_popup_obj_t *obj = (we_popup_obj_t *)owner;
    uint16_t t;
    uint16_t eased;

    if (obj->visible == 0U)
    {
        _popup_stop_anim(obj);
        return;
    }

    if (obj->anim_duration_ms == 0U)
        obj->anim_duration_ms = WE_MSGBOX_ANIM_DURATION_MS;

    if (obj->anim_elapsed_ms + elapsed_ms >= obj->anim_duration_ms)
        obj->anim_elapsed_ms = obj->anim_duration_ms;
    else
        obj->anim_elapsed_ms = (uint16_t)(obj->anim_elapsed_ms + elapsed_ms);

    t = (uint16_t)(((uint32_t)obj->anim_elapsed_ms * 256U) / obj->anim_duration_ms);
    if (t > 256U)
        t = 256U;

    /* 显示：三次缓出（起步快、收尾慢）；隐藏：二次缓入（先慢后快拉走） */
    eased = obj->anim_hiding ? we_ease_in_quad(t) : we_ease_out_cubic(t);
#if (WE_CFG_MONO_STYLE == 1)
    /* 单色样式：整体平移代替淡入淡出——show 滑到停靠位，hide 滑出屏外上方
     * （_popup_set_panel_y 自带旧/新区标脏） */
    {
        int16_t to = obj->anim_hiding ? (int16_t)-(int16_t)obj->panel_h : obj->slide_target_y;

        _popup_set_panel_y(obj, (int16_t)we_lerp(obj->anim_from, to, eased));
    }
#else
    /* 透明度淡入/淡出：show →255(缓出)，hide →0(缓入)。面板位置固定不动。 */
    obj->fade_opa = (uint8_t)we_lerp(obj->anim_from, obj->anim_hiding ? 0 : 255, eased);
    _popup_invalidate_panel(obj);
#endif

    if (obj->anim_elapsed_ms >= obj->anim_duration_ms)
    {
        uint8_t hiding = obj->anim_hiding;

        obj->fade_opa = hiding ? 0U : 255U;
        if (hiding)
        {
            obj->visible = 0U;
            _popup_sync_hit_area(obj); /* 隐藏完成：命中区收零 */
        }
        _popup_invalidate_panel(obj);
        _popup_stop_anim(obj);
        if (hiding)
        {
#if (WE_CFG_MONO_STYLE == 1)
            obj->panel_y = obj->slide_target_y; /* 面板已滑出屏外：回到停靠位，供下次 show 使用 */
#endif
            we_modal_close(obj->base.lcd, (we_obj_t *)obj); /* 淡出结束才解除模态 */
        }
    }
}

static const we_class_t _popup_class = {
    .draw_cb = _popup_draw_cb,
    .event_cb = _popup_event_cb,
    .set_pos_cb = NULL,
};

/**
 * @brief 弹窗通用初始化：写入面板几何/文案/字体/回调/布局，刷新布局并挂入对象链表（初始隐藏、命中区收零）。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 面板最小宽度（像素）。
 * @param h 面板初始高度（像素，布局随内容重算）。
 * @param target_y 弹窗停靠 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param confirm_text 确认按钮文本。
 * @param cancel_text 取消按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param confirm_cb 确认按钮回调函数。
 * @param cancel_cb 取消按钮回调函数。
 * @param layout 弹窗布局类型（仅确认/确认+取消）。
 * @return 无。
 */
static void _popup_init_common(we_popup_obj_t *obj, we_lcd_t *lcd,
                               uint16_t w, uint16_t h, int16_t target_y,
                               const char *title, const char *message,
                               const char *confirm_text, const char *cancel_text,
                               const unsigned char *title_font,
                               const unsigned char *message_font,
                               const unsigned char *button_font,
                               we_popup_action_cb_t confirm_cb,
                               we_popup_action_cb_t cancel_cb,
                               we_popup_layout_t layout)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = 0;
    obj->base.y = 0;
    /* 初始为隐藏态：命中区收零，避免吞掉输入；we_popup_show 时命中区跟随面板 */
    obj->base.w = 0;
    obj->base.h = 0;
    obj->base.parent = NULL;
    obj->base.class_p = &_popup_class;
    obj->base.next = NULL;

    obj->panel_min_w = (uint16_t)WE_MIN((int32_t)w, (int32_t)(lcd->width - WE_MSGBOX_EDGE_MARGIN * 2));
    obj->panel_w = obj->panel_min_w;
    obj->panel_h = h;
    obj->panel_y = target_y;
#if (WE_CFG_MONO_STYLE == 1)
    obj->slide_target_y = target_y;
#endif
    obj->anim_from = 0;
    obj->anim_elapsed_ms = 0U;
    obj->anim_duration_ms = WE_MSGBOX_ANIM_DURATION_MS;
    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    obj->layout = (uint8_t)layout;
    _popup_reset_input(obj);
    obj->visible = 0U;
    obj->animating = 0U;
    obj->anim_hiding = 0U;
    obj->fade_opa = 0U;

    obj->title = title;
    obj->message = message;
    obj->confirm_text = confirm_text;
    obj->cancel_text = cancel_text;
    obj->title_font = title_font;
    obj->message_font = message_font;
    obj->button_font = button_font;
    obj->confirm_cb = confirm_cb;
    obj->cancel_cb = cancel_cb;

    _popup_refresh_layout(obj);

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
}

/**
 * @brief 初始化单确定按钮消息框（仅确定布局），并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 面板最小宽度（像素）。
 * @param h 面板初始高度（像素）。
 * @param target_y 弹窗停靠 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @return 无。
 */
void we_msgbox_ok_obj_init(we_msgbox_obj_t *obj, we_lcd_t *lcd,
                           uint16_t w, uint16_t h, int16_t target_y,
                           const char *title, const char *message,
                           const char *ok_text,
                           const unsigned char *title_font,
                           const unsigned char *message_font,
                           const unsigned char *button_font,
                           we_msgbox_action_cb_t ok_cb)
{
    _popup_init_common(obj, lcd, w, h, target_y, title, message,
                       ok_text, NULL, title_font, message_font, button_font,
                       ok_cb, NULL, WE_POPUP_LAYOUT_CONFIRM);
}

/**
 * @brief 初始化确定+取消双按钮消息框（确定+取消布局），并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param w 面板最小宽度（像素）。
 * @param h 面板初始高度（像素）。
 * @param target_y 弹窗停靠 Y 坐标。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @param ok_text 确定按钮文本。
 * @param cancel_text 取消按钮文本。
 * @param title_font 标题字体资源指针。
 * @param message_font 正文字体资源指针。
 * @param button_font 按钮字体资源指针。
 * @param ok_cb 确定按钮回调函数。
 * @param cancel_cb 取消按钮回调函数。
 * @return 无。
 */
void we_msgbox_ok_cancel_obj_init(we_msgbox_obj_t *obj, we_lcd_t *lcd,
                                  uint16_t w, uint16_t h, int16_t target_y,
                                  const char *title, const char *message,
                                  const char *ok_text, const char *cancel_text,
                                  const unsigned char *title_font,
                                  const unsigned char *message_font,
                                  const unsigned char *button_font,
                                  we_msgbox_action_cb_t ok_cb,
                                  we_msgbox_action_cb_t cancel_cb)
{
    _popup_init_common(obj, lcd, w, h, target_y, title, message,
                       ok_text, cancel_text, title_font, message_font, button_font,
                       ok_cb, cancel_cb, WE_POPUP_LAYOUT_CONFIRM_CANCEL);
}

/**
 * @brief 设置标题与正文并重排（可见时重绘旧/新面板区）。
 * @param obj 目标控件对象指针。
 * @param title 标题文本字符串。
 * @param message 正文文本字符串。
 * @return 无。
 */
void we_popup_set_text(we_popup_obj_t *obj, const char *title, const char *message)
{
    if (obj == NULL)
        return;
    if (obj->visible)
        _popup_invalidate_panel(obj);
    obj->title = title;
    obj->message = message;
    _popup_refresh_layout(obj);
    if (obj->visible)
        _popup_invalidate_panel(obj);
}

/**
 * @brief 设置弹窗按钮文案并重排（按钮宽影响面板宽；可见时重绘旧/新面板区；
 *        单按钮布局仅用 confirm_text）。
 * @param obj 目标控件对象指针。
 * @param confirm_text 确认按钮文本。
 * @param cancel_text 取消按钮文本。
 * @return 无。
 */
void we_popup_set_buttons(we_popup_obj_t *obj, const char *confirm_text, const char *cancel_text)
{
    if (obj == NULL)
        return;
    if (obj->visible)
        _popup_invalidate_panel(obj);
    obj->confirm_text = confirm_text;
    obj->cancel_text = cancel_text;
    _popup_refresh_layout(obj);
    if (obj->visible)
        _popup_invalidate_panel(obj);
}

/**
 * @brief 设置弹窗停靠的目标 Y（自动夹紧到屏内）。
 * @param obj 目标控件对象指针。
 * @param target_y 弹窗目标停靠 Y 坐标。
 * @return 无。
 * @note 可见时立即移到新位置并重绘旧/新区；单色平移动画途中只改停靠位，
 *       由动画走过去（退场结束也回到新停靠位）。
 */
void we_popup_set_target_y(we_popup_obj_t *obj, int16_t target_y)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    target_y = _popup_clamp_y(obj, target_y);
#if (WE_CFG_MONO_STYLE == 1)
    obj->slide_target_y = target_y;
    if (obj->animating)
        return;
#endif
    if (obj->visible)
        _popup_set_panel_y(obj, target_y);
    else
        obj->panel_y = target_y;
}

/**
 * @brief 显示弹窗：挂顶层链并声明模态，按 WE_MSGBOX_USE_ANIM 决定淡入或直接全显。
 * @param obj 目标控件对象指针。
 * @return 无。
 * @note 淡出途中再次显示：从当前透明度（单色：当前位置）接着淡入，不闪。
 */
void we_popup_show(we_popup_obj_t *obj)
{
    uint8_t was_visible;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    was_visible = obj->visible;
    we_obj_attach_to_top(obj->base.lcd, (we_obj_t *)obj); /* 顶层：保证置顶且不打乱普通层 Z 序 */
    we_modal_open(obj->base.lcd, (we_obj_t *)obj);        /* 模态：背后控件收不到触摸与按键 */
    obj->visible = 1U;
    _popup_reset_input(obj);
    obj->anim_elapsed_ms = 0U;
    obj->anim_hiding = 0U;
    obj->anim_duration_ms = WE_MSGBOX_ANIM_DURATION_MS;
    _popup_refresh_layout(obj); /* 内含命中区同步：显示期间命中区 = 面板 */

#if WE_MSGBOX_USE_ANIM
#if (WE_CFG_MONO_STYLE == 1)
    /* 单色样式：不透明度恒 255（阈值二值化下透明度不可用），
     * 面板从屏幕上方（或当前位置）整体平移到停靠位 */
    obj->fade_opa = 255U;
    obj->anim_from = was_visible ? obj->panel_y : (int16_t)-(int16_t)obj->panel_h;
    obj->panel_y = obj->anim_from;
    _popup_sync_hit_area(obj); /* 面板从起点出发：命中区跟随 */
#else
    obj->anim_from = was_visible ? (int16_t)obj->fade_opa : 0;
    obj->fade_opa = (uint8_t)obj->anim_from;
#endif
    we_anim_start(obj->base.lcd, &obj->anim, _popup_anim_step_cb, obj);
    obj->animating = 1U;
#else
    (void)was_visible;
    obj->fade_opa = 255U; /* 不带动画：直接全显 */
    obj->animating = 0U;
#endif

    _popup_invalidate_panel(obj);
}

/**
 * @brief 隐藏弹窗：按 WE_MSGBOX_USE_ANIM 触发淡出动画（淡出结束解除模态），
 *        或直接收零命中区、隐藏并解除模态。
 * @param obj 目标控件对象指针。
 * @return 无。
 */
void we_popup_hide(we_popup_obj_t *obj)
{
    if (obj == NULL)
        return;
    if (obj->visible == 0U && obj->animating == 0U)
        return;

    _popup_reset_input(obj);
    obj->anim_elapsed_ms = 0U;
    obj->anim_hiding = 1U;
    obj->visible = 1U;
    obj->anim_duration_ms = (uint16_t)(WE_MSGBOX_ANIM_DURATION_MS / 2U);
    if (obj->anim_duration_ms < 90U)
        obj->anim_duration_ms = 90U;

#if WE_MSGBOX_USE_ANIM
#if (WE_CFG_MONO_STYLE == 1)
    obj->anim_from = obj->panel_y; /* 单色平移：从当前位置向上滑出屏外（停靠位不变） */
#else
    obj->anim_from = (int16_t)obj->fade_opa; /* 从当前透明度淡出 */
#endif
    we_anim_start(obj->base.lcd, &obj->anim, _popup_anim_step_cb, obj);
    obj->animating = 1U;
#else
    obj->fade_opa = 0U;
    obj->visible = 0U;
    _popup_sync_hit_area(obj); /* 隐藏：命中区收零 */
    obj->animating = 0U;
    obj->anim_hiding = 0U;
    _popup_invalidate_panel(obj);
    we_modal_close(obj->base.lcd, (we_obj_t *)obj);
#endif
}
