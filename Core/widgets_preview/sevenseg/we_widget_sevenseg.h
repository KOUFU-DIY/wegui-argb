#ifndef __WE_WIDGET_SEVENSEG_H
#define __WE_WIDGET_SEVENSEG_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 七段数码管控件（sevenseg）—— preview 孵化区实验控件
 *
 * 用矩形段模拟经典七段数码管（a~g 直角段，preview 允许不做斜切/圆角），
 * 不吃字库：适合大字号时钟、计数器、仪表读数等场景。
 *
 * 布局模型：
 *   - 单字高 digit_h、位数 digit_cnt（含冒号位）在 init 一次性确定；
 *   - 段厚 t = digit_h/8（最小 2），单字宽 = digit_h/2，字间距 = t，
 *     总宽 = digit_cnt*digit_w + (digit_cnt-1)*gap，自动写入 base.w/h；
 *   - 每个字符占用同宽单元格（':' 也占一格，画上下两个 t×t 小方点）。
 *
 * 文本模型：
 *   - 支持 '0'~'9'、'-'、':'、' '；其余字符按空格处理；
 *   - 调用方持有字符串，控件只存指针（零拷贝）；
 *   - 控件内部另存一份定长快照用于"内容变才重绘"判定，
 *     内容未变的 set_text 不触发任何重绘。
 *
 * 鬼影（ghost）：
 *   - 打开后灭段以 off_color 低透明度绘制，模拟真实数码管的暗段底纹。
 *
 * 全程整数运算，零 malloc、零浮点；渲染只用 we_fill_rect（自带 PFB 裁剪
 * 与容器透明度级联）。默认装饰性（不消费输入，事件穿透给背后控件）。
 *
 * preview 限制：
 *   - 标脏按整控件包围盒（未做"只重绘变化的段"的精细脏矩形）；
 *   - 段为纯直角矩形，无 45° 斜切帽、无抗锯齿。
 * -------------------------------------------------------------------------- */

/* 文本快照上限（字符数，含冒号位；digit_cnt 会被钳到该值） */
#ifndef WE_SEVENSEG_MAX_CHARS
#define WE_SEVENSEG_MAX_CHARS 16
#endif

/* 鬼影段透明度（0~255，会再与控件整体 opacity 相乘） */
#ifndef WE_SEVENSEG_GHOST_OPA
#define WE_SEVENSEG_GHOST_OPA 70U
#endif

/* 默认亮段色（青绿 LED 风格） */
#ifndef WE_SEVENSEG_DEF_ON_R
#define WE_SEVENSEG_DEF_ON_R 96
#endif
#ifndef WE_SEVENSEG_DEF_ON_G
#define WE_SEVENSEG_DEF_ON_G 226
#endif
#ifndef WE_SEVENSEG_DEF_ON_B
#define WE_SEVENSEG_DEF_ON_B 200
#endif

/* 默认灭段鬼影色（深灰蓝） */
#ifndef WE_SEVENSEG_DEF_OFF_R
#define WE_SEVENSEG_DEF_OFF_R 66
#endif
#ifndef WE_SEVENSEG_DEF_OFF_G
#define WE_SEVENSEG_DEF_OFF_G 78
#endif
#ifndef WE_SEVENSEG_DEF_OFF_B
#define WE_SEVENSEG_DEF_OFF_B 96
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_sevenseg_obj_t
{
    we_obj_t base;                             /* 必须在首位：包围盒 = 全部数字位 */

    const char *text;                          /* 显示文本指针（调用方持有） */
    char shadow[WE_SEVENSEG_MAX_CHARS + 1];    /* 上次渲染内容快照（变更判定用） */

    uint16_t digit_h;                          /* 单字高（像素） */
    uint16_t digit_w;                          /* 单字宽（init 推导） */
    uint16_t gap;                              /* 字间距（init 推导） */
    uint8_t  seg_t;                            /* 段厚（init 推导，>=2） */
    uint8_t  digit_cnt;                        /* 位数（含冒号位，1..MAX_CHARS） */

    colour_t on_color;                         /* 亮段颜色 */
    colour_t off_color;                        /* 灭段鬼影颜色 */
    uint8_t  ghost;                            /* 1 = 灭段画低透明鬼影 */
    uint8_t  opacity;                          /* 整体不透明度（0~255） */
} we_sevenseg_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化七段数码管控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 包围盒左上角 X（屏幕绝对坐标）。
 * @param y 包围盒左上角 Y。
 * @param digit_h 单字高（像素，建议 >= 16）。
 * @param digit_cnt 位数（含冒号位，自动钳到 1..WE_SEVENSEG_MAX_CHARS）。
 * @return 无。
 * @note 总宽由 digit_h/digit_cnt 自动推导并写入 base.w；初始文本为空，
 *       默认亮段青绿 / 鬼影深灰蓝、ghost 关闭、不透明、装饰性不可点击。
 */
void we_sevenseg_obj_init(we_sevenseg_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t digit_h, uint8_t digit_cnt);

/**
 * @brief 设置显示文本（支持 '0'~'9'、'-'、':'、' '，其余按空格处理）。
 * @param obj 控件对象指针。
 * @param str 文本字符串指针（调用方持有，控件只存指针不拷贝）。
 * @return 无。
 * @note 内容与上次渲染快照一致时直接返回不重绘（指针仍会更新）；
 *       超出 digit_cnt 的字符被忽略，不足的位按空位处理。
 */
void we_sevenseg_set_text(we_sevenseg_obj_t *obj, const char *str);

/**
 * @brief 设置亮段颜色与灭段鬼影颜色。
 * @param obj 控件对象指针。
 * @param on_color 亮段颜色。
 * @param off_color 灭段鬼影颜色（仅 ghost 开启时使用）。
 * @return 无。
 * @note 两个颜色均未变时直接返回不重绘。
 */
void we_sevenseg_set_colors(we_sevenseg_obj_t *obj, colour_t on_color, colour_t off_color);

/**
 * @brief 开关灭段鬼影（模拟数码管暗段底纹）。
 * @param obj 控件对象指针。
 * @param enable 1 = 灭段画低透明鬼影，0 = 灭段不画。
 * @return 无。
 */
void we_sevenseg_set_ghost(we_sevenseg_obj_t *obj, uint8_t enable);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_sevenseg_set_opacity(we_sevenseg_obj_t *obj, uint8_t opacity);

/**
 * @brief 移动控件到新位置（包围盒左上角对齐到 x,y）。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
void we_sevenseg_set_pos(we_sevenseg_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief 删除控件：从对象链表摘除并清空基类状态（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_sevenseg_obj_delete(we_sevenseg_obj_t *obj);

#endif /* __WE_WIDGET_SEVENSEG_H */
