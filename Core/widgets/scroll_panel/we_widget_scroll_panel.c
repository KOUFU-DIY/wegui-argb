#include "we_widget_scroll_panel.h"
#include "we_render.h"


static int16_t _scroll_panel_max_scroll_y(const we_scroll_panel_obj_t *obj);
static void _scroll_panel_shift_children(we_scroll_panel_obj_t *obj, int16_t dy);

static int16_t _scroll_panel_clamp_to_bounds(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t max_scroll;

    if (obj == NULL)
        return scroll_y;

    max_scroll = _scroll_panel_max_scroll_y(obj);
    if (scroll_y < 0)
        scroll_y = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
    return scroll_y;
}

static void _scroll_panel_update_inner_rect(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return;

    obj->inner_x = (int16_t)(obj->base.x + 1);
    obj->inner_y = (int16_t)(obj->base.y + 1);
    obj->inner_w = (int16_t)(obj->base.w - 2);
    obj->inner_h = (int16_t)(obj->base.h - 2);

    if (obj->inner_w < 0)
        obj->inner_w = 0;
    if (obj->inner_h < 0)
        obj->inner_h = 0;
}

static int16_t _scroll_panel_clamp_overscroll(int16_t scroll_y, int16_t max_scroll)
{
#if WE_SCROLL_PANEL_USE_REBOUND
    if (scroll_y < -WE_SCROLL_PANEL_OVERSCROLL_LIMIT)
        scroll_y = -WE_SCROLL_PANEL_OVERSCROLL_LIMIT;
    if (scroll_y > max_scroll + WE_SCROLL_PANEL_OVERSCROLL_LIMIT)
        scroll_y = (int16_t)(max_scroll + WE_SCROLL_PANEL_OVERSCROLL_LIMIT);
#else
    if (scroll_y < 0)
        scroll_y = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
#endif
    return scroll_y;
}

static void _scroll_panel_apply_scroll_raw(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t max_scroll;
    int16_t old;

    if (obj == NULL)
        return;

    max_scroll = _scroll_panel_max_scroll_y(obj);
    scroll_y = _scroll_panel_clamp_overscroll(scroll_y, max_scroll);
    if (obj->scroll_y == scroll_y)
        return;

    old = obj->scroll_y;
    obj->scroll_y = scroll_y;
    obj->anim_scroll_y = scroll_y;
    _scroll_panel_shift_children(obj, (int16_t)(old - scroll_y));
    we_obj_invalidate((we_obj_t *)obj);
}

static void _scroll_panel_stop_anim(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return;

    obj->animating = 0U;
    obj->drag_vy = 0;
    we_anim_stop(obj->base.lcd, &obj->anim);
}

static void _scroll_panel_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)owner;
    int16_t max_scroll;
    int16_t overshoot;
    int16_t step;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    max_scroll = _scroll_panel_max_scroll_y(obj);

#if WE_SCROLL_PANEL_USE_INERTIA
    if (obj->drag_vy != 0)
    {
        step = (int16_t)((obj->drag_vy * (int16_t)elapsed_ms) / 16);
        if (step == 0)
            step = (obj->drag_vy > 0) ? 1 : -1;
        obj->anim_scroll_y = (int16_t)(obj->anim_scroll_y + step);
        obj->drag_vy = (int16_t)((obj->drag_vy * WE_SCROLL_PANEL_INERTIA_FRICTION_NUM) / WE_SCROLL_PANEL_INERTIA_FRICTION_DEN);
        if (obj->drag_vy == 0 && step != 0)
            obj->drag_vy = (step > 0) ? 1 : -1;
        if (obj->drag_vy == 1 || obj->drag_vy == -1)
        {
            if ((obj->anim_scroll_y >= 0 && obj->anim_scroll_y <= max_scroll) || elapsed_ms >= 16U)
                obj->drag_vy = 0;
        }
    }
#endif

#if WE_SCROLL_PANEL_USE_REBOUND
    if (obj->anim_scroll_y < 0)
    {
        overshoot = (int16_t)(-obj->anim_scroll_y);
        step = (int16_t)(overshoot / WE_SCROLL_PANEL_REBOUND_PULL_DIV);
        if (step < 1)
            step = 1;
        if (step > WE_SCROLL_PANEL_REBOUND_MAX_STEP)
            step = WE_SCROLL_PANEL_REBOUND_MAX_STEP;
        obj->anim_scroll_y = (obj->anim_scroll_y + step > 0) ? 0 : (int16_t)(obj->anim_scroll_y + step);
    }
    else if (obj->anim_scroll_y > max_scroll)
    {
        overshoot = (int16_t)(obj->anim_scroll_y - max_scroll);
        step = (int16_t)(overshoot / WE_SCROLL_PANEL_REBOUND_PULL_DIV);
        if (step < 1)
            step = 1;
        if (step > WE_SCROLL_PANEL_REBOUND_MAX_STEP)
            step = WE_SCROLL_PANEL_REBOUND_MAX_STEP;
        obj->anim_scroll_y = (obj->anim_scroll_y - step < max_scroll) ? max_scroll : (int16_t)(obj->anim_scroll_y - step);
    }
#else
    if (obj->anim_scroll_y < 0)
        obj->anim_scroll_y = 0;
    if (obj->anim_scroll_y > max_scroll)
        obj->anim_scroll_y = max_scroll;
#endif

    _scroll_panel_apply_scroll_raw(obj, obj->anim_scroll_y);

    if (obj->drag_vy == 0 && obj->anim_scroll_y >= 0 && obj->anim_scroll_y <= max_scroll)
        _scroll_panel_stop_anim(obj);
}

static void _scroll_panel_start_anim(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;

    /* 挂入中央动画链表（不占槽位、不会失败） */
    we_anim_start(obj->base.lcd, &obj->anim, _scroll_panel_anim_step_cb, obj);
    obj->animating = 1U;
}

/**
 * @brief 按位移整体平移全部子控件（滚动量变化时调用；滚动增大 = 内容上移）。
 * @param obj 目标控件对象指针。
 * @param dx X 方向位移（像素）。
 * @param dy Y 方向位移（像素）。
 * @return 无。
 * @note 子控件绝对坐标是唯一事实源：局部坐标 = 子绝对 - 容器绝对，
 *       按需推导，无槽位表、子控件数量无上限。
 */
static void _scroll_panel_shift_children(we_scroll_panel_obj_t *obj, int16_t dy)
{
    we_obj_t *child;

    if (dy == 0)
        return;
    for (child = obj->children_head; child != NULL; child = child->next)
        we_obj_set_pos(child, child->x, (int16_t)(child->y + dy));
}


static int16_t _scroll_panel_max_scroll_y(const we_scroll_panel_obj_t *obj)
{
    int16_t viewport_h;
    int16_t max_scroll;

    if (obj == NULL)
        return 0;

    _scroll_panel_update_inner_rect((we_scroll_panel_obj_t *)obj);
    viewport_h = obj->inner_h;
    max_scroll = (int16_t)(obj->content_h - viewport_h);
    return (max_scroll > 0) ? max_scroll : 0;
}

static uint8_t _scroll_panel_need_scrollbar(const we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return 0U;

    _scroll_panel_update_inner_rect((we_scroll_panel_obj_t *)obj);
    if (!obj->show_scrollbar || obj->scrollbar_w == 0U)
        return 0U;
    if (obj->inner_h <= 0)
        return 0U;
    if (obj->content_h <= obj->inner_h)
        return 0U;

    return 1U;
}

static uint8_t _scroll_panel_get_scrollbar_thumb_rect(const we_scroll_panel_obj_t *obj,
                                                      int16_t *x, int16_t *y,
                                                      int16_t *w, int16_t *h)
{
    int16_t track_x;
    int16_t track_y;
    int16_t track_h;
    int16_t thumb_h;
    int16_t thumb_y;
    int16_t max_scroll;

    if (!_scroll_panel_need_scrollbar(obj))
        return 0U;

    track_x = (int16_t)(obj->base.x + obj->base.w - obj->scrollbar_w - 2);
    track_y = (int16_t)(obj->base.y + 4);
    track_h = (int16_t)(obj->base.h - 8);
    if (track_h <= 4)
        return 0U;

    thumb_h = (int16_t)(((int32_t)track_h * obj->inner_h) / obj->content_h);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > track_h)
        thumb_h = track_h;

    max_scroll = _scroll_panel_max_scroll_y(obj);
    if (max_scroll > 0)
        thumb_y = (int16_t)(track_y + ((int32_t)(track_h - thumb_h) * obj->scroll_y) / max_scroll);
    else
        thumb_y = track_y;

    if (x != NULL) *x = track_x;
    if (y != NULL) *y = thumb_y;
    if (w != NULL) *w = obj->scrollbar_w;
    if (h != NULL) *h = thumb_h;
    return 1U;
}


static void _scroll_panel_draw_scrollbar(we_scroll_panel_obj_t *obj)
{
    we_lcd_t *lcd;
    int16_t thumb_x;
    int16_t thumb_y;
    int16_t thumb_w;
    int16_t thumb_h;

    if (obj == NULL)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    if (lcd == NULL || obj->scrollbar_opacity == 0U)
        return;

    if (!_scroll_panel_get_scrollbar_thumb_rect(obj, &thumb_x, &thumb_y, &thumb_w, &thumb_h))
        return;

    we_draw_round_rect_analytic_fill(lcd,
                                     thumb_x, thumb_y,
                                     (uint16_t)thumb_w, (uint16_t)thumb_h,
                                     (uint16_t)(thumb_w / 2U),
                                     obj->scrollbar_color, obj->scrollbar_opacity);
}

static void _scroll_panel_draw_core(we_scroll_panel_obj_t *obj, uint8_t draw_children, uint8_t draw_scrollbar)
{
    we_lcd_t *lcd;
    we_obj_t *child;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    uint8_t old_scale;
    int16_t new_x0;
    int16_t new_y0;
    int16_t new_x1;
    int16_t new_y1;

    if (obj == NULL || obj->opacity == 0U)
        return;

    lcd = (we_lcd_t *)obj->base.lcd;
    if (lcd == NULL)
        return;

    _scroll_panel_update_inner_rect(obj);

    we_fill_rect(lcd, obj->base.x, obj->base.y,
                 (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                 obj->border_color, obj->opacity);

    if (obj->inner_w > 0 && obj->inner_h > 0)
    {
        we_fill_rect(lcd, obj->inner_x, obj->inner_y,
                     (uint16_t)obj->inner_w, (uint16_t)obj->inner_h,
                     obj->bg_color, obj->opacity);
    }

    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;
    old_scale = lcd->opa_scale;

    /* 子控件透明度级联（嵌套自动链乘） */
    lcd->opa_scale = we_opa_apply(lcd, obj->opacity);

    new_x0 = WE_MAX(old_pfb_area.x0, obj->inner_x);
    new_y0 = WE_MAX(old_y_start, obj->inner_y);
    new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->inner_x + obj->inner_w - 1));
    new_y1 = WE_MIN(old_y_end, (int16_t)(obj->inner_y + obj->inner_h - 1));

    if (new_x0 <= new_x1 && new_y0 <= new_y1 && draw_children)
    {
        lcd->pfb_area.x0 = new_x0;
        lcd->pfb_area.x1 = new_x1;
        lcd->pfb_y_start = new_y0;
        lcd->pfb_y_end = new_y1;
        lcd->pfb_gram = old_gram + (new_y0 - old_y_start) * lcd->pfb_width + (new_x0 - old_pfb_area.x0);

        child = obj->children_head;
        while (child != NULL)
        {
            if (child->class_p && child->class_p->draw_cb &&
                (child->x + child->w > lcd->pfb_area.x0) && (child->x <= lcd->pfb_area.x1) &&
                (child->y + child->h > lcd->pfb_y_start) && (child->y <= lcd->pfb_y_end))
            {
                child->class_p->draw_cb(child);
            }
            child = child->next;
        }
    }

    lcd->opa_scale = old_scale;
    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;

    if (draw_scrollbar)
        _scroll_panel_draw_scrollbar(obj);
}


#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_NESTED == 1) && (WE_SCROLL_PANEL_USE_KEY == 1)
static uint8_t _scroll_panel_key_cb(void *ptr, uint8_t key_evt);
#endif
static uint8_t _scroll_panel_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_NESTED == 1) && (WE_SCROLL_PANEL_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _scroll_panel_key_cb(ptr, (uint8_t)event);
#endif

    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)ptr;
    int16_t dy_step;

    if (obj == NULL || data == NULL)
        return 0U;

    _scroll_panel_update_inner_rect(obj);

    /* 命中查询：内容区之外（面板边框/内边距）不参与命中，整棵子树跳过。 */
    if (event == WE_EVENT_HIT_TEST)
    {
        return (uint8_t)(data->x >= obj->inner_x && data->x < (obj->inner_x + obj->inner_w) &&
                         data->y >= obj->inner_y && data->y < (obj->inner_y + obj->inner_h));
    }

    /* 拖拽接管询问：子控件正被按压时，位移越阈值即由本面板接管滚动。
     * 内核会替被接管的子控件补一次 RELEASED 回弹，这里只需初始化拖拽状态。 */
    if (event == WE_EVENT_DRAG_BEGIN)
    {
        obj->dragging = 1U;
        obj->last_touch_y = data->y;
        obj->drag_vy = 0;
        return 1U;
    }

    if (event == WE_EVENT_PRESSED)
    {
        /* 走到这里说明没有子控件消费本次按压（空白区）：面板自己接手，
         * 既能直接拖动滚动，也保证后续 STAY/RELEASED 有归属。 */
        _scroll_panel_stop_anim(obj);
        obj->anim_scroll_y = obj->scroll_y;
        obj->drag_vy = 0;
        obj->dragging = 1U;
        obj->last_touch_y = data->y;
        return 1U;
    }

    if (event == WE_EVENT_STAY)
    {
        if (obj->dragging)
        {
            dy_step = (int16_t)(data->y - obj->last_touch_y);
            if (dy_step != 0)
            {
                obj->drag_vy = (int16_t)((-dy_step * WE_SCROLL_PANEL_VELOCITY_GAIN_NUM) / WE_SCROLL_PANEL_VELOCITY_GAIN_DEN);
                _scroll_panel_apply_scroll_raw(obj, (int16_t)(obj->scroll_y - dy_step));
                obj->last_touch_y = data->y;
            }
        }
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        int16_t max_scroll = _scroll_panel_max_scroll_y(obj);

        if (obj->dragging)
        {
#if WE_SCROLL_PANEL_USE_INERTIA
            if (obj->drag_vy != 0)
                _scroll_panel_start_anim(obj);
            else
#endif
#if WE_SCROLL_PANEL_USE_REBOUND
            if (obj->scroll_y < 0 || obj->scroll_y > max_scroll)
                _scroll_panel_start_anim(obj);
            else
#endif
            {
                int16_t old_sc = obj->scroll_y;

                obj->scroll_y = _scroll_panel_clamp_to_bounds(obj, obj->scroll_y);
                obj->anim_scroll_y = obj->scroll_y;
                _scroll_panel_stop_anim(obj);
                _scroll_panel_shift_children(obj, (int16_t)(old_sc - obj->scroll_y));
                we_obj_invalidate((we_obj_t *)obj);
            }
        }
        obj->dragging = 0U;
        return 1U;
    }

    if (event == WE_EVENT_SWIPE_UP)
    {
        _scroll_panel_stop_anim(obj);
        we_scroll_panel_set_scroll_y(obj, (int16_t)(obj->scroll_y + WE_CFG_SWIPE_THRESHOLD));
        obj->dragging = 0U;
        return 1U;
    }

    if (event == WE_EVENT_SWIPE_DOWN)
    {
        _scroll_panel_stop_anim(obj);
        we_scroll_panel_set_scroll_y(obj, (int16_t)(obj->scroll_y - WE_CFG_SWIPE_THRESHOLD));
        obj->dragging = 0U;
        return 1U;
    }

    return 0U;
}

static void _scroll_panel_draw_cb(void *ptr)
{
    _scroll_panel_draw_core((we_scroll_panel_obj_t *)ptr, 1U, 1U);
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_NESTED == 1) && (WE_SCROLL_PANEL_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：容器停靠查询 + 焦点滚动跟随。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 面板自身不消费任何普通键：OK 交焦点管理器下钻进子控件，
 *       方向/前后键交默认导航。子树内有对象获得焦点时收到
 *       WE_KEY_EVT_CHILD_FOCUS，把焦点对象连同光标环外扩一起硬滚进
 *       内容视口（复用触摸滚动的夹紧与 relayout 标脏路径）。
 */
static uint8_t _scroll_panel_key_cb(void *ptr, uint8_t key_evt)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)ptr;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
    {
        /* 仅当子树内存在可聚焦控件时才接受停靠（空面板不设死停靠点） */
        we_obj_t *child;

        if (obj->opacity == 0U)
            return 0U;
        for (child = obj->children_head; child != NULL; child = child->next)
        {
            if (we_focus_candidate(child))
                return 1U;
        }
        return 0U;
    }
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_EVT_CHILD_FOCUS:
    {
        we_obj_t *foc = we_focus_get(obj->base.lcd);
        int16_t margin = (int16_t)(WE_CFG_FOCUS_CURSOR_GAP + WE_CFG_FOCUS_CURSOR_THICKNESS);
        int16_t view_y0 = obj->inner_y;
        int16_t view_y1 = (int16_t)(obj->inner_y + obj->inner_h - 1);
        int16_t top;
        int16_t bottom;
        int16_t d;
        int16_t target;

        if (foc == NULL)
            return 1U;
        top = (int16_t)(foc->y - margin);
        bottom = (int16_t)(foc->y + foc->h - 1 + margin);

        d = 0;
        if (bottom > view_y1)
            d = (int16_t)(bottom - view_y1); /* 下方越界：内容上移 */
        if ((int16_t)(top - d) < view_y0)
            d = (int16_t)(top - view_y0); /* 上方越界/超高子控件：顶部对齐优先 */
        if (d != 0)
        {
            target = _scroll_panel_clamp_to_bounds(obj, (int16_t)(obj->scroll_y + d));
            if (target != obj->scroll_y)
            {
                _scroll_panel_stop_anim(obj); /* 打断惯性/回弹，硬定位 */
                _scroll_panel_apply_scroll_raw(obj, target);
                /* 光标环标脏由 we_obj_set_pos 的焦点钩子自动完成
                 * （relayout 逐子 set_pos 时命中焦点对象即标旧/新环）。 */
            }
        }
        return 1U;
    }
    default:
        return 0U; /* OK 交管理器下钻，方向/前后键交默认导航 */
    }
}
#endif

void we_scroll_panel_obj_init(we_scroll_panel_obj_t *obj, we_lcd_t *lcd,
                              int16_t x, int16_t y, int16_t w, int16_t h,
                              colour_t bg_color, colour_t border_color,
                              uint16_t radius, uint8_t opacity)
{
    static const we_class_t _scroll_panel_class = {
        .draw_cb = _scroll_panel_draw_cb,
        .event_cb = _scroll_panel_event_cb,
        .set_pos_cb = NULL,
        /* 结构位：前缀为 we_child_owner_t，删除/改挂走 children_head。
         * 行为位：焦点 OK 下钻进入子控件 / BACK 退回面板本体；
         * FOCUSABLE 承载空面板拒聚焦查询与 CHILD_FOCUS 滚动跟随。 */
        .class_flags = WE_CLASS_FLAG_CHILD_OWNER | WE_CLASS_FLAG_FOCUS_ENTER
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_NESTED == 1) && (WE_SCROLL_PANEL_USE_KEY == 1)
                       | WE_CLASS_FLAG_FOCUSABLE
#endif
        ,
    };

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_scroll_panel_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;
    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    obj->children_head = NULL;
    obj->scroll_y = 0;
    obj->content_h = h;
    obj->radius = radius;
    obj->opacity = opacity;
    obj->show_scrollbar = 0U;
    obj->scrollbar_w = WE_SCROLL_PANEL_SCROLLBAR_W;
    obj->scrollbar_opacity = 140U;
    obj->bg_color = bg_color;
    obj->border_color = border_color;
    obj->scrollbar_color = border_color;
    obj->last_touch_y = 0;
    obj->drag_vy = 0;
    obj->anim_scroll_y = 0;
    obj->dragging = 0U;
    obj->animating = 0U;
    _scroll_panel_update_inner_rect(obj);

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_obj_delete(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;

    /* 节点归控件所有，删除前必须摘链 */
    we_anim_stop(obj->base.lcd, &obj->anim);

    we_obj_delete((we_obj_t *)obj);
}

void we_scroll_panel_add_child(we_scroll_panel_obj_t *obj, we_obj_t *child)
{
    if (obj == NULL || child == NULL)
        return;
    if (child == (we_obj_t *)obj)
        return;
    if (child->lcd != obj->base.lcd)
        return;
    if (child->parent == (we_obj_t *)obj)
        return; /* 已挂载 */

    we_obj_set_parent(child, (we_obj_t *)obj);
    /* 内容局部 (0,0)：绝对 = 面板原点 - 当前滚动量 */
    we_obj_set_pos(child, obj->base.x, (int16_t)(obj->base.y - obj->scroll_y));
}

void we_scroll_panel_remove_child(we_scroll_panel_obj_t *obj, we_obj_t *child)
{
    if (obj == NULL || child == NULL || child->parent != (we_obj_t *)obj)
        return;

    we_obj_detach(child);
}

void we_scroll_panel_set_child_pos(we_scroll_panel_obj_t *obj, we_obj_t *child, int16_t local_x, int16_t local_y)
{
    if (obj == NULL || child == NULL || child->parent != (we_obj_t *)obj)
        return;

    we_obj_set_pos(child, (int16_t)(obj->base.x + local_x),
                   (int16_t)(obj->base.y + local_y - obj->scroll_y));
}

void we_scroll_panel_relayout(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return;

    /* 兼容：子控件绝对坐标即唯一事实源；保留整体标脏语义。 */
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_scroll_y(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t old;

    if (obj == NULL)
        return;

    scroll_y = _scroll_panel_clamp_to_bounds(obj, scroll_y);
    old = obj->scroll_y;
    if (old == scroll_y)
        return;

    obj->scroll_y = scroll_y;
    obj->anim_scroll_y = scroll_y;
    _scroll_panel_shift_children(obj, (int16_t)(old - scroll_y));
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_content_h(we_scroll_panel_obj_t *obj, int16_t content_h)
{
    if (obj == NULL)
        return;
    _scroll_panel_stop_anim(obj);
    obj->content_h = (content_h > obj->base.h) ? content_h : obj->base.h;
    if (obj->scroll_y > _scroll_panel_max_scroll_y(obj))
        obj->scroll_y = _scroll_panel_max_scroll_y(obj);
    obj->anim_scroll_y = obj->scroll_y;
    obj->drag_vy = 0;
    we_scroll_panel_relayout(obj);
}

void we_scroll_panel_set_radius(we_scroll_panel_obj_t *obj, uint16_t radius)
{
    if (obj == NULL)
        return;
    obj->radius = radius;
    _scroll_panel_update_inner_rect(obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_colors(we_scroll_panel_obj_t *obj,
                                colour_t bg_color,
                                colour_t border_color,
                                colour_t scrollbar_color)
{
    if (obj == NULL)
        return;
    obj->bg_color = bg_color;
    obj->border_color = border_color;
    obj->scrollbar_color = scrollbar_color;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_scrollbar(we_scroll_panel_obj_t *obj, uint8_t show_scrollbar, uint8_t scrollbar_w)
{
    if (obj == NULL)
        return;
    obj->show_scrollbar = show_scrollbar ? 1U : 0U;
    obj->scrollbar_w = (scrollbar_w > 0U) ? scrollbar_w : WE_SCROLL_PANEL_SCROLLBAR_W;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_scrollbar_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    obj->scrollbar_opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_draw_only(we_scroll_panel_obj_t *obj)
{
    _scroll_panel_draw_core(obj, 0U, 0U);
}

void we_scroll_panel_draw_scrollbar_overlay(we_scroll_panel_obj_t *obj)
{
    if (obj == NULL)
        return;
    _scroll_panel_update_inner_rect(obj);
    _scroll_panel_draw_scrollbar(obj);
}

void we_scroll_panel_get_inner_rect(const we_scroll_panel_obj_t *obj,
                                    int16_t *x, int16_t *y,
                                    int16_t *w, int16_t *h)
{
    if (obj == NULL)
        return;
    _scroll_panel_update_inner_rect((we_scroll_panel_obj_t *)obj);
    if (x != NULL) *x = obj->inner_x;
    if (y != NULL) *y = obj->inner_y;
    if (w != NULL) *w = obj->inner_w;
    if (h != NULL) *h = obj->inner_h;
}
