#ifndef __WE_WIDGET_SEGDISP_H
#define __WE_WIDGET_SEGDISP_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 段码数码管控件（segdisp）
 *
 * 模拟经典 a~g 七段数码管 + dp 小数点，不吃字库：适合大字号时钟、
 * 计数器、仪表读数等场景。
 *
 * 内容模型（统一为"每位一个段码字节"）：
 *   - 段码位序 bit0~bit6 = a~g、bit7 = dp，与 TM1650 等数码管驱动
 *     芯片的常见段码表一致，现成段码表可整组喂给 we_segdisp_set_segs()；
 *   - we_segdisp_set_text() 是文本便捷层：字符在 set 时解码进段码
 *     数组（支持 '0'~'9'、'A'~'F'/'a'~'f'、'-'、':'、'.'、' '，
 *     其余按空格处理），字符串不被引用，调用方无需保持其存活；
 *   - '.' 紧跟可显示字符时合并为前一位的 dp（"12.5" 占 3 位），
 *     开头或连续的 '.' 独占一位（只亮 dp）；
 *   - ':' 独占一位（上下两个小方点）；也可用 we_segdisp_set_colon()
 *     把任意位置成冒号位并直接开关两点（典型用法：时钟冒号闪烁，
 *     关闭时 ghost 画暗点而不是数字骨架）。
 *
 * 段形风格：
 *   - WE_SEGDISP_STYLE_BEVEL（默认）：段两端向中线 45° 斜切收尖
 *     （六边形段），逐行/逐列 1px we_fill_rect 扫描实现，每段最多
 *     "段厚"次填充调用；观感接近真实数码管；
 *   - WE_SEGDISP_STYLE_RECT：纯直角矩形段（每段一次 fill，最省）；
 *   - 段厚 < 3 像素时斜切自动退化为矩形。
 *
 * 布局模型：
 *   - 单字宽 digit_w、单字高 digit_h、字间距 gap、位数 digit_cnt
 *     （含冒号位）、段厚 seg_t 全部在 init 一次性确定；
 *   - 除 digit_h（推导基准，必填）外，digit_w / gap / seg_t 传 0 走
 *     自动推导：
 *       段厚   = digit_h/8（最小 2；自定义值钳到 [2, (digit_h-2)/3]，
 *                保证竖段不退化）
 *       单字宽 = digit_h/2（自定义与自动都钳到最小 3*段厚，保证横段
 *                长度 >= 段厚）
 *       字间距 = 段厚
 *   - 总宽 = digit_cnt*digit_w + (digit_cnt-1)*gap，自动写入 base.w/h；
 *   - dp 画在本位右下角的空白角区内（不额外占位）。
 *
 * 鬼影（ghost）：
 *   - 打开后灭段以 off_color 低透明度绘制，模拟真实数码管的暗段底纹
 *     （作用于 a~g 七段与"关闭状态的冒号点"；dp 不画鬼影）。
 *
 * dp 显示开关：
 *   - 默认不显示：段码 bit7 照常存储但不绘制（硬件段码表里 bit7 常被
 *     挪作它用，如时钟模组把冒号接在 dp 线上），需要小数点时用
 *     we_segdisp_set_dp(obj, 1) 打开；文本 '.' 同样要打开才可见。
 *
 * 标脏粒度：所有内容 setter 汇聚到统一的逐位 diff，只有段码/冒号
 * 发生变化的单元格才标脏（时钟 "12:34"→"12:35" 只重绘末位）。
 *
 * 全程整数运算，零 malloc、零浮点；渲染只用 we_fill_rect（自带 PFB
 * 裁剪与容器透明度级联）。默认装饰性（不消费输入，事件穿透给背后控件）。
 *
 * 当前限制：
 *   - 斜切边缘无抗锯齿（45° 硬边）；
 *   - 冒号位与数字位同宽（未做窄冒号位）。
 * -------------------------------------------------------------------------- */

/* 段码位定义（bit0~bit6 = a~g，bit7 = dp）
 *       aaaa
 *      f    b
 *      f    b
 *       gggg
 *      e    c
 *      e    c
 *       dddd   dp                                                          */
#define WE_SEGDISP_SEG_A  0x01U /* 上横 */
#define WE_SEGDISP_SEG_B  0x02U /* 右上竖 */
#define WE_SEGDISP_SEG_C  0x04U /* 右下竖 */
#define WE_SEGDISP_SEG_D  0x08U /* 下横 */
#define WE_SEGDISP_SEG_E  0x10U /* 左下竖 */
#define WE_SEGDISP_SEG_F  0x20U /* 左上竖 */
#define WE_SEGDISP_SEG_G  0x40U /* 中横 */
#define WE_SEGDISP_SEG_DP 0x80U /* 小数点（右下角） */

/* 段形风格（we_segdisp_set_style） */
#define WE_SEGDISP_STYLE_RECT  0U /* 直角矩形段 */
#define WE_SEGDISP_STYLE_BEVEL 1U /* 45° 斜切段帽（默认） */

/* 位数上限（含冒号位；digit_cnt 会被钳到该值。参与结构体布局，
 * 覆盖时应放 we_user_config.h 保证全工程一致） */
#ifndef WE_SEGDISP_MAX_CHARS
#define WE_SEGDISP_MAX_CHARS 8
#endif

/* 鬼影段透明度（0~255，会再与控件整体 opacity 相乘） */
#ifndef WE_SEGDISP_GHOST_OPA
#define WE_SEGDISP_GHOST_OPA 70U
#endif

/* 默认亮段色（青绿 LED 风格） */
#ifndef WE_SEGDISP_DEF_ON_R
#define WE_SEGDISP_DEF_ON_R 96
#endif
#ifndef WE_SEGDISP_DEF_ON_G
#define WE_SEGDISP_DEF_ON_G 226
#endif
#ifndef WE_SEGDISP_DEF_ON_B
#define WE_SEGDISP_DEF_ON_B 200
#endif

/* 默认灭段鬼影色（深灰蓝） */
#ifndef WE_SEGDISP_DEF_OFF_R
#define WE_SEGDISP_DEF_OFF_R 66
#endif
#ifndef WE_SEGDISP_DEF_OFF_G
#define WE_SEGDISP_DEF_OFF_G 78
#endif
#ifndef WE_SEGDISP_DEF_OFF_B
#define WE_SEGDISP_DEF_OFF_B 96
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_segdisp_obj_t
{
    we_obj_t base;                             /* 必须在首位：包围盒 = 全部数字位 */

    uint8_t segs[WE_SEGDISP_MAX_CHARS];       /* 每位段码（bit0~6=a~g，bit7=dp） */
    uint8_t colon_mask[(WE_SEGDISP_MAX_CHARS + 7) / 8]; /* 冒号位标记位图 */

    uint16_t digit_h;                          /* 单字高（像素） */
    uint16_t digit_w;                          /* 单字宽（init 推导） */
    uint16_t gap;                              /* 字间距（init 推导） */
    uint8_t  seg_t;                            /* 段厚（init 推导，>=2） */
    uint8_t  digit_cnt;                        /* 位数（含冒号位，1..MAX_CHARS） */

    colour_t on_color;                         /* 亮段颜色 */
    colour_t off_color;                        /* 灭段鬼影颜色 */
    uint8_t  ghost;                            /* 1 = 灭段画低透明鬼影 */
    uint8_t  dp_show;                          /* 1 = 绘制段码 bit7 的 dp 点（默认 0） */
    uint8_t  style;                            /* 段形风格（WE_SEGDISP_STYLE_x） */
    uint8_t  opacity;                          /* 整体不透明度（0~255） */
} we_segdisp_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化段码数码管控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 包围盒左上角 X（屏幕绝对坐标）。
 * @param y 包围盒左上角 Y。
 * @param digit_w 单字宽（像素）：0 = 自动（digit_h/2）；自定义与自动
 *                都会钳到最小 3×段厚（保证横段长度 >= 段厚）。
 * @param digit_h 单字高（像素，必填，最小钳到 16；自动推导的基准）。
 * @param gap 字间距（像素）：0 = 自动（= 段厚）。
 * @param digit_cnt 位数（含冒号位，自动钳到 1..WE_SEGDISP_MAX_CHARS）。
 * @param seg_t 段厚（像素）：0 = 自动（digit_h/8，最小 2）；非 0 为
 *              自定义值，自动钳到 [2, (digit_h-2)/3]（保证竖段不退化）。
 * @return 无。
 * @note 总宽 = digit_cnt*digit_w + (digit_cnt-1)*gap，自动写入 base.w；
 *       初始全灭，默认斜切段形、亮段青绿 / 鬼影深灰蓝、ghost 关闭、
 *       dp 显示关闭、不透明、装饰性不可点击。
 */
void we_segdisp_obj_init(we_segdisp_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y,
                          uint16_t digit_w, uint16_t digit_h, uint16_t gap,
                          uint8_t digit_cnt, uint8_t seg_t);

/**
 * @brief 设置显示文本（'0'~'9'、'A'~'F'/'a'~'f'、'-'、':'、'.'、' '，其余按空格）。
 * @param obj 控件对象指针。
 * @param str 文本字符串指针（set 时解码进段码数组，不被引用，无存活要求）。
 * @return 无。
 * @note '.' 紧跟可显示字符时合并为前一位的 dp（"12.5" 占 3 位）；
 *       超出 digit_cnt 的字符被忽略，不足的位按空位处理；
 *       逐位 diff 标脏，内容未变的位不重绘。
 */
void we_segdisp_set_text(we_segdisp_obj_t *obj, const char *str);

/**
 * @brief 段码整组直控（全量快照语义）。
 * @param obj 控件对象指针。
 * @param codes 段码数组指针（bit0~6=a~g，bit7=dp；NULL 视为全灭）。
 * @param count 数组长度（超出 digit_cnt 的部分被忽略）。
 * @return 无。
 * @note count 之外的位清灭，所有冒号位标记清除；逐位 diff 标脏。
 *       数组只在调用期间被读取，不被引用。
 */
void we_segdisp_set_segs(we_segdisp_obj_t *obj, const uint8_t *codes, uint8_t count);

/**
 * @brief 设置单个位的段码。
 * @param obj 控件对象指针。
 * @param pos 位索引（0..digit_cnt-1，越界忽略）。
 * @param code 段码（bit0~6=a~g，bit7=dp）。
 * @return 无。
 * @note 该位若是冒号位会转回数字位；段码未变时不重绘。
 */
void we_segdisp_set_seg(we_segdisp_obj_t *obj, uint8_t pos, uint8_t code);

/**
 * @brief 把某位置成冒号位并开/关两点（时钟冒号闪烁）。
 * @param obj 控件对象指针。
 * @param pos 位索引（0..digit_cnt-1，越界忽略）。
 * @param on 1 = 两点点亮，0 = 熄灭（ghost 开启时画暗点底纹）。
 * @return 无。
 * @note 状态未变时不重绘（每帧无脑调用零代价）；用 set_seg/set_text
 *       可把该位改回数字位。
 */
void we_segdisp_set_colon(we_segdisp_obj_t *obj, uint8_t pos, uint8_t on);

/**
 * @brief 设置段形风格。
 * @param obj 控件对象指针。
 * @param style WE_SEGDISP_STYLE_BEVEL（默认，45° 斜切）/ WE_SEGDISP_STYLE_RECT。
 * @return 无。
 * @note 风格未变时不重绘；段厚 < 3 时斜切自动退化为矩形。
 */
void we_segdisp_set_style(we_segdisp_obj_t *obj, uint8_t style);

/**
 * @brief 设置亮段颜色与灭段鬼影颜色。
 * @param obj 控件对象指针。
 * @param on_color 亮段颜色。
 * @param off_color 灭段鬼影颜色（仅 ghost 开启时使用）。
 * @return 无。
 * @note 两个颜色均未变时直接返回不重绘。
 */
void we_segdisp_set_colors(we_segdisp_obj_t *obj, colour_t on_color, colour_t off_color);

/**
 * @brief 开关灭段鬼影（模拟数码管暗段底纹）。
 * @param obj 控件对象指针。
 * @param enable 1 = 灭段画低透明鬼影，0 = 灭段不画。
 * @return 无。
 */
void we_segdisp_set_ghost(we_segdisp_obj_t *obj, uint8_t enable);

/**
 * @brief 开关 dp 小数点的显示（默认关闭）。
 * @param obj 控件对象指针。
 * @param enable 1 = 绘制段码 bit7 的 dp 点，0 = 忽略 bit7 不绘制。
 * @return 无。
 * @note 只控制渲染：段码里的 bit7 照常存储/比较；文本 '.' 合并出的
 *       dp 同样受此开关控制。默认关闭是为兼容把 bit7 挪作它用的
 *       硬件段码表（如时钟模组的冒号线）。
 */
void we_segdisp_set_dp(we_segdisp_obj_t *obj, uint8_t enable);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_segdisp_set_opacity(we_segdisp_obj_t *obj, uint8_t opacity);

/**
 * @brief 移动控件到新位置（包围盒左上角对齐到 x,y）。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
void we_segdisp_set_pos(we_segdisp_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief 删除控件：从对象链表摘除并清空基类状态（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_segdisp_obj_delete(we_segdisp_obj_t *obj);

#endif /* __WE_WIDGET_SEGDISP_H */
