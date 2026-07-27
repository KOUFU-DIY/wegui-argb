#ifndef __WE_WIDGET_MLABEL_H
#define __WE_WIDGET_MLABEL_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 多行文本标签控件（mlabel）—— preview 孵化区实验控件
 *
 * 在 x/y/w/h 固定文本框内自动折行绘制 UTF-8 文本：
 *   - 显式 '\n' 强制换行；
 *   - 英文按空格断词：记录最近空格位置，行宽超出 w 时回退到该空格断行；
 *   - 连续无空格片段（长单词 / 中文）按字符断行；
 *   - 行高 = 字体行高 + line_gap（默认 2px）；完整放不进 h 的行不画；
 *   - ellipsis 开启（默认）时，最后一行若还有剩余文本，行末预留 "..."
 *     宽度截断绘制；
 *   - 对齐方式：WE_MLABEL_LEFT（左对齐，默认）/ WE_MLABEL_CENTER（行居中）。
 *
 * 折行算法在 draw 时流式执行（逐字符 UTF-8 解码 + we_font_get_glyph_info
 * 步进宽累计），不缓存行表——preview 允许每帧重排，"行起点缓存"列入毕业项。
 * 每行内容经内部栈缓冲（WE_MLABEL_LINE_BUF 字节）拷贝出 nul 结尾片段后
 * 交给 we_draw_string 绘制；绘制期间把 PFB 窗口收窄到自身矩形
 * （marquee/group 同款套路），任何越界字形像素都被窗口裁掉。
 *
 * 文本字符串由调用方持有（控件只存 const char* 指针，不拷贝）；
 * 字体经 init 传入；零 malloc；渲染路径零浮点。
 * 装饰性控件：event_cb 恒返回 0，输入事件穿透给背后控件。
 *
 * preview 限制（毕业前需优化项见 widget.md）：
 *   - 任何变化按整控件包围盒标脏，且每帧全量重排 + 重绘；
 *   - 单行字节数超过 WE_MLABEL_LINE_BUF-1 时该行尾部被截断
 *     （280px 宽屏实际达不到）；
 *   - 单个字形步进宽超过 w 时该字符独占一行，墨迹越出部分被 PFB 窗口裁掉。
 * -------------------------------------------------------------------------- */

/* 对齐方式 */
#define WE_MLABEL_LEFT   0U /* 左对齐（默认） */
#define WE_MLABEL_CENTER 1U /* 行居中 */

/* 默认行间距（像素） */
#ifndef WE_MLABEL_DEF_LINE_GAP
#define WE_MLABEL_DEF_LINE_GAP 2U
#endif

/* 单行拷贝栈缓冲字节数（含 '\0' 与可能追加的 "..."） */
#ifndef WE_MLABEL_LINE_BUF
#define WE_MLABEL_LINE_BUF 128U
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_mlabel_obj_t
{
    we_obj_t base;             /* 基类，必须在首位：base.x/y/w/h 为文本框矩形 */

    const char *text;          /* UTF-8 文本指针（调用方持有，不拷贝） */
    const unsigned char *font; /* 字库指针（init 必传） */
    colour_t color;            /* 文字前景色 */
    uint8_t  opacity;          /* 整体不透明度（0~255） */
    uint8_t  align;            /* WE_MLABEL_LEFT / WE_MLABEL_CENTER */
    uint8_t  line_gap;         /* 行间距（像素，行高 = 字体行高 + line_gap） */
    uint8_t  ellipsis;         /* 1 = 超高截断时末行追加 "..."（默认 1） */
} we_mlabel_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化多行文本标签并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 文本框左上角 X（屏幕绝对坐标）。
 * @param y 文本框左上角 Y。
 * @param w 文本框宽度（像素）。
 * @param h 文本框高度（像素）。
 * @param text UTF-8 文本字符串（调用方持有；可为 ""，绘制为空）。
 * @return 无。
 * @note 默认：左对齐、行间距 WE_MLABEL_DEF_LINE_GAP、ellipsis 开、
 *       浅灰前景、不透明；字体经 init 传入。
 */
void we_mlabel_obj_init(we_mlabel_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        const char *text,
                        const unsigned char *font);

/**
 * @brief 更换文本（触发重排 + 重绘）。
 * @param obj 控件对象指针。
 * @param new_text 新的 UTF-8 文本字符串（调用方持有，NULL 忽略）。
 * @return 无。
 * @note 折行在 draw 时流式执行，本接口只更新指针并整框标脏。
 *       不做指针相等短路：调用方可能在原缓冲区内改写内容后重新 set。
 */
void we_mlabel_set_text(we_mlabel_obj_t *obj, const char *new_text);

/**
 * @brief 设置文字颜色并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param color 新的文字前景色。
 * @return 无。
 */
void we_mlabel_set_color(we_mlabel_obj_t *obj, colour_t color);

/**
 * @brief 设置整体不透明度并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255，0 = 完全不绘制）。
 * @return 无。
 */
void we_mlabel_set_opacity(we_mlabel_obj_t *obj, uint8_t opacity);

/**
 * @brief 设置行对齐方式并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param align WE_MLABEL_LEFT / WE_MLABEL_CENTER（非法值按 LEFT 处理）。
 * @return 无。
 */
void we_mlabel_set_align(we_mlabel_obj_t *obj, uint8_t align);

/**
 * @brief 设置行间距并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param gap_px 行间距（像素，0~255；行高 = 字体行高 + gap_px）。
 * @return 无。
 */
void we_mlabel_set_line_gap(we_mlabel_obj_t *obj, uint8_t gap_px);

/**
 * @brief 设置超高截断省略号开关并按需重绘；值未变直接返回。
 * @param obj 控件对象指针。
 * @param on 1 = 末行截断时追加 "..."，0 = 直接截断（非 0 值一律按 1 处理）。
 * @return 无。
 */
void we_mlabel_set_ellipsis(we_mlabel_obj_t *obj, uint8_t on);

/**
 * @brief 从显示链表摘除该控件并清空其对象状态（转调 we_obj_delete）。
 * @param obj 控件对象指针。
 * @return 无。
 * @note mlabel 无动画节点，无需 we_anim_stop。
 */
static inline void we_mlabel_obj_delete(we_mlabel_obj_t *obj)
{
    we_obj_delete((we_obj_t *)obj);
}

#endif /* __WE_WIDGET_MLABEL_H */
