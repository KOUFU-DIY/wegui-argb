#ifndef __WE_WIDGET_QRCODE_H
#define __WE_WIDGET_QRCODE_H

#include "we_gui_driver.h"
#include "we_qr_encoder.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 二维码（qrcode）—— preview 孵化区实验控件
 *
 * 内置独立编码器（we_qr_encoder.c：byte mode / ECC M / 版本 1~4 自动选择），
 * we_qrcode_set_text() 内同步完成编码并把位矩阵拷入控件自身（每实例独立），
 * 渲染时逐行做暗模块横向 run 合并后 we_fill_rect 输出，含 4 模块静区。
 *
 * 控件宽高 = (模块数 + 2*4 静区) * module_px，编码成功后自动更新 base.w/h
 * （版本升降时先标脏旧区域再标脏新区域）。
 *
 * 内容超容量（> WE_QR_TEXT_MAX 字节）时编码失败，控件在当前区域画
 * "底色 + 两条对角粗线（叉）"错误占位，直到下一次 set_text 成功。
 *
 * 装饰性控件：event_cb 恒返回 0，输入穿透。
 * 零 malloc（位矩阵为控件定长成员，编码器工作区为文件级 static）、
 * 渲染内环零浮点、无动画节点（删除直接 we_obj_delete）。
 *
 * preview 限制：标脏按整控件包围盒；编码器不可重入（static 工作区）。
 * -------------------------------------------------------------------------- */

/* 静区宽度（模块数，规范要求 4） */
#define WE_QRCODE_QUIET_ZONE 4

typedef struct
{
    we_obj_t base;        /* 必须在首位：x/y/w/h 为控件外接矩形（含静区） */

    uint8_t module_px;    /* 每模块像素边长（2~6，init 时钳制） */
    uint8_t qr_size;      /* 当前矩阵边长（模块数 21/25/29/33；0=尚无有效编码） */
    uint8_t invert;       /* 反色开关（0=浅底深码，1=深底浅码） */
    uint8_t err_flag;     /* 最近一次 set_text 编码失败标志（画叉占位） */

    colour_t dark_color;  /* 暗模块颜色（数据"墨水"色） */
    colour_t light_color; /* 底色（静区 + 亮模块） */

    uint8_t text_len;     /* 当前文本长度（255=上次内容超长被拒，作哨兵） */
    char text[WE_QR_TEXT_MAX + 1]; /* 当前文本副本（用于内容变更检测） */

    uint8_t bits[WE_QR_MODULES_MAX][WE_QR_ROW_BYTES]; /* 位压缩模块矩阵 */
} we_qrcode_obj_t;

/**
 * @brief 初始化二维码控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标，含静区）。
 * @param y 左上角 Y 坐标。
 * @param module_px 每模块像素边长，钳制到 2~6。
 * @return 无。
 * @note 初始无内容：控件按版本 1 占位尺寸 (21+8)*module_px 显示纯底色面板；
 *       调用 we_qrcode_set_text() 后尺寸自动改为 (模块数+8)*module_px。
 *       默认近黑码色 + 近白底色、不反色。
 */
void we_qrcode_obj_init(we_qrcode_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, uint8_t module_px);

/**
 * @brief 设置二维码内容（ASCII byte mode），内容变化才重新编码重绘。
 * @param obj 控件对象指针。
 * @param str 待编码字符串（NUL 结尾；NULL 按空串处理）。
 * @return 0 表示编码成功，-1 表示失败（超过 WE_QR_TEXT_MAX 字节）。
 * @note 失败时控件转为错误占位显示（底色 + 对角叉），尺寸保持不变；
 *       内容与当前一致时直接返回上次结果，不触发编码与重绘。
 */
int8_t we_qrcode_set_text(we_qrcode_obj_t *obj, const char *str);

/**
 * @brief 设置暗模块颜色与底色。
 * @param obj 控件对象指针。
 * @param dark 暗模块颜色（数据"墨水"色）。
 * @param light 底色（静区 + 亮模块）。
 * @return 无。
 * @note 两色均未变化时直接返回，不触发重绘。
 */
void we_qrcode_set_colors(we_qrcode_obj_t *obj, colour_t dark, colour_t light);

/**
 * @brief 设置反色显示（深底浅码）。
 * @param obj 控件对象指针。
 * @param invert 0=正常（浅底深码），非0=反色。
 * @return 无。
 * @note 值未变化时直接返回。反色码依赖扫码端容错，实际产品慎用。
 */
void we_qrcode_set_invert(we_qrcode_obj_t *obj, uint8_t invert);

/**
 * @brief 删除二维码控件（无动画节点，直接摘链）。
 * @param obj 控件对象指针。
 * @return 无。
 */
static inline void we_qrcode_obj_delete(we_qrcode_obj_t *obj) { we_obj_delete((we_obj_t *)obj); }

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_QRCODE_H */
