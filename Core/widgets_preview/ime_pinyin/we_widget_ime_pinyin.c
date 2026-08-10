/**
 * @file  we_widget_ime_pinyin.c
 * @brief 拼音输入法面板控件（preview）：拼音条 + 候选栏 + 内嵌软键盘
 *
 * 组合复用：面板本体只负责拼音条与候选栏两条横带，键盘区交给内嵌的
 * we_keyboard_obj_t（init 时晚于面板挂链，重叠区命中优先归键盘）。
 * 键盘键值经 key_cb 汇入 _ime_handle_key，与 we_ime_pinyin_inject_key
 * 完全同一入口——自动演示脚本喂键值和真实触摸走的是同一条路。
 *
 * 候选填充：每次从引擎迭代器取码点，先过 we_font_get_glyph_info——
 * 字库没有的字形直接跳过（缺字过滤），填满一页（7 个）或区间尽头为止。
 * 翻页向后是游标续走，回翻靠最近页起点栈（深度 16，溢出挤掉最旧）。
 *
 * 标脏粒度：拼音条/候选栏各自整条横带；按压反馈只标单个槽位矩形；
 * 键盘区永不因面板刷新被标脏。
 */

#include "we_widget_ime_pinyin.h"
#include "../textarea/we_widget_textarea.h" /* 弹层模式绑定目标输入框直接注入 */
#include "we_render.h"
#include <stddef.h>
#include <string.h>

/* 候选栏提示文案（字库须含这些字形，否则退化为空白） */
static const char *const _ime_hint_no_cand = "无候选";

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 相等，0 不等。
 */
static uint8_t _ime_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (a.dat16 == b.dat16) ? 1U : 0U;
#elif (LCD_DEEP == DEEP_RGB888)
    return (a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b) ? 1U : 0U;
#endif
}

/**
 * @brief 重新派生提示灰字色（文字色向面板底色混合，弱化 5/8）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _ime_update_hint_color(we_ime_pinyin_obj_t *obj)
{
    obj->hint_color = we_colour_blend(obj->text_color, obj->bg_color, 96U);
}

/**
 * @brief 查询字库是否含指定码点的字形（候选缺字过滤）。
 * @param obj 传入：控件对象指针。
 * @param cp 传入：Unicode 码点。
 * @return 1 有字形，0 无。
 */
static uint8_t _ime_glyph_ok(const we_ime_pinyin_obj_t *obj, uint16_t cp)
{
    we_glyph_info_t info;

    if (obj->font == NULL)
        return 0U;
    return we_font_get_glyph_info(obj->font, cp, &info);
}

/**
 * @brief 计算候选栏槽位矩形（屏幕绝对坐标）。
 * @param obj 传入：控件对象指针。
 * @param slot 传入：0 = "<"，1..PAGE_CAP = 候选格，PAGE_CAP+1 = ">"。
 * @param out_x 传出：槽位左上角 X。
 * @param out_y 传出：槽位左上角 Y。
 * @param out_w 传出：槽位宽度。
 * @param out_h 传出：槽位高度。
 * @return 1 成功，0 槽位号非法或几何退化。
 * @note 候选格边缘按 "inner_x + i * inner_w / 7" 整数求值，无累计漂移
 *       （keyboard 份数网格同款思路）。
 */
/**
 * @brief 面板内容（拼音条）顶部 Y：弹层模式让位给顶部回显条。
 * @param obj 传入：控件对象指针。
 * @return 拼音条顶部 Y（屏幕绝对坐标）。
 */
static int16_t _ime_bars_y(const we_ime_pinyin_obj_t *obj)
{
    return (int16_t)(obj->base.y + (obj->popup_mode ? WE_KEYBOARD_ECHO_H : 0));
}

static uint8_t _ime_slot_rect(const we_ime_pinyin_obj_t *obj, int8_t slot,
                              int16_t *out_x, int16_t *out_y, int16_t *out_w, int16_t *out_h)
{
    int16_t bar_y = (int16_t)(_ime_bars_y(obj) + WE_IME_PINYIN_BUF_H);
    int16_t inner_x = (int16_t)(obj->base.x + WE_IME_PINYIN_PAGER_W);
    int16_t inner_w = (int16_t)(obj->base.w - 2 * WE_IME_PINYIN_PAGER_W);

    if (slot < 0 || slot > WE_IME_PINYIN_PAGE_CAP + 1 || inner_w <= 0)
        return 0U;

    *out_y = bar_y;
    *out_h = WE_IME_PINYIN_CAND_H;

    if (slot == 0) /* "<" 回翻键：左端 */
    {
        *out_x = obj->base.x;
        *out_w = WE_IME_PINYIN_PAGER_W;
    }
    else if (slot == WE_IME_PINYIN_PAGE_CAP + 1) /* ">" 翻页键：右端 */
    {
        *out_x = (int16_t)(obj->base.x + obj->base.w - WE_IME_PINYIN_PAGER_W);
        *out_w = WE_IME_PINYIN_PAGER_W;
    }
    else /* 候选格 1..7 */
    {
        int16_t i = (int16_t)(slot - 1);
        int16_t x0 = (int16_t)(inner_x + i * inner_w / WE_IME_PINYIN_PAGE_CAP);
        int16_t x1 = (int16_t)(inner_x + (i + 1) * inner_w / WE_IME_PINYIN_PAGE_CAP);

        *out_x = x0;
        *out_w = (int16_t)(x1 - x0);
    }
    return (*out_w > 0) ? 1U : 0U;
}

/**
 * @brief 触点命中检测：返回候选栏槽位号。
 * @param obj 传入：控件对象指针。
 * @param px 传入：触点 X（屏幕绝对坐标）。
 * @param py 传入：触点 Y。
 * @return 槽位号（0..PAGE_CAP+1），未命中返回 -1。
 */
static int8_t _ime_hit_slot(const we_ime_pinyin_obj_t *obj, int16_t px, int16_t py)
{
    int8_t slot;

    for (slot = 0; slot <= WE_IME_PINYIN_PAGE_CAP + 1; slot++)
    {
        int16_t sx;
        int16_t sy;
        int16_t sw;
        int16_t sh;

        if (!_ime_slot_rect(obj, slot, &sx, &sy, &sw, &sh))
            continue;
        if (px >= sx && px < (int16_t)(sx + sw) && py >= sy && py < (int16_t)(sy + sh))
            return slot;
    }
    return -1;
}

/**
 * @brief 标脏拼音条横带。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _ime_invalidate_buf_bar(we_ime_pinyin_obj_t *obj)
{
    we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, _ime_bars_y(obj),
                           obj->base.w, WE_IME_PINYIN_BUF_H);
}

/**
 * @brief 标脏候选栏横带。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _ime_invalidate_cand_bar(we_ime_pinyin_obj_t *obj)
{
    we_obj_invalidate_area((we_obj_t *)obj, obj->base.x,
                           (int16_t)(_ime_bars_y(obj) + WE_IME_PINYIN_BUF_H),
                           obj->base.w, WE_IME_PINYIN_CAND_H);
}

/**
 * @brief 标脏弹层模式的顶部回显条。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _ime_invalidate_echo(we_ime_pinyin_obj_t *obj)
{
    if (obj->popup_mode && WE_KEYBOARD_ECHO_H > 0)
        we_obj_invalidate_area((we_obj_t *)obj, obj->base.x, obj->base.y,
                               obj->base.w, WE_KEYBOARD_ECHO_H);
}

/**
 * @brief 取当前绑定的目标输入框，目标已被删除时自动解绑并返回 NULL
 * @param obj 传入：本控件对象指针
 * @return 有效目标指针；未绑定或目标已被删除返回 NULL
 * @note 跨控件裸指针绑定的防悬空口径：we_obj_delete 会把被删对象的
 *       class_p 清空，这里据此识别失效绑定并自动置空（与
 *       we_gui_indev_handler 对 pressed_obj 的防御同源）。
 */
static void *_ime_target(we_ime_pinyin_obj_t *obj)
{
    we_obj_t *t = (we_obj_t *)obj->target;

    if (t != NULL && t->class_p == NULL)
    {
        obj->target = NULL;
        t = NULL;
    }
    return (void *)t;
}

/**
 * @brief 统一上屏出口：弹层模式绑定目标时直接注入输入框，再走 commit 回调。
 * @param obj 传入：控件对象指针。
 * @param utf8 传入：候选字/透传键值字符串（"\b" = 退格）。
 * @return 无。
 */
static void _ime_emit(we_ime_pinyin_obj_t *obj, const char *utf8)
{
    if (obj->popup_mode && _ime_target(obj) != NULL)
    {
        we_textarea_input((we_textarea_obj_t *)obj->target, utf8);
        _ime_invalidate_echo(obj); /* 回显条同步刷新 */
    }
    if (obj->commit_cb != NULL)
        obj->commit_cb(obj, utf8);
}

/**
 * @brief 标脏单个候选栏槽位。
 * @param obj 传入：控件对象指针。
 * @param slot 传入：槽位号。
 * @return 无。
 */
static void _ime_invalidate_slot(we_ime_pinyin_obj_t *obj, int8_t slot)
{
    int16_t sx;
    int16_t sy;
    int16_t sw;
    int16_t sh;

    if (_ime_slot_rect(obj, slot, &sx, &sy, &sw, &sh))
        we_obj_invalidate_area((we_obj_t *)obj, sx, sy, sw, sh);
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
/* --------------------------------------------------------------------------
 * 候选栏键控光标（模态键通道用，随内嵌键盘的 WE_KEYBOARD_USE_KEY 门控）
 * -------------------------------------------------------------------------- */

/**
 * @brief 判断候选栏槽位当前是否可用（光标只在可用槽位间移动/落位）。
 * @param obj 传入：控件对象指针。
 * @param slot 传入：槽位号（0="<"，1..7=候选格，8=">"）。
 * @return 1 可用，0 禁用/空槽。
 */
static uint8_t _ime_cand_slot_enabled(const we_ime_pinyin_obj_t *obj, int8_t slot)
{
    if (slot == 0)
        return (obj->back_depth > 0U) ? 1U : 0U;
    if (slot == WE_IME_PINYIN_PAGE_CAP + 1)
        return obj->has_more ? 1U : 0U;
    return ((uint8_t)(slot - 1) < obj->page_cnt) ? 1U : 0U;
}

/**
 * @brief 退出候选区键控光标：恢复键盘键光标环，清掉候选环与按压残留。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 候选刷新（选字上屏/重新敲字母）、DOWN/BACK 回退、弹层关闭
 *       都收敛到这里；未激活时空操作。
 */
static void _ime_cand_zone_exit(we_ime_pinyin_obj_t *obj)
{
    if (obj->cand_focus < 0)
        return;
    _ime_invalidate_slot(obj, obj->cand_focus);
    obj->cand_focus = -1;
    if (obj->pressed)
    {
        obj->pressed = 0U;
        _ime_invalidate_slot(obj, obj->press_slot);
    }
    obj->press_slot = -1;
    obj->kb.focus_idx = (obj->cand_kb_idx >= 0) ? obj->cand_kb_idx : 0;
    obj->cand_kb_idx = -1;
    we_obj_invalidate_area((we_obj_t *)obj, obj->kb.base.x, obj->kb.base.y,
                           obj->kb.base.w, obj->kb.base.h); /* 键环重现 */
}

/**
 * @brief 进入候选区键控光标：暂隐键盘键环，光标落到第一个候选格。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 调用方保证 page_cnt > 0（槽位 1 必然可用）。
 */
static void _ime_cand_zone_enter(we_ime_pinyin_obj_t *obj)
{
    obj->cand_kb_idx = obj->kb.focus_idx;
    obj->kb.focus_idx = -1; /* 暂隐键盘键环：同一时刻只有一个光标 */
    obj->kb.pressed = 0U;   /* 丢弃键盘侧未配对的 OK 按压，防跨区悬挂触发 */
    obj->kb.press_idx = -1;
    we_obj_invalidate_area((we_obj_t *)obj, obj->kb.base.x, obj->kb.base.y,
                           obj->kb.base.w, obj->kb.base.h);
    obj->cand_focus = 1;
    _ime_invalidate_slot(obj, obj->cand_focus);
}

/**
 * @brief 候选区光标左右移动：按方向逐格找下一个可用槽位，找不到原地不动。
 * @param obj 传入：控件对象指针。
 * @param dir 传入：-1 向左 / +1 向右。
 * @return 无。
 */
static void _ime_cand_move(we_ime_pinyin_obj_t *obj, int8_t dir)
{
    int8_t slot = (int8_t)(obj->cand_focus + dir);

    while (slot >= 0 && slot <= WE_IME_PINYIN_PAGE_CAP + 1)
    {
        if (_ime_cand_slot_enabled(obj, slot))
        {
            _ime_invalidate_slot(obj, obj->cand_focus);
            obj->cand_focus = slot;
            _ime_invalidate_slot(obj, slot);
            return;
        }
        slot = (int8_t)(slot + dir);
    }
}

/**
 * @brief 翻页/上屏后光标就近落回可用槽位（页面已无候选则退回键盘区）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 只修值不标脏：本函数仅跟在 page/select 之后调用，候选栏横带
 *       已被它们整体标脏；选字清空缓冲的场景由候选刷新钩子先行退区。
 */
static void _ime_cand_clamp_cursor(we_ime_pinyin_obj_t *obj)
{
    int8_t slot = obj->cand_focus;

    if (slot < 0)
        return;
    if (obj->page_cnt == 0U)
    {
        _ime_cand_zone_exit(obj);
        return;
    }
    if (_ime_cand_slot_enabled(obj, slot))
        return;
    if (slot == 0)
        obj->cand_focus = 1; /* "<" 变禁用：落到首个候选 */
    else
        obj->cand_focus = (int8_t)obj->page_cnt; /* 候选变少 / ">" 变禁用：落到末个候选 */
}
#endif /* WE_CFG_ENABLE_KEY_INPUT && WE_KEYBOARD_USE_KEY */

/**
 * @brief 从当前游标填充一页候选（经字库缺字过滤），并预探是否还有后页。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 进入时 obj->iter 指向本页起点；返回时 obj->iter 停在本页末尾
 *       之后（即下一页起点），obj->page_start 记录本页起点供回翻压栈。
 *       预探用迭代器副本，不动真游标；副本会跳过后续所有缺字字形，
 *       最坏 O(区间剩余候选数)（preview 放宽，只发生在翻页/重填时）。
 */
static void _ime_fill_page(we_ime_pinyin_obj_t *obj)
{
    uint16_t cp;

    obj->page_start = obj->iter;
    obj->page_cnt = 0U;

    while (obj->page_cnt < WE_IME_PINYIN_PAGE_CAP)
    {
        if (!we_pinyin_iter_next(&obj->iter, &cp))
            break;
        if (_ime_glyph_ok(obj, cp))
            obj->page_cp[obj->page_cnt++] = cp; /* 字库没有的字直接跳过 */
    }

    obj->has_more = 0U;
    {
        we_pinyin_iter_t peek = obj->iter; /* 副本预探：真游标保持在下一页起点 */

        while (we_pinyin_iter_next(&peek, &cp))
        {
            if (_ime_glyph_ok(obj, cp))
            {
                obj->has_more = 1U;
                break;
            }
        }
    }
}

/**
 * @brief 按当前拼音缓冲重建候选状态（区间检索 + 回到第一页）。
 * @param obj 传入：控件对象指针。
 * @return 无。
 * @note 缓冲空 = 空闲态（区间清零、候选清空）；检索失败 = 无候选态
 *       （候选栏画灰字提示）。两条横带整体标脏。
 */
static void _ime_refresh_candidates(we_ime_pinyin_obj_t *obj)
{
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
    _ime_cand_zone_exit(obj); /* 候选整体重建：键控光标退回键盘区 */
#endif
    obj->back_depth = 0U;
    obj->pressed = 0U;
    obj->press_slot = -1;

    if (obj->pylen == 0U)
    {
        obj->range_first = 0U;
        obj->range_count = 0U;
        we_pinyin_iter_init(&obj->iter, 0U, 0U);
        obj->page_start = obj->iter;
        obj->page_cnt = 0U;
        obj->has_more = 0U;
    }
    else
    {
        uint16_t first = 0U;
        uint16_t count = 0U;

        (void)we_pinyin_match(obj->pybuf, &first, &count); /* 精确命中音节按字典序必在区间首 */
        obj->range_first = first;
        obj->range_count = count;
        we_pinyin_iter_init(&obj->iter, first, count);
        _ime_fill_page(obj);
    }

    _ime_invalidate_buf_bar(obj);
    _ime_invalidate_cand_bar(obj);
}

/**
 * @brief 把一个候选码点转 UTF-8 并经 commit 回调上屏，然后清空缓冲。
 * @param obj 传入：控件对象指针。
 * @param cp 传入：候选 Unicode 码点。
 * @return 无。
 */
static void _ime_commit_cp(we_ime_pinyin_obj_t *obj, uint16_t cp)
{
    if (we_pinyin_cp_to_utf8(cp, obj->commit_utf8) == 0U)
        return;
    _ime_emit(obj, obj->commit_utf8);

    obj->pylen = 0U;
    obj->pybuf[0] = '\0';
    _ime_refresh_candidates(obj);
}

/**
 * @brief 键值统一入口：键盘 key_cb 与 inject_key 都汇到这里。
 * @param obj 传入：控件对象指针。
 * @param key 传入：键面字符串。
 * @return 无。
 * @note 路由规则见头文件注释；只有"键盘处于小写页时的单个小写字母"
 *       才进拼音缓冲，其余键按缓冲空/非空决定透传或忽略。
 */
static void _ime_handle_key(we_ime_pinyin_obj_t *obj, const char *key)
{
    if (key == NULL || key[0] == '\0')
        return;

    /* 退格：缓冲非空删字母，缓冲空透传给宿主 */
    if (key[0] == '\b' && key[1] == '\0')
    {
        if (obj->pylen > 0U)
        {
            obj->pylen--;
            obj->pybuf[obj->pylen] = '\0';
            _ime_refresh_candidates(obj);
        }
        else
        {
            _ime_emit(obj, "\b");
        }
        return;
    }

    /* 空格：缓冲非空 = 选当前页第 1 候选，缓冲空透传 */
    if (key[0] == ' ' && key[1] == '\0')
    {
        if (obj->pylen > 0U)
        {
            if (obj->page_cnt > 0U)
                _ime_commit_cp(obj, obj->page_cp[0]);
            /* 无候选（非法音节）时忽略：等用户退格修正 */
        }
        else
        {
            _ime_emit(obj, " ");
        }
        return;
    }

    /* 小写字母且键盘处于小写页：进拼音缓冲（上限 7，满则忽略） */
    if (key[0] >= 'a' && key[0] <= 'z' && key[1] == '\0' &&
        obj->kb.page == WE_KEYBOARD_PAGE_LOWER)
    {
        if (obj->pylen < WE_IME_PINYIN_BUF_MAX)
        {
            obj->pybuf[obj->pylen++] = key[0];
            obj->pybuf[obj->pylen] = '\0';
            _ime_refresh_candidates(obj);
        }
        return;
    }

    /* 其余键值（数字/符号/大写字母页）：中英直通，仅缓冲空时透传 */
    if (obj->pylen == 0U)
        _ime_emit(obj, key);
    /* 缓冲非空时忽略（preview 放宽，见头文件） */
}

/**
 * @brief 内嵌键盘键值回调：由键盘对象指针反推面板对象后汇入统一入口。
 * @param kb 传入：内嵌键盘对象指针（即 &obj->kb）。
 * @param key 传入：键面字符串。
 * @return 无。
 */
static void _ime_kb_key_cb(void *kb, const char *key)
{
    we_ime_pinyin_obj_t *obj =
        (we_ime_pinyin_obj_t *)((char *)kb - offsetof(we_ime_pinyin_obj_t, kb));

    _ime_handle_key(obj, key);
}

/* --------------------------------------------------------------------------
 * 绘图回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 在槽位矩形中水平/垂直居中绘制一小段文本（按有效像素区 bbox 垂直居中）。
 * @param obj 传入：控件对象指针。
 * @param str 传入：NUL 结尾 UTF-8 文本。
 * @param sx 传入：槽位左上角 X。
 * @param sy 传入：槽位左上角 Y。
 * @param sw 传入：槽位宽度。
 * @param sh 传入：槽位高度。
 * @param color 传入：文字颜色。
 * @return 无。
 */
static void _ime_draw_centered(we_ime_pinyin_obj_t *obj, const char *str,
                               int16_t sx, int16_t sy, int16_t sw, int16_t sh, colour_t color)
{
    we_lcd_t *lcd = obj->base.lcd;
    uint16_t txt_w = we_get_text_width(obj->font, str);
    int8_t y_top;
    int8_t y_bot;
    int16_t tx;
    int16_t ty;

    we_get_text_bbox(obj->font, str, &y_top, &y_bot);
    tx = (int16_t)(sx + sw / 2 - (int16_t)(txt_w / 2U));
    ty = (int16_t)(sy + sh / 2 - (y_top + y_bot) / 2);
    we_draw_string(lcd, tx, ty, obj->font, str, color, obj->opacity);
}

/**
 * @brief 控件绘制回调，向当前 PFB 输出拼音条与候选栏。
 * @param ptr 回调透传对象指针。
 * @return 无。
 * @note 键盘区由内嵌键盘对象自绘（链表在面板之后，绘制在其上），
 *       这里只画上面两条横带；越出 PFB 的写入由原语裁剪丢弃。
 */
static void _ime_pinyin_draw_cb(void *ptr)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;
    int16_t bars_y = _ime_bars_y(obj);
    int16_t bar_y = (int16_t)(bars_y + WE_IME_PINYIN_BUF_H);
    int8_t slot;

    if (obj->opacity == 0U)
        return;

    /* 0. 弹层模式回显条：镜像目标输入框内容（keyboard 回显条同款） */
    if (obj->popup_mode && WE_KEYBOARD_ECHO_H > 0)
    {
        int16_t pad_x = 8;
        int16_t inner_x = (int16_t)(obj->base.x + pad_x);
        int16_t inner_w = (int16_t)(obj->base.w - 2 * pad_x);
        int16_t avail_w = (int16_t)(inner_w - 2);

        we_fill_rect(lcd, obj->base.x, obj->base.y, (uint16_t)obj->base.w,
                     (uint16_t)WE_KEYBOARD_ECHO_H, obj->bg_color, obj->opacity);

        if (_ime_target(obj) != NULL && obj->font != NULL && avail_w > 0)
        {
            const char *text = we_textarea_get_text((const we_textarea_obj_t *)obj->target);
            we_area_t old_pfb_area = lcd->pfb_area;
            uint16_t old_y_start = lcd->pfb_y_start;
            uint16_t old_y_end = lcd->pfb_y_end;
            colour_t *old_gram = lcd->pfb_gram;
            int16_t new_x0 = WE_MAX(old_pfb_area.x0, inner_x);
            int16_t new_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
            int16_t new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(inner_x + inner_w - 1));
            int16_t new_y1 = WE_MIN((int16_t)old_y_end,
                                    (int16_t)(obj->base.y + WE_KEYBOARD_ECHO_H - 1));

            if (new_x0 <= new_x1 && new_y0 <= new_y1)
            {
                uint16_t line_h = we_font_get_line_height(obj->font);
                int16_t text_y = (int16_t)(obj->base.y + ((int16_t)WE_KEYBOARD_ECHO_H -
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
                we_fill_rect(lcd, cursor_x, (int16_t)(obj->base.y + 4), 2U,
                             (uint16_t)(WE_KEYBOARD_ECHO_H - 8), obj->press_color,
                             obj->opacity);
            }

            lcd->pfb_area = old_pfb_area;
            lcd->pfb_y_start = old_y_start;
            lcd->pfb_y_end = old_y_end;
            lcd->pfb_gram = old_gram;
        }
    }

    /* 1. 拼音条底 + 候选栏底 */
    we_fill_rect(lcd, obj->base.x, bars_y,
                 (uint16_t)obj->base.w, (uint16_t)WE_IME_PINYIN_BUF_H,
                 obj->buf_color, obj->opacity);
    we_fill_rect(lcd, obj->base.x, bar_y,
                 (uint16_t)obj->base.w, (uint16_t)WE_IME_PINYIN_CAND_H,
                 obj->bg_color, obj->opacity);

    if (obj->font == NULL)
        return;

    /* 2. 拼音条：已敲字母左对齐；空闲时给灰字提示 */
    {
        const char *txt = (obj->pylen > 0U) ? obj->pybuf : "pinyin";
        colour_t c = (obj->pylen > 0U) ? obj->text_color : obj->hint_color;
        int8_t y_top;
        int8_t y_bot;
        int16_t ty;

        we_get_text_bbox(obj->font, txt, &y_top, &y_bot);
        ty = (int16_t)(bars_y + WE_IME_PINYIN_BUF_H / 2 - (y_top + y_bot) / 2);
        we_draw_string(lcd, (int16_t)(obj->base.x + 8), ty, obj->font, txt, c, obj->opacity);
    }

    /* 3. 候选栏 */
    if (obj->pylen > 0U && obj->page_cnt == 0U)
    {
        /* 非法音节 / 全部缺字：整栏灰字提示 */
        _ime_draw_centered(obj, _ime_hint_no_cand, obj->base.x, bar_y,
                           obj->base.w, WE_IME_PINYIN_CAND_H, obj->hint_color);
        return;
    }

    for (slot = 0; slot <= WE_IME_PINYIN_PAGE_CAP + 1; slot++)
    {
        int16_t sx;
        int16_t sy;
        int16_t sw;
        int16_t sh;
        const char *label = NULL;
        char cand_utf8[4];
        colour_t color = obj->text_color;
        uint8_t enabled = 1U;

        if (!_ime_slot_rect(obj, slot, &sx, &sy, &sw, &sh))
            continue;

        if (slot == 0)
        {
            label = "<";
            enabled = (obj->back_depth > 0U) ? 1U : 0U;
        }
        else if (slot == WE_IME_PINYIN_PAGE_CAP + 1)
        {
            label = ">";
            enabled = obj->has_more;
        }
        else if ((uint8_t)(slot - 1) < obj->page_cnt)
        {
            (void)we_pinyin_cp_to_utf8(obj->page_cp[slot - 1], cand_utf8);
            label = cand_utf8;
        }

        if (label == NULL)
            continue; /* 空候选格：不画 */

        /* 按压高亮底（圆角小块） */
        if (obj->pressed && slot == obj->press_slot && enabled)
        {
            uint16_t r = 4U;

            we_draw_round_rect_analytic_fill(lcd, (int16_t)(sx + 1), (int16_t)(sy + 2),
                                             (uint16_t)(sw - 2), (uint16_t)(sh - 4),
                                             r, obj->press_color, obj->opacity);
        }
        if (!enabled)
            color = obj->hint_color;

        _ime_draw_centered(obj, label, sx, sy, sw, sh, color);

#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
        /* 候选区键控光标环：槽位内缘 2px 描边（聚焦导航色，键环同色系） */
        if (slot == obj->cand_focus)
        {
            colour_t rc = RGB888TODEV(WE_CFG_FOCUS_CURSOR_R, WE_CFG_FOCUS_CURSOR_G,
                                      WE_CFG_FOCUS_CURSOR_B);

            we_fill_rect(lcd, sx, sy, (uint16_t)sw, 2U, rc, obj->opacity);
            we_fill_rect(lcd, sx, (int16_t)(sy + sh - 2), (uint16_t)sw, 2U, rc, obj->opacity);
            we_fill_rect(lcd, sx, sy, 2U, (uint16_t)sh, rc, obj->opacity);
            we_fill_rect(lcd, (int16_t)(sx + sw - 2), sy, 2U, (uint16_t)sh, rc, obj->opacity);
        }
#endif
    }
}

/* --------------------------------------------------------------------------
 * 事件回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件事件回调：候选栏按压/拖出/点击状态机（btnmatrix 同源）。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 = 事件已消费，0 = 穿透（仅完全透明时）。
 * @note 面板（拼音条 + 候选栏）矩形内的触摸一律消费；键盘区的触摸
 *       由链表更靠后的内嵌键盘对象拦截，不会派发到这里。
 */
static uint8_t _ime_panel_event_body(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)ptr;
    int8_t slot;

    if (obj->opacity == 0U)
        return 0U;

    switch (event)
    {
    case WE_EVENT_PRESSED:
        slot = _ime_hit_slot(obj, data->x, data->y);
        if (slot >= 0)
        {
            obj->press_slot = slot;
            obj->pressed = 1U;
            _ime_invalidate_slot(obj, slot);
        }
        else
        {
            obj->press_slot = -1;
            obj->pressed = 0U;
        }
        return 1U;

    case WE_EVENT_STAY:
        if (obj->press_slot >= 0 &&
            _ime_hit_slot(obj, data->x, data->y) != obj->press_slot)
        {
            /* 拖出原槽位：取消按压态，本次触摸不再产生点击 */
            if (obj->pressed)
            {
                obj->pressed = 0U;
                _ime_invalidate_slot(obj, obj->press_slot);
            }
            obj->press_slot = -1;
        }
        return 1U;

    case WE_EVENT_RELEASED:
        if (obj->pressed)
        {
            obj->pressed = 0U;
            _ime_invalidate_slot(obj, obj->press_slot);
        }
        return 1U;

    case WE_EVENT_CLICKED:
        slot = _ime_hit_slot(obj, data->x, data->y);
        if (slot >= 0 && slot == obj->press_slot)
        {
            obj->press_slot = -1;
            if (slot == 0)
                (void)we_ime_pinyin_page(obj, -1);
            else if (slot == WE_IME_PINYIN_PAGE_CAP + 1)
                (void)we_ime_pinyin_page(obj, 1);
            else
                (void)we_ime_pinyin_select(obj, (uint8_t)(slot - 1));
        }
        else
        {
            obj->press_slot = -1;
        }
        return 1U;

    default:
        break;
    }
    return 1U; /* 面板矩形内其余事件（SWIPE 等）也不穿透 */
}

static void _ime_popup_close_cb(void *owner);
static uint8_t _ime_popup_event(void *owner, we_event_t event, we_indev_data_t *data);
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
static uint8_t _ime_popup_key_cb(void *owner, uint8_t code);
#endif
static void _ime_popup_draw(void *owner);
void we_ime_pinyin_popup_hide(we_ime_pinyin_obj_t *obj);

/**
 * @brief 统一事件入口：弹层模式承接模态语义（MODAL_CLOSE/键直送/区域路由），
 *        普通模式直通面板处理体。
 */
static uint8_t _ime_pinyin_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)ptr;

    if (obj->popup_mode)
    {
        if (event == WE_EVENT_MODAL_CLOSE)
        {
            we_obj_invalidate((we_obj_t *)obj);
            we_obj_detach((we_obj_t *)obj);
            _ime_popup_close_cb(obj);
            return 1U;
        }
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
        if ((uint8_t)event >= WE_KEY_UP)
            return _ime_popup_key_cb(obj, (uint8_t)event);
#endif
        return _ime_popup_event(obj, event, data);
    }
    return _ime_panel_event_body(ptr, event, data);
}

/**
 * @brief 统一绘制入口：弹层模式画全套（面板三条 + 内嵌键盘），普通模式画面板。
 */
static void _ime_class_draw_cb(void *ptr)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)ptr;

    if (obj->popup_mode)
    {
        _ime_popup_draw(obj);
        return;
    }
    _ime_pinyin_draw_cb(ptr);
}

static const we_class_t _ime_pinyin_class = {
    .draw_cb = _ime_class_draw_cb,
    .event_cb = _ime_pinyin_event_cb,
    .set_pos_cb = NULL /* preview 限制：不支持移动（内嵌键盘坐标不跟随） */
};

/* --------------------------------------------------------------------------
 * 弹层模式：滑入/收回状态机 + 弹层回调（keyboard 弹层同款结构）
 *
 * 面板与内嵌键盘都不挂普通对象链表，由 LCD 弹层统一承载：弹层 draw
 * 依次画回显条/拼音条/候选栏（面板 draw_cb）与内嵌键盘（经类分发），
 * 弹层 event 按"本次触摸按下的区域"路由到键盘或面板的事件机。
 * -------------------------------------------------------------------------- */

#define _IME_SLIDE_HIDDEN 0U
#define _IME_SLIDE_IN     1U
#define _IME_SLIDE_SHOWN  2U
#define _IME_SLIDE_OUT    3U



/**
 * @brief 按 slide_q8 计算面板 y（自屏底升起），内嵌键盘坐标同步跟随。
 * @param obj 传入：控件对象指针。
 * @return 无。
 */
static void _ime_slide_apply(we_ime_pinyin_obj_t *obj)
{
    we_lcd_t *lcd = obj->base.lcd;
    int16_t ny = (int16_t)((int32_t)lcd->height -
                           ((int32_t)obj->base.h * (int32_t)obj->slide_q8) / 256);

    /* 顶层对象：面板位移经 we_obj_set_pos（旧/新整区标脏，覆盖内嵌键盘），
     * 内嵌键盘坐标随后同步（kb 不在链上，由面板 draw 分派绘制）。 */
    we_obj_set_pos((we_obj_t *)obj, obj->base.x, ny);
    obj->kb.base.y = (int16_t)(_ime_bars_y(obj) + WE_IME_PINYIN_BUF_H +
                               WE_IME_PINYIN_CAND_H);
}

/**
 * @brief 滑动动画步进：推进 Q8 进度并落位；滑出到底释放弹层。
 * @param owner 传入：控件对象指针（we_anim_t.owner 透传）。
 * @param elapsed_ms 传入：本次步进经过的毫秒数。
 * @return 无。
 */
static void _ime_slide_step_cb(void *owner, uint16_t elapsed_ms)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)owner;
    int32_t step;

    if (obj == NULL || elapsed_ms == 0U)
        return;

    step = ((int32_t)elapsed_ms * 256) / (int32_t)WE_KEYBOARD_ANIM_MS;
    if (step < 1)
        step = 1;

    if (obj->slide_state == _IME_SLIDE_IN)
    {
        int32_t q = (int32_t)obj->slide_q8 + step;

        if (q >= 256)
        {
            q = 256;
            obj->slide_state = _IME_SLIDE_SHOWN;
            we_anim_stop(obj->base.lcd, &obj->anim);
        }
        obj->slide_q8 = (uint16_t)q;
        _ime_slide_apply(obj);
    }
    else if (obj->slide_state == _IME_SLIDE_OUT)
    {
        int32_t q = (int32_t)obj->slide_q8 - step;

        if (q <= 0)
        {
            obj->slide_q8 = 0U;
            _ime_slide_apply(obj);
            we_anim_stop(obj->base.lcd, &obj->anim);
            we_modal_close(obj->base.lcd, (we_obj_t *)obj);
            we_obj_detach((we_obj_t *)obj); /* 已滑至屏外，摘链无需再标脏 */
            _ime_popup_close_cb(obj);       /* 复位为 HIDDEN */
            return;
        }
        obj->slide_q8 = (uint16_t)q;
        _ime_slide_apply(obj);
    }
    else
    {
        we_anim_stop(obj->base.lcd, &obj->anim);
    }
}

/**
 * @brief 弹层关闭回调：正常滑出或被其他弹层替换时统一复位状态。
 * @param owner 传入：控件对象指针。
 * @return 无。
 */
static void _ime_popup_close_cb(void *owner)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)owner;

    we_anim_stop(obj->base.lcd, &obj->anim);
    obj->slide_state = _IME_SLIDE_HIDDEN;
    obj->slide_q8 = 0U;
    obj->base.y = (int16_t)obj->base.lcd->height;
    obj->kb.base.y = (int16_t)obj->base.lcd->height;
    obj->pressed = 0U;
    obj->press_slot = -1;
#if (WE_CFG_ENABLE_KEY_INPUT == 1) && (WE_KEYBOARD_USE_KEY == 1)
    if (obj->cand_focus >= 0) /* 候选区光标激活中被关闭：恢复键盘键光标 */
    {
        obj->cand_focus = -1;
        obj->kb.focus_idx = (obj->cand_kb_idx >= 0) ? obj->cand_kb_idx : 0;
        obj->cand_kb_idx = -1;
    }
#endif
    obj->kb.pressed = 0U;
    obj->kb.press_idx = -1;
    obj->popup_press_kb = 0U;
    if (_ime_target(obj) != NULL) /* 弹层关闭 = 目标退出编辑态（光标熄灭停表） */
        we_textarea_set_editing((we_textarea_obj_t *)obj->target, 0U);
}

/**
 * @brief 弹层事件回调：面板上方按下 = 收回；按下区域决定后续事件路由。
 * @param owner 传入：控件对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 恒为 1（模态弹层吞掉全部输入）。
 */
static uint8_t _ime_popup_event(void *owner, we_event_t event, we_indev_data_t *data)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)owner;

    if (obj->slide_state == _IME_SLIDE_OUT || obj->slide_state == _IME_SLIDE_HIDDEN)
        return 1U;
    if (event == WE_EVENT_PRESSED && data != NULL)
    {
        if (data->y < obj->base.y)
        {
            we_ime_pinyin_popup_hide(obj); /* 点面板外部 = 收回 */
            return 1U;
        }
        obj->popup_press_kb = (data->y >= obj->kb.base.y) ? 1U : 0U;
    }

    /* 按"按下时的区域"路由整个触摸序列，避免跨区拖动后状态错乱 */
    if (obj->popup_press_kb)
    {
        if (obj->kb.base.class_p != NULL && obj->kb.base.class_p->event_cb != NULL)
            return obj->kb.base.class_p->event_cb(&obj->kb, event, data);
        return 1U;
    }
    return _ime_panel_event_body(obj, event, data);
}

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
#if (WE_KEYBOARD_USE_KEY == 1)
/**
 * @brief 候选区键控光标处理：上探进入、左右巡航、OK 双沿激活、下/BACK 回退。
 * @param obj 传入：控件对象指针。
 * @param code 传入：语义键编码（松开沿带 WE_KEY_RELEASE_FLAG）。
 * @return 1 = 已消费（含上探进入判定命中），0 = 交键盘网格导航。
 * @note 光标在键盘区时仅拦"顶行再按上且有候选"的上探沿；候选区激活
 *       期间吞掉全部按键（含松开沿），保证与键盘 OK 双沿互不串扰。
 */
static uint8_t _ime_cand_key(we_ime_pinyin_obj_t *obj, uint8_t code)
{
    uint8_t key = (uint8_t)(code & (uint8_t)~WE_KEY_RELEASE_FLAG);
    uint8_t release = ((code & WE_KEY_RELEASE_FLAG) != 0U) ? 1U : 0U;

    if (obj->cand_focus < 0)
    {
        /* 键盘区：顶行再按"上"且当前页有候选 → 光标上探进候选栏 */
        if (!release && key == WE_KEY_UP && obj->page_cnt > 0U &&
            we_keyboard_focus_row(&obj->kb) == 0)
        {
            _ime_cand_zone_enter(obj);
            return 1U;
        }
        return 0U;
    }

    if (release)
    {
        /* OK 松开沿：确认激活光标槽位（无按下沿配对时忽略） */
        if (key == WE_KEY_OK && obj->pressed && obj->press_slot == obj->cand_focus)
        {
            int8_t slot = obj->press_slot;

            obj->pressed = 0U;
            obj->press_slot = -1;
            _ime_invalidate_slot(obj, slot);
            if (slot == 0)
                (void)we_ime_pinyin_page(obj, -1);
            else if (slot == WE_IME_PINYIN_PAGE_CAP + 1)
                (void)we_ime_pinyin_page(obj, 1);
            else
                (void)we_ime_pinyin_select(obj, (uint8_t)(slot - 1));
            _ime_cand_clamp_cursor(obj); /* 选字清缓冲时已在刷新钩子里退区 */
        }
        return 1U;
    }

    switch (key)
    {
    case WE_KEY_LEFT:
    case WE_KEY_PREV:
        _ime_cand_move(obj, -1);
        return 1U;
    case WE_KEY_RIGHT:
    case WE_KEY_NEXT:
        _ime_cand_move(obj, 1);
        return 1U;
    case WE_KEY_OK: /* 按下沿：光标槽位进入按压高亮，松开沿触发 */
        obj->press_slot = obj->cand_focus;
        obj->pressed = 1U;
        _ime_invalidate_slot(obj, obj->cand_focus);
        return 1U;
    case WE_KEY_DOWN:
    case WE_KEY_BACK: /* 回键盘区（BACK 在键盘区才收回弹层） */
        _ime_cand_zone_exit(obj);
        return 1U;
    default:
        return 1U; /* 候选区吞掉其余键（UP 已在顶部无操作） */
    }
}
#endif /* WE_KEYBOARD_USE_KEY */

/**
 * @brief 模态键通道回调：候选区光标优先拦截，其余转发键盘键光标导航，
 *        BACK（键盘区）收回。
 * @param owner 传入：控件对象指针。
 * @param code 传入：语义键编码（松开沿带 WE_KEY_RELEASE_FLAG）。
 * @return 恒为 1（模态弹层吞掉全部按键）。
 */
static uint8_t _ime_popup_key_cb(void *owner, uint8_t code)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)owner;

    if (obj->slide_state == _IME_SLIDE_OUT || obj->slide_state == _IME_SLIDE_HIDDEN)
        return 1U;
#if (WE_KEYBOARD_USE_KEY == 1)
    if (_ime_cand_key(obj, code))
        return 1U;
    if (!we_keyboard_key_nav(&obj->kb, code))
        we_ime_pinyin_popup_hide(obj); /* BACK = 收回 */
#else
    if ((code & WE_KEY_RELEASE_FLAG) == 0U && code == WE_KEY_BACK)
        we_ime_pinyin_popup_hide(obj);
#endif
    return 1U;
}
#endif /* WE_CFG_ENABLE_KEY_INPUT */

/**
 * @brief 弹层绘制回调：回显条/拼音条/候选栏 + 内嵌键盘（经类分发）。
 * @param owner 传入：控件对象指针。
 * @return 无。
 */
static void _ime_popup_draw(void *owner)
{
    we_ime_pinyin_obj_t *obj = (we_ime_pinyin_obj_t *)owner;

    _ime_pinyin_draw_cb(obj);
    if (obj->kb.base.class_p != NULL && obj->kb.base.class_p->draw_cb != NULL)
        obj->kb.base.class_p->draw_cb(&obj->kb);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化拼音输入法面板并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 面板左上角 X（屏幕绝对坐标）。
 * @param y 面板左上角 Y。
 * @param w 面板宽度（像素）。
 * @param h 面板总高度（像素，含内嵌键盘区）。
 * @param font 字库指针（候选栏渲染中文用）。
 * @return 无。
 */
void we_ime_pinyin_obj_init(we_ime_pinyin_obj_t *obj, we_lcd_t *lcd,
                            int16_t x, int16_t y, int16_t w, int16_t h,
                            const unsigned char *font)
{
    int16_t kb_y;
    int16_t kb_h;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_ime_pinyin_class;
    obj->base.parent = NULL;
    obj->base.next = NULL;

    if (font == NULL)
        return; /* 字体必传 */
    obj->font = font;
    obj->commit_cb = NULL;

    obj->pybuf[0] = '\0';
    obj->pylen = 0U;
    obj->range_first = 0U;
    obj->range_count = 0U;
    we_pinyin_iter_init(&obj->iter, 0U, 0U);
    obj->page_start = obj->iter;
    obj->back_depth = 0U;
    obj->page_cnt = 0U;
    obj->has_more = 0U;
    obj->press_slot = -1;
    obj->pressed = 0U;
    obj->cand_focus = -1;
    obj->cand_kb_idx = -1;
    obj->commit_utf8[0] = '\0';

    obj->bg_color = RGB888TODEV(26, 32, 44);   /* 与内嵌键盘面板同色，视觉连成一体 */
    obj->buf_color = RGB888TODEV(36, 44, 58);
    obj->text_color = RGB888TODEV(236, 241, 248);
    obj->press_color = RGB888TODEV(64, 152, 231);
    _ime_update_hint_color(obj);
    obj->opacity = 255U;

    obj->target = NULL;
    obj->popup_mode = 0U;
    obj->slide_state = _IME_SLIDE_HIDDEN;
    obj->slide_q8 = 0U;
    obj->popup_press_kb = 0U;
    obj->anim.next = NULL;
    obj->anim.step_cb = NULL;
    obj->anim.owner = NULL;

    /* 面板本体先挂链（先绘制、后命中），键盘随后挂链（后绘制、先命中） */
    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    kb_y = (int16_t)(y + WE_IME_PINYIN_BUF_H + WE_IME_PINYIN_CAND_H);
    kb_h = (int16_t)(h - WE_IME_PINYIN_BUF_H - WE_IME_PINYIN_CAND_H);
    if (kb_h < 0)
        kb_h = 0;
    we_keyboard_obj_init(&obj->kb, lcd, x, kb_y, w, kb_h, obj->font);
    we_keyboard_set_key_cb(&obj->kb, _ime_kb_key_cb);

    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 初始化弹层模式拼音输入法（面板与内嵌键盘均不挂对象链表）。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param h 面板总高（含回显条 + 拼音条 + 候选栏 + 键盘区）。
 * @param font 字库指针（须含中文字形；必传）。
 * @return 无。
 */
void we_ime_pinyin_popup_init(we_ime_pinyin_obj_t *obj, we_lcd_t *lcd,
                              int16_t h, const unsigned char *font)
{
    int16_t kb_h;

    if (obj == NULL || lcd == NULL || font == NULL || h <= 0)
        return;

    we_ime_pinyin_obj_init(obj, lcd, 0, (int16_t)lcd->height,
                           (int16_t)lcd->width, h, font);
    if (obj->base.class_p == NULL)
        return; /* 底层 init 被参数守卫拒绝 */

    /* 弹层承载：把面板与内嵌键盘从普通链表摘下（不清对象字段） */
    we_obj_detach((we_obj_t *)obj);
    we_obj_detach((we_obj_t *)&obj->kb);

    obj->popup_mode = 1U;

    /* 弹层几何：键盘区高度扣掉回显条，坐标全部按隐藏位摆放 */
    kb_h = (int16_t)(h - WE_KEYBOARD_ECHO_H - WE_IME_PINYIN_BUF_H - WE_IME_PINYIN_CAND_H);
    if (kb_h < 0)
        kb_h = 0;
    obj->kb.base.h = kb_h;
    obj->kb.base.y = (int16_t)lcd->height;
}

/**
 * @brief 弹出拼音输入法：占用 LCD 弹层并从屏底滑入。
 * @param obj 控件对象指针（须为 popup_init 创建）。
 * @param target_textarea 绑定的目标输入框（we_textarea_obj_t*，可 NULL）。
 * @return 无。
 */
void we_ime_pinyin_popup_show(we_ime_pinyin_obj_t *obj, void *target_textarea)
{
    we_lcd_t *lcd;

    if (obj == NULL || obj->base.lcd == NULL || obj->popup_mode == 0U)
        return;
    lcd = obj->base.lcd;

    /* 编辑态交接：旧目标熄灭光标，新目标进入编辑态开始闪烁 */
    if (_ime_target(obj) != NULL && obj->target != target_textarea)
        we_textarea_set_editing((we_textarea_obj_t *)obj->target, 0U);
    obj->target = target_textarea;
    if (target_textarea != NULL)
        we_textarea_set_editing((we_textarea_obj_t *)target_textarea, 1U);

    if (obj->slide_state == _IME_SLIDE_SHOWN)
        return;

    if (obj->slide_state == _IME_SLIDE_HIDDEN)
    {
        /* 首次弹出：面板对象挂顶层链并声明模态（内嵌键盘不单独挂链，
         * 由面板 draw 分派绘制、触摸按区域路由）。滑出中反向 re-show
         * 时仍在链上/仍是模态，无需重复挂接。 */
        we_obj_attach_to_top(lcd, (we_obj_t *)obj);
        we_modal_open(lcd, (we_obj_t *)obj);
    }

    obj->slide_state = _IME_SLIDE_IN;
    we_anim_start(lcd, &obj->anim, _ime_slide_step_cb, obj);
}

/**
 * @brief 收回拼音输入法：滑出到屏外后释放 LCD 弹层。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_ime_pinyin_popup_hide(we_ime_pinyin_obj_t *obj)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->popup_mode == 0U)
        return;
    if (obj->slide_state == _IME_SLIDE_HIDDEN || obj->slide_state == _IME_SLIDE_OUT)
        return;
    if (_ime_target(obj) != NULL) /* 收回即退出编辑态（光标立即熄灭） */
        we_textarea_set_editing((we_textarea_obj_t *)obj->target, 0U);
    obj->slide_state = _IME_SLIDE_OUT;
    we_anim_start(obj->base.lcd, &obj->anim, _ime_slide_step_cb, obj);
}

/**
 * @brief 输入法呼出 thunk：bind_textarea 的内部 summon 回调。
 * @param editor 传入：绑定的输入法面板对象指针。
 * @param ta 传入：触发呼出的输入框对象指针。
 * @return 无。
 */
static void _ime_summon(void *editor, void *ta)
{
    we_ime_pinyin_popup_show((we_ime_pinyin_obj_t *)editor, ta);
}

/**
 * @brief 把弹层输入法绑定到输入框：点击输入框（或聚焦后按 OK）呼出。
 * @param ta 输入框对象指针（we_textarea_obj_t*）。
 * @param ime 弹层输入法对象指针（须为 popup_init 创建；NULL 解绑）。
 * @return 无。
 * @note 与 we_textarea_bind_keyboard 同一挂点，后绑者生效；
 *       单例输入法可绑定多个输入框，呼出时以触发框为注入目标。
 */
void we_ime_pinyin_bind_textarea(void *ta, we_ime_pinyin_obj_t *ime)
{
    we_textarea_bind_editor((we_textarea_obj_t *)ta, ime,
                            (ime != NULL) ? _ime_summon : NULL);
}

/**
 * @brief 注册 commit 上屏回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_ime_pinyin_set_commit_cb(we_ime_pinyin_obj_t *obj, we_ime_pinyin_commit_cb_t cb)
{
    if (obj == NULL)
        return;
    obj->commit_cb = cb;
}

/**
 * @brief 注入一个键值（与内嵌键盘 key_cb 完全同一入口）。
 * @param obj 控件对象指针。
 * @param key 键面字符串。
 * @return 无。
 */
void we_ime_pinyin_inject_key(we_ime_pinyin_obj_t *obj, const char *key)
{
    if (obj == NULL)
        return;
    _ime_handle_key(obj, key);
}

/**
 * @brief 选中当前候选页第 slot 个字并 commit 上屏。
 * @param obj 控件对象指针。
 * @param slot 候选格序号（0 .. page_cnt-1）。
 * @return 1 = 已上屏，0 = 槽位为空/越界。
 */
uint8_t we_ime_pinyin_select(we_ime_pinyin_obj_t *obj, uint8_t slot)
{
    if (obj == NULL || slot >= obj->page_cnt)
        return 0U;

    _ime_commit_cp(obj, obj->page_cp[slot]);
    return 1U;
}

/**
 * @brief 候选栏翻页（>0 向后，<0 回翻）。
 * @param obj 控件对象指针。
 * @param dir 翻页方向。
 * @return 1 = 翻动成功，0 = 已到边界。
 */
uint8_t we_ime_pinyin_page(we_ime_pinyin_obj_t *obj, int8_t dir)
{
    if (obj == NULL || dir == 0)
        return 0U;

    if (dir > 0)
    {
        if (!obj->has_more)
            return 0U;

        /* 本页起点压栈（满则挤掉最旧一页） */
        if (obj->back_depth >= WE_IME_PINYIN_BACK_MAX)
        {
            memmove(&obj->back_stack[0], &obj->back_stack[1],
                    sizeof(obj->back_stack[0]) * (WE_IME_PINYIN_BACK_MAX - 1U));
            obj->back_depth = WE_IME_PINYIN_BACK_MAX - 1U;
        }
        obj->back_stack[obj->back_depth++] = obj->page_start;
        /* obj->iter 已停在下一页起点，直接填充 */
        _ime_fill_page(obj);
    }
    else
    {
        if (obj->back_depth == 0U)
            return 0U;

        obj->iter = obj->back_stack[--obj->back_depth];
        _ime_fill_page(obj);
    }

    _ime_invalidate_cand_bar(obj);
    return 1U;
}

/**
 * @brief 运行期开/关二级字候选并刷新候选栏（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param enable 1 = 候选含二级字，0 = 只出一级字。
 * @return 无。
 */
void we_ime_pinyin_set_l2(we_ime_pinyin_obj_t *obj, uint8_t enable)
{
    if (obj == NULL)
        return;

    enable = (enable != 0U) ? 1U : 0U;
    if (we_pinyin_get_l2() == enable)
        return;

    we_pinyin_set_l2(enable);
    if (we_pinyin_get_l2() != enable)
        return; /* 引擎未编译二级支持：空操作 */

    if (obj->pylen > 0U)
    {
        /* 开关改变候选全集：区间不变，回到第一页重填 */
        we_pinyin_iter_init(&obj->iter, obj->range_first, obj->range_count);
        obj->back_depth = 0U;
        _ime_fill_page(obj);
        _ime_invalidate_cand_bar(obj);
    }
}

/**
 * @brief 设置四项配色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param bg 面板底色（候选栏背景）。
 * @param buf_bg 拼音条底色。
 * @param text 文字色。
 * @param press 按压高亮色。
 * @return 无。
 */
void we_ime_pinyin_set_colors(we_ime_pinyin_obj_t *obj, colour_t bg,
                              colour_t buf_bg, colour_t text, colour_t press)
{
    if (obj == NULL)
        return;
    if (_ime_colour_eq(obj->bg_color, bg) &&
        _ime_colour_eq(obj->buf_color, buf_bg) &&
        _ime_colour_eq(obj->text_color, text) &&
        _ime_colour_eq(obj->press_color, press))
        return;

    obj->bg_color = bg;
    obj->buf_color = buf_bg;
    obj->text_color = text;
    obj->press_color = press;
    _ime_update_hint_color(obj);
    _ime_invalidate_buf_bar(obj);
    _ime_invalidate_cand_bar(obj);
}

/**
 * @brief 设置整体不透明度并按需重绘（同步设置内嵌键盘）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_ime_pinyin_set_opacity(we_ime_pinyin_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;

    obj->opacity = opacity;
    we_keyboard_set_opacity(&obj->kb, opacity);
    _ime_invalidate_buf_bar(obj);
    _ime_invalidate_cand_bar(obj);
}

/**
 * @brief 删除控件：弹层模式先收弹层摘滑动节点，再删内嵌键盘与面板本体。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_ime_pinyin_obj_delete(we_ime_pinyin_obj_t *obj)
{
    if (obj == NULL)
        return;

    if (obj->base.lcd != NULL)
        we_anim_stop(obj->base.lcd, &obj->anim);
    we_keyboard_obj_delete(&obj->kb);
    /* 弹层态的模态引用与顶层摘链由 we_obj_delete 的内核回收统一完成 */
    we_obj_delete((we_obj_t *)obj);
}
