#ifndef __WE_WIDGET_BOX_H
#define __WE_WIDGET_BOX_H

#include "we_gui_driver.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * 矩形面板控件（box）
 *
 * 一块可作卡片/面板底板的矩形，支持：
 *   - 四角各自独立配置：圆角 / 切角（45°）/ 直角（半径 0），半径互不影响
 *   - 边框：厚度 + 颜色（0 = 无边框），圆角/切角处边框随轮廓走
 *   - 纯色填充 + 整体透明度
 *   - 可选：填充颜色动画 / 透明度动画（默认编译期关闭，见 WE_BOX_USE_ANIM）
 *
 * 渲染策略（M0 友好）：
 *   - 中央与四条直边全部走 we_fill_rect 快速整块填充；
 *   - 仅四个 K×K 角落方块（K = max(各角半径, 边框厚, 边框厚+内半径)）做合成：
 *     圆角带边框时用 we_mask_quarter_ring_alpha 单次 4x4 子采样同时求
 *     外/内覆盖（外减内即边框环）；切角 45° 覆盖率为精确整数解析
 *     （alpha 仅 0/128/255），按行分段整块写入，无逐像素函数调用。
 *
 * 所有 set 接口在目标值与当前值相同时直接返回，不触发重绘。
 * 默认装饰性（不消费输入，事件穿透给背后控件），需要交互时设 user_event_cb。
 * -------------------------------------------------------------------------- */

/* 动画总开关（编译期）：默认 0（关闭）——
 * 关闭时结构体省去两个 we_anim_t 节点与快照字段（省 RAM），动画推进/回调不编译
 * （省 ROM），we_box_anim_* 退化为“立即生效”的兼容桩，调用方代码无需改动。
 * 需要填充颜色/透明度平滑过渡时，在包含本头文件前定义 WE_BOX_USE_ANIM 为 1。 */
#ifndef WE_BOX_USE_ANIM
#define WE_BOX_USE_ANIM 0
#endif

/* 默认动画时长（毫秒），可在每次 we_box_anim_* 调用时单独指定覆盖 */
#ifndef WE_BOX_ANIM_MS
#define WE_BOX_ANIM_MS 300U
#endif

/* 默认圆角半径（像素，四角统一） */
#ifndef WE_BOX_DEF_RADIUS
#define WE_BOX_DEF_RADIUS 8U
#endif

/* 默认填充色（包含本头文件前可用宏覆盖） */
#ifndef WE_BOX_DEF_R
#define WE_BOX_DEF_R  38
#endif
#ifndef WE_BOX_DEF_G
#define WE_BOX_DEF_G  46
#endif
#ifndef WE_BOX_DEF_B
#define WE_BOX_DEF_B  60
#endif

/* 角落索引（与 WE_MASK_QUADRANT_xx 同序：左上/右上/左下/右下） */
typedef enum
{
    WE_BOX_LT = 0,
    WE_BOX_RT,
    WE_BOX_LB,
    WE_BOX_RB
} we_box_corner_idx_t;

/* 角落样式（半径为 0 时无论样式如何都是直角） */
typedef enum
{
    WE_BOX_CORNER_ROUND = 0, /* 圆角：四分之一圆抗锯齿 */
    WE_BOX_CORNER_CHAMFER    /* 切角：45° 直线切边 */
} we_box_corner_style_t;

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef uint8_t (*we_box_event_cb_t)(void *obj, we_event_t event,
                                     we_indev_data_t *data);

typedef struct we_box_obj_t
{
    we_obj_t base;              /* base.x/y/w/h = 矩形几何 */

    uint8_t  corner_styles;     /* 四角样式打包：每角 2bit（we_box_corner_style_t），
                                 * 位移 = 角索引*2，按 we_box_corner_idx_t 索引 */
    uint8_t  corner_r[4];       /* 各角半径/切角尺寸（像素），0 = 直角，上限 255 */
    colour_t bg_color;          /* 填充色 */
    colour_t border_color;      /* 边框色 */
    uint8_t  border_w;          /* 边框厚度（像素），0 = 无边框 */
    uint8_t  opacity;           /* 整体不透明度（0~255） */

#if WE_BOX_USE_ANIM
    we_anim_t    anim_col;      /* 填充颜色动画节点 */
    we_anim_t    anim_opa;      /* 透明度动画节点 */
    we_ease_fn_t ease;          /* 缓动（两通道共用，省 RAM） */
    uint16_t     col_ms;        /* 颜色动画时长 */
    uint16_t     opa_ms;        /* 透明度动画时长 */
    uint16_t     col_t;         /* 颜色进度 0..256（Q8），>=256 表示空闲 */
    uint16_t     opa_t;         /* 透明度进度 0..256（Q8），>=256 表示空闲 */
    colour_t     c_from;        /* 颜色起点 */
    colour_t     c_to;          /* 颜色终点 */
    uint8_t      opa_from;      /* 透明度起点 */
    uint8_t      opa_to;        /* 透明度终点 */
#endif

    we_box_event_cb_t user_event_cb; /* 非 NULL 时接管全部事件 */
} we_box_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化矩形面板控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param w 宽度（像素）。
 * @param h 高度（像素）。
 * @note 默认：四角圆角 WE_BOX_DEF_RADIUS、默认填充色、无边框、不透明、装饰性不可点击。
 */
void we_box_obj_init(we_box_obj_t *obj, we_lcd_t *lcd,
                     int16_t x, int16_t y, int16_t w, int16_t h);

/**
 * @brief 单独配置一个角落的样式与半径。
 * @param idx 角落索引（WE_BOX_LT/RT/LB/RB）。
 * @param style 圆角或切角。
 * @param r 半径/切角尺寸（像素），0 = 直角，上限 255；绘制时按宽高各半自动钳制。
 */
void we_box_set_corner(we_box_obj_t *obj, we_box_corner_idx_t idx,
                       we_box_corner_style_t style, uint16_t r);

/**
 * @brief 一键设置四角为同一半径的圆角。
 */
void we_box_set_radius(we_box_obj_t *obj, uint16_t r);

/**
 * @brief 立即设置填充色并刷新（无动画）。
 */
void we_box_set_color(we_box_obj_t *obj, colour_t color);

/**
 * @brief 设置边框厚度与颜色（width=0 关闭边框）。
 */
void we_box_set_border(we_box_obj_t *obj, colour_t color, uint8_t width);

/**
 * @brief 设置整体不透明度并按需重绘。
 */
void we_box_set_opacity(we_box_obj_t *obj, uint8_t opacity);

/**
 * @brief 移动到新位置（左上角对齐到 x,y）。
 */
void we_box_set_pos(we_box_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief 修改宽高（左上角不动）。
 */
void we_box_set_size(we_box_obj_t *obj, int16_t w, int16_t h);

/**
 * @brief 填充颜色动画：当前色平滑过渡到目标色。
 * @param dur_ms 时长（毫秒）。
 * @param ease 缓动函数（NULL 退回默认 we_ease_in_out_sine）。
 * @note WE_BOX_USE_ANIM 默认为 0，此时退化为立即设色；置 1 后与透明度动画可同时进行。
 */
void we_box_anim_color(we_box_obj_t *obj, colour_t target,
                       uint16_t dur_ms, we_ease_fn_t ease);

/**
 * @brief 透明度动画：当前不透明度平滑过渡到目标值。
 * @note WE_BOX_USE_ANIM 默认为 0，此时退化为立即设置；置 1 后与颜色动画可同时进行。
 */
void we_box_anim_opacity(we_box_obj_t *obj, uint8_t target, uint16_t dur_ms,
                         we_ease_fn_t ease);

/**
 * @brief 设置自定义事件回调（非 NULL 时接管全部输入事件）。
 */
void we_box_set_event_cb(we_box_obj_t *obj, we_box_event_cb_t cb);

/**
 * @brief 删除控件并从对象链表移除。
 * @note WE_BOX_USE_ANIM=1 时内部先 we_anim_stop（两个动画节点）再 we_obj_delete。
 */
void we_box_obj_delete(we_box_obj_t *obj);

#endif /* __WE_WIDGET_BOX_H */
