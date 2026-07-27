#include "we_widget_chart_bar.h"
#include "we_render.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * chart_bar —— 柱状图（preview 孵化区）
 *
 * 数据模型：values[WE_CHART_BAR_MAX] 环形缓冲 + head 指针（独立实现，
 * 思路同 chart 控件的环形推值）。display[i] = values[(head+i) 回绕]，
 * push 写入 values[head] 后 head 前进一格 —— 最新值恒在最右，旧值左移。
 * set_all 直接重置 head=0 后整帧拷入（values[0] 即最左柱）。
 *
 * 渲染全部 we_fill_rect：横向网格线（低透明）→ 柱体（不透明）→ 1px 基线。
 * 柱位布局：槽宽 = w/bar_cnt，缝隙 = 槽宽/4（最小 1），余数居中吸收，
 * 与 spectrum 控件同一套等分策略，全部整数运算。
 * -------------------------------------------------------------------------- */

/* 横向网格线透明度（低透明，不喧宾夺主） */
#define WE_CHART_BAR_GRID_ALPHA 70U
/* 底部基线透明度（比网格醒目，仍留一点底色透出） */
#define WE_CHART_BAR_BASE_ALPHA 200U

/**
 * @brief 比较两个颜色值是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 表示相等，0 表示不等。
 */
static uint8_t _chart_bar_color_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#else
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 计算柱高可用量程（总高扣除底部 1px 基线）。
 * @param obj 传入：控件对象指针。
 * @return 可用高度（像素，最小 1）。
 */
static int16_t _chart_bar_usable_h(const we_chart_bar_obj_t *obj)
{
    int16_t u = (int16_t)(obj->base.h - 1);
    return (u < 1) ? 1 : u;
}

/**
 * @brief 控件绘制回调：网格线 + 柱体 + 基线，全部 we_fill_rect。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 */
static void _chart_bar_draw_cb(void *ptr)
{
    we_chart_bar_obj_t *obj = (we_chart_bar_obj_t *)ptr;
    we_lcd_t *lcd;
    int16_t usable;
    int16_t base_y;
    int16_t slot_w;
    int16_t bar_w;
    int16_t gap;
    int16_t inset;
    uint8_t i;
    uint8_t idx;

    if (obj == NULL)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->base.w <= 0 || obj->base.h <= 0 || obj->bar_cnt == 0U)
        return;

    usable = _chart_bar_usable_h(obj);
    base_y = (int16_t)(obj->base.y + obj->base.h - 1); /* 基线所在行 */

    /* 柱位布局：控件宽度等分为 bar_cnt 个槽，槽内留缝隙，余数居中吸收 */
    slot_w = (int16_t)(obj->base.w / obj->bar_cnt);
    if (slot_w < 1)
        slot_w = 1;
    gap = (int16_t)(slot_w / 4);
    if (gap < 1 && slot_w >= 2)
        gap = 1;
    bar_w = (int16_t)(slot_w - gap);
    if (bar_w < 1)
    {
        bar_w = 1;
        gap = (int16_t)(slot_w - 1);
    }
    inset = (int16_t)((obj->base.w - slot_w * (int16_t)obj->bar_cnt) / 2);

    /* 1. 横向网格线：量程内均匀分布，低透明（0 关闭） */
    if (obj->grid_rows > 0U)
    {
        uint8_t k;

        for (k = 1U; k <= obj->grid_rows; k++)
        {
            int16_t gy = (int16_t)(base_y - (int16_t)(((int32_t)usable * k) / (obj->grid_rows + 1U)));
            we_fill_rect(lcd, obj->base.x, gy, (uint16_t)obj->base.w, 1U,
                         obj->grid_color, WE_CHART_BAR_GRID_ALPHA);
        }
    }

    /* 2. 柱体：display[i] = values[(head+i) 回绕]，比较回绕替代 %（M0 无硬件除法） */
    idx = obj->head;
    for (i = 0U; i < obj->bar_cnt; i++)
    {
        int16_t v = (int16_t)obj->values[idx];
        int16_t bx = (int16_t)(obj->base.x + inset + (int16_t)i * slot_w + gap / 2);

        idx++;
        if (idx >= obj->bar_cnt)
            idx = 0U;

        if (v > usable)
            v = usable; /* 像素高度钳到量程 */
        if (v > 0)
        {
            we_fill_rect(lcd, bx, (int16_t)(base_y - v),
                         (uint16_t)bar_w, (uint16_t)v, obj->bar_color, 255U);
        }
    }

    /* 3. 底部基线：1px 横贯整个控件 */
    we_fill_rect(lcd, obj->base.x, base_y, (uint16_t)obj->base.w, 1U,
                 obj->grid_color, WE_CHART_BAR_BASE_ALPHA);
}

/**
 * @brief 控件事件回调：装饰性控件，不消费任何输入。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 恒返回 0（穿透给背后控件）。
 */
static uint8_t _chart_bar_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

static const we_class_t _chart_bar_class = {
    .draw_cb = _chart_bar_draw_cb,
    .event_cb = _chart_bar_event_cb,
    .set_pos_cb = NULL, /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化柱状图控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标）。
 * @param y 传入：左上角 Y 坐标。
 * @param w 传入：控件宽度（像素）。
 * @param h 传入：控件高度（像素）。
 * @param bar_cnt 传入：柱数（钳制到 1..WE_CHART_BAR_MAX）。
 * @return 无。
 */
void we_chart_bar_obj_init(we_chart_bar_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           uint8_t bar_cnt)
{
    if (obj == NULL || lcd == NULL)
        return;

    if (bar_cnt == 0U)
        bar_cnt = 1U;
    if (bar_cnt > (uint8_t)WE_CHART_BAR_MAX)
        bar_cnt = (uint8_t)WE_CHART_BAR_MAX;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_chart_bar_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->bar_cnt = bar_cnt;
    obj->head = 0U;
    obj->grid_rows = 0U;
    memset(obj->values, 0, sizeof(obj->values));

    obj->bar_color = RGB888TODEV(86, 196, 244);  /* 青蓝柱色 */
    obj->grid_color = RGB888TODEV(96, 104, 118); /* 暗灰网格/基线色 */

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 推入一个新值（像素高度）：最新值进右端，整体视觉左移一格。
 * @param obj 传入：控件对象指针。
 * @param value_px 传入：柱高（像素），绘制时钳到可用高度。
 * @return 无。
 */
void we_chart_bar_push(we_chart_bar_obj_t *obj, uint8_t value_px)
{
    if (obj == NULL)
        return;

    obj->values[obj->head] = value_px;
    obj->head++;
    if (obj->head >= obj->bar_cnt) /* 比较回绕替代 % */
        obj->head = 0U;

    we_obj_invalidate((we_obj_t *)obj); /* preview：滚动本就整幅变化，整控件标脏 */
}

/**
 * @brief 整帧覆盖全部柱值（values[0] 为最左柱）。
 * @param obj 传入：控件对象指针。
 * @param values 传入：像素高度数组指针，长度至少 bar_cnt 字节。
 * @return 无。
 */
void we_chart_bar_set_all(we_chart_bar_obj_t *obj, const uint8_t *values)
{
    uint8_t i;
    uint8_t idx;

    if (obj == NULL || values == NULL)
        return;

    /* 与当前显示序列逐柱比较：整帧一致直接返回 */
    idx = obj->head;
    for (i = 0U; i < obj->bar_cnt; i++)
    {
        if (obj->values[idx] != values[i])
            break;
        idx++;
        if (idx >= obj->bar_cnt)
            idx = 0U;
    }
    if (i >= obj->bar_cnt)
        return;

    obj->head = 0U;
    memcpy(obj->values, values, obj->bar_cnt);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置柱体颜色与网格线（含基线）颜色。
 * @param obj 传入：控件对象指针。
 * @param bar 传入：柱体颜色。
 * @param grid 传入：网格线 + 基线颜色。
 * @return 无。
 */
void we_chart_bar_set_colors(we_chart_bar_obj_t *obj, colour_t bar, colour_t grid)
{
    if (obj == NULL)
        return;
    if (_chart_bar_color_eq(obj->bar_color, bar) &&
        _chart_bar_color_eq(obj->grid_color, grid))
        return;

    obj->bar_color = bar;
    obj->grid_color = grid;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置横向网格线数（0 = 关闭）。
 * @param obj 传入：控件对象指针。
 * @param rows 传入：网格线数，超过 WE_CHART_BAR_GRID_MAX 时钳制。
 * @return 无。
 */
void we_chart_bar_set_grid(we_chart_bar_obj_t *obj, uint8_t rows)
{
    if (obj == NULL)
        return;
    if (rows > (uint8_t)WE_CHART_BAR_GRID_MAX)
        rows = (uint8_t)WE_CHART_BAR_GRID_MAX;
    if (obj->grid_rows == rows)
        return;

    obj->grid_rows = rows;
    we_obj_invalidate((we_obj_t *)obj);
}
