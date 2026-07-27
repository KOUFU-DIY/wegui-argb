#ifndef __WE_WIDGET_GAUGE_H
#define __WE_WIDGET_GAUGE_H

#include "we_gui_driver.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * 仪表盘控件（gauge）—— preview 孵化区实验控件
 *
 * 表盘内切于 base.x/y/w/h 的 w×h 区域：中心 = 矩形中心，
 * 外半径 R = min(w,h)/2 - 2（留 2px 抗锯齿羽化余量；
 * min(w,h) < WE_GAUGE_SMALL_SIZE 时按护栏进一步钳缩，见下）。
 *
 * 组成（全部复用现有绘图原语，不新增渲染图元）：
 *   - 主刻度：沿弧线均布的短线段，外端在 R、内端向心收 tick_len，
 *     we_draw_line_round 绘制（圆头小线段，端点偏移缓存于结构体）；
 *   - 指针：中心指向 0.72R 的圆头粗线（we_draw_line_round）；
 *   - 中心圆帽：we_draw_round_rect_analytic_fill 退化为实心抗锯齿圆
 *     （w = h = 直径，radius = 半径）。
 *
 * 角度统一 512 步制（0..511 = 一圈，90° = 128，用 WE_DEG() 换算），
 * 0 = +X 方向，屏幕 Y 轴向下，角度增大即视觉顺时针。
 * 默认 start = WE_DEG(135)、sweep = WE_DEG(270)：经典"开口朝下"表盘，
 * 顺时针从 135° 扫到 405°。
 *
 * 数值模型：int32 量程 [v_min, v_max] 线性映射到扫角。
 *   - we_gauge_set_value：立即就位（打断进行中的扫动）；
 *   - we_gauge_anim_value：经单个中央动画节点（we_anim_t，不占 timer 槽）
 *     平滑扫动，缓动函数可用 we_gauge_set_ease 配置（默认缓入缓出正弦）。
 *
 * 全程整数运算（Q15 三角 / Q8 进度插值），零 malloc、渲染路径零浮点。
 * 所有 setter 值未变时直接返回不重绘。
 * 默认装饰性：event_cb 返回 0，输入事件穿透给背后控件。
 *
 * 毕业级优化（做法详见 widget.md"已完成的毕业优化"）：
 *   - 指针差分标脏：数值变化只标"旧指针位形 + 新指针位形"两块包围盒
 *     （各自并入中心帽并钳到控件框，两块独立提交不合成大盒），
 *     静态刻度区零重绘；set_colors/set_range/set_tick_count/set_opacity
 *     等结构性变化仍整控件标脏。
 *   - 刻度几何缓存：主刻度内外端点相对表盘中心的偏移在 init/set_range/
 *     set_tick_count 时一次算好（上限 WE_GAUGE_TICK_MAX 条），draw 内
 *     零三角函数、零乘除；偏移相对中心存储，控件移动无需重算。
 *   - Q16 量程斜率：set_range 时预除 slope_q16，draw/anim 内只乘 + 移位；
 *     span ≤ 65536 时误差 ≤ 1 角度步且正向扫角端点精确落位，
 *     更大跨度自动回退除法保精度。
 *   - 极小表盘护栏：min(w,h) < WE_GAUGE_SMALL_SIZE 时按最外元素 AA 晕圈
 *     钳缩外半径，抗锯齿羽化与差分脏块均不越出控件包围盒。
 *
 * 量程限制：跨度 |v_max - v_min| 需小于 2^22，防止 we_lerp 插值的
 * int32 中间量溢出（角度映射本身对任意 span 无溢出，见 .c 内注释）。
 * -------------------------------------------------------------------------- */

/* 默认起始角 / 扫角（512 步制，包含本头文件前可用宏覆盖） */
#ifndef WE_GAUGE_DEF_START
#define WE_GAUGE_DEF_START WE_DEG(135)
#endif
#ifndef WE_GAUGE_DEF_SWEEP
#define WE_GAUGE_DEF_SWEEP WE_DEG(270)
#endif

/* 默认主刻度条数（建议 9~13）/ 刻度线长 / 刻度线宽（像素） */
#ifndef WE_GAUGE_DEF_TICK_CNT
#define WE_GAUGE_DEF_TICK_CNT 11U
#endif
#ifndef WE_GAUGE_DEF_TICK_LEN
#define WE_GAUGE_DEF_TICK_LEN 9U
#endif
#ifndef WE_GAUGE_TICK_W
#define WE_GAUGE_TICK_W 2U
#endif

/* 主刻度几何缓存上限（条）：tick_cnt 在 init/set_tick_count 写入口
 * 被钳制到该值。每条缓存 4 个 int16（内/外端点相对中心偏移），
 * 16 条共 128 字节 RAM。 */
#ifndef WE_GAUGE_TICK_MAX
#define WE_GAUGE_TICK_MAX 16U
#endif

/* 极小表盘护栏阈值（像素）：min(w,h) 低于该值时按最外元素的
 * AA 晕圈（线宽/2 + 2px 羽化）钳缩外半径，保证抗锯齿不越出包围盒。 */
#ifndef WE_GAUGE_SMALL_SIZE
#define WE_GAUGE_SMALL_SIZE 40
#endif

/* 默认指针线宽 / 中心圆帽直径（像素） */
#ifndef WE_GAUGE_DEF_PTR_W
#define WE_GAUGE_DEF_PTR_W 4U
#endif
#ifndef WE_GAUGE_DEF_CAP_W
#define WE_GAUGE_DEF_CAP_W 10U
#endif

/* 指针长度占外半径比例（Q8：184/256 ≈ 0.72） */
#ifndef WE_GAUGE_PTR_LEN_Q8
#define WE_GAUGE_PTR_LEN_Q8 184
#endif

/* 默认刻度色（灰蓝）与指针色（亮红） */
#ifndef WE_GAUGE_TICK_R
#define WE_GAUGE_TICK_R 148
#endif
#ifndef WE_GAUGE_TICK_G
#define WE_GAUGE_TICK_G 162
#endif
#ifndef WE_GAUGE_TICK_B
#define WE_GAUGE_TICK_B 184
#endif
#ifndef WE_GAUGE_PTR_R
#define WE_GAUGE_PTR_R 255
#endif
#ifndef WE_GAUGE_PTR_G
#define WE_GAUGE_PTR_G 96
#endif
#ifndef WE_GAUGE_PTR_B
#define WE_GAUGE_PTR_B 84
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_gauge_obj_t
{
    we_obj_t base;             /* 基类，必须在首位：base.x/y/w/h 为表盘外接矩形 */

    int16_t  start_angle;      /* 起始角（512 步制，对应量程下限） */
    int16_t  sweep;            /* 扫过角（512 步制，正值顺时针，对应量程上限） */
    int32_t  v_min;            /* 量程下限 */
    int32_t  v_max;            /* 量程上限（> v_min） */
    int32_t  value;            /* 业务目标值（钳制在量程内） */
    int32_t  disp_value;       /* 当前显示值（扫动期间为插值中间量） */

    uint8_t  tick_cnt;         /* 主刻度条数（0 = 不画刻度） */
    uint8_t  tick_len;         /* 刻度线长（像素，从外沿向心） */
    colour_t tick_color;       /* 刻度颜色 */
    colour_t pointer_color;    /* 指针颜色（中心帽同色） */
    uint8_t  pointer_w;        /* 指针线宽（>= 1） */
    uint8_t  cap_w;            /* 中心圆帽直径（像素） */
    uint8_t  opacity;          /* 整体不透明度（0~255） */

    /* 量程映射 Q16 预除斜率：value→角度 只乘 + 移位（见 _gauge_update_slope） */
    int32_t  slope_q16;        /* (sweep << 16) / span；span > 65536 时为 0（回退除法） */

    /* 主刻度几何缓存：端点相对表盘中心的偏移（init/set_range/set_tick_count
     * 时重算，相对中心存储 → 控件移动无需重算；draw 内零 we_cos/we_sin。
     * tick_cnt 在写入口被钳制 <= WE_GAUGE_TICK_MAX，缓存条数恒等于 tick_cnt） */
    int16_t  tick_ox[WE_GAUGE_TICK_MAX];   /* 外端点 X 偏移 */
    int16_t  tick_oy[WE_GAUGE_TICK_MAX];   /* 外端点 Y 偏移 */
    int16_t  tick_ix[WE_GAUGE_TICK_MAX];   /* 内端点 X 偏移 */
    int16_t  tick_iy[WE_GAUGE_TICK_MAX];   /* 内端点 Y 偏移 */

    /* 扫动动画：单个中央动画引擎节点（不占 GUI timer 槽） */
    we_anim_t    anim;         /* 动画节点（归控件所有，删除前必须摘链） */
    we_ease_fn_t ease;         /* 缓动函数（NULL 时按线性推进） */
    uint16_t     anim_ms;      /* 本次扫动时长（毫秒） */
    uint16_t     anim_t;       /* Q8 进度 0..256，>= 256 表示空闲 */
    int32_t      v_from;       /* 扫动起点值 */
    int32_t      v_to;         /* 扫动终点值 */
} we_gauge_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化仪表盘控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 外接矩形左上角 X（屏幕绝对坐标）。
 * @param y 外接矩形左上角 Y。
 * @param w 外接矩形宽度（像素）。
 * @param h 外接矩形高度（像素，表盘内切于 min(w,h) 正方形）。
 * @return 无。
 * @note 默认：量程 0..100、start = WE_DEG(135)、sweep = WE_DEG(270)、
 *       11 条刻度、4px 亮红指针、10px 中心帽、不透明、装饰性不可点击、
 *       初值 = 量程下限。
 */
void we_gauge_obj_init(we_gauge_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, int16_t w, int16_t h);

/**
 * @brief 设置量程 [v_min, v_max]。
 * @param obj 控件对象指针。
 * @param v_min 量程下限。
 * @param v_max 量程上限（必须大于 v_min，否则忽略本次调用）。
 * @return 无。
 * @note 当前值与显示值会重新钳制到新量程；量程未变时直接返回。
 *       内部重算 Q16 映射斜率（结构性变化，整控件标脏）。
 *       跨度 |v_max - v_min| 应小于 2^22（we_lerp int32 中间量防溢出）。
 */
void we_gauge_set_range(we_gauge_obj_t *obj, int32_t v_min, int32_t v_max);

/**
 * @brief 立即设置数值并刷新（无动画；进行中的扫动会被打断并就位）。
 * @param obj 控件对象指针。
 * @param value 目标值，自动钳制到量程内。
 * @return 无。
 */
void we_gauge_set_value(we_gauge_obj_t *obj, int32_t value);

/**
 * @brief 扫动动画：显示值从当前位置平滑扫到目标值。
 * @param obj 控件对象指针。
 * @param target 目标值，自动钳制到量程内。
 * @param dur_ms 扫动时长（毫秒，0 = 立即到位）。
 * @return 无。
 * @note 单 we_anim_t 节点：扫动中再次调用会以当前显示值为新起点重新扫动；
 *       缓动函数用 we_gauge_set_ease 配置。
 */
void we_gauge_anim_value(we_gauge_obj_t *obj, int32_t target, uint16_t dur_ms);

/**
 * @brief 读取业务目标值（扫动进行中返回的是扫动终点值）。
 * @param obj 控件对象指针。
 * @return 目标值；obj 为 NULL 时返回 0。
 */
int32_t we_gauge_get_value(const we_gauge_obj_t *obj);

/**
 * @brief 读取当前显示值（扫动进行中为插值中间量，适合驱动数字联动显示）。
 * @param obj 控件对象指针。
 * @return 显示值；obj 为 NULL 时返回 0。
 */
int32_t we_gauge_get_disp_value(const we_gauge_obj_t *obj);

/**
 * @brief 设置刻度颜色与指针颜色（中心帽随指针同色）。
 * @param obj 控件对象指针。
 * @param tick_color 刻度颜色。
 * @param pointer_color 指针颜色。
 * @return 无。
 */
void we_gauge_set_colors(we_gauge_obj_t *obj, colour_t tick_color,
                         colour_t pointer_color);

/**
 * @brief 设置主刻度条数。
 * @param obj 控件对象指针。
 * @param count 刻度条数（0 = 不画刻度，1 = 画在扫角中点，建议 9~13）。
 * @return 无。
 * @note 内部重建刻度几何缓存；超过 WE_GAUGE_TICK_MAX 的条数被钳制。
 */
void we_gauge_set_tick_count(we_gauge_obj_t *obj, uint8_t count);

/**
 * @brief 设置扫动动画缓动函数（来自 we_motion.h，t ∈ [0, 256]）。
 * @param obj 控件对象指针。
 * @param ease 缓动函数指针，NULL 时退回线性。
 * @return 无。
 */
void we_gauge_set_ease(we_gauge_obj_t *obj, we_ease_fn_t ease);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_gauge_set_opacity(we_gauge_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件：先摘除动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_gauge_obj_delete(we_gauge_obj_t *obj);

#endif /* __WE_WIDGET_GAUGE_H */
