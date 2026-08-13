#ifndef __WE_WIDGET_IMGBTN_H
#define __WE_WIDGET_IMGBTN_H

#include "image_res.h"
#include "we_gui_driver.h"

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪图片按钮的按键回调与可聚焦性，其余控件不受影响。 */
#ifndef WE_IMGBTN_USE_KEY
#define WE_IMGBTN_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 图片按钮控件（imgbtn）
 *
 * 用一张（或两张）图片资源充当按钮皮肤：
 *   - img_normal：常态图（必填），控件宽高直接取自资源头并写入 base.w/h；
 *   - img_pressed：按压态图（可 NULL）。传 NULL 时按压视觉退化为自动变暗，
 *     省一张资源。
 *
 * 支持的格式与 img 控件完全一致（共用渲染层的 we_img_render_auto 分发）：
 * RGB565 / ARGB8565 未压缩、两种索引 QOI（受 WE_CFG_ENABLE_INDEXED_QOI 裁剪）、
 * A1/A2/A4/A8 透明位图（用 color 前景色上色，默认白色，可 set_color 改）。
 * 图片指针由调用方持有，控件不拷贝。
 *
 * 自动变暗按格式分两路：带逐像素透明度的图（ARGB8565 / A1~A8）按
 * WE_IMGBTN_DIM_SCALE 压低整体透明度，避免叠矩形在透明区留下方形黑影；
 * 不透明图（RGB565 等）绘制后叠一层 WE_IMGBTN_DIM_OPA 的半透明黑。
 *
 * 格式在 init / set_imgs 一次性校验（与 img 控件同口径）：两张图里有任何
 * 一张不被支持，则 class_p 置 NULL——控件既不绘制也不参与命中与聚焦，
 * 不会出现"看不见却能点"的隐形按钮；后续用 set_imgs 换成合法资源即恢复可用。
 *
 * 交互（参考 btn 的事件状态机，由内核 pressed_obj 路由保证配对）：
 *   - PRESSED  → 切换按压态视觉并重绘；
 *   - RELEASED → 恢复常态视觉；
 *   - CLICKED  → 按下并在框内释放，触发 clicked_cb 回调。
 * event_cb 恒返回 1（交互控件消费事件）。
 * 按键（WE_IMGBTN_USE_KEY）：btn 同款 OK 双沿——按下沿进按压视觉，
 * 松开沿回弹并触发 clicked_cb，FLASH_END（取消）仅回弹不点击。
 *
 * 当前限制：
 *   - 双图尺寸不一致时统一按常态图包围盒命中与标脏；
 *   - 无 disabled 态、无长按重复触发。
 * -------------------------------------------------------------------------- */

/* 不透明图（RGB565 等）的按压变暗遮罩透明度（img_pressed == NULL 时生效，0~255） */
#ifndef WE_IMGBTN_DIM_OPA
#define WE_IMGBTN_DIM_OPA 90U
#endif

/* 带透明通道的图（ARGB8565 / A1~A8）按压时的整体透明度缩放系数（0~255，
 * 255 = 不变暗）：按 opacity * SCALE / 255 压低，避免方形黑影 */
#ifndef WE_IMGBTN_DIM_SCALE
#define WE_IMGBTN_DIM_SCALE 165U
#endif

struct we_imgbtn_obj_t;

/**
 * @brief 点击回调类型。
 * @param btn 触发回调的图片按钮控件指针。
 */
typedef void (*we_imgbtn_clicked_cb_t)(struct we_imgbtn_obj_t *btn);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_imgbtn_obj_t
{
    we_obj_t base;                      /* 必须在首位：w/h 取自 img_normal 资源头 */

    const uint8_t *img_normal;          /* 常态图资源指针（调用方持有） */
    const uint8_t *img_pressed;         /* 按压态图资源指针（可 NULL → 自动变暗） */
    we_imgbtn_clicked_cb_t clicked_cb;  /* 点击回调（可 NULL） */
    colour_t color;                     /* A1/A2/A4/A8 透明位图的前景色，默认白色 */
    uint8_t pressed;                    /* 当前按压视觉状态（1 = 按下） */
    uint8_t opacity;                    /* 整体不透明度（0~255） */
} we_imgbtn_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化图片按钮控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param img_normal 常态图资源指针（IMG_RGB565 未压缩，不可 NULL）。
 * @param img_pressed 按压态图资源指针；NULL = 按压时在常态图上叠半透明黑变暗。
 * @param clicked_cb 点击回调（可 NULL），按下并在框内释放时触发。
 * @return 无。
 * @note 控件宽高从 img_normal 资源头读出；两张图建议同尺寸
 *       （命中与重绘均按 img_normal 的包围盒进行）。
 *       任一张图格式不是 IMG_RGB565 时控件被置为不可用（不画、不可点、
 *       不可聚焦），可用 we_imgbtn_set_imgs 换合法资源恢复。
 *       同一个对象只能 init 一次；重复 init 会把已在链上的对象再次挂链。
 */
void we_imgbtn_obj_init(we_imgbtn_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y,
                        const uint8_t *img_normal, const uint8_t *img_pressed,
                        we_imgbtn_clicked_cb_t clicked_cb);

/**
 * @brief 运行时更换按钮图片（播放/暂停这类切图按钮用）。
 * @param obj 控件对象指针。
 * @param img_normal 新的常态图（IMG_RGB565 未压缩，不可 NULL）。
 * @param img_pressed 新的按压态图；NULL = 回到叠黑变暗。
 * @return 无。
 * @note 校验不通过时整体不改（不会出现半更新状态）；通过时同步刷新
 *       base.w/h 并对新旧包围盒各标脏一次，尺寸不同也不留残影。
 *       这是换图的唯一正确途径——不要对已挂链的对象重复调用 obj_init。
 */
void we_imgbtn_set_imgs(we_imgbtn_obj_t *obj, const uint8_t *img_normal,
                        const uint8_t *img_pressed);

/* --------------------------------------------------------------------------
 * 平凡 setter 收成头文件内联（与 img 控件同口径）：逻辑短、调用点少，
 * 比单独保留函数实体更有机会省 ROM。
 * -------------------------------------------------------------------------- */

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
static inline void we_imgbtn_set_opacity(we_imgbtn_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->base.lcd == NULL || obj->opacity == opacity)
    {
        return;
    }
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置 A1/A2/A4/A8 透明位图的前景色并按需重绘（其余格式忽略该颜色）。
 * @param obj 控件对象指针。
 * @param color 新的前景色。
 * @return 无。
 */
static inline void we_imgbtn_set_color(we_imgbtn_obj_t *obj, colour_t color)
{
    if (obj == NULL || obj->base.lcd == NULL)
    {
        return;
    }
#if (LCD_DEEP == DEEP_RGB565)
    if (obj->color.dat16 == color.dat16)
    {
        return;
    }
#elif (LCD_DEEP == DEEP_RGB888)
    if (obj->color.rgb.r == color.rgb.r && obj->color.rgb.g == color.rgb.g &&
        obj->color.rgb.b == color.rgb.b)
    {
        return;
    }
#endif

    obj->color = color;
    if (obj->opacity > 0U)
    {
        we_obj_invalidate((we_obj_t *)obj);
    }
}

/**
 * @brief 移动控件到新位置（左上角对齐到 x,y）。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 * @note 转调 we_obj_set_pos：焦点光标环的前后标脏由内核统一处理。
 */
static inline void we_imgbtn_set_pos(we_imgbtn_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_set_pos((we_obj_t *)obj, x, y);
}

/**
 * @brief 删除控件：从对象链表摘除并清空基类状态（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
static inline void we_imgbtn_obj_delete(we_imgbtn_obj_t *obj)
{
    we_obj_delete((we_obj_t *)obj);
}

#endif /* __WE_WIDGET_IMGBTN_H */
