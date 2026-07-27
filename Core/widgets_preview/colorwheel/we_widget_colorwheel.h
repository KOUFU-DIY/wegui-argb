#ifndef __WE_WIDGET_COLORWHEEL_H
#define __WE_WIDGET_COLORWHEEL_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * HSV 色轮控件（colorwheel）—— preview 孵化区实验控件
 *
 * 在 size×size 包围盒内画一圈内切 HSV 色环（S=V=255 固定，只选 Hue），
 * 环带宽约 size/5，当前选中角度处画一枚白色圆点标记。
 *
 * 渲染模型（draw_cb 逐像素扫描自身 bbox，直写 pfb_gram）：
 *   1. 像素→(dx,dy)→d²=dx²+dy²，整数比较判环带内外；
 *   2. 环带内外边缘各 1px 用 d² 线性渐隐做简易 AA；
 *   3. 角度 = 整数八分区 atan2 近似（多项式修正，误差 < 1/512 圈），输出 0..511；
 *   4. Hue→RGB 用六段线性插值纯整数转换；
 *   5. 标记点用 we_draw_round_rect_analytic_fill 圆点（深色描边 + 白芯）。
 *
 * 交互：PRESSED / STAY 把触点向量转角度更新 hue（复用同一 atan2 近似），
 * 值变化时触发 changed_cb 回调并整体重绘；按住拖出控件仍持续跟随
 * （事件由内核按 pressed_obj 路由）。中心空洞附近的触点忽略（角度不稳定）。
 *
 * 角度统一 512 步制：0 = +X 方向，屏幕 Y 向下，128 = 正下方，顺时针增。
 * 全程整数运算，零 malloc、零浮点。
 *
 * preview 限制：
 *   - 每次 hue 变化按整控件包围盒标脏 → 整环全部重绘（含逐像素 atan2）；
 *   - 命中检测为包围盒粒度（bbox 四角空白区也会吃掉 PRESSED）。
 * -------------------------------------------------------------------------- */

/* 标记点圆心所在半径 = (r_out + r_in)/2；标记点半径 = 环带宽/2 - 1（最小 3） */
#ifndef WE_COLORWHEEL_MARK_MIN_R
#define WE_COLORWHEEL_MARK_MIN_R 3
#endif

struct we_colorwheel_obj_t;

/**
 * @brief 选色变化回调类型。
 * @param cw 触发回调的色轮控件指针（可强转回 we_colorwheel_obj_t*）。
 * @param c 当前选中颜色（S=V=255 的纯色相色）。
 */
typedef void (*we_colorwheel_changed_cb_t)(void *cw, colour_t c);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_colorwheel_obj_t
{
    we_obj_t base;                          /* 必须在首位：w=h=size */

    uint16_t hue;                           /* 当前色相（512 步制 0..511） */
    uint16_t r_out;                         /* 环带外半径（像素） */
    uint16_t r_in;                          /* 环带内半径（像素） */
    uint8_t  opacity;                       /* 整体不透明度（0~255） */
    we_colorwheel_changed_cb_t changed_cb;  /* 选色变化回调（可 NULL） */
} we_colorwheel_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化色轮控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 包围盒左上角 X（屏幕绝对坐标）。
 * @param y 包围盒左上角 Y。
 * @param size 包围盒边长（色环内切，建议 >= 40）。
 * @return 无。
 * @note 默认 hue = 0（红色）、不透明、无回调；环带宽约 size/5。
 */
void we_colorwheel_obj_init(we_colorwheel_obj_t *obj, we_lcd_t *lcd,
                            int16_t x, int16_t y, uint16_t size);

/**
 * @brief 读取当前选中颜色（S=V=255 的纯色相色，已转为设备色格式）。
 * @param obj 控件对象指针。
 * @return 当前颜色；obj 为 NULL 时返回黑色。
 */
colour_t we_colorwheel_get_color(const we_colorwheel_obj_t *obj);

/**
 * @brief 读取当前选中色相对应的 RGB888 三通道（与 LCD 色深无关）。
 * @param obj 控件对象指针。
 * @param r 传出：红色分量（0~255），可 NULL。
 * @param g 传出：绿色分量（0~255），可 NULL。
 * @param b 传出：蓝色分量（0~255），可 NULL。
 * @return 无。
 * @note 适合把选色结果直接喂给 RGB LED / 数值显示等与屏幕色深无关的用途。
 */
void we_colorwheel_get_rgb(const we_colorwheel_obj_t *obj,
                           uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * @brief 读取当前色相（512 步制 0..511）。
 * @param obj 控件对象指针。
 * @return 当前 hue；obj 为 NULL 时返回 0。
 */
uint16_t we_colorwheel_get_hue(const we_colorwheel_obj_t *obj);

/**
 * @brief 程序化设置色相并按需重绘。
 * @param obj 控件对象指针。
 * @param hue 目标色相（512 步制，内部按 & 0x1FF 归一化）。
 * @return 无。
 * @note 值未变时直接返回；程序化设置不触发 changed_cb（回调只响应用户交互）。
 */
void we_colorwheel_set_hue(we_colorwheel_obj_t *obj, uint16_t hue);

/**
 * @brief 注册选色变化回调（用户拖动/点击导致 hue 变化时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_colorwheel_set_changed_cb(we_colorwheel_obj_t *obj, we_colorwheel_changed_cb_t cb);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_colorwheel_set_opacity(we_colorwheel_obj_t *obj, uint8_t opacity);

/**
 * @brief 移动控件到新位置（包围盒左上角对齐到 x,y）。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
void we_colorwheel_set_pos(we_colorwheel_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief 删除控件：从对象链表摘除并清空基类状态（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_colorwheel_obj_delete(we_colorwheel_obj_t *obj);

#endif /* __WE_WIDGET_COLORWHEEL_H */
