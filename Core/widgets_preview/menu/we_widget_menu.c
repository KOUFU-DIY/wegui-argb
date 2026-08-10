#include "we_widget_menu.h"
#include "we_scroll.h"

/* 滚动物理参数（沿用本控件既有宏值；快扫用自有 kick 模型，slice 占位不用） */
static const we_scroll_cfg_t _menu_scroll_cfg = {
    WE_MENU_DRAG_THRESHOLD,   WE_MENU_OVERSCROLL_LIMIT, WE_MENU_INERTIA_NUM,
    WE_MENU_INERTIA_DEN,      WE_MENU_REBOUND_PULL_DIV, WE_MENU_REBOUND_MAX_STEP,
    128,
};
#include "we_render.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * menu —— 多级菜单（preview 孵化区）
 *
 * 结构：标题栏（title 居中 + 非根页返回箭头）+ PFB 收窄裁剪的行区
 * （按压高亮条 / 左对齐文字 / 子页 ">" 提示 / 行底 1px 分隔线）+
 * 内容溢出时右缘常显滚动条。
 *
 * 导航：固定深度页面栈（每帧存页指针 + 滚动位置）。点击 submenu 行
 * 入栈，返回箭头 / 行区右滑出栈。页面切换经中央动画节点做 200ms
 * 水平滑入过渡（旧页 + 新页整体 X 偏移，we_ease_out_quad + we_lerp）。
 *
 * 滚动：像素级硬夹紧（list 同款），拖拽跟手 + 松手简化惯性
 * （速度每步衰减 7/8，独立动画节点推进直到归零）。
 * -------------------------------------------------------------------------- */

/**
 * @brief 将透明度按控件整体不透明度缩放。
 * @param a 传入：原始透明度（0~255）。
 * @param opacity 传入：控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _menu_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/**
 * @brief 比较两个颜色是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _menu_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 取当前页（栈顶帧的页指针）。
 * @param obj 传入：控件对象指针。
 * @return 当前页指针（可能为 NULL）。
 */
static const we_menu_page_t *_menu_cur_page(const we_menu_obj_t *obj)
{
    return obj->stack[obj->depth - 1U].page;
}

/**
 * @brief 取当前页滚动位置（栈顶帧的 scroll_px）。
 * @param obj 传入：控件对象指针。
 * @return 当前页滚动像素。
 */
static int32_t _menu_cur_scroll(const we_menu_obj_t *obj)
{
    return obj->stack[obj->depth - 1U].scroll_px;
}

/**
 * @brief 行区顶部屏幕 Y 坐标（标题栏之下）。
 * @param obj 传入：控件对象指针。
 * @return 行区顶部 Y。
 */
static int16_t _menu_rows_y(const we_menu_obj_t *obj)
{
    return (int16_t)(obj->base.y + (int16_t)obj->title_h);
}

/**
 * @brief 行区可视高度（控件高 - 标题栏高，下限 0）。
 * @param obj 传入：控件对象指针。
 * @return 行区高（像素）。
 */
static int16_t _menu_rows_h(const we_menu_obj_t *obj)
{
    int16_t h = (int16_t)(obj->base.h - (int16_t)obj->title_h);
    return (h > 0) ? h : 0;
}

/**
 * @brief 指定页的内容总高度（行数 × 行高）。
 * @param obj 传入：控件对象指针。
 * @param page 传入：目标页指针。
 * @return 内容总高（像素）。
 */
static int32_t _menu_content_h(const we_menu_obj_t *obj, const we_menu_page_t *page)
{
    if (page == NULL)
        return 0;
    return (int32_t)page->count * (int32_t)obj->row_h;
}

/**
 * @brief 当前页最大可滚动像素（内容不溢出行区时为 0）。
 * @param obj 传入：控件对象指针。
 * @return 最大 scroll_px（>= 0）。
 */
static int32_t _menu_max_scroll(const we_menu_obj_t *obj)
{
    int32_t m = _menu_content_h(obj, _menu_cur_page(obj)) - (int32_t)_menu_rows_h(obj);
    return (m > 0) ? m : 0;
}

/**
 * @brief 将当前页 scroll_px 硬夹紧到 [0, max] 后写回栈顶帧并按需标脏。
 * @param obj 传入：控件对象指针。
 * @param new_scroll 传入：目标滚动像素。
 * @return 无。
 */
static void _menu_scroll_commit(we_menu_obj_t *obj)
{
    if (obj->stack[obj->depth - 1U].scroll_px == obj->sc.pos)
        return;
    obj->stack[obj->depth - 1U].scroll_px = obj->sc.pos;
    we_obj_invalidate((we_obj_t *)obj); /* preview：整控件包围盒标脏 */
}

static void _menu_apply_scroll(we_menu_obj_t *obj, int32_t new_scroll)
{
    obj->sc.pos = _menu_cur_scroll(obj);
    if (we_scroll_set(&obj->sc, new_scroll, _menu_max_scroll(obj)))
        _menu_scroll_commit(obj);
}


/**
 * @brief 停止惯性动画并清零速度（摘链停表）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _menu_stop_inertia(we_menu_obj_t *obj)
{
    obj->sc.animating = 0U;
    obj->sc.vel = 0;
    we_anim_stop(obj->base.lcd, &obj->anim);
}

/**
 * @brief 惯性/回弹动画步进回调：速度积分 + 7/8 衰减 + 越界橡皮筋回弹。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次调度经过的毫秒数。
 * @return 无。
 * @note 越界段速度额外减半，让过冲快速交棒给回弹；回弹每步拉回
 *       "过冲/WE_MENU_REBOUND_PULL_DIV"（1..MAX_STEP px），整数缓动
 *       天然先快后慢。速度归零且回到边界内时自行摘链（list 同款）。
 */
static void _menu_inertia_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_menu_obj_t *obj = (we_menu_obj_t *)owner;

    if (obj == NULL || elapsed_ms == 0U)
        return;
    if (!obj->sc.animating)
    {
        _menu_stop_inertia(obj);
        return;
    }

    obj->sc.pos = _menu_cur_scroll(obj); /* 每步以页栈为准载入（页切换安全） */
    if (we_scroll_anim_step(&obj->sc, &_menu_scroll_cfg, elapsed_ms, _menu_max_scroll(obj)))
        _menu_scroll_commit(obj);

    if (!obj->sc.animating)
        _menu_stop_inertia(obj);
}

/* --------------------------------------------------------------------------
 * 页面切换水平滑入过渡（中央动画引擎）
 * -------------------------------------------------------------------------- */

/**
 * @brief 结束过渡：清标志、丢弃旧页引用、摘链并整体标脏。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _menu_stop_transition(we_menu_obj_t *obj)
{
    obj->transitioning = 0U;
    obj->trans_prev_page = NULL;
    we_anim_stop(obj->base.lcd, &obj->trans_anim);
}

/**
 * @brief 过渡动画步进回调：累计时长并整体标脏，到时自行摘链。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次调度经过的毫秒数。
 * @return 无。
 */
static void _menu_trans_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_menu_obj_t *obj = (we_menu_obj_t *)owner;

    if (obj == NULL || elapsed_ms == 0U)
        return;
    if (!obj->transitioning)
    {
        _menu_stop_transition(obj);
        return;
    }

    if ((uint32_t)obj->trans_t + (uint32_t)elapsed_ms >= (uint32_t)WE_MENU_TRANS_MS)
    {
        _menu_stop_transition(obj); /* 就位即摘链 */
        we_obj_invalidate((we_obj_t *)obj);
        return;
    }

    obj->trans_t = (uint16_t)(obj->trans_t + elapsed_ms);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 启动一次页面切换过渡（时长为 0 或几何退化时直切）。
 * @param obj 传入：控件对象指针。
 * @param prev_page 传入：滑出的旧页指针。
 * @param prev_scroll 传入：旧页滑出时的滚动位置。
 * @param forward 传入：1 = 进入子页（新页从右滑入），0 = 返回（从左滑入）。
 * @return 无。
 * @note 过渡中再次导航会以当前页为新起点重启过渡（旧过渡直接落位）。
 */
static void _menu_start_transition(we_menu_obj_t *obj, const we_menu_page_t *prev_page,
                                   int32_t prev_scroll, uint8_t forward)
{
    if (WE_MENU_TRANS_MS == 0U || obj->base.w <= 0)
    {
        _menu_stop_transition(obj); /* 直切 */
        return;
    }

    obj->transitioning = 1U;
    obj->trans_t = 0U;
    obj->trans_from = forward ? obj->base.w : (int16_t)(-obj->base.w);
    obj->trans_prev_page = prev_page;
    obj->trans_prev_scroll = prev_scroll;
    we_anim_start(obj->base.lcd, &obj->trans_anim, _menu_trans_step_cb, obj);
}

/**
 * @brief 计算过渡当前进度对应的新页 X 偏移。
 * @param obj 传入：控件对象指针。
 * @return 新页行内容整体 X 偏移（像素，过渡结束后为 0）。
 */
static int16_t _menu_trans_ofs(const we_menu_obj_t *obj)
{
    uint32_t p;
    uint16_t eased;

    if (!obj->transitioning)
        return 0;

    p = ((uint32_t)obj->trans_t * 256U) / ((WE_MENU_TRANS_MS > 0U) ? (uint32_t)WE_MENU_TRANS_MS : 1U);
    if (p > 256U)
        p = 256U;
    eased = we_ease_out_quad((uint16_t)p);
    return (int16_t)we_lerp((int32_t)obj->trans_from, 0, eased);
}

/* --------------------------------------------------------------------------
 * 命中测试
 * -------------------------------------------------------------------------- */

/**
 * @brief 行区命中测试：返回触摸点所在的当前页行索引。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触摸 X（屏幕绝对坐标）。
 * @param py 传入：触摸 Y。
 * @return 行索引；未命中任何行（越界/标题栏/内容之外）返回 -1。
 */
static int16_t _menu_hit_row(const we_menu_obj_t *obj, int16_t px, int16_t py)
{
    const we_menu_page_t *page = _menu_cur_page(obj);
    int16_t rows_y = _menu_rows_y(obj);
    int16_t rows_h = _menu_rows_h(obj);
    int32_t content_y;
    int32_t row;

    if (page == NULL || page->count == 0U || obj->row_h == 0U || rows_h <= 0)
        return -1;
    if (px < obj->base.x || px >= (int16_t)(obj->base.x + obj->base.w) ||
        py < rows_y || py >= (int16_t)(rows_y + rows_h))
        return -1;

    content_y = (int32_t)(py - rows_y) + _menu_cur_scroll(obj);
    row = content_y / (int32_t)obj->row_h;
    if (row < 0 || row >= (int32_t)page->count)
        return -1;
    return (int16_t)row;
}

/**
 * @brief 返回箭头热区命中测试（非根页才有效）。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触摸 X（屏幕绝对坐标）。
 * @param py 传入：触摸 Y。
 * @return 1 命中返回热区，0 未命中。
 */
static uint8_t _menu_hit_back(const we_menu_obj_t *obj, int16_t px, int16_t py)
{
    if (obj->depth <= 1U)
        return 0U;
    if (px < obj->base.x || px >= (int16_t)(obj->base.x + WE_MENU_BACK_ZONE_W) ||
        py < obj->base.y || py >= (int16_t)(obj->base.y + (int16_t)obj->title_h))
        return 0U;
    return 1U;
}

/**
 * @brief 触摸点是否落在行区内（标题栏之下、控件矩形之内）。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触摸 X（屏幕绝对坐标）。
 * @param py 传入：触摸 Y。
 * @return 1 在行区内，0 不在。
 */
static uint8_t _menu_hit_rows_area(const we_menu_obj_t *obj, int16_t px, int16_t py)
{
    int16_t rows_y = _menu_rows_y(obj);

    if (px < obj->base.x || px >= (int16_t)(obj->base.x + obj->base.w) ||
        py < rows_y || py >= (int16_t)(rows_y + _menu_rows_h(obj)))
        return 0U;
    return 1U;
}

/* --------------------------------------------------------------------------
 * 导航（入栈 / 出栈）
 * -------------------------------------------------------------------------- */

/**
 * @brief 进入子页：当前页入栈保持滚动，子页滚动从 0 开始。
 * @param obj 传入：控件对象指针。
 * @param page 传入：目标子页指针。
 * @return 无。
 * @note 栈满（WE_MENU_STACK_MAX）时忽略本次导航。
 */
static void _menu_push(we_menu_obj_t *obj, const we_menu_page_t *page)
{
    const we_menu_page_t *prev;
    int32_t prev_scroll;

    if (page == NULL || obj->depth >= WE_MENU_STACK_MAX)
        return;

    _menu_stop_inertia(obj);
    _menu_apply_scroll(obj, _menu_cur_scroll(obj)); /* 入栈前把过冲吸回边界 */
    prev = _menu_cur_page(obj);
    prev_scroll = _menu_cur_scroll(obj);

    obj->stack[obj->depth].page = page;
    obj->stack[obj->depth].scroll_px = 0;
    obj->depth++;

    _menu_start_transition(obj, prev, prev_scroll, 1U);
    we_obj_invalidate((we_obj_t *)obj);
}

/* --------------------------------------------------------------------------
 * 绘制
 * -------------------------------------------------------------------------- */

/**
 * @brief 绘制 ">" 子页提示箭头（两段圆头短线拼折角，尖端朝右）。
 * @param obj 传入：控件对象指针。
 * @param cx 传入：折角中心 X（屏幕绝对坐标）。
 * @param cy 传入：折角中心 Y。
 * @return 无。
 */
static void _menu_draw_chevron_right(we_menu_obj_t *obj, int16_t cx, int16_t cy)
{
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t opa = _menu_scale_opa(255U, obj->opacity);

    we_draw_line_round(lcd, (int16_t)(cx - 2), (int16_t)(cy - 4),
                       (int16_t)(cx + 2), cy, 2U, obj->arrow_color, opa);
    we_draw_line_round(lcd, (int16_t)(cx + 2), cy,
                       (int16_t)(cx - 2), (int16_t)(cy + 4), 2U, obj->arrow_color, opa);
}

/**
 * @brief 绘制 "<" 返回箭头（两段圆头短线拼折角，尖端朝左）。
 * @param obj 传入：控件对象指针。
 * @param cx 传入：折角中心 X（屏幕绝对坐标）。
 * @param cy 传入：折角中心 Y。
 * @return 无。
 */
static void _menu_draw_back_arrow(we_menu_obj_t *obj, int16_t cx, int16_t cy)
{
    we_lcd_t *lcd = obj->base.lcd;
    uint8_t opa = _menu_scale_opa(255U, obj->opacity);

    we_draw_line_round(lcd, (int16_t)(cx + 3), (int16_t)(cy - 5),
                       (int16_t)(cx - 2), cy, 2U, obj->arrow_color, opa);
    we_draw_line_round(lcd, (int16_t)(cx - 2), cy,
                       (int16_t)(cx + 3), (int16_t)(cy + 5), 2U, obj->arrow_color, opa);
}

/**
 * @brief 绘制一页的全部可见行（高亮条 / 文字 / ">" 提示 / 分隔线）。
 * @param obj 传入：控件对象指针。
 * @param page 传入：要绘制的页（当前页或过渡中的旧页）。
 * @param scroll 传入：该页滚动位置（像素）。
 * @param x_ofs 传入：行内容整体 X 偏移（过渡滑入用，0 = 就位）。
 * @param show_press 传入：是否绘制按压行高亮（仅当前页且非过渡时）。
 * @return 无。
 * @note 调用前 PFB 窗口必须已收窄到行区，本函数不再做逐行裁剪。
 */
static void _menu_draw_page_rows(we_menu_obj_t *obj, const we_menu_page_t *page,
                                 int32_t scroll, int16_t x_ofs, uint8_t show_press)
{
    we_lcd_t *lcd = obj->base.lcd;
    int16_t rows_y = _menu_rows_y(obj);
    int16_t rows_y1 = (int16_t)(rows_y + _menu_rows_h(obj) - 1);
    int16_t base_x = (int16_t)(obj->base.x + x_ofs);
    int32_t first;
    int32_t top_ofs;
    int16_t iy;
    int32_t idx;

    if (page == NULL || page->items == NULL || page->count == 0U || obj->row_h == 0U)
        return;

    first = scroll / (int32_t)obj->row_h;   /* 首个（可能半露）行 */
    top_ofs = scroll % (int32_t)obj->row_h; /* 首行被裁掉的顶部像素 */
    iy = (int16_t)(rows_y - (int16_t)top_ofs);

    for (idx = first; idx < (int32_t)page->count; idx++)
    {
        const we_menu_item_t *item = &page->items[idx];
        int16_t ty;
        int8_t y_top;
        int8_t y_bot;

        if (iy > rows_y1)
            break; /* 已画到行区底部之外 */

        /* 按压行高亮：内缩小圆角条（不精确贴合面板大圆角，preview 可接受） */
        if (show_press && (int16_t)idx == obj->pressed_row)
        {
            we_draw_round_rect_analytic_fill(lcd,
                                             (int16_t)(base_x + 2),
                                             (int16_t)(iy + 1),
                                             (uint16_t)(obj->base.w - 4),
                                             (uint16_t)(obj->row_h - 2U),
                                             6U, obj->press_color, obj->opacity);
        }

        /* 行文字：左对齐 + 行内垂直居中 */
        if (item->label != NULL)
        {
            we_get_text_bbox(obj->font, item->label, &y_top, &y_bot);
            ty = (int16_t)(iy + (int16_t)obj->row_h / 2 - (y_top + y_bot) / 2);
            we_draw_string(lcd, (int16_t)(base_x + WE_MENU_TEXT_PAD), ty,
                           obj->font, item->label, obj->text_color,
                           _menu_scale_opa(255U, obj->opacity));
        }

        /* 带子页的行：右侧 ">" 提示（避开滚动条保留带） */
        if (item->submenu != NULL)
        {
            _menu_draw_chevron_right(obj, (int16_t)(base_x + obj->base.w - 14),
                                     (int16_t)(iy + (int16_t)obj->row_h / 2));
        }

        /* 行底 1px 分隔线（低透明度，最后一行不画） */
        if (idx < (int32_t)(page->count - 1U))
        {
            we_fill_rect(lcd,
                         (int16_t)(base_x + WE_MENU_TEXT_PAD),
                         (int16_t)(iy + (int16_t)obj->row_h - 1),
                         (uint16_t)(obj->base.w - 2 * WE_MENU_TEXT_PAD), 1U,
                         obj->sep_color,
                         _menu_scale_opa(WE_MENU_SEP_OPA, obj->opacity));
        }

        iy = (int16_t)(iy + (int16_t)obj->row_h);
    }
}

/**
 * @brief 绘制行区右缘常显滚动条（无轨道，仅胶囊滑块，内容溢出才显示）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 滑块高按"行区高/内容高"比例（下限 8px），位置按滚动进度插值；
 *       轨道下端避开面板圆角，过渡期间由调用方跳过本函数。
 */
static void _menu_draw_scrollbar(we_menu_obj_t *obj)
{
    we_lcd_t *lcd = obj->base.lcd;
    int32_t content_h = _menu_content_h(obj, _menu_cur_page(obj));
    int32_t max_scroll = _menu_max_scroll(obj);
    int16_t rows_h = _menu_rows_h(obj);
    int16_t track_x;
    int16_t track_y0;
    int16_t track_h;
    int16_t thumb_h;
    int16_t thumb_y;

    if (max_scroll == 0 || content_h <= 0 || rows_h <= 0)
        return; /* 内容未溢出，无需滚动条 */

    track_x = (int16_t)(obj->base.x + obj->base.w - WE_MENU_SB_MARGIN - WE_MENU_SB_WIDTH);
    track_y0 = (int16_t)(_menu_rows_y(obj) + 2);
    track_h = (int16_t)(rows_h - 4 - (int16_t)obj->radius);
    if (track_h < (int16_t)obj->row_h)
        track_h = rows_h; /* 圆角过大时退化为整高轨道 */

    thumb_h = (int16_t)(((int32_t)track_h * (int32_t)rows_h) / content_h);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > track_h)
        thumb_h = track_h;

    {
        /* 过冲期间按夹紧后的滚动值计算，滑块不越出轨道（list 同款） */
        int32_t sb_scroll = _menu_cur_scroll(obj);

        if (sb_scroll < 0)
            sb_scroll = 0;
        if (sb_scroll > max_scroll)
            sb_scroll = max_scroll;
        thumb_y = (int16_t)(track_y0 +
                  (int32_t)(track_h - thumb_h) * sb_scroll / max_scroll);
    }

    we_draw_round_rect_analytic_fill(lcd, track_x, thumb_y,
                                     (uint16_t)WE_MENU_SB_WIDTH, (uint16_t)thumb_h,
                                     (uint16_t)(WE_MENU_SB_WIDTH / 2),
                                     obj->sb_color,
                                     _menu_scale_opa(WE_MENU_SB_OPA, obj->opacity));
}

/**
 * @brief 绘制标题栏（背景 + 返回箭头 + 居中标题文字）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _menu_draw_title_bar(we_menu_obj_t *obj)
{
    we_lcd_t *lcd = obj->base.lcd;
    const we_menu_page_t *page = _menu_cur_page(obj);
    int16_t cy = (int16_t)(obj->base.y + (int16_t)obj->title_h / 2);

    /* 标题栏底色：整块圆角矩形 + 底部补方角（顶角随面板圆角，底边取直）。
     * 补块在半透明主题下会与圆角带二次混色，preview 可接受。 */
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, obj->title_h,
                                     obj->radius, obj->title_bg_color, obj->opacity);
    if (obj->radius > 0U && obj->title_h > obj->radius)
    {
        we_fill_rect(lcd, obj->base.x,
                     (int16_t)(obj->base.y + (int16_t)(obj->title_h - obj->radius)),
                     (uint16_t)obj->base.w, obj->radius,
                     obj->title_bg_color, obj->opacity);
    }

    /* 非根页：左侧返回箭头（按压时衬高亮小圆角条） */
    if (obj->depth > 1U)
    {
        if (obj->pressed_back)
        {
            we_draw_round_rect_analytic_fill(lcd,
                                             (int16_t)(obj->base.x + 6),
                                             (int16_t)(obj->base.y + 3),
                                             26U, (uint16_t)(obj->title_h - 6U),
                                             7U, obj->press_color, obj->opacity);
        }
        _menu_draw_back_arrow(obj, (int16_t)(obj->base.x + 18), cy);
    }

    /* 页标题：水平居中 + 垂直居中 */
    if (page != NULL && page->title != NULL)
    {
        uint16_t tw = we_get_text_width(obj->font, page->title);
        int16_t tx = (int16_t)(obj->base.x + ((int16_t)obj->base.w - (int16_t)tw) / 2);
        int16_t ty;
        int8_t y_top;
        int8_t y_bot;

        we_get_text_bbox(obj->font, page->title, &y_top, &y_bot);
        ty = (int16_t)(cy - (y_top + y_bot) / 2);
        we_draw_string(lcd, tx, ty, obj->font, page->title,
                       obj->title_text_color, _menu_scale_opa(255U, obj->opacity));
    }
}

/**
 * @brief 绘制回调：面板背景 + 标题栏 + PFB 收窄裁剪的行区 + 滚动条。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 * @note 过渡期间行区先画滑出的旧页再画滑入的新页（偏移相差一个控件宽），
 *       滚动条在过渡期间隐藏（避免比例/位置跳变）。
 */
static void _menu_draw_cb(void *ptr)
{
    we_menu_obj_t *obj = (we_menu_obj_t *)ptr;
    we_lcd_t *lcd;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    int16_t rows_y;
    int16_t clip_x0;
    int16_t clip_y0;
    int16_t clip_x1;
    int16_t clip_y1;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    /* 1. 面板背景（radius 为 0 时解析填充自动退化为直角矩形） */
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     obj->radius, obj->bg_color, obj->opacity);

    /* 2. 标题栏（当前页 title + 非根页返回箭头） */
    _menu_draw_title_bar(obj);

    if (_menu_rows_h(obj) <= 0 || obj->font == NULL)
        return;

    /* 3. PFB 窗口收窄：把行内容裁剪在行区矩形内（save/restore 套路） */
    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    rows_y = _menu_rows_y(obj);
    clip_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
    clip_y0 = WE_MAX((int16_t)old_y_start, rows_y);
    clip_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
    clip_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

    if (clip_x0 <= clip_x1 && clip_y0 <= clip_y1)
    {
        int16_t ofs = _menu_trans_ofs(obj);

        lcd->pfb_area.x0 = clip_x0;
        lcd->pfb_area.x1 = clip_x1;
        lcd->pfb_y_start = (uint16_t)clip_y0;
        lcd->pfb_y_end = (uint16_t)clip_y1;
        lcd->pfb_gram = old_gram + (clip_y0 - (int16_t)old_y_start) * lcd->pfb_width
                                 + (clip_x0 - old_pfb_area.x0);

        if (obj->transitioning && obj->trans_prev_page != NULL)
        {
            /* 旧页滑出：与新页保持一个控件宽的相对距离 */
            _menu_draw_page_rows(obj, obj->trans_prev_page, obj->trans_prev_scroll,
                                 (int16_t)(ofs - obj->trans_from), 0U);
        }
        _menu_draw_page_rows(obj, _menu_cur_page(obj), _menu_cur_scroll(obj),
                             ofs, (uint8_t)(!obj->transitioning));

        lcd->pfb_area = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram = old_gram;
    }

    /* 4. 内容溢出时叠加右缘常显滚动条（过渡期间隐藏） */
    if (!obj->transitioning)
        _menu_draw_scrollbar(obj);
}

/* --------------------------------------------------------------------------
 * 事件
 * -------------------------------------------------------------------------- */

/**
 * @brief 事件回调：返回箭头点击 / 行按压高亮 / 拖拽滚动 / 行点击导航 /
 *        右滑（右拖）返回 / 纵向轻扫惯性。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 1 表示消费事件，0 表示穿透。
 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_MENU_USE_KEY == 1)
static uint8_t _menu_key_cb(void *ptr, uint8_t key_evt);
#endif
static uint8_t _menu_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_MENU_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _menu_key_cb(ptr, (uint8_t)event);
#endif

    we_menu_obj_t *obj = (we_menu_obj_t *)ptr;

    if (obj == NULL || data == NULL)
        return 0U;

    if (event == WE_EVENT_PRESSED)
    {
        _menu_stop_inertia(obj); /* 按住即停惯性 */
        if (obj->transitioning)
        {
            obj->sc.tracking = 0U; /* 过渡期间忽略新按压（200ms 内） */
            obj->press_in_rows = 0U;
            return 1U;
        }
        obj->sc.pos = _menu_cur_scroll(obj); /* 会话开始：从页栈载入工作副本 */
        we_scroll_press(&obj->sc, data->y);
        obj->press_x = data->x;
        obj->press_in_rows = _menu_hit_rows_area(obj, data->x, data->y);
        obj->pressed_row = obj->press_in_rows ? _menu_hit_row(obj, data->x, data->y) : -1;
        obj->pressed_back = _menu_hit_back(obj, data->x, data->y);
        if (obj->pressed_row >= 0 || obj->pressed_back)
            we_obj_invalidate((we_obj_t *)obj); /* 按压高亮 */
        return 1U;
    }

    /* SWIPE 事件在 RELEASED 之后到达（tracking 已清零），必须先于
     * tracking 守卫处理。右滑返回已由 RELEASED 的位移判定统一覆盖
     * （拖动与轻扫共用一条路径，避免与 SWIPE_RIGHT 重复出栈），
     * 这里只补"快速纵向轻扫"的惯性初速（无 STAY 时拖拽路径测不到速度）。 */
    if (event == WE_EVENT_SWIPE_UP || event == WE_EVENT_SWIPE_DOWN)
    {
        if (obj->press_in_rows && !obj->transitioning)
        {
            int16_t dy = (int16_t)(data->y - obj->sc.press_c);
            int16_t kick = (int16_t)(-dy / WE_MENU_KICK_DIV);

            if (kick > WE_MENU_KICK_MAX)
                kick = WE_MENU_KICK_MAX;
            if (kick < -WE_MENU_KICK_MAX)
                kick = -WE_MENU_KICK_MAX;
            if (kick != 0)
            {
                obj->sc.pos = _menu_cur_scroll(obj);
                obj->sc.vel = kick;
                obj->sc.animating = 1U;
                we_anim_start(obj->base.lcd, &obj->anim, _menu_inertia_step_cb, obj);
            }
        }
        return 1U;
    }
    if (event == WE_EVENT_SWIPE_LEFT || event == WE_EVENT_SWIPE_RIGHT)
        return 1U; /* 消费防穿透（返回导航已在 RELEASED 完成） */

    if (!obj->sc.tracking)
        return 1U; /* 无有效按压序列，仅消费 */

    if (event == WE_EVENT_STAY)
    {
        /* 返回箭头按压中：移出热区即取消（不再触发返回） */
        if (obj->pressed_back)
        {
            if (!_menu_hit_back(obj, data->x, data->y))
            {
                obj->pressed_back = 0U;
                we_obj_invalidate((we_obj_t *)obj);
            }
            return 1U;
        }

        if (!obj->press_in_rows)
            return 1U;

        {
            int32_t old_pos = obj->sc.pos;
            uint8_t r = we_scroll_stay(&obj->sc, &_menu_scroll_cfg, data->y,
                                       _menu_max_scroll(obj));

            if (r == 1U && obj->pressed_row >= 0)
            {
                obj->pressed_row = -1; /* 进入拖拽即取消行按压态 */
                we_obj_invalidate((we_obj_t *)obj);
            }
            if (r != 0U && obj->sc.pos != old_pos)
                _menu_scroll_commit(obj);
        }
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        uint8_t was_drag = obj->sc.dragging;
        uint8_t back_pressed = obj->pressed_back;
        int16_t row = obj->pressed_row;
        int16_t dx = (int16_t)(data->x - obj->press_x);
        int16_t dy = (int16_t)(data->y - obj->sc.press_c);
        int16_t adx = (dx >= 0) ? dx : (int16_t)(-dx);
        int16_t ady = (dy >= 0) ? dy : (int16_t)(-dy);

        obj->sc.tracking = 0U;
        obj->sc.dragging = 0U;
        obj->pressed_back = 0U;

        /* 返回箭头：按压与释放都落在热区内才触发返回 */
        if (back_pressed)
        {
            we_obj_invalidate((we_obj_t *)obj); /* 清除箭头按压高亮 */
            if (_menu_hit_back(obj, data->x, data->y))
                we_menu_back(obj);
            return 1U;
        }

        /* 行区水平右拖（含慢速拖动，SWIPE 只覆盖无 STAY 的快扫）→ 返回上一级 */
        if (!was_drag && obj->press_in_rows &&
            adx >= WE_CFG_SWIPE_THRESHOLD && adx > ady)
        {
            if (obj->pressed_row >= 0)
            {
                obj->pressed_row = -1;
                we_obj_invalidate((we_obj_t *)obj);
            }
            if (dx > 0)
                we_menu_back(obj); /* 左拖暂无动作，仅取消点击 */
            return 1U;
        }

        if (!was_drag && row >= 0)
        {
            /* 未拖拽：释放点仍在同一行才算点击 */
            if (_menu_hit_row(obj, data->x, data->y) == row)
            {
                const we_menu_page_t *page = _menu_cur_page(obj);

                if (page != NULL && (uint16_t)row < page->count)
                {
                    const we_menu_item_t *item = &page->items[row];

                    if (item->submenu != NULL)
                        _menu_push(obj, item->submenu); /* 入栈进子页 */
                    else if (obj->action_cb != NULL)
                        obj->action_cb(obj, item->action_id, item);
                }
            }
            obj->pressed_row = -1;
            we_obj_invalidate((we_obj_t *)obj); /* 清除按压高亮 */
        }
        else if (was_drag && obj->sc.vel != 0)
        {
            /* 拖拽松手：带惯性（越界回弹并入同一动画节点） */
            obj->sc.pos = _menu_cur_scroll(obj);
            obj->sc.animating = 1U;
            we_anim_start(obj->base.lcd, &obj->anim, _menu_inertia_step_cb, obj);
        }
        else
        {
            /* 无速度松手（含按住定格在过冲区后松开）：越界则纯回弹 */
            int32_t cur = _menu_cur_scroll(obj);

            if (cur < 0 || cur > _menu_max_scroll(obj))
            {
                obj->sc.pos = cur;
                obj->sc.vel = 0;
                obj->sc.animating = 1U;
                we_anim_start(obj->base.lcd, &obj->anim, _menu_inertia_step_cb, obj);
            }
        }
        return 1U;
    }

    /* CLICKED：点击已在 RELEASED 处理，这里仅消费防穿透 */
    return 1U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_MENU_USE_KEY == 1)
/**
 * @brief 键控高亮行落位（钳制到当前页行数）并硬滚动跟随到完整可见。
 * @param obj 传入：控件对象指针。
 * @param row 传入：目标行序号（越界自动钳制）。
 * @return 无。
 * @note 复用触摸按压行 pressed_row 作键控光标（list 同款零新增视觉）；
 *       落位前先停惯性，preview 口径整控件标脏。
 */
static void _menu_key_hover_to(we_menu_obj_t *obj, int16_t row)
{
    const we_menu_page_t *page = _menu_cur_page(obj);
    int32_t row_top;
    int32_t view_h;

    if (page == NULL || page->count == 0U || obj->row_h == 0U)
        return;
    if (row < 0)
        row = 0;
    if (row >= (int16_t)page->count)
        row = (int16_t)(page->count - 1U);
    if (row == obj->pressed_row)
        return;

    _menu_stop_inertia(obj);
    obj->pressed_row = row;

    /* 滚动跟随：目标行不完整可见时硬滚到完整露出 */
    row_top = (int32_t)row * (int32_t)obj->row_h;
    view_h = (int32_t)_menu_rows_h(obj);
    if (row_top < _menu_cur_scroll(obj))
        _menu_apply_scroll(obj, row_top);
    else if (row_top + (int32_t)obj->row_h > _menu_cur_scroll(obj) + view_h)
        _menu_apply_scroll(obj, row_top + (int32_t)obj->row_h - view_h);
    we_obj_invalidate((we_obj_t *)obj); /* preview：整控件标脏（含高亮行变化） */
}

/**
 * @brief 按键/焦点回调：OK 进编辑态行巡航，编辑态上下键移动高亮 + 滚动
 *        跟随，OK 激活行（子页入栈 / 叶子动作回调），BACK 逐级出栈返回。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 根页 BACK 不消费，交焦点管理器退出编辑态；页面过渡期间吞键
 *       （与触摸"过渡期间忽略新按压"一致）。
 */
static uint8_t _menu_key_cb(void *ptr, uint8_t key_evt)
{
    we_menu_obj_t *obj = (we_menu_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    const we_menu_page_t *page;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U && obj->root != NULL) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (obj->transitioning)
            return 1U; /* 过渡期间吞键 */
        if (!we_focus_edit_active(lcd))
        {
            we_focus_edit_enter(lcd);
            /* 进入编辑态：无有效高亮行时落到当前可见首行 */
            page = _menu_cur_page(obj);
            if (page != NULL && page->count > 0U &&
                (obj->pressed_row < 0 || obj->pressed_row >= (int16_t)page->count))
            {
                int16_t row = (obj->row_h != 0U)
                                  ? (int16_t)(_menu_cur_scroll(obj) / (int32_t)obj->row_h)
                                  : 0;

                obj->pressed_row = -1; /* 置无效使 hover_to 必然落位 */
                _menu_key_hover_to(obj, row);
            }
        }
        else
        {
            /* 编辑态 OK = 激活高亮行（镜像触摸点击行路径） */
            page = _menu_cur_page(obj);
            if (page != NULL && obj->pressed_row >= 0 &&
                (uint16_t)obj->pressed_row < page->count)
            {
                const we_menu_item_t *item = &page->items[obj->pressed_row];

                if (item->submenu != NULL)
                {
                    uint8_t d = obj->depth;

                    _menu_push(obj, item->submenu);
                    if (obj->depth != d) /* 入栈成功（栈满被忽略时保持原高亮） */
                        obj->pressed_row = (item->submenu->count > 0U) ? 0 : -1;
                }
                else if (obj->action_cb != NULL)
                {
                    obj->action_cb(obj, item->action_id, item);
                }
            }
        }
        return 1U;
    case WE_KEY_UP:
    case WE_KEY_DOWN:
        if (!we_focus_edit_active(lcd))
            return 0U; /* 导航态：方向键交焦点管理器移动焦点 */
        if (!obj->transitioning)
        {
            int16_t row = (obj->pressed_row < 0)
                              ? 0
                              : (int16_t)(obj->pressed_row +
                                          ((key_evt == WE_KEY_DOWN) ? 1 : -1));

            _menu_key_hover_to(obj, row);
        }
        return 1U;
    case WE_KEY_BACK:
        if (!we_focus_edit_active(lcd))
            return 0U; /* 导航态 BACK 交焦点管理器（退容器/清焦点） */
        if (obj->transitioning)
            return 1U;
        if (obj->depth > 1U)
        {
            we_menu_back(obj);
            obj->pressed_row = -1; /* 返回后高亮重置，下次上下键从可见首行落位 */
            return 1U;
        }
        return 0U; /* 根页：交焦点管理器退出编辑态 */
    default:
        return 0U;
    }
}
#endif

static const we_class_t _menu_class = {
    .draw_cb = _menu_draw_cb,
    .event_cb = _menu_event_cb,
    .set_pos_cb = NULL, /* 通用移动逻辑（旧区标脏 + 新区标脏）已足够 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_MENU_USE_KEY == 1)
    .class_flags = WE_CLASS_FLAG_FOCUSABLE, /* 键/焦点走统一 event_cb 通道 */
#endif
};

/* --------------------------------------------------------------------------
 * 公共 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化多级菜单控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param h 传入：控件高度（像素）。
 * @param root 传入：根页指针（调用方持有，控件只存指针）。
 * @return 无。
 */
void we_menu_obj_init(we_menu_obj_t *obj, we_lcd_t *lcd,
                      int16_t x, int16_t y, int16_t w, int16_t h,
                      const we_menu_page_t *root, const unsigned char *font)
{
    uint16_t line_h;
    uint8_t i;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_menu_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->root = root;
    for (i = 0U; i < WE_MENU_STACK_MAX; i++)
    {
        obj->stack[i].page = NULL;
        obj->stack[i].scroll_px = 0;
    }
    obj->stack[0].page = root;
    obj->depth = 1U;

    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;
    line_h = we_font_get_line_height(obj->font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->row_h = (uint16_t)(line_h + 2U * WE_MENU_ROW_PAD);
    obj->title_h = (uint16_t)(line_h + 2U * WE_MENU_TITLE_PAD);

    obj->radius = WE_MENU_DEF_RADIUS;
    obj->opacity = 255U;

    obj->bg_color = RGB888TODEV(32, 38, 50);
    obj->title_bg_color = RGB888TODEV(44, 54, 72);
    obj->title_text_color = RGB888TODEV(236, 241, 248);
    obj->text_color = RGB888TODEV(214, 221, 233);
    obj->sep_color = RGB888TODEV(220, 228, 242);
    obj->press_color = RGB888TODEV(62, 92, 132);
    obj->arrow_color = RGB888TODEV(150, 164, 186);
    obj->sb_color = RGB888TODEV(200, 210, 226);
    obj->action_cb = NULL;

    obj->pressed_row = -1;
    obj->pressed_back = 0U;
    obj->press_in_rows = 0U;
    obj->sc.tracking = 0U;
    obj->sc.dragging = 0U;
    obj->press_x = 0;
    obj->sc.press_c = 0;
    obj->sc.last_c = 0;
    obj->sc.press_pos = 0;

    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;
    obj->sc.animating = 0U;
    obj->sc.vel = 0;

    obj->trans_anim.next = NULL;
    obj->trans_anim.step_cb = NULL;
    obj->trans_anim.owner = NULL;
    obj->transitioning = 0U;
    obj->trans_t = 0U;
    obj->trans_from = 0;
    obj->trans_prev_page = NULL;
    obj->trans_prev_scroll = 0;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置叶子行动作回调。
 * @param obj 传入：控件对象指针。
 * @param cb 传入：回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_menu_set_action_cb(we_menu_obj_t *obj, we_menu_action_cb_t cb)
{
    if (obj == NULL || obj->action_cb == cb)
        return;
    obj->action_cb = cb;
}

/**
 * @brief 返回上一级页面（出栈，带水平滑入过渡；根页时无操作）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_menu_back(we_menu_obj_t *obj)
{
    const we_menu_page_t *prev;
    int32_t prev_scroll;

    if (obj == NULL || obj->depth <= 1U)
        return;

    _menu_stop_inertia(obj);
    _menu_apply_scroll(obj, _menu_cur_scroll(obj)); /* 出栈前把过冲吸回边界 */
    prev = _menu_cur_page(obj);
    prev_scroll = _menu_cur_scroll(obj);
    obj->depth--;

    _menu_start_transition(obj, prev, prev_scroll, 0U);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 复位到根页（清空页面栈与根页滚动，无过渡动画）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_menu_reset(we_menu_obj_t *obj)
{
    if (obj == NULL)
        return;

    _menu_stop_inertia(obj);
    _menu_stop_transition(obj);

    obj->depth = 1U;
    obj->stack[0].scroll_px = 0;
    obj->pressed_row = -1;
    obj->pressed_back = 0U;
    obj->sc.tracking = 0U;
    obj->sc.dragging = 0U;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置主题配色（六色一组，值全部未变时直接返回）。
 * @param obj 传入：控件对象指针。
 * @param bg 传入：行区面板背景色。
 * @param title_bg 传入：标题栏背景色。
 * @param text 传入：行文字色。
 * @param title_text 传入：标题文字色。
 * @param press 传入：按压高亮背景色。
 * @param accent 传入：强调色（返回箭头 / ">" 提示 / 滚动条滑块）。
 * @return 无。
 */
void we_menu_set_colors(we_menu_obj_t *obj, colour_t bg, colour_t title_bg,
                        colour_t text, colour_t title_text,
                        colour_t press, colour_t accent)
{
    if (obj == NULL)
        return;
    if (_menu_colour_eq(obj->bg_color, bg) &&
        _menu_colour_eq(obj->title_bg_color, title_bg) &&
        _menu_colour_eq(obj->text_color, text) &&
        _menu_colour_eq(obj->title_text_color, title_text) &&
        _menu_colour_eq(obj->press_color, press) &&
        _menu_colour_eq(obj->arrow_color, accent) &&
        _menu_colour_eq(obj->sb_color, accent))
        return;

    obj->bg_color = bg;
    obj->title_bg_color = title_bg;
    obj->text_color = text;
    obj->title_text_color = title_text;
    obj->press_color = press;
    obj->arrow_color = accent;
    obj->sb_color = accent;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除菜单控件：先摘除惯性与过渡两个动画节点再摘链。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_menu_obj_delete(we_menu_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    _menu_stop_inertia(obj);    /* 节点归控件所有，删除前必须摘链 */
    _menu_stop_transition(obj);
    we_obj_delete((we_obj_t *)obj);
}
