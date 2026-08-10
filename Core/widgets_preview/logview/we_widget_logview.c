#include "we_widget_logview.h"
#include "we_scroll.h"

/* 滚动物理参数：无惯性无过冲档。scroll_px 语义为"距底偏移"（下拉看历史
 * 时增大），与组件"pos = press_pos - 位移"的常规方向相反，因此喂入的
 * 主轴坐标取负（-y），等效 pos = press_pos + dy。 */
static const we_scroll_cfg_t _lv_scroll_cfg = {
    WE_LOGVIEW_DRAG_THRESHOLD, 0, 1, 1, 1, 1, 128,
};
#include "we_render.h"

/* --------------------------------------------------------------------------
 * logview —— 滚动日志窗（preview 孵化区）
 *
 * 行存储：调用方提供的扁平缓冲（line_cnt x line_len），head 为下一条写入
 * 槽位，used 为有效行数；逻辑行号 j（0=最旧）对应物理槽
 * (head - used + j) mod line_cnt。写满后最旧行被覆盖。
 *
 * 滚动坐标系：scroll_px 为距"内容底部对齐"的向上偏移（0=贴底最新）。
 * 内容不足一屏时从面板顶部排布（终端习惯）；溢出后：
 *   视口内容顶 = content_h - view_h - scroll_px
 * push 时跟随态钉在 0；非跟随态 scroll_px += row_h 保持视口画面不动
 *（写满覆盖最旧行时同样成立，夹紧兜底）。
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色值是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 表示相等，0 表示不等。
 */
static uint8_t _lv_color_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#else
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 将透明度按控件整体不透明度缩放。
 * @param a 传入：原始透明度（0~255）。
 * @param opacity 传入：控件整体不透明度（0~255）。
 * @return 缩放后的透明度（0~255）。
 */
static uint8_t _lv_scale_opa(uint8_t a, uint8_t opacity)
{
    if (opacity == 255U)
        return a;
    return we_div255((uint32_t)a * (uint32_t)opacity);
}

/**
 * @brief 取逻辑行的物理槽起始地址（j=0 为最旧行）。
 * @param obj 传入：控件对象指针。
 * @param j 传入：逻辑行号（0..used-1）。
 * @return 该行文本首地址。
 */
static char *_lv_line_at(const we_logview_obj_t *obj, uint16_t j)
{
    uint16_t slot = (uint16_t)((obj->head + obj->line_cnt - obj->used + j) % obj->line_cnt);
    return obj->line_buf + (uint32_t)slot * obj->line_len;
}

/**
 * @brief 视口可视高度（面板高扣除上下内边距）。
 * @param obj 传入：控件对象指针。
 * @return 可视高度（像素，最小 1）。
 */
static int32_t _lv_view_h(const we_logview_obj_t *obj)
{
    int32_t v = (int32_t)obj->base.h - 2 * WE_LOGVIEW_V_PAD;
    return (v < 1) ? 1 : v;
}

/**
 * @brief 内容总高度（有效行数 x 行高）。
 * @param obj 传入：控件对象指针。
 * @return 内容总高（像素）。
 */
static int32_t _lv_content_h(const we_logview_obj_t *obj)
{
    return (int32_t)obj->used * (int32_t)obj->row_h;
}

/**
 * @brief 最大可向上滚动像素（内容不溢出时为 0）。
 * @param obj 传入：控件对象指针。
 * @return 最大 scroll_px（>= 0）。
 */
static int32_t _lv_max_scroll(const we_logview_obj_t *obj)
{
    int32_t m = _lv_content_h(obj) - _lv_view_h(obj);
    return (m > 0) ? m : 0;
}

/**
 * @brief 将 scroll_px 夹紧到 [0, max] 后应用，并同步跟随标志。
 * @param obj 传入：控件对象指针。
 * @param new_scroll 传入：目标滚动像素。
 * @param sync_follow 传入：非 0 时按落点更新 follow（0=离底暂停/贴底恢复不生效）。
 * @return 无。
 */
static void _lv_apply_scroll(we_logview_obj_t *obj, int32_t new_scroll, uint8_t sync_follow)
{
    int32_t max_scroll = _lv_max_scroll(obj);

    if (new_scroll < 0)
        new_scroll = 0;
    if (new_scroll > max_scroll)
        new_scroll = max_scroll;

    if (sync_follow)
        obj->follow = (new_scroll == 0) ? 1U : 0U; /* 拖离底部暂停，拖回贴底恢复 */

    if (new_scroll == obj->scroll_px)
        return;

    obj->scroll_px = new_scroll;
    we_obj_invalidate((we_obj_t *)obj); /* preview：整控件包围盒标脏 */
}

/**
 * @brief 绘制右缘细滚动条（内容溢出时常显，位置按滚动比例）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _lv_draw_scrollbar(we_logview_obj_t *obj)
{
    we_lcd_t *lcd = obj->base.lcd;
    int32_t content_h = _lv_content_h(obj);
    int32_t max_scroll = _lv_max_scroll(obj);
    int16_t track_x;
    int16_t track_y0;
    int16_t track_h;
    int16_t thumb_h;
    int16_t thumb_y;

    if (max_scroll == 0 || content_h <= 0)
        return; /* 内容未溢出，无需滚动条 */

    track_x = (int16_t)(obj->base.x + obj->base.w - WE_LOGVIEW_SB_MARGIN - WE_LOGVIEW_SB_WIDTH);
    track_y0 = (int16_t)(obj->base.y + (int16_t)obj->radius);
    track_h = (int16_t)(obj->base.h - 2 * (int16_t)obj->radius);
    if (track_h < (int16_t)obj->row_h)
        track_h = obj->base.h; /* 圆角过大时退化为整高轨道 */

    thumb_h = (int16_t)(((int32_t)track_h * _lv_view_h(obj)) / content_h);
    if (thumb_h < 8)
        thumb_h = 8;
    if (thumb_h > track_h)
        thumb_h = track_h;

    /* scroll_px 自底往上计，滑块位置需换算成"距顶比例" */
    thumb_y = (int16_t)(track_y0 +
              (int32_t)(track_h - thumb_h) * (max_scroll - obj->scroll_px) / max_scroll);

    we_draw_round_rect_analytic_fill(lcd, track_x, thumb_y,
                                     (uint16_t)WE_LOGVIEW_SB_WIDTH, (uint16_t)thumb_h,
                                     (uint16_t)(WE_LOGVIEW_SB_WIDTH / 2),
                                     obj->sb_color,
                                     _lv_scale_opa(WE_LOGVIEW_SB_OPA, obj->opacity));
}

/**
 * @brief 控件绘制回调：圆角面板 + PFB 收窄裁剪的逐行日志 + 滚动条。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 */
static void _lv_draw_cb(void *ptr)
{
    we_logview_obj_t *obj = (we_logview_obj_t *)ptr;
    we_lcd_t *lcd;
    we_area_t old_pfb_area;
    uint16_t old_y_start;
    uint16_t old_y_end;
    colour_t *old_gram;
    int16_t clip_x0;
    int16_t clip_y0;
    int16_t clip_x1;
    int16_t clip_y1;

    if (obj == NULL || obj->opacity == 0U)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL)
        return;

    /* 1. 深色圆角面板背景 */
    we_draw_round_rect_analytic_fill(lcd, obj->base.x, obj->base.y,
                                     (uint16_t)obj->base.w, (uint16_t)obj->base.h,
                                     obj->radius, obj->bg_color, obj->opacity);

    if (obj->used == 0U || obj->line_buf == NULL || obj->row_h == 0U)
        return;

    /* 2. PFB 窗口收窄：把行内容裁剪在面板内边距矩形内（半露行不渗出） */
    old_pfb_area = lcd->pfb_area;
    old_y_start = lcd->pfb_y_start;
    old_y_end = lcd->pfb_y_end;
    old_gram = lcd->pfb_gram;

    clip_x0 = WE_MAX(old_pfb_area.x0, (int16_t)(obj->base.x + 2));
    clip_y0 = WE_MAX((int16_t)old_y_start, (int16_t)(obj->base.y + WE_LOGVIEW_V_PAD));
    clip_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 3));
    clip_y1 = WE_MIN((int16_t)old_y_end,
                     (int16_t)(obj->base.y + obj->base.h - WE_LOGVIEW_V_PAD - 1));

    if (clip_x0 <= clip_x1 && clip_y0 <= clip_y1)
    {
        int16_t view_y0 = (int16_t)(obj->base.y + WE_LOGVIEW_V_PAD);
        int32_t view_h = _lv_view_h(obj);
        int32_t top_c = _lv_content_h(obj) - view_h - obj->scroll_px; /* 视口内容顶 */
        int32_t j;
        int16_t iy;

        if (top_c < 0)
            top_c = 0; /* 内容不足一屏：从面板顶部排布 */

        j = top_c / (int32_t)obj->row_h;                       /* 首个（可能半露）行 */
        iy = (int16_t)(view_y0 - (int16_t)(top_c % (int32_t)obj->row_h));

        lcd->pfb_area.x0 = clip_x0;
        lcd->pfb_area.x1 = clip_x1;
        lcd->pfb_y_start = (uint16_t)clip_y0;
        lcd->pfb_y_end = (uint16_t)clip_y1;
        lcd->pfb_gram = old_gram + (clip_y0 - (int16_t)old_y_start) * lcd->pfb_width
                                 + (clip_x0 - old_pfb_area.x0);

        for (; j < (int32_t)obj->used; j++)
        {
            const char *text = _lv_line_at(obj, (uint16_t)j);

            if (iy > (int16_t)(obj->base.y + obj->base.h - WE_LOGVIEW_V_PAD - 1))
                break; /* 已画到视口底部之外 */

            if (text[0] != '\0')
            {
                int8_t y_top;
                int8_t y_bot;
                int16_t ty;

                we_get_text_bbox(obj->font, text, &y_top, &y_bot);
                ty = (int16_t)(iy + (int16_t)obj->row_h / 2 - (y_top + y_bot) / 2);
                we_draw_string(lcd, (int16_t)(obj->base.x + WE_LOGVIEW_TEXT_PAD), ty,
                               obj->font, text, obj->text_color, obj->opacity);
            }

            iy = (int16_t)(iy + (int16_t)obj->row_h);
        }
    }

    lcd->pfb_area = old_pfb_area;
    lcd->pfb_y_start = old_y_start;
    lcd->pfb_y_end = old_y_end;
    lcd->pfb_gram = old_gram;

    /* 3. 内容溢出时叠加右缘滚动条 */
    _lv_draw_scrollbar(obj);
}

/**
 * @brief 控件事件回调：拖拽跟手滚动，离底暂停跟随，贴底恢复。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 恒返回 1（交互控件，消费事件）。
 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_LOGVIEW_USE_KEY == 1)
static uint8_t _lv_key_cb(void *ptr, uint8_t key_evt);
#endif
static uint8_t _lv_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_LOGVIEW_USE_KEY == 1)
    /* 统一事件通道：语义键/焦点通知（0x10+）分流到键处理器 */
    if ((uint8_t)event >= WE_KEY_UP)
        return _lv_key_cb(ptr, (uint8_t)event);
#endif

    we_logview_obj_t *obj = (we_logview_obj_t *)ptr;

    if (obj == NULL || data == NULL)
        return 0U;

    if (event == WE_EVENT_PRESSED)
    {
        obj->sc.pos = obj->scroll_px;              /* 会话开始：载入当前位置 */
        we_scroll_press(&obj->sc, (int16_t)-data->y); /* 距底偏移语义：坐标取负 */
        return 1U;
    }

    if (!obj->sc.tracking)
        return 1U; /* 无有效按压序列，仅消费 */

    if (event == WE_EVENT_STAY)
    {
        /* 内容跟手：手指下移 -> 看到更早历史 -> scroll_px（距底偏移）增大；
         * 坐标取负喂入使组件的 press_pos - 位移 等效为 press_pos + dy */
        if (we_scroll_stay(&obj->sc, &_lv_scroll_cfg, (int16_t)-data->y,
                           _lv_max_scroll(obj)) != 0U)
            _lv_apply_scroll(obj, obj->sc.pos, 1U);
        return 1U;
    }

    if (event == WE_EVENT_RELEASED)
    {
        /* 无惯性档：不调 we_scroll_release（避免按速度误启动动画） */
        obj->sc.tracking = 0U;
        obj->sc.dragging = 0U;
        obj->sc.vel = 0;
        return 1U;
    }

    /* CLICKED / SWIPE：无点击行为，仅消费防穿透 */
    return 1U;
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_LOGVIEW_USE_KEY == 1)
/**
 * @brief 按键/焦点回调：OK 进出编辑态，编辑态上下键按行高翻阅日志。
 * @param ptr 回调透传对象指针。
 * @param key_evt 语义键值或焦点通知（we_key_evt_t）。
 * @return 非 0 表示已消费。
 * @note 上键翻历史（scroll_px 增大）自动暂停跟随，下键回向最新、滚回
 *       贴底自动恢复跟随（_lv_apply_scroll 的 sync_follow 路径，与拖拽
 *       同语义）；内容不溢出时拒绝聚焦；BACK 交焦点管理器退出编辑态。
 */
static uint8_t _lv_key_cb(void *ptr, uint8_t key_evt)
{
    we_logview_obj_t *obj = (we_logview_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    switch (key_evt)
    {
    case WE_KEY_EVT_FOCUS:
        return (obj->opacity != 0U && _lv_max_scroll(obj) > 0) ? 1U : 0U;
    case WE_KEY_EVT_DEFOCUS:
        return 1U;
    case WE_KEY_OK:
        if (we_focus_edit_active(lcd))
            we_focus_edit_exit(lcd);
        else
            we_focus_edit_enter(lcd);
        return 1U;
    case WE_KEY_UP:
    case WE_KEY_DOWN:
        if (!we_focus_edit_active(lcd))
            return 0U; /* 导航态：方向键交焦点管理器移动焦点 */
        if (obj->row_h != 0U)
        {
            int32_t step = (key_evt == WE_KEY_UP) ? (int32_t)obj->row_h
                                                  : -(int32_t)obj->row_h;

            _lv_apply_scroll(obj, obj->scroll_px + step, 1U);
        }
        return 1U;
    default:
        return 0U;
    }
}
#endif

static const we_class_t _logview_class = {
    .draw_cb = _lv_draw_cb,
    .event_cb = _lv_event_cb,
    .set_pos_cb = NULL, /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_CFG_FOCUS_EDIT == 1) && (WE_LOGVIEW_USE_KEY == 1)
    .class_flags = WE_CLASS_FLAG_FOCUSABLE, /* 键/焦点走统一 event_cb 通道 */
#endif
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化日志窗控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param h 传入：控件高度（像素）。
 * @param line_buf 传入：行缓冲基址（line_cnt x line_len 字节，调用方持有）。
 * @param line_len 传入：单行字节容量（含 \0）。
 * @param line_cnt 传入：行槽总数。
 * @return 无。
 */
void we_logview_obj_init(we_logview_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w, int16_t h,
                         char *line_buf, uint16_t line_len, uint16_t line_cnt, const unsigned char *font)
{
    uint16_t line_h;

    if (obj == NULL || lcd == NULL || font == NULL)
        return;
    if (line_buf == NULL || line_len == 0U || line_cnt == 0U)
        return; /* 行存储由调用方提供，容量非法直接拒绝 */

    obj->font = font; /* 字体必传（上方守卫已拦 NULL） */
    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_logview_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->line_buf = line_buf;
    obj->line_len = line_len;
    obj->line_cnt = line_cnt;
    obj->head = 0U;
    obj->used = 0U;

    line_h = we_font_get_line_height(obj->font);
    if (line_h == 0U)
        line_h = 16U; /* 字库异常兜底 */
    obj->row_h = (uint16_t)(line_h + 2U);

    obj->radius = WE_LOGVIEW_DEF_RADIUS;
    obj->scroll_px = 0;
    we_scroll_reset(&obj->sc);
    obj->follow = 1U;
    obj->opacity = 255U;

    obj->bg_color = RGB888TODEV(22, 27, 36);       /* 深色终端底 */
    obj->text_color = RGB888TODEV(178, 218, 190);  /* 淡绿日志字 */
    obj->sb_color = RGB888TODEV(200, 210, 226);


    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 追加一条日志（拷贝进环形行缓冲，超长截断）。
 * @param obj 传入：控件对象指针。
 * @param str 传入：日志字符串（UTF-8）。
 * @return 无。
 */
void we_logview_push(we_logview_obj_t *obj, const char *str)
{
    char *dst;
    uint16_t i;
    uint16_t cap;

    if (obj == NULL || str == NULL || obj->line_buf == NULL || obj->base.lcd == NULL)
        return;

    /* 1. 拷贝进 head 槽（超长截断，恒补 \0） */
    dst = obj->line_buf + (uint32_t)obj->head * obj->line_len;
    cap = (uint16_t)(obj->line_len - 1U);
    for (i = 0U; i < cap && str[i] != '\0'; i++)
        dst[i] = str[i];
    dst[i] = '\0';

    obj->head = (uint16_t)((obj->head + 1U) % obj->line_cnt);
    if (obj->used < obj->line_cnt)
        obj->used++;

    /* 2. 滚动语义：跟随态钉在贴底；非跟随态视口内容保持不动
     *（内容底部长高一行，距底偏移同步 +row_h，夹紧兜底） */
    if (obj->follow)
    {
        obj->scroll_px = 0;
    we_scroll_reset(&obj->sc);
    }
    else
    {
        int32_t max_scroll = _lv_max_scroll(obj);
        obj->scroll_px += (int32_t)obj->row_h;
        if (obj->scroll_px > max_scroll)
            obj->scroll_px = max_scroll;
        /* 拖拽进行中：同步补偿拖拽基准，避免下一次 STAY 回跳一行 */
        if (obj->sc.tracking)
            obj->sc.press_pos += (int32_t)obj->row_h;
    }

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 清空全部日志并复位滚动（跟随开关维持原状）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_logview_clear(we_logview_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    if (obj->used == 0U && obj->scroll_px == 0)
        return; /* 本就是空窗 */

    obj->head = 0U;
    obj->used = 0U;
    obj->scroll_px = 0;
    we_scroll_reset(&obj->sc); /* 清空日志即结束当前触摸会话 */
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置面板底色与文字色。
 * @param obj 传入：控件对象指针。
 * @param bg 传入：面板底色。
 * @param text 传入：日志文字色。
 * @return 无。
 */
void we_logview_set_colors(we_logview_obj_t *obj, colour_t bg, colour_t text)
{
    if (obj == NULL)
        return;
    if (_lv_color_eq(obj->bg_color, bg) && _lv_color_eq(obj->text_color, text))
        return;

    obj->bg_color = bg;
    obj->text_color = text;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 开关自动跟随最新行。
 * @param obj 传入：控件对象指针。
 * @param follow 传入：0=暂停跟随，非0=恢复跟随并滚到最新。
 * @return 无。
 */
void we_logview_set_follow(we_logview_obj_t *obj, uint8_t follow)
{
    uint8_t val;

    if (obj == NULL)
        return;
    val = follow ? 1U : 0U;
    if (obj->follow == val)
        return;

    obj->follow = val;
    if (val)
        _lv_apply_scroll(obj, 0, 0U); /* 恢复跟随立即滚到最新行 */
}

/**
 * @brief 查询当前是否处于自动跟随态。
 * @param obj 传入：控件对象指针。
 * @return 1=跟随中，0=已暂停。
 */
uint8_t we_logview_get_follow(const we_logview_obj_t *obj)
{
    return (obj == NULL) ? 0U : obj->follow;
}

/**
 * @brief 设置控件整体透明度并按需重绘。
 * @param obj 传入：控件对象指针。
 * @param opacity 传入：不透明度（0~255）。
 * @return 无。
 */
void we_logview_set_opacity(we_logview_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 删除日志窗控件（无动画节点，直接摘链）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
void we_logview_obj_delete(we_logview_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL)
        return;
    we_obj_delete((we_obj_t *)obj);
}
