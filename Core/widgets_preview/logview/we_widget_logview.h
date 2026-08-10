#ifndef __WE_WIDGET_LOGVIEW_H
#define __WE_WIDGET_LOGVIEW_H

#include "we_gui_driver.h"
#include "we_scroll.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪日志窗的按键回调与可聚焦性，其余控件不受影响。
 * 键控滚动依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_LOGVIEW_USE_KEY
#define WE_LOGVIEW_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 滚动日志窗（logview）—— preview 孵化区实验控件
 *
 * 深色圆角面板 + 等行高文本的滚动日志窗：we_logview_push() 逐条追加，
 * 最新行显示在底部。行存储为调用方提供的扁平二维缓冲
 *（line_cnt 行 x line_len 字节），环形复用——写满后最旧行被覆盖，
 * 控件自身零 malloc。
 *
 * 滚动模型：scroll_px 为距"内容底部对齐"位置的向上偏移
 *（0 = 贴底显示最新行，越大越往历史方向），硬夹紧无回弹。
 * 自动跟随（follow）态下每次 push 保持贴底；拖拽离开底部自动暂停跟随，
 * 拖回最底（scroll_px 回到 0）或调 we_logview_set_follow(1) 恢复。
 * 按键（WE_LOGVIEW_USE_KEY，依赖编辑态）：OK 进出编辑态，编辑态上键
 * 按行高上翻历史（自动暂停跟随）、下键回向最新（滚回贴底自动恢复跟随，
 * 与拖拽同语义）；内容不溢出时拒绝聚焦。
 *
 * 渲染：圆角面板背景 + PFB 收窄裁剪的逐行文字（半露行不渗出边界）+
 * 右缘细滚动条（内容溢出时常显，位置按滚动比例）。
 *
 * 交互控件：event_cb 恒返回 1（消费事件）；拖拽跟手滚动，无惯性。
 * 删除用 we_logview_obj_delete（无动画节点，直接摘链）。
 *
 * preview 限制：标脏按整控件包围盒；push 即整窗重绘。
 * -------------------------------------------------------------------------- */

/* 行文字左右内边距（像素） */
#ifndef WE_LOGVIEW_TEXT_PAD
#define WE_LOGVIEW_TEXT_PAD 8
#endif

/* 面板上下内边距（像素） */
#ifndef WE_LOGVIEW_V_PAD
#define WE_LOGVIEW_V_PAD 4
#endif

/* 面板默认圆角半径（像素） */
#ifndef WE_LOGVIEW_DEF_RADIUS
#define WE_LOGVIEW_DEF_RADIUS 8U
#endif

/* 判定为拖拽滚动的位移阈值（像素） */
#ifndef WE_LOGVIEW_DRAG_THRESHOLD
#define WE_LOGVIEW_DRAG_THRESHOLD 5
#endif

/* 滚动条几何：滑块宽 / 距右缘边距 / 透明度 */
#ifndef WE_LOGVIEW_SB_WIDTH
#define WE_LOGVIEW_SB_WIDTH 4
#endif
#ifndef WE_LOGVIEW_SB_MARGIN
#define WE_LOGVIEW_SB_MARGIN 3
#endif
#ifndef WE_LOGVIEW_SB_OPA
#define WE_LOGVIEW_SB_OPA 120U
#endif

typedef struct we_logview_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h 为控件外接矩形 */

    /* 4 字节对齐成员在前，消 padding */
    char *line_buf;             /* 行缓冲基址（调用方提供，line_cnt x line_len 扁平数组） */
    const unsigned char *font;  /* 字体资源（init 必传） */
    we_scroll_t sc;             /* 滚动物理状态机（无惯性档；scroll_px 为距底偏移，
                                 * 方向与常规相反，喂入时主轴坐标取负） */
    int32_t scroll_px;          /* 距底部对齐位置的向上偏移（0 = 贴底） */

    /* 2 字节成员 */
    uint16_t line_len;          /* 单行字节容量（含结尾 \0） */
    uint16_t line_cnt;          /* 行槽总数 */
    uint16_t head;              /* 环形写指针：下一条日志写入的槽位 */
    uint16_t used;              /* 已写入的有效行数（<= line_cnt） */
    uint16_t row_h;             /* 行高（像素，= 字体行高 + 2） */
    uint16_t radius;            /* 面板圆角半径 */
    colour_t bg_color;          /* 面板底色 */
    colour_t text_color;        /* 日志文字色 */
    colour_t sb_color;          /* 滚动条滑块色 */

    /* 1 字节成员与状态位域 */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */
    uint8_t follow : 1;         /* 自动跟随最新行标志 */
} we_logview_obj_t;

/**
 * @brief 初始化日志窗控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param h 控件高度（像素）。
 * @param line_buf 行缓冲基址（调用方持有，line_cnt x line_len 字节，生命周期须覆盖控件）。
 * @param line_len 单行字节容量（含 \0，建议 >= 24）。
 * @param line_cnt 行槽总数（环形复用）。
 * @return 无。
 * @note 默认：深色面板、自动跟随开启、无日志内容；
 *       line_buf 为 NULL 或容量参数为 0 时拒绝初始化。
 */
void we_logview_obj_init(we_logview_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, int16_t w, int16_t h,
                         char *line_buf, uint16_t line_len, uint16_t line_cnt,
                        const unsigned char *font);

/**
 * @brief 追加一条日志（拷贝进环形行缓冲，超长截断）。
 * @param obj 控件对象指针。
 * @param str 日志字符串（UTF-8，拷贝后可释放/复用）。
 * @return 无。
 * @note 处于自动跟随态时 push 后保持滚动贴底显示最新行；
 *       非跟随态（用户正在查看历史）时保持当前视口内容不动。
 */
void we_logview_push(we_logview_obj_t *obj, const char *str);

/**
 * @brief 清空全部日志并复位滚动。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_logview_clear(we_logview_obj_t *obj);

/**
 * @brief 设置面板底色与文字色。
 * @param obj 控件对象指针。
 * @param bg 面板底色。
 * @param text 日志文字色。
 * @return 无。
 * @note 两色均未变化时直接返回，不触发重绘。
 */
void we_logview_set_colors(we_logview_obj_t *obj, colour_t bg, colour_t text);

/**
 * @brief 开关自动跟随最新行。
 * @param obj 控件对象指针。
 * @param follow 0=暂停跟随（视口停在当前位置），非0=恢复跟随并滚到最新。
 * @return 无。
 */
void we_logview_set_follow(we_logview_obj_t *obj, uint8_t follow);

/**
 * @brief 查询当前是否处于自动跟随态。
 * @param obj 控件对象指针。
 * @return 1=跟随中，0=已暂停或 obj 为 NULL。
 */
uint8_t we_logview_get_follow(const we_logview_obj_t *obj);

/**
 * @brief 设置控件整体透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_logview_set_opacity(we_logview_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除日志窗控件（无动画节点，直接摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_logview_obj_delete(we_logview_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_LOGVIEW_H */
