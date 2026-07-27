#ifndef __WE_WIDGET_HOLD_BTN_H
#define __WE_WIDGET_HOLD_BTN_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 长按确认按钮（hold_btn）—— preview 孵化区实验控件
 *
 * 圆形按钮 + 外圈分段充能环：按住期间充能进度随时间增长（计时放在
 * 中央动画节点里推进，不依赖 STAY 事件的派发频率）；松手未满则进度以
 * 2 倍速回退到 0；充满则触发回调一次 + 核心圆闪亮反馈，随后进入已触发
 * 态（保持点亮，需 we_hold_btn_reset 复位后才能再次充能）。
 *
 * 渲染：核心实心圆（we_draw_round_rect_analytic_fill 退化）+ 标签文字
 * 居中（PFB 收窄裁剪在控件矩形内）+ 充能环。充能环用 512 步制逐段画法：
 * 环带按 WE_HOLD_BTN_RING_SEGS 段近似，每段一条 we_draw_line_round
 * 短粗线（从内半径指向外半径的径向辐条），暗色画满一圈作轨道，
 * 亮色画 0..progress 段作充能进度。
 *
 * 交互控件：event_cb 恒返回 1（消费事件）。
 * 零 malloc、渲染内环零浮点（角度查 we_cos/we_sin Q15 表）。
 * 删除前必须 we_hold_btn_obj_delete（内部先 we_anim_stop 再摘链）。
 *
 * preview 限制：标脏按整控件包围盒；充能环为分段辐条近似（非连续弧带）。
 * -------------------------------------------------------------------------- */

/* 默认充满时长（毫秒），可用 we_hold_btn_set_hold_ms 运行时修改 */
#ifndef WE_HOLD_BTN_DEF_HOLD_MS
#define WE_HOLD_BTN_DEF_HOLD_MS 1200U
#endif

/* 充能环分段数（整圈辐条数，512 步制等分） */
#ifndef WE_HOLD_BTN_RING_SEGS
#define WE_HOLD_BTN_RING_SEGS 48U
#endif

/* 触发闪亮反馈时长（毫秒） */
#ifndef WE_HOLD_BTN_FLASH_MS
#define WE_HOLD_BTN_FLASH_MS 320U
#endif

/* 松手回退速度倍率（进度回退 = 充能速度 x 该倍率） */
#ifndef WE_HOLD_BTN_DECAY_MUL
#define WE_HOLD_BTN_DECAY_MUL 2U
#endif

/* 未充能轨道辐条透明度（0~255） */
#ifndef WE_HOLD_BTN_TRACK_OPA
#define WE_HOLD_BTN_TRACK_OPA 64U
#endif

/**
 * @brief 充满触发回调。
 * @param hb 触发的按钮对象指针（we_hold_btn_obj_t *，以 void * 透传）。
 * @return 无。
 * @note 整个"按住->充满"过程只触发一次；复位后可再次触发。
 */
typedef void (*we_hold_btn_triggered_cb_t)(void *hb);

typedef struct we_hold_btn_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h = size x size 外接正方形 */

    const char *label;          /* 按钮中心标签文字（调用方持有，只存指针） */
    uint16_t hold_ms;           /* 充满所需按住时长（毫秒） */
    uint16_t charge_ms;         /* 已累计充能时间（毫秒，内部） */
    uint16_t progress;          /* 当前充能进度 Q8（0..256，内部） */
    uint16_t flash;             /* 触发闪亮反馈强度 Q8（256->0 衰减，内部） */

    colour_t bg_color;          /* 核心圆底色 */
    colour_t ring_color;        /* 充能环亮色（轨道为其低透明度形态） */
    colour_t text_color;        /* 标签文字色 */

    we_hold_btn_triggered_cb_t triggered_cb; /* 充满触发回调（可为 NULL） */

    we_anim_t anim;             /* 中央动画节点（充能/回退/闪亮共用一个） */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */
    uint8_t pressed;            /* 当前按压中标志（内部） */
    uint8_t triggered;          /* 已触发锁定态（需 reset 复位，内部） */
    const unsigned char *font;  /* 字体资源（init 必传） */
} we_hold_btn_obj_t;

/**
 * @brief 初始化长按确认按钮并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param size 外接正方形边长（像素，建议 >= 60）。
 * @param label 按钮中心标签文字（UTF-8，调用方持有，可为 NULL）。
 * @return 无。
 * @note 默认：hold 1200ms、深蓝底 + 亮青环 + 近白文字、未触发。
 */
void we_hold_btn_obj_init(we_hold_btn_obj_t *obj, we_lcd_t *lcd,
                          int16_t x, int16_t y, int16_t size, const char *label,
                        const unsigned char *font);

/**
 * @brief 设置充满所需按住时长。
 * @param obj 控件对象指针。
 * @param hold_ms 充满时长（毫秒，0 按 1 处理）；值未变直接返回。
 * @return 无。
 * @note 充能进行中修改会按新时长重新折算显示进度。
 */
void we_hold_btn_set_hold_ms(we_hold_btn_obj_t *obj, uint16_t hold_ms);

/**
 * @brief 设置充满触发回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_hold_btn_set_triggered_cb(we_hold_btn_obj_t *obj,
                                  we_hold_btn_triggered_cb_t cb);

/**
 * @brief 设置核心圆底色 / 充能环亮色 / 标签文字色。
 * @param obj 控件对象指针。
 * @param bg 核心圆底色。
 * @param ring 充能环亮色。
 * @param text 标签文字色。
 * @return 无。
 * @note 三色均未变化时直接返回，不触发重绘。
 */
void we_hold_btn_set_colors(we_hold_btn_obj_t *obj,
                            colour_t bg, colour_t ring, colour_t text);

/**
 * @brief 复位按钮：清除已触发态与充能进度，回到待机可再次充能。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_hold_btn_reset(we_hold_btn_obj_t *obj);

/**
 * @brief 查询当前充能进度。
 * @param obj 控件对象指针。
 * @return 进度 Q8（0..256，256 = 已充满）；obj 为 NULL 返回 0。
 */
uint16_t we_hold_btn_get_progress(const we_hold_btn_obj_t *obj);

/**
 * @brief 查询是否处于已触发锁定态。
 * @param obj 控件对象指针。
 * @return 1=已触发（等待 reset），0=未触发或 obj 为 NULL。
 */
uint8_t we_hold_btn_is_triggered(const we_hold_btn_obj_t *obj);

/**
 * @brief 设置控件整体透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_hold_btn_set_opacity(we_hold_btn_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除按钮控件：先摘除动画节点（we_anim_stop）再摘链。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_hold_btn_obj_delete(we_hold_btn_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_HOLD_BTN_H */
