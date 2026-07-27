#include "we_widget_tabview.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _tv_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 计算单段宽度（扣除左右内边距后等分）。
 * @param obj 传入：控件对象指针。
 * @return 段宽（像素），段数为 0 或几何退化时返回 0。
 */
static int16_t _tv_seg_w(const we_tabview_obj_t *obj)
{
    int16_t inner_w;

    if (obj->count == 0U)
        return 0;
    inner_w = (int16_t)(obj->base.w - 2 * WE_TABVIEW_PAD);
    if (inner_w <= 0)
        return 0;
    return (int16_t)(inner_w / (int16_t)obj->count);
}

/**
 * @brief 计算指定段的高亮块目标偏移（相对 base.x）。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：段序号。
 * @return 高亮块左缘相对控件左缘的偏移（像素）。
 */
static int16_t _tv_seg_ofs(const we_tabview_obj_t *obj, uint8_t idx)
{
    return (int16_t)(WE_TABVIEW_PAD + (int16_t)idx * _tv_seg_w(obj));
}

/**
 * @brief 触点命中检测：返回命中的段序号。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触点 X（屏幕绝对坐标）。
 * @param py 传入：触点 Y。
 * @return 命中的段序号（0 起），未命中返回 -1。
 * @note 内边距带就近归入相邻段，整条区域均可点击。
 */
static int16_t _tv_hit_seg(const we_tabview_obj_t *obj, int16_t px, int16_t py)
{
    int16_t seg_w = _tv_seg_w(obj);
    int16_t rx;
    int16_t seg;

    if (seg_w <= 0)
        return -1;
    if (px < obj->base.x || px >= (int16_t)(obj->base.x + obj->base.w) ||
        py < obj->base.y || py >= (int16_t)(obj->base.y + obj->base.h))
        return -1;

    rx = (int16_t)(px - obj->base.x - WE_TABVIEW_PAD);
    if (rx < 0)
        rx = 0;
    seg = (int16_t)(rx / seg_w);
    if (seg >= (int16_t)obj->count)
        seg = (int16_t)(obj->count - 1U);
    return seg;
}

/* --------------------------------------------------------------------------
 * 高亮块滑动动画（中央动画引擎）
 * -------------------------------------------------------------------------- */

/**
 * @brief 中央动画引擎回调：按 Q8 进度推进高亮块偏移并标脏。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 * @note 到达终点后就位并自行摘链，空闲零开销。
 */
static void _tv_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_tabview_obj_t *obj = (we_tabview_obj_t *)owner;
    uint32_t dt;

    if (obj == NULL)
        return;
    if (elapsed_ms == 0U)
        return;

    if (obj->anim_t >= 256U)
    {
        we_anim_stop(obj->base.lcd, &obj->anim);
        return;
    }

    /* Q8 进度推进：dt = elapsed / anim_ms（至少 1，防止长时长下停滞；
     * 时长宏被覆盖为 0 时按 1ms 处理，编译期常量折叠，无运行时开销） */
    {
        uint32_t dur = (WE_TABVIEW_ANIM_MS > 0U) ? (uint32_t)WE_TABVIEW_ANIM_MS : 1U;
        dt = ((uint32_t)elapsed_ms * 256U) / dur;
    }
    if (dt == 0U)
        dt = 1U;

    if ((uint32_t)obj->anim_t + dt >= 256U)
    {
        obj->anim_t = 256U;
        obj->hl_ofs = obj->hl_to;
        we_anim_stop(obj->base.lcd, &obj->anim); /* 就位即摘链 */
    }
    else
    {
        obj->anim_t = (uint16_t)(obj->anim_t + dt);
        obj->hl_ofs = (int16_t)we_lerp(obj->hl_from, obj->hl_to,
                                       we_ease_out_quad(obj->anim_t));
    }

    /* preview 放宽：整控件包围盒标脏 */
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 切换 active 段：更新值、启动高亮块滑动，按需触发回调。
 * @param obj 传入：控件对象指针。
 * @param idx 传入：目标段序号。
 * @param fire_cb 传入：1 = 值变时触发 changed_cb（点击路径），0 = 不触发（程序路径）。
 * @return 无。
 */
static void _tv_goto(we_tabview_obj_t *obj, uint8_t idx, uint8_t fire_cb)
{
    if (idx >= obj->count || idx == obj->active)
        return;

    obj->active = idx;

    /* 以当前显示位置为新起点，中途重定向也能自然衔接 */
    obj->hl_from = obj->hl_ofs;
    obj->hl_to = _tv_seg_ofs(obj, idx);
    obj->anim_t = 0U;
    we_anim_start(obj->base.lcd, &obj->anim, _tv_anim_step_cb, obj);

    /* 文字亮暗立即切换，先整体标脏一次 */
    we_obj_invalidate((we_obj_t *)obj);

    if (fire_cb && obj->changed_cb != NULL)
        obj->changed_cb(obj, idx);
}

/* --------------------------------------------------------------------------
 * 绘图回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调，向当前 PFB 输出可视内容。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 绘制顺序：整条圆角底 -> active 高亮块（按 hl_ofs 动画位置）-> 各段居中文字。
 */
static void _tabview_draw_cb(void *ptr)
{
    we_tabview_obj_t *obj = (we_tabview_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    int16_t seg_w = _tv_seg_w(obj);
    int16_t hl_h;
    colour_t dim_text;
    int16_t i;

    if (obj->opacity == 0U || obj->base.w <= 0 || obj->base.h <= 0)
        return;

    /* 1. 整条圆角底（radius = h/2，胶囊形分段控制器底条） */
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     (uint16_t)(obj->base.h / 2),
                                     obj->bar_color, obj->opacity);

    if (seg_w <= 0 || obj->labels == NULL)
        return;

    /* 2. active 段高亮块（X 位置来自动画偏移 hl_ofs） */
    hl_h = (int16_t)(obj->base.h - 2 * WE_TABVIEW_PAD);
    if (hl_h > 0)
    {
        we_draw_round_rect_analytic_fill(lcd,
                                         (int16_t)(obj->base.x + obj->hl_ofs),
                                         (int16_t)(obj->base.y + WE_TABVIEW_PAD),
                                         (uint16_t)seg_w, (uint16_t)hl_h,
                                         (uint16_t)(hl_h / 2),
                                         obj->hl_color, obj->opacity);
    }

    /* 3. 各段文字居中：active 用全亮文字色，其余向底条色压暗 */
    dim_text = we_colour_blend(obj->text_color, obj->bar_color,
                               (uint8_t)WE_TABVIEW_DIM_TEXT_A);
    for (i = 0; i < (int16_t)obj->count; i++)
    {
        const char *label = obj->labels[i];
        uint16_t txt_w;
        int8_t y_top;
        int8_t y_bot;
        int16_t seg_cx;
        int16_t txt_x;
        int16_t txt_y;

        if (label == NULL || obj->font == NULL)
            continue;

        txt_w = we_get_text_width(obj->font, label);
        we_get_text_bbox(obj->font, label, &y_top, &y_bot);
        seg_cx = (int16_t)(obj->base.x + WE_TABVIEW_PAD + i * seg_w + seg_w / 2);
        txt_x = (int16_t)(seg_cx - (int16_t)(txt_w / 2U));
        txt_y = (int16_t)(obj->base.y + obj->base.h / 2 - (y_top + y_bot) / 2);

        we_draw_string(lcd, txt_x, txt_y, obj->font, label,
                       (i == (int16_t)obj->active) ? obj->text_color : dim_text,
                       obj->opacity);
    }
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调，处理按压/点击输入。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 = 事件已消费，0 = 穿透。
 */
static uint8_t _tabview_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_tabview_obj_t *obj = (we_tabview_obj_t *)ptr;
    int16_t seg;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        obj->press_seg = _tv_hit_seg(obj, data->x, data->y);
        return (obj->press_seg >= 0) ? 1U : 0U;

    case WE_EVENT_CLICKED:
        seg = _tv_hit_seg(obj, data->x, data->y);
        if (seg >= 0 && seg == obj->press_seg)
        {
            obj->press_seg = -1;
            _tv_goto(obj, (uint8_t)seg, 1U);
            return 1U;
        }
        obj->press_seg = -1;
        return 0U;

    case WE_EVENT_RELEASED:
    case WE_EVENT_STAY:
        /* 页签条无按压视觉；持有触摸序列即可 */
        return (obj->press_seg >= 0) ? 1U : 0U;

    default:
        break;
    }
    return 0U;
}

static const we_class_t _tabview_class = {
    .draw_cb = _tabview_draw_cb,
    .event_cb = _tabview_event_cb,
    .set_pos_cb = NULL
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化页签条控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param w 整条宽度（像素）。
 * @param h 整条高度（像素）。
 * @param labels 页签名数组（调用方持有，count 个）。
 * @param count 段数。
 * @return 无。
 */
void we_tabview_obj_init(we_tabview_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w, int16_t h,
                         const char *const *labels, uint8_t count, const unsigned char *font)
{
    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_tabview_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    obj->labels = labels;
    obj->count = count;
    obj->active = 0U;

    obj->bar_color = RGB888TODEV(44, 52, 66);
    obj->hl_color = RGB888TODEV(64, 152, 231);
    obj->text_color = RGB888TODEV(239, 243, 250);
    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;

    obj->changed_cb = NULL;

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    obj->anim_t = 256U; /* 空闲 */
    obj->hl_ofs = _tv_seg_ofs(obj, 0U);
    obj->hl_from = obj->hl_ofs;
    obj->hl_to = obj->hl_ofs;

    obj->press_seg = -1;
    obj->opacity = 255U;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 程序切换 active 段，高亮块平滑滑动到目标段（不触发回调）。
 * @param obj 控件对象指针。
 * @param idx 目标段序号（越界或与当前相同则直接返回）。
 * @return 无。
 */
void we_tabview_set_active(we_tabview_obj_t *obj, uint8_t idx)
{
    if (obj == NULL)
        return;
    _tv_goto(obj, idx, 0U);
}

/**
 * @brief 读取当前 active 段序号。
 * @param obj 控件对象指针。
 * @return active 段序号（0 起）；obj 为 NULL 时返回 0。
 */
uint8_t we_tabview_get_active(const we_tabview_obj_t *obj)
{
    if (obj == NULL)
        return 0U;
    return obj->active;
}

/**
 * @brief 注册 active 改变回调（点击切换且值变时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_tabview_set_changed_cb(we_tabview_obj_t *obj, we_tabview_changed_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->changed_cb = cb;
}

/**
 * @brief 设置三项配色：底条色 / 高亮块色 / 文字色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param bar 底条色。
 * @param hl 高亮块色。
 * @param text active 文字色。
 * @return 无。
 */
void we_tabview_set_colors(we_tabview_obj_t *obj, colour_t bar,
                           colour_t hl, colour_t text)
{
    if (obj == NULL)
        return;
    if (_tv_colour_eq(obj->bar_color, bar) &&
        _tv_colour_eq(obj->hl_color, hl) &&
        _tv_colour_eq(obj->text_color, text))
        return;

    obj->bar_color = bar;
    obj->hl_color = hl;
    obj->text_color = text;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_tabview_set_opacity(we_tabview_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除控件：先摘除动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_tabview_obj_delete(we_tabview_obj_t *obj)
{
    if (obj == NULL)
        return;
    /* 动画节点归控件所有，删除前必须摘链 */
    we_anim_stop(obj->base.lcd, &obj->anim);
    we_obj_delete((we_obj_t *)obj);
}
