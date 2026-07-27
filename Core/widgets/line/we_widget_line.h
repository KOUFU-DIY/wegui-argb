#ifndef __WE_WIDGET_LINE_H
#define __WE_WIDGET_LINE_H

#include "we_gui_driver.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * 线段控件（line）
 *
 * 一根可配置 起点/终点、线宽、线帽、颜色 的线段，支持：
 *   - 端点动画（起止点 from→to 平滑过渡）
 *   - 整体平移 / 平移动画
 *   - 颜色 / 颜色动画
 * 几何动画与颜色动画各用一个独立的中央动画引擎节点，可同时进行、互不干扰。
 *
 * 复用框架既有能力（不新增渲染图元）：
 *   - we_draw_line(...)：Xiaolin Wu 整数 Q8 抗锯齿，已支持任意线宽与容器透明度级联；
 *   - 圆头 cap 由 we_draw_round_rect_analytic_fill 退化为抗锯齿圆，画在端点处；
 *   - we_anim_t（链入 lcd->anim_head，不占 GUI task 槽）+ we_lerp + we_ease_*。
 *
 * 默认装饰性（不消费输入），需要交互时设 user_event_cb。命中测试按包围盒粒度。
 * -------------------------------------------------------------------------- */

/* 动画总开关（编译期）：置 0 可彻底去掉动画代码与占用——
 * 结构体省去两个 we_anim_t 节点与快照字段（省 RAM），动画推进/回调不编译（省 ROM），
 * 同时 we_line_anim_* 退化为“立即生效”的兼容桩，调用方代码无需改动。 */
#ifndef WE_LINE_USE_ANIM
#define WE_LINE_USE_ANIM 1
#endif

/* 默认动画时长（毫秒），可在每次 we_line_anim_* 调用时单独指定覆盖 */
#ifndef WE_LINE_ANIM_MS
#define WE_LINE_ANIM_MS 300U
#endif

/* 默认线宽（像素） */
#ifndef WE_LINE_DEF_WIDTH
#define WE_LINE_DEF_WIDTH 3U
#endif

/* 默认线色（包含本头文件前可用宏覆盖） */
#ifndef WE_LINE_DEF_R
#define WE_LINE_DEF_R  88
#endif
#ifndef WE_LINE_DEF_G
#define WE_LINE_DEF_G 166
#endif
#ifndef WE_LINE_DEF_B
#define WE_LINE_DEF_B 240
#endif

/* 线帽样式 */
typedef enum
{
    WE_LINE_CAP_BUTT = 0, /* 平头：端点处直接截断 */
    WE_LINE_CAP_ROUND     /* 圆头：端点叠加一枚抗锯齿圆（半径=线宽/2） */
} we_line_cap_t;

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef uint8_t (*we_line_event_cb_t)(void *obj, we_event_t event,
                                      we_indev_data_t *data);

typedef struct we_line_obj_t
{
    we_obj_t base;              /* base.x/y/w/h = 线包围盒（由端点+线宽自动推导） */

    int16_t  x0, y0, x1, y1;    /* 当前端点（屏幕绝对坐标） */
    uint8_t  width;             /* 线宽（像素，>=1） */
    uint8_t  cap;               /* we_line_cap_t */
    colour_t color;             /* 线色 */
    uint8_t  opacity;           /* 整体不透明度（0~255） */

#if WE_LINE_USE_ANIM
    we_anim_t    anim_geo;      /* 几何动画节点（端点/平移） */
    we_anim_t    anim_col;      /* 颜色动画节点 */
    we_ease_fn_t ease;          /* 缓动（几何/颜色/透明度三通道共用，省 RAM） */
    uint16_t     geo_ms;        /* 几何动画时长 */
    uint16_t     col_ms;        /* 颜色动画时长 */
    uint16_t     geo_t;         /* 几何进度 0..256（Q8），>=256 表示空闲 */
    uint16_t     col_t;         /* 颜色进度 0..256（Q8），>=256 表示空闲 */
    int16_t      g_from[4];     /* 端点起点：x0,y0,x1,y1 */
    int16_t      g_to[4];       /* 端点终点：x0,y0,x1,y1 */
    colour_t     c_from;        /* 颜色起点 */
    colour_t     c_to;          /* 颜色终点 */
    we_anim_t    anim_opa;      /* 透明度动画节点 */
    uint16_t     opa_ms;        /* 透明度动画时长 */
    uint16_t     opa_t;         /* 透明度进度 0..256（Q8），>=256 表示空闲 */
    uint8_t      opa_from;      /* 透明度起点 */
    uint8_t      opa_to;        /* 透明度终点 */
#endif

    we_line_event_cb_t user_event_cb; /* 非 NULL 时接管全部事件 */
} we_line_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化线段控件并挂载到 LCD 对象链表。
 * @param obj 线段控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x0 起点 X（屏幕绝对坐标）。
 * @param y0 起点 Y。
 * @param x1 终点 X。
 * @param y1 终点 Y。
 * @note 默认：线宽 WE_LINE_DEF_WIDTH、圆头、默认蓝色、不透明、装饰性不可点击。
 */
void we_line_obj_init(we_line_obj_t *obj, we_lcd_t *lcd,
                      int16_t x0, int16_t y0, int16_t x1, int16_t y1);

/**
 * @brief 立即设置起点/终点并刷新（无动画）。
 */
void we_line_set_points(we_line_obj_t *obj, int16_t x0, int16_t y0,
                        int16_t x1, int16_t y1);

/**
 * @brief 设置线宽（像素，>=1）。
 */
void we_line_set_width(we_line_obj_t *obj, uint8_t width);

/**
 * @brief 设置线帽样式（平头/圆头）。
 */
void we_line_set_cap(we_line_obj_t *obj, we_line_cap_t cap);

/**
 * @brief 立即设置线色并刷新（无动画）。
 */
void we_line_set_color(we_line_obj_t *obj, colour_t color);

/**
 * @brief 设置整体不透明度并按需重绘。
 */
void we_line_set_opacity(we_line_obj_t *obj, uint8_t opacity);

/**
 * @brief 立即整体平移（两端同偏移，无动画）。
 */
void we_line_move(we_line_obj_t *obj, int16_t dx, int16_t dy);

/**
 * @brief 端点动画：当前端点平滑过渡到目标端点。
 * @param dur_ms 时长（毫秒）。
 * @param ease 缓动函数（NULL 退回默认 we_ease_in_out_sine）。
 * @note WE_LINE_USE_ANIM=0 时退化为立即设置目标端点。
 */
void we_line_anim_points(we_line_obj_t *obj, int16_t x0, int16_t y0,
                         int16_t x1, int16_t y1, uint16_t dur_ms,
                         we_ease_fn_t ease);

/**
 * @brief 平移动画：整体平滑平移 (dx,dy)。
 * @note WE_LINE_USE_ANIM=0 时退化为立即平移。
 */
void we_line_anim_move(we_line_obj_t *obj, int16_t dx, int16_t dy,
                       uint16_t dur_ms, we_ease_fn_t ease);

/**
 * @brief 颜色动画：当前颜色平滑过渡到目标颜色。
 * @note WE_LINE_USE_ANIM=0 时退化为立即设色。与几何动画可同时进行。
 */
void we_line_anim_color(we_line_obj_t *obj, colour_t target,
                        uint16_t dur_ms, we_ease_fn_t ease);

/**
 * @brief 透明度动画：当前不透明度平滑过渡到目标值。
 * @note WE_LINE_USE_ANIM=0 时退化为立即设置。与几何/颜色动画可同时进行。
 */
void we_line_anim_opacity(we_line_obj_t *obj, uint8_t target, uint16_t dur_ms,
                          we_ease_fn_t ease);

/**
 * @brief 设置自定义事件回调（非 NULL 时接管全部输入事件）。
 */
void we_line_set_event_cb(we_line_obj_t *obj, we_line_event_cb_t cb);

/**
 * @brief 删除线段控件并从对象链表/动画系统移除。
 * @note 内部先 we_anim_stop（两个动画节点）再 we_obj_delete。
 */
void we_line_obj_delete(we_line_obj_t *obj);

#endif /* __WE_WIDGET_LINE_H */
