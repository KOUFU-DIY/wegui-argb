#ifndef __WE_WIDGET_MARQUEE_H
#define __WE_WIDGET_MARQUEE_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 跑马灯标签控件（marquee）—— preview 孵化区实验控件
 *
 * 一条固定宽度的单行文本框：
 *   - 文本宽 <= 控件宽：静止左对齐显示，不滚动、不占动画链；
 *   - 文本宽 >  控件宽：循环滚动。draw_cb 先把 PFB 窗口收窄到自身矩形
 *     （group 同款套路），再按偏移绘制 "文本 + 间隔 + 文本开头" 两段，
 *     超出部分被窗口自动裁掉；offset 到达 文本宽+间隔 后回零，
 *     并在接缝处停留 pause_ms 再继续。
 *
 * 滚动由单个中央动画节点推进（we_anim_t，不占 GUI timer 槽）：
 *   毫像素整数累计 frac_acc += elapsed_ms * speed（int32），
 *   每凑满 1000 前进 1px，无浮点、无累计漂移。
 *
 * 文本字符串由调用方持有（控件只存 const char* 指针，不拷贝）。
 * 字体经 init 传入，可用 we_marquee_set_font 更换（仅支持
 * font2c internal 字库）；控件高度 = 字体行高 + 2*WE_MARQUEE_PAD_Y。
 * 装饰性控件：class 的 event_cb 为 NULL，完全不拦截输入。
 *
 * 毕业级优化（做法详见 widget.md"已完成的毕业优化"）：
 *   - 自建窗口化字形绘制循环（we_font_get_glyph_info +
 *     we_font_get_bitmap_info + 行对齐 alpha blit），不再走 we_draw_string
 *     全量遍历：窗口左侧完全裁掉的字形按 adv_w 游标快进跳过（零像素访问、
 *     零位图取址），越过窗口右缘立即 break，逐帧成本只与可见字形数相关；
 *   - 单行截断语义明确：绘制与测宽同口径，遇 '\n' 即止（只显示第一行）；
 *   - 文本像素宽在 init/set_text/set_font 时一次缓存（text_w），draw 零重测。
 * -------------------------------------------------------------------------- */

/* 默认滚动速度（像素/秒） */
#ifndef WE_MARQUEE_DEF_SPEED
#define WE_MARQUEE_DEF_SPEED 30U
#endif

/* 默认接缝停留时长（毫秒） */
#ifndef WE_MARQUEE_DEF_PAUSE
#define WE_MARQUEE_DEF_PAUSE 800U
#endif

/* 两段文本之间的循环间隔（像素） */
#ifndef WE_MARQUEE_GAP
#define WE_MARQUEE_GAP 40
#endif

/* 文本上下留白（像素），控件高度 = 行高 + 2*PAD */
#ifndef WE_MARQUEE_PAD_Y
#define WE_MARQUEE_PAD_Y 2
#endif

/* 速度上限（像素/秒），防止 int32 毫像素累计溢出 */
#ifndef WE_MARQUEE_SPEED_MAX
#define WE_MARQUEE_SPEED_MAX 2000U
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_marquee_obj_t
{
    we_obj_t base;             /* 必须在首位：base.x/y/w/h 为可视窗口矩形 */

    const char *text;          /* UTF-8 文本指针（调用方持有，不拷贝） */
    const unsigned char *font; /* 字库指针（init 传入，set_font 可换） */
    we_anim_t anim;            /* 滚动动画节点（归控件所有，删除前必须摘链） */
    int32_t  frac_acc;         /* 毫像素累计（elapsed_ms * speed，凑满 1000 进 1px） */

    uint16_t text_w;           /* 当前文本像素宽（init/set_text/set_font 时重测缓存） */
    uint16_t speed;            /* 滚动速度（像素/秒，1~WE_MARQUEE_SPEED_MAX） */
    uint16_t pause_ms;         /* 接缝停留时长（毫秒） */
    uint16_t pause_acc;        /* 停留阶段已累计毫秒 */
    int16_t  offset;           /* 当前滚动偏移（0..text_w+GAP-1，向左为正） */
    colour_t color;            /* 文字前景色 */

    uint8_t  opacity;          /* 整体不透明度（0~255） */
    uint8_t  scrolling : 1;    /* 1 = 动画节点已挂入中央动画链 */
    uint8_t  paused : 1;       /* 1 = 正处于接缝停留阶段 */
} we_marquee_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化跑马灯标签并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 可视窗口左上角 X（屏幕绝对坐标）。
 * @param y 可视窗口左上角 Y。
 * @param w 可视窗口宽度（像素）。
 * @param text UTF-8 文本字符串（调用方持有）。
 * @param font 字体资源指针（必传；NULL 时不执行初始化）。
 * @param color 文字前景色。
 * @return 无。
 * @note 高度自动 = 字体行高 + 2*WE_MARQUEE_PAD_Y；文本宽超过 w 时自动开始滚动。
 *       默认：速度 WE_MARQUEE_DEF_SPEED、停留 WE_MARQUEE_DEF_PAUSE、不透明。
 */
void we_marquee_obj_init(we_marquee_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w,
                         const char *text, const unsigned char *font, colour_t color);

/**
 * @brief 更换文本并重置滚动位置（回到起点、清除停留状态）。
 * @param obj 控件对象指针。
 * @param new_text 新的 UTF-8 文本字符串（调用方持有）。
 * @return 无。
 * @note 不做指针相等短路：调用方可能在原缓冲区内改写内容后重新 set。
 */
void we_marquee_set_text(we_marquee_obj_t *obj, const char *new_text);

/**
 * @brief 更换字库：重测文本宽、重置滚动位置，控件高度随行高更新。
 * @param obj 控件对象指针。
 * @param font 新字库指针（仅支持 font2c internal 字库，NULL 或行高
 *             非法时忽略本次调用）；指针未变直接返回。
 * @return 无。
 * @note 高度变化的标脏顺序：先标脏旧矩形（擦除旧高度残留），再改
 *       h/text_w，最后标脏新矩形——旧高大于新高时下沿不留残影。
 */
void we_marquee_set_font(we_marquee_obj_t *obj, const unsigned char *font);

/**
 * @brief 设置滚动速度（像素/秒）。
 * @param obj 控件对象指针。
 * @param px_per_s 速度，钳制到 1..WE_MARQUEE_SPEED_MAX；值未变直接返回。
 * @return 无。
 */
void we_marquee_set_speed(we_marquee_obj_t *obj, uint16_t px_per_s);

/**
 * @brief 设置循环接缝处的停留时长（毫秒）。
 * @param obj 控件对象指针。
 * @param ms 停留毫秒数（0 = 不停留）；值未变直接返回。
 * @return 无。
 */
void we_marquee_set_pause(we_marquee_obj_t *obj, uint16_t ms);

/**
 * @brief 设置文字颜色并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param color 新的文字前景色。
 * @return 无。
 */
void we_marquee_set_color(we_marquee_obj_t *obj, colour_t color);

/**
 * @brief 设置整体不透明度并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255；0 时停止滚动动画）。
 * @return 无。
 */
void we_marquee_set_opacity(we_marquee_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件：先摘除滚动动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_marquee_obj_delete(we_marquee_obj_t *obj);

#endif /* __WE_WIDGET_MARQUEE_H */
