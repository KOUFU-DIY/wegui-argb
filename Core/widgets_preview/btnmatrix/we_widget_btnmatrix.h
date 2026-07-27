#ifndef __WE_WIDGET_BTNMATRIX_H
#define __WE_WIDGET_BTNMATRIX_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 按键矩阵控件（btnmatrix）—— preview 孵化区实验控件
 *
 * 一个字符串数组渲染整面按键：labels 按行优先排列（rows*cols 个），
 * NULL 或 "" 表示该格为空位（不绘制、不响应输入）。
 * 控件内部把 base.w/base.h 等分成 rows x cols 网格（格间距 WE_BTNMATRIX_GAP），
 * 每键绘制解析抗锯齿圆角底 + 居中文字，按压键切换按压底色。
 *
 * 交互状态机（与 btn 同源）：
 *   PRESSED  命中非空格 -> 记录格序号并高亮；
 *   STAY     拖出原格   -> 取消按压态（本次触摸不再产生点击）；
 *   CLICKED  在原格释放 -> 触发 clicked_cb(bm, key_idx, label)。
 *
 * 数据驱动：labels 数组由调用方持有，控件只保存 const 指针，不拷贝文本。
 * 零 malloc、零浮点；无动画节点（删除无需摘链）。
 *
 * preview 限制：
 *   - 标脏放宽为整控件包围盒粒度之内（单格标脏已做，文字不裁剪到格内，
 *     过长文本会溢出按键，毕业前需按格子做 PFB 裁剪）；
 *   - rows*cols 须 <= 255（回调 key_idx 为 uint8_t）；
 *   - 每次重绘遍历全部格子，依赖 PFB 裁剪丢弃格外写入，毕业前需
 *     按脏区做格子级裁剪提速。
 * -------------------------------------------------------------------------- */

/* 格间距（像素，含行距与列距），可在包含本头文件前用宏覆盖 */
#ifndef WE_BTNMATRIX_GAP
#define WE_BTNMATRIX_GAP 5
#endif

/* 按键圆角半径（像素），绘制时按格宽/格高各半自动钳制 */
#ifndef WE_BTNMATRIX_RADIUS
#define WE_BTNMATRIX_RADIUS 8U
#endif

/* 点击回调：bm 为 we_btnmatrix_obj_t*，key_idx 为行优先格序号，label 为对应键名 */
typedef void (*we_btnmatrix_clicked_cb_t)(void *bm, uint8_t key_idx, const char *label);

/* --------------------------------------------------------------------------
 * 控件结构体
 * -------------------------------------------------------------------------- */
typedef struct we_btnmatrix_obj_t
{
    we_obj_t base;                        /* 必须在首位：base.x/y/w/h 为矩阵外接矩形 */

    const char *const *labels;            /* 键名数组（调用方持有，行优先 rows*cols 个） */
    const unsigned char *font;            /* 字库（init 必传） */
    we_btnmatrix_clicked_cb_t clicked_cb; /* 点击回调（可为 NULL） */

    int16_t press_idx;                    /* 本次触摸按下的格序号，-1 = 无 */
    colour_t bg_color;                    /* 普通按键底色 */
    colour_t bg_press_color;              /* 按压按键底色 */
    colour_t text_color;                  /* 键名文字色 */

    uint8_t rows;                         /* 行数 */
    uint8_t cols;                         /* 列数 */
    uint8_t opacity;                      /* 整体不透明度（0~255） */
    uint8_t pressed : 1;                  /* 1 = 按压高亮显示中 */
} we_btnmatrix_obj_t;

/* --------------------------------------------------------------------------
 * API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化按键矩阵控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 外接矩形左上角 X（屏幕绝对坐标）。
 * @param y 外接矩形左上角 Y。
 * @param w 外接矩形宽度（像素）。
 * @param h 外接矩形高度（像素）。
 * @param labels 键名数组，行优先排列，须包含 rows*cols 个元素；
 *               NULL 或 "" 元素表示该格空位（不绘制、不响应）。
 *               数组与文本由调用方持有，控件只存指针。
 * @param rows 行数（>=1）。
 * @param cols 列数（>=1，且 rows*cols <= 255）。
 * @return 无。
 * @note 默认：暗灰底/亮蓝按压底/浅色文字、init 传入字体、不透明。
 */
void we_btnmatrix_obj_init(we_btnmatrix_obj_t *obj, we_lcd_t *lcd,
                           int16_t x, int16_t y, int16_t w, int16_t h,
                           const char *const *labels, uint8_t rows, uint8_t cols,
                        const unsigned char *font);

/**
 * @brief 注册按键点击回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针（bm, key_idx, label），NULL 表示取消。
 * @return 无。
 */
void we_btnmatrix_set_clicked_cb(we_btnmatrix_obj_t *obj, we_btnmatrix_clicked_cb_t cb);

/**
 * @brief 设置三项配色：普通底色 / 按压底色 / 文字色。
 * @param obj 控件对象指针。
 * @param bg 普通按键底色。
 * @param bg_press 按压按键底色。
 * @param text 键名文字色。
 * @return 无。
 * @note 三项均与当前值相同时直接返回，不触发重绘。
 */
void we_btnmatrix_set_colors(we_btnmatrix_obj_t *obj, colour_t bg,
                             colour_t bg_press, colour_t text);

/**
 * @brief 设置整体不透明度并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255），值未变时直接返回。
 * @return 无。
 */
void we_btnmatrix_set_opacity(we_btnmatrix_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件并从对象链表移除（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_btnmatrix_obj_delete(we_btnmatrix_obj_t *obj);

#endif /* __WE_WIDGET_BTNMATRIX_H */
