#ifndef __WE_WIDGET_INDICATOR_H
#define __WE_WIDGET_INDICATOR_H

#include "we_gui_driver.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * 圆形状态指示灯控件
 *
 * 一盏圆形指示灯，在“熄灭/点亮”两态之间做颜色亮灭过渡，可选外发光晕。
 * 典型用途：电源/连接/告警等状态指示。默认只读（由代码 set_state 驱动），
 * 也可开启点击翻转。
 *
 * 复用框架既有能力：
 *   - we_draw_round_rect_analytic_fill(d, d, r=d/2) 退化为抗锯齿圆；
 *   - 每对象 GUI task + we_lerp + we_ease_* 推进动画，时长运行时可配；
 *   - 光晕由同心半透明圆叠加构成，全部落在 base box 内，不会漏刷脏矩形。
 * -------------------------------------------------------------------------- */

/* 是否启用亮灭过渡动画的编译期总开关（运行时仍可单独关闭某盏灯） */
#ifndef WE_INDICATOR_USE_ANIM
#define WE_INDICATOR_USE_ANIM 1
#endif

/* 默认动画时长（毫秒），可通过 we_indicator_set_anim() 运行时修改 */
#ifndef WE_INDICATOR_ANIM_MS
#define WE_INDICATOR_ANIM_MS 250U
#endif

/* 光晕相对核心圆的额外半径占比（256=1.0）。核心圆按 (256-该值) 收缩，
 * 余下外圈留给光晕，保证光晕始终在 base box 内绘制。 */
#ifndef WE_INDICATOR_GLOW_RATIO
#define WE_INDICATOR_GLOW_RATIO 80U
#endif

/* 光晕在最亮时的峰值透明度（0~255） */
#ifndef WE_INDICATOR_GLOW_ALPHA
#define WE_INDICATOR_GLOW_ALPHA 120U
#endif

/* 默认点亮色 / 熄灭色（包含本头文件前可用宏覆盖） */
#ifndef WE_INDICATOR_ON_R
#define WE_INDICATOR_ON_R   52
#endif
#ifndef WE_INDICATOR_ON_G
#define WE_INDICATOR_ON_G  199
#endif
#ifndef WE_INDICATOR_ON_B
#define WE_INDICATOR_ON_B   89
#endif
#ifndef WE_INDICATOR_OFF_R
#define WE_INDICATOR_OFF_R  60
#endif
#ifndef WE_INDICATOR_OFF_G
#define WE_INDICATOR_OFF_G  60
#endif
#ifndef WE_INDICATOR_OFF_B
#define WE_INDICATOR_OFF_B  66
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef uint8_t (*we_indicator_event_cb_t)(void *obj, we_event_t event,
                                           we_indev_data_t *data);

typedef struct we_indicator_obj_t
{
    we_obj_t base;
    colour_t on_color;        /* 点亮色 */
    colour_t off_color;       /* 熄灭色 */
    we_indicator_event_cb_t user_event_cb; /* 非 NULL 时接管全部事件 */
    we_ease_fn_t ease;        /* 缓动函数，默认 we_ease_in_out_sine */
    uint16_t anim_ms;         /* 动画时长（毫秒） */
    uint16_t anim_acc_ms;     /* 已累计动画时间 */
    uint16_t progress;        /* 当前视觉进度，0..256（Q8） */
    we_anim_t anim;           /* 中央动画引擎节点（不占 GUI task 槽） */
    uint8_t  state;           /* 目标态：0=灭，1=亮 */
    uint8_t  opacity;         /* 整体不透明度（0~255） */
    uint8_t  pressed;         /* 当前是否被按下 */
    uint8_t  anim_enabled;    /* 0=瞬切，非0=动画过渡 */
    uint8_t  glow;            /* 0=纯圆，非0=点亮时外发光晕 */
    uint8_t  clickable;       /* 0=只读，非0=点击翻转 */
} we_indicator_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化圆形指示灯并挂载到 LCD 对象链表。
 * @param obj 指示灯控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @note 默认：熄灭、绿色点亮、带光晕、动画开启、只读不可点击。
 */
void we_indicator_obj_init(we_indicator_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, int16_t w, int16_t h);

/**
 * @brief 设置目标状态（按当前动画配置过渡或瞬切）。
 * @param obj 指示灯控件对象指针。
 * @param on 目标状态，0=灭，非0=亮。
 */
void we_indicator_set_state(we_indicator_obj_t *obj, uint8_t on);

/**
 * @brief 翻转状态（灭/亮互换）。
 * @param obj 指示灯控件对象指针。
 */
void we_indicator_toggle(we_indicator_obj_t *obj);

/**
 * @brief 查询当前目标状态。
 * @param obj 指示灯控件对象指针。
 * @return 1=亮，0=灭或 obj 为 NULL。
 */
uint8_t we_indicator_get_state(const we_indicator_obj_t *obj);

/**
 * @brief 设置点亮色与熄灭色并刷新。
 * @param obj 指示灯控件对象指针。
 * @param on_color 点亮颜色。
 * @param off_color 熄灭颜色。
 */
void we_indicator_set_colors(we_indicator_obj_t *obj, colour_t on_color,
                             colour_t off_color);

/**
 * @brief 配置动画开关与时长（速度）。
 * @param obj 指示灯控件对象指针。
 * @param enabled 0=瞬切无动画，非0=平滑过渡。
 * @param duration_ms 过渡时长（毫秒），enabled 为 0 时忽略。
 */
void we_indicator_set_anim(we_indicator_obj_t *obj, uint8_t enabled,
                           uint16_t duration_ms);

/**
 * @brief 设置缓动函数（来自 we_motion.h）。
 * @param obj 指示灯控件对象指针。
 * @param ease 缓动函数指针，NULL 时退回线性。
 */
void we_indicator_set_ease(we_indicator_obj_t *obj, we_ease_fn_t ease);

/**
 * @brief 开关外发光晕。
 * @param obj 指示灯控件对象指针。
 * @param enable 0=纯圆，非0=点亮时显示光晕。
 */
void we_indicator_set_glow(we_indicator_obj_t *obj, uint8_t enable);

/**
 * @brief 设置是否允许点击翻转状态。
 * @param obj 指示灯控件对象指针。
 * @param clickable 0=只读，非0=点击翻转。
 */
void we_indicator_set_clickable(we_indicator_obj_t *obj, uint8_t clickable);

/**
 * @brief 设置自定义事件回调（非 NULL 时接管全部输入事件）。
 * @param obj 指示灯控件对象指针。
 * @param cb 事件回调，NULL 时恢复内建行为。
 */
void we_indicator_set_event_cb(we_indicator_obj_t *obj,
                               we_indicator_event_cb_t cb);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 指示灯控件对象指针。
 * @param opacity 不透明度（0~255）。
 */
void we_indicator_set_opacity(we_indicator_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除指示灯控件并从对象链表/任务系统移除。
 * @param obj 指示灯控件对象指针。
 */
void we_indicator_obj_delete(we_indicator_obj_t *obj);

#endif /* __WE_WIDGET_INDICATOR_H */
