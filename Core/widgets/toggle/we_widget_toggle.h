#ifndef __WE_WIDGET_TOGGLE_H
#define __WE_WIDGET_TOGGLE_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 是否启用滑动切换动画
 *
 * 1（默认）：点击或程序调用 we_toggle_toggle() 时，滑块从一侧平滑滑到另一侧，
 *           轨道颜色同步从灰到绿（或反向）渐变。
 *
 * 0：        切换时直接跳变到目标位置，无任何动画过渡。
 *           适合 RAM/Flash 极度紧张或不需要动画的场景。
 * -------------------------------------------------------------------------- */
#ifndef WE_TOGGLE_USE_ANIM
#define WE_TOGGLE_USE_ANIM 1
#endif

/* --------------------------------------------------------------------------
 * 动画总步数（仅 WE_TOGGLE_USE_ANIM == 1 时有效）
 *
 * GUI 内部 task 每累计 WE_TOGGLE_ANIM_STEP_MS 毫秒推进 1 步，
 * 共推进 WE_TOGGLE_ANIM_STEPS 步后动画结束。
 * 以默认 16ms 步进为例：
 *   8 步 ≈ 128ms（推荐，接近 iOS 手感）
 *   6 步 ≈  96ms
 *   4 步 ≈  64ms（更快速）
 * -------------------------------------------------------------------------- */
#ifndef WE_TOGGLE_ANIM_STEPS
#define WE_TOGGLE_ANIM_STEPS 8
#endif

/* --------------------------------------------------------------------------
 * 动画每步的时间间隔（毫秒）
 *
 * 默认 16ms，适合大多数 60FPS 左右的主循环。
 * 如果主循环更慢，可以适当调大；如果希望动画更利落，可以适当调小。
 * -------------------------------------------------------------------------- */
#ifndef WE_TOGGLE_ANIM_STEP_MS
#define WE_TOGGLE_ANIM_STEP_MS 16U
#endif

/* --------------------------------------------------------------------------
 * 滑块与轨道边缘的间距（像素）
 * 调小可让滑块更大，调大可让轨道边框更明显。
 * -------------------------------------------------------------------------- */
#ifndef WE_TOGGLE_THUMB_PAD
#define WE_TOGGLE_THUMB_PAD 2
#endif

/* --------------------------------------------------------------------------
 * 颜色配置（仿 iOS 默认风格，可在包含此头文件之前用宏覆盖）
 *
 * ON  轨道色：iOS 绿  (52, 199,  89)
 * OFF 轨道色：iOS 灰  (120, 120, 128)
 * 滑块色：    纯白    (255, 255, 255)
 *
 * 示例——改为蓝色 ON：
 *   #define WE_TOGGLE_COLOR_ON_R  10
 *   #define WE_TOGGLE_COLOR_ON_G 132
 *   #define WE_TOGGLE_COLOR_ON_B 255
 *   #include "we_widget_toggle.h"
 * -------------------------------------------------------------------------- */

/* ON 轨道色 */
#ifndef WE_TOGGLE_COLOR_ON_R
#define WE_TOGGLE_COLOR_ON_R   52
#endif
#ifndef WE_TOGGLE_COLOR_ON_G
#define WE_TOGGLE_COLOR_ON_G  199
#endif
#ifndef WE_TOGGLE_COLOR_ON_B
#define WE_TOGGLE_COLOR_ON_B   89
#endif

/* OFF 轨道色 */
#ifndef WE_TOGGLE_COLOR_OFF_R
#define WE_TOGGLE_COLOR_OFF_R 120
#endif
#ifndef WE_TOGGLE_COLOR_OFF_G
#define WE_TOGGLE_COLOR_OFF_G 120
#endif
#ifndef WE_TOGGLE_COLOR_OFF_B
#define WE_TOGGLE_COLOR_OFF_B 128
#endif

/* 滑块色 */
#ifndef WE_TOGGLE_COLOR_THUMB_R
#define WE_TOGGLE_COLOR_THUMB_R 255
#endif
#ifndef WE_TOGGLE_COLOR_THUMB_G
#define WE_TOGGLE_COLOR_THUMB_G 255
#endif
#ifndef WE_TOGGLE_COLOR_THUMB_B
#define WE_TOGGLE_COLOR_THUMB_B 255
#endif

/* 按下时轨道颜色额外向黑色混合的 alpha（0=不暗化，255=全黑），建议 30~50 */
#ifndef WE_TOGGLE_PRESS_DARKEN
#define WE_TOGGLE_PRESS_DARKEN 40
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef uint8_t (*we_toggle_event_cb_t)(void *obj, we_event_t event, we_indev_data_t *data);

typedef struct
{
    we_obj_t base;
    we_toggle_event_cb_t user_event_cb;
    uint8_t opacity;
    uint8_t checked : 1; /* 目标状态：1=ON，0=OFF */
    uint8_t pressed : 1; /* 当前是否被按下 */
#if WE_TOGGLE_USE_ANIM
    uint8_t anim_step;   /* 当前动画步数：0=完全 OFF，WE_TOGGLE_ANIM_STEPS=完全 ON */
    uint16_t anim_acc_ms;/* GUI task 累计的未消费动画时间 */
#endif
} we_toggle_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化控件对象并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 目标区域左上角 X 坐标。
 * @param y 目标区域左上角 Y 坐标。
 * @param w 目标区域宽度（像素）。
 * @param h 目标区域高度（像素）。
 * @param event_cb 回调函数指针。
 * @return 无。
 */
void we_toggle_obj_init(we_toggle_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, int16_t h,
                        we_toggle_event_cb_t event_cb);

/**
 * @brief 设置开关状态（立即跳到目标状态，不走过渡动画）。
 * @param obj 开关控件对象指针。
 * @param checked 目标状态，0=关，非0=开。
 */
void we_toggle_set_checked(we_toggle_obj_t *obj, uint8_t checked);

/**
 * @brief 切换开关状态（开/关互换）。
 * @param obj 开关控件对象指针。
 */
void we_toggle_toggle(we_toggle_obj_t *obj);

/**
 * @brief 查询当前开关状态。
 * @param obj 开关控件对象指针。
 * @return 1 表示开启，0 表示关闭或 obj 为 NULL。
 */
uint8_t we_toggle_is_checked(const we_toggle_obj_t *obj);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_toggle_set_opacity(we_toggle_obj_t *obj, uint8_t opacity);

/**
 * @brief 占位接口：当前实现不需要外部主动推进动画。
 * @param obj 开关控件对象指针。
 */
static inline void we_toggle_update_anim(we_toggle_obj_t *obj)
{
    (void)obj;
}

/**
 * @brief 删除开关控件对象并从对象链表移除。
 * @param obj 开关控件对象指针。
 */
static inline void we_toggle_obj_delete(we_toggle_obj_t *obj)
{
we_obj_delete((we_obj_t *)obj);
}

#endif /* __WE_WIDGET_TOGGLE_H */
