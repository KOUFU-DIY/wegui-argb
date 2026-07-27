#ifndef __WE_WIDGET_RADIO_H
#define __WE_WIDGET_RADIO_H

#include "we_gui_driver.h"

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪单选组的按键回调与可聚焦性，其余控件不受影响。
 * 键控选行依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_RADIO_USE_KEY
#define WE_RADIO_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 单选组控件（radio）—— preview 孵化区实验控件
 *
 * 一个字符串数组渲染垂直排列的互斥单选行：每行左侧圆形指示器 + 右侧文字。
 * 行高 = max(字体行高, 指示器直径) + 2 * WE_RADIO_ROW_PAD，
 * 控件总高 = count * 行高，由 init 自动算出写入 base.h。
 *
 * 指示器画法（全部解析抗锯齿圆 = radius 拉满的圆角矩形）：
 *   1. 大圆       —— 选中行用选中色，未选中行用圈色；
 *   2. 小一号背景圆 —— 掏出空心外环（按压行时用高亮底色掏，视觉连贯）；
 *   3. 选中行再叠中心实心小圆（选中色）。
 *
 * 交互：点击行切换选中（互斥）；值变才触发 changed_cb 并只标脏受影响的
 * 新旧两行；按压行绘制轻微高亮，拖出行取消按压态。
 * 按键（WE_RADIO_USE_KEY，依赖编辑态）：OK 进出编辑态，编辑态上下键
 * 直接移动选中行（走触摸同款选中路径，值变触发 changed_cb）。
 *
 * 数据驱动：labels 数组由调用方持有，控件只保存 const 指针，不拷贝文本。
 * 零 malloc、零浮点；无动画节点（删除无需摘链）。
 *
 * preview 限制：
 *   - 文字不裁剪到行宽内，过长文本会溢出控件右缘；
 *   - 重绘遍历全部行，依赖 PFB 裁剪丢弃行外写入。
 * -------------------------------------------------------------------------- */

/* 行上下内边距（像素），可在包含本头文件前用宏覆盖 */
#ifndef WE_RADIO_ROW_PAD
#define WE_RADIO_ROW_PAD 6
#endif

/* 圆形指示器直径（像素） */
#ifndef WE_RADIO_IND_D
#define WE_RADIO_IND_D 18
#endif

/* 指示器外环厚度（像素） */
#ifndef WE_RADIO_RING_W
#define WE_RADIO_RING_W 2
#endif

/* 指示器与文字的水平间距（像素） */
#ifndef WE_RADIO_TEXT_GAP
#define WE_RADIO_TEXT_GAP 8
#endif

/* 按压行高亮：向白色混合的 alpha（0=不亮化，255=纯白），建议 20~40 */
#ifndef WE_RADIO_PRESS_LIGHTEN
#define WE_RADIO_PRESS_LIGHTEN 26
#endif

/* 选中改变回调：radio 为 we_radio_obj_t*，idx 为新选中的行序号 */
typedef void (*we_radio_changed_cb_t)(void *radio, uint8_t idx);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_radio_obj_t
{
    we_obj_t base;                     /* 必须在首位：base.h 由 init 按行数自动算出 */

    const char *const *labels;         /* 选项名数组（调用方持有，count 个） */
    const unsigned char *font;         /* 字库（init 必传） */
    we_radio_changed_cb_t changed_cb;  /* 选中改变回调（可为 NULL） */

    uint16_t row_h;                    /* 行高（init 时按字体行高与指示器直径算出） */
    int16_t press_row;                 /* 本次触摸按下的行序号，-1 = 无 */
    colour_t ring_color;               /* 未选中外环圈色 */
    colour_t sel_color;                /* 选中外环 + 中心实心圆颜色 */
    colour_t text_color;               /* 选项文字色 */

    uint8_t count;                     /* 选项行数 */
    uint8_t selected;                  /* 当前选中行序号（0 起） */
    uint8_t opacity;                   /* 整体不透明度（0~255） */
    uint8_t pressed : 1;               /* 1 = 按压高亮显示中 */
} we_radio_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化单选组控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param w 控件宽度（像素，整行均可点击）。
 * @param labels 选项名数组（调用方持有，count 个，不得含 NULL）。
 * @param count 选项行数（>=1）。
 * @return 无。
 * @note 控件总高 base.h = count * 行高，由本函数自动算出；
 *       默认选中第 0 行、灰蓝圈色/亮蓝选中色/浅色文字、不透明。
 */
void we_radio_obj_init(we_radio_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, int16_t w,
                       const char *const *labels, uint8_t count,
                        const unsigned char *font);

/**
 * @brief 程序设置选中行（互斥），只标脏受影响的新旧两行。
 * @param obj 控件对象指针。
 * @param idx 目标行序号（越界或与当前相同则直接返回）。
 * @return 无。
 * @note 程序设置不触发 changed_cb（与 checkbox set_checked 约定一致）；
 *       点击交互路径才回调。
 */
void we_radio_set_selected(we_radio_obj_t *obj, uint8_t idx);

/**
 * @brief 读取当前选中行序号。
 * @param obj 控件对象指针。
 * @return 选中行序号（0 起）；obj 为 NULL 时返回 0。
 */
uint8_t we_radio_get_selected(const we_radio_obj_t *obj);

/**
 * @brief 注册选中改变回调（点击切换且值变时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针（radio, idx），NULL 表示取消。
 * @return 无。
 */
void we_radio_set_changed_cb(we_radio_obj_t *obj, we_radio_changed_cb_t cb);

/**
 * @brief 设置三项配色：未选中圈色 / 选中色 / 文字色。
 * @param obj 控件对象指针。
 * @param ring 未选中外环圈色。
 * @param sel 选中外环与中心实心圆颜色。
 * @param text 选项文字色。
 * @return 无。
 * @note 三项均与当前值相同时直接返回，不触发重绘。
 */
void we_radio_set_colors(we_radio_obj_t *obj, colour_t ring,
                         colour_t sel, colour_t text);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255），值未变时直接返回。
 * @return 无。
 */
void we_radio_set_opacity(we_radio_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件并从对象链表移除（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_radio_obj_delete(we_radio_obj_t *obj);

#endif /* __WE_WIDGET_RADIO_H */
