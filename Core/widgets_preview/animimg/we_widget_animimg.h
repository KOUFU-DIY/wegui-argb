#ifndef __WE_WIDGET_ANIMIMG_H
#define __WE_WIDGET_ANIMIMG_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 帧动画控件（animimg）—— preview 孵化区实验控件
 *
 * 按固定间隔循环播放一组裸像素帧：
 *   - 每帧是 w×h 个 RGB565 像素的裸数组（uint16_t，本机字节序，
 *     不带 image_res 资源头），帧指针数组与像素数据均由调用方持有；
 *   - 帧推进挂在中央动画引擎上（单个 we_anim_t 节点，不占 GUI timer 槽），
 *     step_cb 累计 elapsed_ms，跨过 interval_ms 才换帧，帧号变化才标脏；
 *   - 渲染为控件自写的带 PFB 条带裁剪逐像素 blit（裁剪套路照 box 角落
 *     合成函数），we_color_from_rgb565 转设备色 + we_store_color 落盘，
 *     半透明时走 we_store_blended_color 混色。
 *
 * 零 malloc、渲染内环零浮点。删除控件必须走 we_animimg_obj_delete
 * （内部先 we_anim_stop 摘链再 we_obj_delete，动画节点归控件所有）。
 * 默认装饰性（不消费输入，事件穿透给背后控件）。
 *
 * preview 限制：
 *   - 标脏按整控件包围盒（换帧即整幅重绘，未做帧间 diff）；
 *   - 帧尺寸必须与控件 w/h 严格一致，控件不做缩放/居中。
 * -------------------------------------------------------------------------- */

/* 默认帧间隔（毫秒，set_frames 未指定合法值时的兜底） */
#ifndef WE_ANIMIMG_DEF_INTERVAL_MS
#define WE_ANIMIMG_DEF_INTERVAL_MS 100U
#endif

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_animimg_obj_t
{
    we_obj_t base;                    /* 必须在首位：w/h = 帧像素尺寸 */

    const uint16_t *const *frames;    /* 帧指针数组（调用方持有，可 NULL） */
    uint8_t  frame_cnt;               /* 帧数 */
    uint8_t  cur;                     /* 当前帧号（0..frame_cnt-1） */
    uint8_t  playing;                 /* 1 = 播放中 */
    uint8_t  opacity;                 /* 整体不透明度（0~255） */
    uint16_t interval_ms;             /* 帧间隔（毫秒，>=1） */
    uint16_t acc_ms;                  /* 帧推进累计毫秒 */

    we_anim_t anim;                   /* 中央动画节点（归控件所有，删除前必须摘链） */
} we_animimg_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化帧动画控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param w 帧宽（像素，> 0）。
 * @param h 帧高（像素，> 0）。
 * @return 无。
 * @note 初始无帧数据、停止状态；先 we_animimg_set_frames 再 we_animimg_start。
 */
void we_animimg_obj_init(we_animimg_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w, int16_t h);

/**
 * @brief 绑定帧序列（裸 RGB565 像素帧，调用方持有）并回到第 0 帧。
 * @param obj 控件对象指针。
 * @param frames 帧指针数组，每帧为 w×h 个 uint16_t RGB565 像素（本机字节序）。
 * @param frame_cnt 帧数（0 或 frames 为 NULL 表示清空）。
 * @param interval_ms 帧间隔（毫秒，0 时取 WE_ANIMIMG_DEF_INTERVAL_MS）。
 * @return 无。
 * @note 总是触发一次重绘（同一指针下帧内容可能已被调用方重新生成）；
 *       播放状态保持不变，清空帧后绘制与推进自动空转。
 */
void we_animimg_set_frames(we_animimg_obj_t *obj, const uint16_t *const *frames,
                           uint8_t frame_cnt, uint16_t interval_ms);

/**
 * @brief 开始播放（把帧推进节点挂上中央动画引擎）。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 无帧数据时忽略；重复 start 安全（we_anim_start 已在链上则只更新回调）。
 */
void we_animimg_start(we_animimg_obj_t *obj);

/**
 * @brief 停止播放（把帧推进节点从中央动画引擎摘除，停在当前帧）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_animimg_stop(we_animimg_obj_t *obj);

/**
 * @brief 设置帧间隔并按需生效。
 * @param obj 控件对象指针。
 * @param interval_ms 帧间隔（毫秒，0 会被钳为 1）。
 * @return 无。
 * @note 值未变时直接返回；已累计的毫秒保留，下一次跨帧按新间隔判定。
 */
void we_animimg_set_interval(we_animimg_obj_t *obj, uint16_t interval_ms);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_animimg_set_opacity(we_animimg_obj_t *obj, uint8_t opacity);

/**
 * @brief 移动控件到新位置（左上角对齐到 x,y）。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 * @return 无。
 */
void we_animimg_set_pos(we_animimg_obj_t *obj, int16_t x, int16_t y);

/**
 * @brief 删除控件：先摘除动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 动画节点归控件所有，必须先摘链，否则中央动画链留悬空指针。
 */
void we_animimg_obj_delete(we_animimg_obj_t *obj);

#endif /* __WE_WIDGET_ANIMIMG_H */
