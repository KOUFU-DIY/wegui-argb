#ifndef __WE_WIDGET_SCALE_H
#define __WE_WIDGET_SCALE_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 刻度尺控件（scale）—— preview 孵化区实验控件
 *
 * 一条直线刻度尺：基线 + 主刻度长线 + 小刻度短线 + 主刻度整数数值 + 指针。
 * 两种朝向：
 *   - WE_SCALE_H：水平尺，刻度朝下，数字居中于主刻度下方，
 *     指针为基线上方指向下的小三角；控件尺寸 w = len，h = WE_SCALE_H_THICKNESS。
 *   - WE_SCALE_V：垂直尺，刻度朝右，数字靠右（刻度区右侧、垂直居中对齐刻度），
 *     指针为基线左侧指向右的小三角；控件尺寸 w = WE_SCALE_V_THICKNESS，h = len。
 * 厚度全部由固定宏推导（指针高 + 基线厚 + 主刻度长 + 间隙 + 数字区），
 * 调用方只需给出轴向长度 len。
 *
 * 渲染全部使用 we_fill_rect 1px 竖/横条拼刻度（无 AA，省事且横平竖直），
 * 数字用 we_draw_string（字体经 init 传入，ASCII 数字必有字形）。
 * 指针 = WE_SCALE_PTR_LEN 行（列）递减宽度的 fill_rect 拼出的实心小三角。
 *
 * 数值模型：int32 量程 [v_min, v_max] 线性映射到轴向像素：
 *   pos = (v - v_min) * (len - 1) / (v_max - v_min)
 * 全程 int32 整数乘除。len <= 320 时要求量程跨度 |v_max - v_min| < 2^22，
 * 防止映射乘法与 we_lerp 插值的 int32 中间量溢出。
 * 垂直尺方向约定：v_min 在顶端，数值向下递增（与像素 Y 同向）。
 *
 * 指针支持两种更新：
 *   - we_scale_set_value：立即就位（打断进行中的动画）；
 *   - we_scale_anim_value：经单个中央动画节点（we_anim_t，不占 GUI timer 槽）
 *     以缓入缓出正弦从当前显示值平滑滑动到目标值。
 *
 * 零 malloc；渲染路径零浮点；文本为控件内部小栈缓冲格式化的整数。
 * 装饰性控件：event_cb 恒返回 0，输入事件穿透给背后控件。
 *
 * preview 限制（毕业前需优化项见 widget.md）：
 *   - 指针/数值变化按整控件包围盒标脏；
 *   - 主刻度数量超过 len 时（刻度密过像素）整组刻度与数字直接不画；
 *   - 垂直尺数字区宽度固定 WE_SCALE_V_TEXT_W，超宽数字（长负数）会被
 *     PFB/脏区裁掉右端。
 * -------------------------------------------------------------------------- */

/* 朝向枚举 */
#define WE_SCALE_H 0U /* 水平尺：刻度朝下 */
#define WE_SCALE_V 1U /* 垂直尺：刻度朝右 */

/* 指针三角高度（H 尺）/ 宽度（V 尺），即递减 fill_rect 的行/列数 */
#ifndef WE_SCALE_PTR_LEN
#define WE_SCALE_PTR_LEN 4
#endif

/* 基线厚度（像素） */
#ifndef WE_SCALE_LINE_W
#define WE_SCALE_LINE_W 2
#endif

/* 主刻度线长 / 小刻度线长（像素，均从基线向刻度侧延伸） */
#ifndef WE_SCALE_MAJOR_LEN
#define WE_SCALE_MAJOR_LEN 10
#endif
#ifndef WE_SCALE_MINOR_LEN
#define WE_SCALE_MINOR_LEN 5
#endif

/* 刻度区与数字区之间的间隙（像素） */
#ifndef WE_SCALE_TEXT_GAP
#define WE_SCALE_TEXT_GAP 2
#endif

/* 数字区高度（H 尺用，= 默认 ASCII16 字库行高 18） */
#ifndef WE_SCALE_TEXT_H
#define WE_SCALE_TEXT_H 18
#endif

/* 数字区宽度（V 尺用，容纳 3~4 个数字字符，如 "-20"） */
#ifndef WE_SCALE_V_TEXT_W
#define WE_SCALE_V_TEXT_W 34
#endif

/* 控件厚度（垂直于轴向的尺寸），全部由上面的固定宏推导 */
#define WE_SCALE_H_THICKNESS \
    (WE_SCALE_PTR_LEN + WE_SCALE_LINE_W + WE_SCALE_MAJOR_LEN + WE_SCALE_TEXT_GAP + WE_SCALE_TEXT_H)
#define WE_SCALE_V_THICKNESS \
    (WE_SCALE_PTR_LEN + WE_SCALE_LINE_W + WE_SCALE_MAJOR_LEN + WE_SCALE_TEXT_GAP + WE_SCALE_V_TEXT_W)

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_scale_obj_t
{
    we_obj_t base;          /* 基类，必须在首位：base.x/y/w/h 为控件包围盒 */

    uint8_t  orientation;   /* WE_SCALE_H / WE_SCALE_V */
    uint16_t len;           /* 轴向长度（像素，>= 2） */

    int32_t  v_min;         /* 量程下限 */
    int32_t  v_max;         /* 量程上限（> v_min） */
    uint16_t major_step;    /* 主刻度值间隔（> 0） */
    uint8_t  minor_div;     /* 每两个主刻度之间的小刻度条数（0 = 无小刻度） */

    colour_t line_color;    /* 基线与刻度线颜色 */
    colour_t text_color;    /* 主刻度数字颜色 */
    colour_t pointer_color; /* 指针三角颜色 */
    uint8_t  show_pointer;  /* 1 = 绘制指针，0 = 隐藏 */

    int32_t  value;         /* 业务目标值（钳制在量程内） */
    int32_t  disp_value;    /* 当前显示值（动画期间为插值中间量） */

    /* 指针滑动动画：单个中央动画引擎节点（不占 GUI timer 槽） */
    we_anim_t anim;         /* 动画节点（归控件所有，删除前必须摘链） */
    uint16_t  anim_ms;      /* 本次滑动时长（毫秒） */
    uint16_t  anim_t;       /* Q8 进度 0..256，>= 256 表示空闲 */
    int32_t   v_from;       /* 滑动起点值 */
    int32_t   v_to;         /* 滑动终点值 */
    const unsigned char *font;  /* 字体资源（init 必传） */
} we_scale_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化刻度尺控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 控件左上角 X（屏幕绝对坐标）。
 * @param y 控件左上角 Y。
 * @param len 轴向长度（像素，内部钳制下限 2；建议 <= 320）。
 * @param orientation 朝向：WE_SCALE_H（水平，刻度朝下）/ WE_SCALE_V（垂直，刻度朝右）。
 * @return 无。
 * @note 厚度由固定宏推导：H 尺 h = WE_SCALE_H_THICKNESS，V 尺 w = WE_SCALE_V_THICKNESS。
 *       默认：量程 0..100、主步 10、小分 4、指针显示、初值 = 量程下限，
 *       线色灰蓝 / 数字浅灰 / 指针亮红。
 */
void we_scale_obj_init(we_scale_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, uint16_t len, uint8_t orientation,
                        const unsigned char *font);

/**
 * @brief 设置量程 [v_min, v_max]。
 * @param obj 控件对象指针。
 * @param v_min 量程下限。
 * @param v_max 量程上限（必须大于 v_min，否则忽略本次调用）。
 * @return 无。
 * @note 当前值与显示值会重新钳制到新量程并打断进行中的动画；量程未变直接返回。
 *       跨度 |v_max - v_min| 应小于 2^22（int32 中间量防溢出，len <= 320）。
 */
void we_scale_set_range(we_scale_obj_t *obj, int32_t v_min, int32_t v_max);

/**
 * @brief 设置刻度密度。
 * @param obj 控件对象指针。
 * @param major_step 主刻度值间隔（必须 > 0，否则忽略本次调用）。
 * @param minor_div 每两个主刻度之间的小刻度条数（0 = 不画小刻度）。
 * @return 无。
 * @note 值未变直接返回。主刻度数量超过 len 时绘制阶段会整组跳过刻度与数字。
 */
void we_scale_set_ticks(we_scale_obj_t *obj, uint16_t major_step, uint8_t minor_div);

/**
 * @brief 设置三组颜色：刻度线色 / 数字文字色 / 指针色。
 * @param obj 控件对象指针。
 * @param line_color 基线与刻度线颜色。
 * @param text_color 主刻度数字颜色。
 * @param pointer_color 指针三角颜色。
 * @return 无。
 * @note 三者都未变化时直接返回。
 */
void we_scale_set_colors(we_scale_obj_t *obj, colour_t line_color,
                         colour_t text_color, colour_t pointer_color);

/**
 * @brief 立即设置指针数值并刷新（无动画；进行中的滑动会被打断并就位）。
 * @param obj 控件对象指针。
 * @param v 目标值，自动钳制到量程内。
 * @return 无。
 */
void we_scale_set_value(we_scale_obj_t *obj, int32_t v);

/**
 * @brief 指针滑动动画：显示值从当前位置平滑滑到目标值（缓入缓出正弦）。
 * @param obj 控件对象指针。
 * @param v 目标值，自动钳制到量程内。
 * @param dur_ms 滑动时长（毫秒，0 = 立即到位）。
 * @return 无。
 * @note 单 we_anim_t 节点：滑动中再次调用会以当前显示值为新起点无缝改道。
 */
void we_scale_anim_value(we_scale_obj_t *obj, int32_t v, uint16_t dur_ms);

/**
 * @brief 设置指针显示开关。
 * @param obj 控件对象指针。
 * @param show 1 = 显示指针，0 = 隐藏（非 0 值一律按 1 处理）。
 * @return 无。
 * @note 值未变直接返回。
 */
void we_scale_set_show_pointer(we_scale_obj_t *obj, uint8_t show);

/**
 * @brief 删除控件：先摘除动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_scale_obj_delete(we_scale_obj_t *obj);

#endif /* __WE_WIDGET_SCALE_H */
