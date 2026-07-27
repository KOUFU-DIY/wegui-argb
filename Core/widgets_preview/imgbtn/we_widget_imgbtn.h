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
 * 图片按钮控件（imgbtn）—— preview 孵化区实验控件
 *
 * 用一张（或两张）RGB565 未压缩图片资源充当按钮皮肤：
 *   - img_normal：常态图（必填），控件宽高直接取自资源头并写入 base.w/h；
 *   - img_pressed：按压态图（可 NULL）。传 NULL 时按压视觉退化为
 *     "常态图上叠一层半透明黑 we_fill_rect"（透明度 WE_IMGBTN_DIM_OPA，
 *     会再与整体 opacity 相乘），省一张资源。
 *
 * 资源格式与 img 控件一致（image_res.h 信息头 + 大端 RGB565 像素流），
 * 渲染直接走 we_img_render_rgb565（自带 PFB 裁剪与容器透明度级联）。
 * 图片指针由调用方持有，控件不拷贝。
 *
 * 交互（参考 btn 的事件状态机，由内核 pressed_obj 路由保证配对）：
 *   - PRESSED  → 切换按压态视觉并重绘；
 *   - RELEASED → 恢复常态视觉；
 *   - CLICKED  → 按下并在框内释放，触发 clicked_cb 回调。
 * event_cb 恒返回 1（交互控件消费事件）。
 * 按键（WE_IMGBTN_USE_KEY）：btn 同款 OK 双沿——按下沿进按压视觉，
 * 松开沿回弹并触发 clicked_cb，FLASH_END（取消）仅回弹不点击。
 *
 * preview 限制：
 *   - 仅支持 IMG_RGB565 未压缩格式（indexed QOI 等其他格式跳过不画）；
 *   - 按压变暗遮罩为整块矩形，不跟随图片透明区轮廓。
 * -------------------------------------------------------------------------- */

/* 按压变暗遮罩透明度（img_pressed == NULL 时生效，0~255） */
#ifndef WE_IMGBTN_DIM_OPA
#define WE_IMGBTN_DIM_OPA 90U
#endif

struct we_imgbtn_obj_t;

/**
 * @brief 点击回调类型。
 * @param btn 触发回调的图片按钮控件指针（可强转回 we_imgbtn_obj_t*）。
 */
typedef void (*we_imgbtn_clicked_cb_t)(void *btn);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_imgbtn_obj_t
{
    we_obj_t base;                      /* 必须在首位：w/h 取自 img_normal 资源头 */

    const uint8_t *img_normal;          /* 常态图资源指针（调用方持有） */
    const uint8_t *img_pressed;         /* 按压态图资源指针（可 NULL → 叠黑变暗） */
    we_imgbtn_clicked_cb_t clicked_cb;  /* 点击回调（可 NULL） */
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
 * @return 无。
 * @note 控件宽高从 img_normal 资源头读出；两张图建议同尺寸
 *       （命中与重绘均按 img_normal 的包围盒进行）。
 */
void we_imgbtn_obj_init(we_imgbtn_obj_t *obj, we_lcd_t *lcd, int16_t x, int16_t y,
                        const uint8_t *img_normal, const uint8_t *img_pressed);

/**
 * @brief 注册点击回调（按下并在框内释放时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示取消。
 * @return 无。
 */
void we_imgbtn_set_clicked_cb(we_imgbtn_obj_t *obj, we_imgbtn_clicked_cb_t cb);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_imgbtn_set_opacity(we_imgbtn_obj_t *obj, uint8_t opacity);

/**
 * @brief 移动控件到新位置（左上角对齐到 x,y）。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
void we_imgbtn_set_pos(we_imgbtn_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief 删除控件：从对象链表摘除并清空基类状态（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_imgbtn_obj_delete(we_imgbtn_obj_t *obj);

#endif /* __WE_WIDGET_IMGBTN_H */
