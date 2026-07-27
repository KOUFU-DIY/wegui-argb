#ifndef __WE_WIDGET_CALENDAR_H
#define __WE_WIDGET_CALENDAR_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪日历的按键回调与可聚焦性，其余控件不受影响。
 * 键控选日依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_CALENDAR_USE_KEY
#define WE_CALENDAR_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 日历（calendar）—— preview 孵化区实验控件
 *
 * 月视图日历：标题行（"YYYY-MM" + 左右 "<" ">" 翻月热区）+ 星期表头
 * （Su Mo Tu We Th Fr Sa）+ 6 行 7 列日期网格。选中日绘制圆角高亮块，
 * "今日" 绘制胶囊描边环（与选中块同形态，仅描边不填充）。
 *
 * 万年历为纯整数算法（1583+ 公历）：
 *   - 闰年：四年一闰、百年不闰、四百年再闰；
 *   - 当月 1 日星期几：基姆拉尔森计算公式（Kim-Larsen），
 *     内部先得 0=周一，再换算为 0=周日 与表头列对齐。
 *
 * 布局完全由 w/h 等分推导：列宽 = w/7，行高 = h/8
 * （1 行标题 + 1 行表头 + 6 行日期），余数像素均分到四周留白。
 *
 * 交互：
 *   - 点击日期格：选中该日 + 触发 changed 回调（值未变不回调）；
 *   - 点击 "<" / ">" 热区（标题行左/右各 2 列宽）：翻月 + 触发回调；
 *   - 左右快速滑动（SWIPE）：等效点击 ">" / "<"；
 *   - 程序 set 接口（set_month/set_selected/prev/next_month）不触发回调；
 *   - 按键（WE_CALENDAR_USE_KEY，依赖编辑态）：OK 进出编辑态，编辑态
 *     左右键 ±1 天、上下键 ±7 天移动选中日，越过月首/月尾自动翻月落位
 *     （到达年限边界不再翻），每次变化触发 changed 回调（与触摸一致）。
 *
 * 数据全部内嵌定长字段，零 malloc；渲染内环纯整数无浮点；
 * 无动画节点，删除直接 we_calendar_obj_delete（无需 we_anim_stop）。
 *
 * preview 限制：任何状态变化按整控件包围盒标脏；当月外日期留空不显示。
 * -------------------------------------------------------------------------- */

/* 公历年份下限（1582-10 格里历改革，从 1583 起整年有效） */
#ifndef WE_CALENDAR_MIN_YEAR
#define WE_CALENDAR_MIN_YEAR 1583U
#endif

/* 年份上限（标题按 4 位数字格式化） */
#ifndef WE_CALENDAR_MAX_YEAR
#define WE_CALENDAR_MAX_YEAR 9999U
#endif

/* 今日环描边厚度（像素） */
#ifndef WE_CALENDAR_RING_W
#define WE_CALENDAR_RING_W 2U
#endif

/* 按压反馈块透明度（0~255，叠加在日期格/翻月热区上） */
#ifndef WE_CALENDAR_PRESS_OPA
#define WE_CALENDAR_PRESS_OPA 90U
#endif

/**
 * @brief 选中日期变化回调（仅用户交互触发：点日期格 / 点翻月热区 / 滑动翻月）。
 * @param cal 日历控件对象指针（we_calendar_obj_t *，以 void * 透传）。
 * @param year 新选中的年份。
 * @param month 新选中的月份（1~12）。
 * @param day 新选中的日（1~当月天数）。
 * @return 无。
 */
typedef void (*we_calendar_changed_cb_t)(void *cal, uint16_t year, uint8_t month, uint8_t day);

typedef struct we_calendar_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h 为控件外接矩形 */

    uint16_t year;              /* 当前显示年份（WE_CALENDAR_MIN_YEAR..MAX_YEAR） */
    uint8_t month;              /* 当前显示月份（1~12） */
    uint8_t sel_day;            /* 选中日（1~当月天数，始终钳制在当月内） */
    uint8_t first_wday;         /* 缓存：当月 1 日星期几（0=周日..6=周六） */
    uint8_t month_days;         /* 缓存：当月天数（28~31） */

    uint16_t today_year;        /* 今日标记年份（0 = 不显示今日环） */
    uint8_t today_month;        /* 今日标记月份 */
    uint8_t today_day;          /* 今日标记日 */

    const unsigned char *font;  /* 字体资源（init 必传） */
    colour_t title_color;       /* 标题（"YYYY-MM" 与翻月箭头）文字色 */
    colour_t weekday_color;     /* 星期表头文字色 */
    colour_t day_color;         /* 日期数字文字色 */
    colour_t sel_bg_color;      /* 选中日高亮块色 */
    colour_t today_ring_color;  /* 今日描边环色 */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */

    we_calendar_changed_cb_t changed_cb; /* 选中变化回调（可为 NULL） */

    /* --- 按压状态（内部） --- */
    int16_t press_zone;         /* 当前按压热区编码（内部使用，-1 = 无） */
    uint8_t pressed;            /* 按压反馈显示中标志 */

    char title_buf[8];          /* 标题文本缓存 "YYYY-MM" + NUL */
} we_calendar_obj_t;

/**
 * @brief 初始化日历控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素，建议 >= 140）。
 * @param h 控件高度（像素，建议 >= 128）。
 * @return 无。
 * @note 默认显示 2000-01、选中 1 日、无今日标记、深色主题配色、
 *       字体经 init 传入；随后用 we_calendar_set_month /
 *       we_calendar_set_selected / we_calendar_set_today 设置初始状态。
 */
void we_calendar_obj_init(we_calendar_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t w, int16_t h,
                        const unsigned char *font);

/**
 * @brief 设置显示的年月（程序设置，不触发回调）。
 * @param obj 控件对象指针。
 * @param year 年份，钳制到 [WE_CALENDAR_MIN_YEAR, WE_CALENDAR_MAX_YEAR]。
 * @param month 月份，钳制到 [1, 12]。
 * @return 无。
 * @note 选中日会钳制到新月份天数内；钳制后年月均未变时直接返回。
 */
void we_calendar_set_month(we_calendar_obj_t *obj, uint16_t year, uint8_t month);

/**
 * @brief 设置选中日（程序设置，不触发回调）。
 * @param obj 控件对象指针。
 * @param day 目标日，钳制到 [1, 当月天数]。
 * @return 无。
 * @note 钳制后值未变时直接返回。
 */
void we_calendar_set_selected(we_calendar_obj_t *obj, uint8_t day);

/**
 * @brief 读取当前选中的完整日期（即当前显示年月 + 选中日）。
 * @param obj 控件对象指针。
 * @param out_year 传出：选中年份（可传 NULL 忽略）。
 * @param out_month 传出：选中月份（可传 NULL 忽略）。
 * @param out_day 传出：选中日（可传 NULL 忽略）。
 * @return 无。
 */
void we_calendar_get_selected(const we_calendar_obj_t *obj,
                              uint16_t *out_year, uint8_t *out_month, uint8_t *out_day);

/**
 * @brief 翻到上一个月（程序设置，不触发回调）。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 1 月上翻自动退到上一年 12 月；到达 WE_CALENDAR_MIN_YEAR-01 后不再上翻；
 *       选中日钳制到新月份天数内。
 */
void we_calendar_prev_month(we_calendar_obj_t *obj);

/**
 * @brief 翻到下一个月（程序设置，不触发回调）。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 12 月下翻自动进到下一年 1 月；到达 WE_CALENDAR_MAX_YEAR-12 后不再下翻；
 *       选中日钳制到新月份天数内。
 */
void we_calendar_next_month(we_calendar_obj_t *obj);

/**
 * @brief 设置选中日期变化回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示不回调。
 * @return 无。
 * @note 仅用户交互（点日期格/点翻月热区/滑动翻月）触发；程序 set 接口不触发。
 */
void we_calendar_set_changed_cb(we_calendar_obj_t *obj, we_calendar_changed_cb_t cb);

/**
 * @brief 设置五项配色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param title 标题（年月 + 箭头）文字色。
 * @param weekday 星期表头文字色。
 * @param day_text 日期数字文字色。
 * @param sel_bg 选中日高亮块色。
 * @param today_ring 今日描边环色。
 * @return 无。
 */
void we_calendar_set_colors(we_calendar_obj_t *obj, colour_t title, colour_t weekday,
                            colour_t day_text, colour_t sel_bg, colour_t today_ring);

/**
 * @brief 设置"今日"标记日期（今日环仅在显示到对应年月时绘制）。
 * @param obj 控件对象指针。
 * @param year 今日年份，传 0 表示关闭今日环。
 * @param month 今日月份（1~12，非法值视为关闭）。
 * @param day 今日日（1~当月天数，非法值视为关闭）。
 * @return 无。
 */
void we_calendar_set_today(we_calendar_obj_t *obj, uint16_t year, uint8_t month, uint8_t day);

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_calendar_set_opacity(we_calendar_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除日历控件并从对象链表移除（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_calendar_obj_delete(we_calendar_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_CALENDAR_H */
