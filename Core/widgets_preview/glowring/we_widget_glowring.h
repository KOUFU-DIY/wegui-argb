#ifndef __WE_WIDGET_GLOWRING_H
#define __WE_WIDGET_GLOWRING_H

#include "wegui.h"

/* 热路径函数的段属性：代码在外挂 Flash 里 XIP 的平台可在 we_user_config.h 里把它定义成
 * RAM 段属性，让 draw_cb 搬进 RAM 执行；没有定义时为空 */
#ifndef WE_FAST_MEM
#define WE_FAST_MEM
#endif

/* --------------------------------------------------------------------------
 * 透明渐变色环控件（glowring）—— preview 孵化区实验控件
 *
 * 在 size×size 包围盒内画一圈内切的单色环带（默认绿色），环带透明度沿
 * 圆周对称渐变：起点角 angle 处 alpha = 0（全透明），顺时针与逆时针两侧
 * 上升，到 angle + 180° 处 alpha = 255（全不透明），再对称回落 —— 即
 * 0 → 255 → 0 的镜像对称剖面。angle 可任意旋转。
 *
 * 剖面形状由 WE_GLOWRING_PROFILE 选：
 *   1（默认）余弦剖面 alpha = (1 − cos Δ) / 2 —— 等价于把一条穿过圆心、两端
 *     落在环两极的线性渐变（SVG linearGradient）投到圆周上，透明端与不透明端
 *     各有一段平缓区，过渡柔和；
 *   0 三角剖面 alpha ∝ |Δ|，角度上严格线性，两极是尖点。
 * 可选"双色"：透明端另给一个尾色（we_glowring_set_tail_color），颜色随
 * 剖面在尾色→主色之间插值（相当于两端 stop 颜色不同的线性渐变）。
 * 可选"实心"：we_glowring_set_solid 关掉渐变，整环等于主色 × opacity。
 * 可选"形状"：we_glowring_set_shape 挂一张"角度 → 外半径"表（Q4，2^n 项），
 * 外沿按表变粗变细、内沿仍是正圆，表随 angle 一起旋转 —— 用来画
 * "细圆 + 几个凸台"这类手绘风格的环并让它转起来；半径可用
 * we_glowring_set_radii 在包围盒内运行期改。
 *
 * 渲染模型（draw_cb 逐像素扫描 PFB 切片与包围盒的交集，直写 pfb_gram）：
 *   1. 像素→(dx,dy)→d² 沿行增量递推（d² += 2dx+1，无乘法），整数比较判环带；
 *   2. 行内一旦踏入中心空洞就直接跳到对称点，空洞像素零遍历；
 *   3. 角度 = 八分区折叠 + 倒数表 + atan 表（三次查表 / 一次乘法，无除法），
 *      输出 512 步制，最大误差 < 1 步；
 *   4. alpha = 三角形剖面（|Δ角| 线性），再乘环带内外边缘 1px 渐隐与整体
 *      不透明度（255 时零成本跳过）；
 *   5. 全程整数，零 malloc、零浮点、热路径零除法（仅每次 draw 入口算两个
 *      常量倒数）。
 *
 * 脏区策略（本控件的核心优化点）：
 *   - 旋转 / 换色 / 改透明度只按"宽度贴圆周的水平条带"标脏环带（默认 4 块：
 *     中心带左右各 1 块 + 上下各 1 块；WE_GLOWRING_DIRTY_RECTS 可选 6 / 8），
 *     包围盒四角与中心空洞的内容（下层控件、背景）不参与刷新；
 *   - 条带边界在 init 时穷举最优并存入对象，运行期标脏零计算；
 *   - 位置不变时任何属性变化都不会触发整包围盒刷新。
 *
 * 角度统一 512 步制：0 = +X 方向，屏幕 Y 向下，128 = 正下方，顺时针增。
 *
 * 限制：
 *   - 纯显示控件（event_cb = NULL），触摸直接穿透到下层；
 *   - 外半径上限 256px（倒数表长度），size 超出时自动截断；
 *   - 单色样式（MONO）下渐变透明度经端口二值化后为硬边环，不额外适配。
 * -------------------------------------------------------------------------- */

/* 默认环色：绿色（RGB888） */
#ifndef WE_GLOWRING_DEF_R
#define WE_GLOWRING_DEF_R (40)
#endif
#ifndef WE_GLOWRING_DEF_G
#define WE_GLOWRING_DEF_G (220)
#endif
#ifndef WE_GLOWRING_DEF_B
#define WE_GLOWRING_DEF_B (110)
#endif

/* 透明度剖面：1 = 余弦（线性渐变投影，默认）；0 = 三角（角度线性） */
#ifndef WE_GLOWRING_PROFILE
#define WE_GLOWRING_PROFILE (1)
#endif

/* RGB565 有序抖动（1 = 开）：565 每通道仅 32/64 级，长渐变会出现 1 级宽的
 * 台阶纹；开启后用 4x4 Bayer 阈值做舍入，台阶被打散为肉眼不可见的细纹。
 * RGB888 / GRAY8 色深下该宏无作用。 */
#ifndef WE_GLOWRING_DITHER
#define WE_GLOWRING_DITHER (1)
#endif

/* 脏矩形分块数（4 / 6 / 8）：旋转、换色、改透明度时环带按水平条带提交，
 * 条带宽度贴圆周（init 时穷举最优边界，运行期零计算）。
 *   4：中心带左右 2 块 + 上下各 1 块（默认；相对整包围盒约省 25~30%）
 *   6：上下各再切 1 刀（再省约 8%）
 *   8：侧带也挖空洞（细环再省约 10%；每只环占 8 个脏矩形槽，多只环并存时
 *      请把 WE_CFG_DIRTY_MAX_NUM 提到 16 以上，否则管理器会把它们合并回去）
 * 块数 >= 6 时内部仍会评估更少块数的方案，取面积最小者。 */
#ifndef WE_GLOWRING_DIRTY_RECTS
#define WE_GLOWRING_DIRTY_RECTS (8)
#endif
#if (WE_GLOWRING_DIRTY_RECTS != 4) && (WE_GLOWRING_DIRTY_RECTS != 6) && (WE_GLOWRING_DIRTY_RECTS != 8)
#error "WE_GLOWRING_DIRTY_RECTS must be 4, 6 or 8"
#endif

/* 环带最小宽度（像素）：内外各 1px 渐隐带 + 至少 2px 实体 */
#ifndef WE_GLOWRING_MIN_BAND
#define WE_GLOWRING_MIN_BAND (4U)
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_glowring_obj_t
{
    we_obj_t base;      /* 必须在首位：w=h=size */

    colour_t color;     /* 环色（设备色格式）—— 不透明端的颜色 */
    colour_t tail_color;/* 透明端颜色（two_tone = 1 时生效，沿剖面向 color 插值） */
    uint16_t angle;     /* 透明端（alpha=0）所在角度，512 步制 0..511 */
    uint16_t r_out;     /* 环带外半径（像素，含 1px 渐隐带） */
    uint16_t r_in;      /* 环带内半径（像素，d² <= r_in² 的像素不画） */
    uint8_t  opacity;   /* 整体不透明度（0~255） */
    uint8_t  two_tone;  /* 1 = 双色渐变（tail_color → color） */
    uint8_t  solid;     /* 1 = 实心环（无渐变，整环 = color × opacity） */
    uint8_t  shape_log2;            /* 形状表项数 log2（0 = 无形状表） */
    const uint16_t *shape_r_q4;     /* 形状表：相对透明端角度 → 外半径（Q4，1/16 px），调用方持有 */

    /* 脏矩形分块方案（init 时算好，相对圆心的半宽/行号，见 .c 的 _gr_plan_build）
     *   plan_kind：实际采用的块数 4 / 6 / 8（<= WE_GLOWRING_DIRTY_RECTS）
     *   p_m：中心带行范围 |dy| <= p_m；p_xim：中心带空洞半宽
     *   p_t：侧带 1 的末行（块数 6/8）；p_xo1 / p_xo2：侧带 1 / 2 的外半宽
     *   p_xi1：侧带 1 的空洞半宽（块数 8） */
    uint8_t  plan_kind;
    int16_t  p_m, p_t, p_xim, p_xo1, p_xi1, p_xo2;
} we_glowring_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化透明渐变色环并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 包围盒左上角 X（屏幕绝对坐标）。
 * @param y 包围盒左上角 Y。
 * @param size 包围盒边长（色环内切；最小 16，外半径上限 256）。
 * @param band 环带宽度（像素）；0 = 自动取 size/5。
 * @return 无。
 * @note 默认绿色、angle = 0（透明端在 +X 方向，最亮处在 -X 方向）、不透明。
 */
void we_glowring_obj_init(we_glowring_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, uint16_t size, uint16_t band);

/**
 * @brief 设置透明端角度（旋转色环）并只标脏环带区域。
 * @param obj 控件对象指针。
 * @param angle 目标角度（512 步制，内部按 & 0x1FF 归一化）。
 * @return 无。
 * @note 值未变时直接返回；中心空洞不刷新。
 */
void we_glowring_set_angle(we_glowring_obj_t *obj, uint16_t angle);

/**
 * @brief 在当前角度上叠加一个增量（可为负，便于定时器连续旋转）。
 * @param obj 控件对象指针。
 * @param delta 角度增量（512 步制）。
 * @return 无。
 */
void we_glowring_rotate(we_glowring_obj_t *obj, int16_t delta);

/**
 * @brief 读取当前透明端角度。
 * @param obj 控件对象指针。
 * @return 当前 angle（0..511）；obj 为 NULL 时返回 0。
 */
uint16_t we_glowring_get_angle(const we_glowring_obj_t *obj);

/**
 * @brief 设置环色并只标脏环带区域。
 * @param obj 控件对象指针。
 * @param color 新颜色（设备色格式，用 RGB888TODEV 构造）。
 * @return 无。
 */
void we_glowring_set_color(we_glowring_obj_t *obj, colour_t color);

/**
 * @brief 设置透明端颜色（双色渐变）并只标脏环带区域。
 * @param obj 控件对象指针。
 * @param tail 透明端颜色（设备色格式）。
 * @param enable 1 = 启用双色（tail → color 沿剖面插值）；0 = 关闭，回到单色。
 * @return 无。
 * @note 相当于两端 stop 颜色不同的线性渐变（如 lime → 白透明）。
 */
void we_glowring_set_tail_color(we_glowring_obj_t *obj, colour_t tail, uint8_t enable);

/**
 * @brief 实心模式开关：1 = 整环按 color × opacity 平铺，无渐变。
 * @param obj 控件对象指针。
 * @param solid 1 开 / 0 关。
 * @return 无。
 * @note 实心时热路径跳过角度计算，比渐变更快；用于错误态的纯色红环。
 */
void we_glowring_set_solid(we_glowring_obj_t *obj, uint8_t solid);

/**
 * @brief 挂载 / 卸下"角度 → 外半径"形状表。
 * @param obj 控件对象指针。
 * @param r_q4 表指针（Q4 外半径，项数 = 2^log2_n，索引 0 对应透明端角度 angle，
 *             随角度顺时针递增，项间线性插值；调用方持有，NULL = 卸下）。
 * @param log2_n 项数的 log2（建议 7 = 128 项，每项 2.8°）。
 * @return 无。
 * @note 外半径上限自动取表中最大值 +1（AA 带），不得超过 size/2 − 1；
 *       内半径不变。挂载前后各按各自的条带方案标脏一次。
 */
void we_glowring_set_shape(we_glowring_obj_t *obj, const uint16_t *r_q4, uint8_t log2_n);

/**
 * @brief 在包围盒内运行期改外 / 内半径（重算脏区条带方案）。
 * @param obj 控件对象指针。
 * @param r_out 新外半径（<= size/2 − 1）。
 * @param r_in 新内半径（r_out − r_in >= WE_GLOWRING_MIN_BAND）。
 * @return 无。
 */
void we_glowring_set_radii(we_glowring_obj_t *obj, uint16_t r_out, uint16_t r_in);

/**
 * @brief 设置整体不透明度并只标脏环带区域。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_glowring_set_opacity(we_glowring_obj_t *obj, uint8_t opacity);

/**
 * @brief 移动控件到新位置（包围盒左上角对齐到 x,y），新旧位置均只标脏环带。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
void we_glowring_set_pos(we_glowring_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief 删除控件：从对象链表摘除并清空基类状态（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_glowring_obj_delete(we_glowring_obj_t *obj);

#endif /* __WE_WIDGET_GLOWRING_H */
