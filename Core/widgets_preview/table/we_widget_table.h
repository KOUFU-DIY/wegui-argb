#ifndef __WE_WIDGET_TABLE_H
#define __WE_WIDGET_TABLE_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪表格的按键回调与可聚焦性，其余控件不受影响。
 * 键控滚动依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_TABLE_USE_KEY
#define WE_TABLE_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 简易表格（table）—— preview 孵化区实验控件
 *
 * 表头行固定顶部 + 数据行区可拖拽垂直滚动的网格表格：
 *   - 表头行 = cells 第 0 行，带底色，不随滚动移动；
 *   - 数据行区经 PFB 窗口收窄裁剪，半露行不渗出；
 *   - 单元格文本左对齐 + 每列独立 PFB 收窄裁剪，超宽文本按列边界截断；
 *   - 1px 低透明度网格线（列分隔竖线 + 行分隔横线）；
 *   - 可选斑马纹隔行着色；
 *   - 内容超高时右缘常显细滚动条（胶囊滑块）。
 *
 * 数据驱动：cells 为行优先一维字符串指针数组（row_cnt × col_cnt，
 * 含表头行），由调用方持有，控件只保存 const 指针绝不拷贝文本。
 * 调用方原地改写单元格缓冲内容后，调 we_table_refresh 触发重绘。
 *
 * 列宽按权重分配：col_edge[i] = w × 权重前缀和 / 权重总和，权重存入
 * 控件内定长数组（<= WE_TABLE_COL_MAX），NULL = 全列等分。
 *
 * 滚动模型：scroll_px 像素级累计偏移（int32），硬夹紧到
 * [0, 数据区内容高 - 数据区高]，无回弹无惯性。
 * 按键（WE_TABLE_USE_KEY，依赖编辑态）：OK 进出编辑态，编辑态上下键
 * 按行高步进滚动；内容不溢出时拒绝聚焦（无可滚动量的死停靠点）。
 *
 * 零 malloc；渲染内环纯整数无浮点；无动画节点，删除直接
 * we_table_obj_delete（无需 we_anim_stop）。
 *
 * preview 限制：任何状态变化按整控件包围盒标脏；无单元格点击回调。
 * -------------------------------------------------------------------------- */

/* 最大列数（列权重与列边界缓存的定长数组上限） */
#ifndef WE_TABLE_COL_MAX
#define WE_TABLE_COL_MAX 6
#endif

/* 行内上下边距（像素）：默认行高 = 字体行高 + 2 * PAD */
#ifndef WE_TABLE_ROW_PAD
#define WE_TABLE_ROW_PAD 6U
#endif

/* 单元格文本左内边距（像素） */
#ifndef WE_TABLE_CELL_PAD_X
#define WE_TABLE_CELL_PAD_X 6
#endif

/* 判定为拖拽滚动的位移阈值（像素） */
#ifndef WE_TABLE_DRAG_THRESHOLD
#define WE_TABLE_DRAG_THRESHOLD 6
#endif

/* 网格线透明度（0~255，低透明度淡线） */
#ifndef WE_TABLE_GRID_OPA
#define WE_TABLE_GRID_OPA 56U
#endif

/* 滚动条几何：滑块宽 / 距右缘边距 / 透明度 */
#ifndef WE_TABLE_SB_WIDTH
#define WE_TABLE_SB_WIDTH 4
#endif
#ifndef WE_TABLE_SB_MARGIN
#define WE_TABLE_SB_MARGIN 2
#endif
#ifndef WE_TABLE_SB_OPA
#define WE_TABLE_SB_OPA 120U
#endif

typedef struct we_table_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h 为控件外接矩形 */

    /* 4 字节对齐成员在前，消 padding */
    const char *const *cells;   /* 行优先单元格文本数组（调用方持有，含表头行） */
    const unsigned char *font;  /* 字体资源（init 必传） */
    int32_t scroll_px;          /* 数据行区像素级滚动偏移（0 = 顶部对齐） */
    int32_t press_scroll;       /* 按下时 scroll_px */

    /* 2 字节成员 */
    uint16_t row_cnt;           /* 总行数（含表头行 = 第 0 行） */
    uint16_t row_h;             /* 行高（像素，表头与数据行同高） */
    int16_t press_y;            /* 按下时触摸 Y */
    int16_t col_edge[WE_TABLE_COL_MAX + 1]; /* 缓存：列边界相对 X（0..w） */
    colour_t head_bg_color;     /* 表头行底色 */
    colour_t head_text_color;   /* 表头文字色 */
    colour_t cell_text_color;   /* 数据单元格文字色 */
    colour_t grid_color;        /* 网格线颜色（以低透明度绘制） */
    colour_t zebra_color;       /* 斑马纹行底色 */
    colour_t sb_color;          /* 滚动条滑块色 */

    /* 1 字节成员与状态位域 */
    uint8_t col_weight[WE_TABLE_COL_MAX];   /* 列宽权重（>=1） */
    uint8_t col_cnt;            /* 列数（1..WE_TABLE_COL_MAX） */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */
    uint8_t zebra : 1;          /* 斑马纹隔行着色开关 */
    uint8_t tracking : 1;       /* 本次触摸序列是否有效 */
    uint8_t dragging : 1;       /* 是否已进入拖拽滚动 */
} we_table_obj_t;

/**
 * @brief 初始化表格控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param h 控件高度（像素）。
 * @param col_cnt 列数，钳制到 [1, WE_TABLE_COL_MAX]。
 * @return 无。
 * @note 字体 init 必传、行高 = 字体行高 + 2*WE_TABLE_ROW_PAD、
 *       全列等分、斑马纹开、深色主题配色、初始无数据
 *       （需再调 we_table_set_cells）。
 */
void we_table_obj_init(we_table_obj_t *obj, we_lcd_t *lcd,
                       int16_t x, int16_t y, int16_t w, int16_t h, uint8_t col_cnt,
                        const unsigned char *font);

/**
 * @brief 绑定单元格文本数组（控件只保存指针，不复制内容）。
 * @param obj 控件对象指针。
 * @param cells 行优先一维字符串指针数组（row_cnt × col_cnt，第 0 行 =
 *              表头行），需在控件生命周期内保持有效；元素可为 NULL（留空）。
 * @param row_cnt 总行数（含表头行）。
 * @return 无。
 * @note 绑定后滚动复位到顶部；数组指针与行数均未变时直接返回
 *       （原地改写缓冲内容请改用 we_table_refresh 触发重绘）。
 */
void we_table_set_cells(we_table_obj_t *obj, const char *const *cells, uint16_t row_cnt);

/**
 * @brief 设置列宽权重（按权重占比分配列宽）。
 * @param obj 控件对象指针。
 * @param weights 权重数组（长度 >= 列数），拷入控件定长数组；
 *                元素 0 按 1 处理；传 NULL = 全列等分。
 * @return 无。
 * @note 权重全部未变时直接返回。
 */
void we_table_set_col_weights(we_table_obj_t *obj, const uint8_t *weights);

/**
 * @brief 设置行高（像素，表头与数据行同高）。
 * @param obj 控件对象指针。
 * @param row_h 新行高（0 时忽略；值未变直接返回）。
 * @return 无。
 * @note 修改后滚动偏移会重新夹紧到新内容高度范围内。
 */
void we_table_set_row_h(we_table_obj_t *obj, uint16_t row_h);

/**
 * @brief 设置五项配色（全部未变时直接返回）。
 * @param obj 控件对象指针。
 * @param head_bg 表头行底色。
 * @param head_text 表头文字色。
 * @param cell_text 数据单元格文字色。
 * @param grid 网格线颜色。
 * @param zebra 斑马纹行底色。
 * @return 无。
 */
void we_table_set_colors(we_table_obj_t *obj, colour_t head_bg, colour_t head_text,
                         colour_t cell_text, colour_t grid, colour_t zebra);

/**
 * @brief 斑马纹隔行着色开关（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param en 1 开启，0 关闭。
 * @return 无。
 */
void we_table_set_zebra(we_table_obj_t *obj, uint8_t en);

/**
 * @brief 单元格内容原地更新后的手动重绘接口。
 * @param obj 控件对象指针。
 * @return 无。
 * @note cells 数据由调用方持有：调用方直接改写某单元格指向的缓冲内容后，
 *       控件无法感知，需调本接口按整控件包围盒标脏重绘。
 */
void we_table_refresh(we_table_obj_t *obj);

/**
 * @brief 设置整体不透明度并按需重绘（值未变时直接返回）。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_table_set_opacity(we_table_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除表格控件并从对象链表移除（无动画节点，无需摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_table_obj_delete(we_table_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_TABLE_H */
