#ifndef __WE_WIDGET_TABVIEW_H
#define __WE_WIDGET_TABVIEW_H

#include "we_gui_driver.h"
#include "we_motion.h"

/* --------------------------------------------------------------------------
 * 页签条控件（tabview）—— preview 孵化区实验控件
 *
 * 分段控制器（segmented control）样式：整条圆角底 + 横向等分 count 段，
 * active 段下方压一块圆角高亮块，各段文字居中（active 段文字更亮，
 * 非 active 文字向底条色方向压暗）。
 *
 * 高亮块滑动：切换 active 时高亮块 X 从当前位置平滑滑到目标段，
 * 由单个中央动画节点驱动（we_anim_t，链入 lcd->anim_head，不占 timer 槽），
 * Q8 进度 + we_ease_out_quad 缓动 + we_lerp 插值，全程整数运算。
 * 高亮块位置以相对控件左上角的偏移保存，移动控件不破坏动画几何。
 *
 * 控件本身只是"页签条"：页面内容的显隐由调用方在 changed_cb 里自行处理
 * （惯用法：每页一个 group，用 we_group_set_opacity(0/255) 显隐，
 *  全透明 group 不拦输入）。
 *
 * 数据驱动：labels 数组由调用方持有，控件只保存 const 指针，不拷贝文本。
 * 零 malloc、零浮点。删除前必须摘除动画节点（we_tabview_obj_delete 已代劳）。
 *
 * preview 限制：
 *   - 动画每步标脏整条控件包围盒（未做"旧位置+新位置"两块精细标脏）；
 *   - 文字不裁剪到段内，过长页签名会溢出到相邻段。
 * -------------------------------------------------------------------------- */

/* 高亮块与底条边缘的内边距（像素），可在包含本头文件前用宏覆盖 */
#ifndef WE_TABVIEW_PAD
#define WE_TABVIEW_PAD 3
#endif

/* 高亮块滑动动画时长（毫秒） */
#ifndef WE_TABVIEW_ANIM_MS
#define WE_TABVIEW_ANIM_MS 220U
#endif

/* 非 active 段文字向底条色压暗后保留的前景权重（0~255，越小越暗） */
#ifndef WE_TABVIEW_DIM_TEXT_A
#define WE_TABVIEW_DIM_TEXT_A 150
#endif

/* active 改变回调：tv 为 we_tabview_obj_t*，idx 为新 active 段序号 */
typedef void (*we_tabview_changed_cb_t)(void *tv, uint8_t idx);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_tabview_obj_t
{
    we_obj_t base;                      /* 必须在首位：base.x/y/w/h 为整条页签条 */

    const char *const *labels;          /* 页签名数组（调用方持有，count 个） */
    const unsigned char *font;          /* 字库（init 必传） */
    we_tabview_changed_cb_t changed_cb; /* active 改变回调（可为 NULL） */
    we_anim_t anim;                     /* 高亮块滑动动画节点（归控件所有，删除前必须摘链） */

    uint16_t anim_t;                    /* Q8 进度 0..256，>=256 表示空闲 */
    int16_t hl_from;                    /* 滑动起点偏移（相对 base.x） */
    int16_t hl_to;                      /* 滑动终点偏移（相对 base.x） */
    int16_t hl_ofs;                     /* 高亮块当前偏移（相对 base.x） */
    int16_t press_seg;                  /* 本次触摸按下的段序号，-1 = 无 */
    colour_t bar_color;                 /* 底条色 */
    colour_t hl_color;                  /* 高亮块色 */
    colour_t text_color;                /* active 文字色（非 active 自动压暗） */

    uint8_t count;                      /* 段数 */
    uint8_t active;                     /* 当前 active 段序号（0 起） */
    uint8_t opacity;                    /* 整体不透明度（0~255） */
} we_tabview_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化页签条控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param w 整条宽度（像素，横向等分 count 段）。
 * @param h 整条高度（像素）。
 * @param labels 页签名数组（调用方持有，count 个，不得含 NULL）。
 * @param count 段数（>=1）。
 * @return 无。
 * @note 默认：active = 0、暗灰底条/亮蓝高亮块/浅色文字、init 传入字体、不透明。
 */
void we_tabview_obj_init(we_tabview_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w, int16_t h,
                         const char *const *labels, uint8_t count,
                        const unsigned char *font);

/**
 * @brief 程序切换 active 段，高亮块平滑滑动到目标段。
 * @param obj 控件对象指针。
 * @param idx 目标段序号（越界或与当前相同则直接返回）。
 * @return 无。
 * @note 程序设置不触发 changed_cb（与 checkbox set_checked 约定一致）；
 *       点击交互路径才回调。
 */
void we_tabview_set_active(we_tabview_obj_t *obj, uint8_t idx);

/**
 * @brief 读取当前 active 段序号。
 * @param obj 控件对象指针。
 * @return active 段序号（0 起）；obj 为 NULL 时返回 0。
 */
uint8_t we_tabview_get_active(const we_tabview_obj_t *obj);

/**
 * @brief 注册 active 改变回调（点击切换且值变时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针（tv, idx），NULL 表示取消。
 * @return 无。
 */
void we_tabview_set_changed_cb(we_tabview_obj_t *obj, we_tabview_changed_cb_t cb);

/**
 * @brief 设置三项配色：底条色 / 高亮块色 / 文字色。
 * @param obj 控件对象指针。
 * @param bar 底条色。
 * @param hl 高亮块色。
 * @param text active 文字色（非 active 段自动向底条色压暗）。
 * @return 无。
 * @note 三项均与当前值相同时直接返回，不触发重绘。
 */
void we_tabview_set_colors(we_tabview_obj_t *obj, colour_t bar,
                           colour_t hl, colour_t text);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255），值未变时直接返回。
 * @return 无。
 */
void we_tabview_set_opacity(we_tabview_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件：先摘除动画节点（we_anim_stop）再从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_tabview_obj_delete(we_tabview_obj_t *obj);

#endif /* __WE_WIDGET_TABVIEW_H */
