#include "we_widget_scroll_panel.h"
#include "wegui.h"

/* --------------------------------------------------------------------------
 * 内容区（视口）：边框以内 1px 的矩形。由 base 直接推导，不另存缓存
 * -------------------------------------------------------------------------- */

static int16_t _scroll_panel_inner_w(const we_scroll_panel_obj_t *obj)
{
    return (obj->base.w > 2) ? (int16_t)(obj->base.w - 2) : 0;
}

static int16_t _scroll_panel_inner_h(const we_scroll_panel_obj_t *obj)
{
    return (obj->base.h > 2) ? (int16_t)(obj->base.h - 2) : 0;
}

/**
 * @brief 最大可滚动量（内容高 - 视口高，不足时为 0）。
 * @param obj 目标控件对象指针。
 * @return 最大 scroll_y。
 */
static int16_t _scroll_panel_max_scroll_y(const we_scroll_panel_obj_t *obj)
{
    int16_t max_scroll = (int16_t)(obj->content_h - _scroll_panel_inner_h(obj));

    return (max_scroll > 0) ? max_scroll : 0;
}

/**
 * @brief 把滚动量硬夹紧到 [0, max]。
 * @param obj 目标控件对象指针。
 * @param scroll_y 输入滚动量。
 * @return 夹紧后的滚动量。
 */
static int16_t _scroll_panel_clamp_to_bounds(const we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t max_scroll = _scroll_panel_max_scroll_y(obj);

    if (scroll_y < 0)
        scroll_y = 0;
    if (scroll_y > max_scroll)
        scroll_y = max_scroll;
    return scroll_y;
}

/**
 * @brief 把滚动量软夹紧到越界上限（开启回弹时允许过冲，否则等同硬夹紧）。
 * @param scroll_y 输入滚动量。
 * @param max_scroll 最大滚动量。
 * @return 夹紧后的滚动量。
 */
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

/**
 * @brief 按位移整体平移全部子控件（滚动量变化时调用；滚动增大 = 内容上移）。
 * @param obj 目标控件对象指针。
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

/**
 * @brief 应用新的滚动量：软夹紧、平移子控件、标脏整个面板。
 * @param obj 目标控件对象指针。
 * @param scroll_y 目标滚动量。
 * @return 无。
 * @note 先标面板再平移子控件：子控件的新旧足迹都落在面板里，脏矩形
 *       管理器按“已被包含”直接丢弃，不再逐个裁剪合并。
 */
static void _scroll_panel_apply_scroll_raw(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    int16_t old;

    scroll_y = _scroll_panel_clamp_overscroll(scroll_y, _scroll_panel_max_scroll_y(obj));
    obj->anim_scroll_y = scroll_y;
    if (obj->scroll_y == scroll_y)
        return;

    old = obj->scroll_y;
    obj->scroll_y = scroll_y;
    we_obj_invalidate((we_obj_t *)obj);
    _scroll_panel_shift_children(obj, (int16_t)(old - scroll_y));
}

static void _scroll_panel_stop_anim(we_scroll_panel_obj_t *obj)
{
    obj->drag_vy = 0;
    we_anim_stop(obj->base.lcd, &obj->anim);
}

/**
 * @brief 惯性/回弹动画步进回调。
 * @param owner 控件对象指针。
 * @param elapsed_ms 本次调度经过的毫秒数。
 * @return 无。
 * @note 单步最多按 64ms 推进，主循环偶尔卡顿时不会一步跳出很远；
 *       惯性位移后立即按越界上限夹紧，冲到上限就开始回弹，不在上限处停顿。
 */
static void _scroll_panel_anim_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)owner;
    int16_t max_scroll;
    int16_t overshoot;
    int16_t step;

    if (elapsed_ms == 0U)
        return;
    if (elapsed_ms > 64U)
        elapsed_ms = 64U;

    max_scroll = _scroll_panel_max_scroll_y(obj);

#if WE_SCROLL_PANEL_USE_INERTIA
    if (obj->drag_vy != 0)
    {
        step = (int16_t)((obj->drag_vy * (int16_t)elapsed_ms) / 16);
        if (step == 0)
            step = (obj->drag_vy > 0) ? 1 : -1;
        obj->anim_scroll_y = _scroll_panel_clamp_overscroll((int16_t)(obj->anim_scroll_y + step), max_scroll);
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
    /* 挂入中央动画链表（不会失败） */
    we_anim_start(obj->base.lcd, &obj->anim, _scroll_panel_anim_step_cb, obj);
}

/**
 * @brief 计算滚动条滑块矩形。
 * @param obj 目标控件对象指针。
 * @param x/y/w/h 传出：滑块矩形。
 * @return 1 = 需要画滚动条；0 = 不画（未开启 / 内容未溢出 / 轨道太短）。
 * @note 滑块高按“视口高 / 内容高”比例（下限 8px），位置按滚动进度落轨
 *       （过冲期间按夹紧后的滚动量计算，不越出轨道）。
 */
static uint8_t _scroll_panel_thumb_rect(const we_scroll_panel_obj_t *obj,
                                        int16_t *x, int16_t *y, int16_t *w, int16_t *h)
{
    int16_t inner_h = _scroll_panel_inner_h(obj);
    int16_t track_y = (int16_t)(obj->base.y + 4);
    int16_t track_h = (int16_t)(obj->base.h - 8);
    int16_t max_scroll;
    int16_t scroll;
    int16_t thumb_h;

    if (!obj->show_scrollbar || obj->scrollbar_w == 0U || inner_h <= 0 ||
        obj->content_h <= inner_h || track_h <= 4)
        return 0U;

    thumb_h = (int16_t)(((int32_t)track_h * inner_h) / obj->content_h);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > track_h)
        thumb_h = track_h;

    max_scroll = _scroll_panel_max_scroll_y(obj);
    scroll = obj->scroll_y;
    if (scroll < 0)
        scroll = 0;
    if (scroll > max_scroll)
        scroll = max_scroll;

    *x = (int16_t)(obj->base.x + obj->base.w - obj->scrollbar_w - 2);
    *y = (int16_t)(track_y + ((int32_t)(track_h - thumb_h) * scroll) / max_scroll);
    *w = obj->scrollbar_w;
    *h = thumb_h;
    return 1U;
}

/**
 * @brief 控件绘制回调：1px 边框 + 内容区背景（子控件由内核按 content_cb 递归绘制）。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 边框只画环带、背景只画环内，两者不重叠：半透明时各像素只混合一次。
 *       radius > 0 时外壳为圆角（背景同心半径 radius-1）；子控件区仍是矩形，
 *       靠近四角的子控件需自行留出内边距。
 */
static void _scroll_panel_draw_cb(void *ptr)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    if (obj->opacity == 0U)
        return;

    we_draw_round_rect_ring(lcd, obj->base.x, obj->base.y,
                            (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                            obj->radius, 1U, obj->border_color, obj->opacity);
    if (obj->base.w > 2 && obj->base.h > 2)
        we_draw_round_rect_analytic_fill(lcd, (int16_t)(obj->base.x + 1), (int16_t)(obj->base.y + 1),
                                         (uint16_t)(obj->base.w - 2), (uint16_t)(obj->base.h - 2),
                                         (uint16_t)((obj->radius > 0U) ? (obj->radius - 1U) : 0U),
                                         obj->bg_color, obj->opacity);
}

/**
 * @brief 子控件区：内容区（边框以内），透明度随本面板级联。
 * @param ptr 回调透传对象指针。
 * @param area 传出：子控件可见区（屏幕绝对坐标，含端点）。
 * @return 子控件级联透明度（0 = 子树不画不命中）。
 */
static uint8_t _scroll_panel_content_cb(void *ptr, we_rect_t *area)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)ptr;

    area->x0 = (int16_t)(obj->base.x + 1);
    area->y0 = (int16_t)(obj->base.y + 1);
    area->x1 = (int16_t)(obj->base.x + _scroll_panel_inner_w(obj));
    area->y1 = (int16_t)(obj->base.y + _scroll_panel_inner_h(obj));
    return obj->opacity;
}

/**
 * @brief 子控件画完后补画滚动条（压在内容之上，随面板透明度一起淡出）。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _scroll_panel_draw_post_cb(void *ptr)
{
    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)ptr;
    int16_t tx, ty, tw, th;

    if (obj->opacity == 0U || obj->scrollbar_opacity == 0U)
        return;
    if (!_scroll_panel_thumb_rect(obj, &tx, &ty, &tw, &th))
        return;

    we_draw_round_rect_analytic_fill(obj->base.lcd, tx, ty, (uint16_t)tw, (uint16_t)th,
                                     (uint16_t)(tw / 2), obj->scrollbar_color,
                                     we_opa_scale(obj->scrollbar_opacity, obj->opacity));
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_NESTED == 1) && (WE_SCROLL_PANEL_USE_KEY == 1)
static uint8_t _scroll_panel_key_cb(void *ptr, uint8_t key_evt);
#endif

/**
 * @brief 事件回调：命中查询 / 拖拽接管 / 空白处拖动 / 惯性与回弹 / 轻扫。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 已消费，0 未处理。
 */
static uint8_t _scroll_panel_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_NESTED == 1) && (WE_SCROLL_PANEL_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _scroll_panel_key_cb(ptr, (uint8_t)event);
#endif

    we_scroll_panel_obj_t *obj = (we_scroll_panel_obj_t *)ptr;
    int16_t dy_step;

    if (data == NULL)
        return 0U;

    /* 命中查询：内容区之外（面板边框）不参与命中，整棵子树跳过。 */
    if (event == WE_EVENT_HIT_TEST)
    {
        return (uint8_t)(data->x > obj->base.x && data->x <= obj->base.x + _scroll_panel_inner_w(obj) &&
                         data->y > obj->base.y && data->y <= obj->base.y + _scroll_panel_inner_h(obj));
    }

    /* 拖拽接管询问：子控件正被按压时，位移越阈值即由本面板接管滚动。
     * 内核会替被接管的子控件补一次 RELEASED 回弹；进行中的惯性/回弹先停下，
     * 不与手指跟随叠加。 */
    if (event == WE_EVENT_DRAG_BEGIN)
    {
        _scroll_panel_stop_anim(obj);
        obj->anim_scroll_y = obj->scroll_y;
        obj->dragging = 1U;
        obj->last_touch_y = data->y;
        return 1U;
    }

    if (event == WE_EVENT_PRESSED)
    {
        /* 走到这里说明没有子控件消费本次按压（空白区）：面板自己接手。
         * 位移越过 WE_SCROLL_PANEL_DRAG_THRESHOLD 才开始拖动（轻点的手指抖动不滚动）。 */
        _scroll_panel_stop_anim(obj);
        obj->anim_scroll_y = obj->scroll_y;
        obj->dragging = 0U;
        obj->last_touch_y = data->y;
        return 1U;
    }

    if (event == WE_EVENT_STAY)
    {
        dy_step = (int16_t)(data->y - obj->last_touch_y);
        if (!obj->dragging && WE_ABS(dy_step) >= WE_SCROLL_PANEL_DRAG_THRESHOLD)
            obj->dragging = 1U; /* 越过阈值：从按下点起整段跟手 */
        if (obj->dragging && dy_step != 0)
        {
            obj->drag_vy = (int16_t)((-dy_step * WE_SCROLL_PANEL_VELOCITY_GAIN_NUM) / WE_SCROLL_PANEL_VELOCITY_GAIN_DEN);
            _scroll_panel_apply_scroll_raw(obj, (int16_t)(obj->scroll_y - dy_step));
            obj->last_touch_y = data->y;
        }
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        if (obj->dragging)
        {
            int16_t max_scroll = _scroll_panel_max_scroll_y(obj);

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
                _scroll_panel_stop_anim(obj);
                _scroll_panel_apply_scroll_raw(obj, _scroll_panel_clamp_to_bounds(obj, obj->scroll_y));
            }
            (void)max_scroll;
        }
        obj->dragging = 0U;
        return 1U;
    }

    if (event == WE_EVENT_SWIPE_UP || event == WE_EVENT_SWIPE_DOWN)
    {
        _scroll_panel_stop_anim(obj);
        we_scroll_panel_set_scroll_y(obj, (int16_t)(obj->scroll_y + ((event == WE_EVENT_SWIPE_UP)
                                                                         ? WE_CFG_SWIPE_THRESHOLD
                                                                         : -WE_CFG_SWIPE_THRESHOLD)));
        obj->dragging = 0U;
        return 1U;
    }

    return 0U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_NESTED == 1) && (WE_SCROLL_PANEL_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：容器停靠查询 + 焦点滚动跟随。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（WE_KEY_xx / WE_KEY_EVT_xx）。
 * @return 非 0 表示已消费。
 * @note 面板自身不消费任何普通键：OK 交焦点管理器下钻进子控件，
 *       方向/前后键交默认导航。子树内有对象获得焦点时收到
 *       WE_KEY_EVT_CHILD_FOCUS，把焦点对象连同光标环外扩一起硬滚进
 *       内容视口（复用触摸滚动的夹紧与平移路径）。
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
        int16_t view_y0 = (int16_t)(obj->base.y + 1);
        int16_t view_y1 = (int16_t)(obj->base.y + _scroll_panel_inner_h(obj));
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
                 * （逐子 set_pos 时命中焦点对象即标旧/新环）。 */
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
        .content_cb = _scroll_panel_content_cb,
        .draw_post_cb = _scroll_panel_draw_post_cb,
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
    obj->content_h = _scroll_panel_inner_h(obj); /* 未设内容高：恰好一屏，不可滚动 */
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

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
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

    /* 先按旧父容器标脏旧位置：改挂之后再标会被裁到面板范围内，旧处留残影 */
    we_obj_invalidate(child);
    we_obj_set_parent(child, (we_obj_t *)obj);
    /* 内容局部 (0,0)：绝对 = 面板原点 - 当前滚动量 */
    we_obj_set_pos(child, obj->base.x, (int16_t)(obj->base.y - obj->scroll_y));
}

void we_scroll_panel_remove_child(we_scroll_panel_obj_t *obj, we_obj_t *child)
{
    if (obj == NULL || child == NULL || child->parent != (we_obj_t *)obj)
        return;

    we_obj_invalidate(child);
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

    /* 子控件绝对坐标即唯一事实源，这里只整体标脏 */
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_scroll_y(we_scroll_panel_obj_t *obj, int16_t scroll_y)
{
    if (obj == NULL)
        return;
    _scroll_panel_apply_scroll_raw(obj, _scroll_panel_clamp_to_bounds(obj, scroll_y));
}

void we_scroll_panel_set_content_h(we_scroll_panel_obj_t *obj, int16_t content_h)
{
    int16_t inner_h;

    if (obj == NULL)
        return;
    inner_h = _scroll_panel_inner_h(obj);
    _scroll_panel_stop_anim(obj);
    obj->content_h = (content_h > inner_h) ? content_h : inner_h;
    /* 滚动量随新范围硬夹紧（过冲态一并归位），子控件同步平移 */
    _scroll_panel_apply_scroll_raw(obj, _scroll_panel_clamp_to_bounds(obj, obj->scroll_y));
    we_obj_invalidate((we_obj_t *)obj); /* 滚动条长短随内容高变化 */
}

void we_scroll_panel_set_radius(we_scroll_panel_obj_t *obj, uint16_t radius)
{
    if (obj == NULL || obj->radius == radius)
        return;
    obj->radius = radius;
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
    if (obj == NULL || obj->scrollbar_opacity == opacity)
        return;
    obj->scrollbar_opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_set_opacity(we_scroll_panel_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_scroll_panel_get_inner_rect(const we_scroll_panel_obj_t *obj,
                                    int16_t *x, int16_t *y,
                                    int16_t *w, int16_t *h)
{
    if (obj == NULL)
        return;
    if (x != NULL) *x = (int16_t)(obj->base.x + 1);
    if (y != NULL) *y = (int16_t)(obj->base.y + 1);
    if (w != NULL) *w = _scroll_panel_inner_w(obj);
    if (h != NULL) *h = _scroll_panel_inner_h(obj);
}
