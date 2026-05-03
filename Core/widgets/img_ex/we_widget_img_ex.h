#ifndef WE_WIDGET_IMG_EX_H
#define WE_WIDGET_IMG_EX_H

#include "image_res.h"
#include "we_gui_driver.h"
/* --------------------------------------------------------------------------
 * img_ex 使用说明
 *
 * 当前版本的旋转缩放控件，只支持“未压缩的 RGB565 原图”：
 * - 支持：IMG_RGB565
 * - 不支持：RLE / QOI / 索引QOI / 其他压缩格式
 *
 * 原因很直接：
 * 1. img_ex 的核心是逐像素逆映射采样
 * 2. 每个目标像素都需要快速随机访问源图像素
 * 3. 压缩图无法做到这种低成本随机访问
 *
 * 所以如果要给 img_ex 使用图片资源，建议直接准备原始 RGB565 图。
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * 图片旋转缩放控件的取样模式
 * 0:临近插值, 更高的性能
 * 1:线性插值, 更高的质量
 * -------------------------------------------------------------------------- */
#ifndef WE_IMG_EX_SAMPLE_MODE
#define WE_IMG_EX_SAMPLE_MODE (0)
#endif

/* --------------------------------------------------------------------------
 * 是否开启旋转图的边缘羽化抗锯齿
 * 0:关闭, 更高的性能
 * 1:打开, 更高的质量
 * -------------------------------------------------------------------------- */
#ifndef WE_IMG_EX_ENABLE_EDGE_AA
#define WE_IMG_EX_ENABLE_EDGE_AA (1)
#endif

/* --------------------------------------------------------------------------
 * 是否启用 RGB565 专用快速混色
 * 无需更改, 自动打开
 * -------------------------------------------------------------------------- */
#ifndef WE_IMG_EX_ENABLE_FAST_RGB565_BLEND
#if (LCD_DEEP == DEEP_RGB565)
#define WE_IMG_EX_ENABLE_FAST_RGB565_BLEND 1
#else
#define WE_IMG_EX_ENABLE_FAST_RGB565_BLEND 0
#endif
#endif

/* --------------------------------------------------------------------------
 * 是否打开旋转图控件的全局透明度功能
 * 0:关闭, 更高的性能
 * 1:打开, 更完整的功能
 * -------------------------------------------------------------------------- */
#ifndef WE_IMG_EX_ASSUME_OPAQUE
#define WE_IMG_EX_ASSUME_OPAQUE 0
#endif

/* --------------------------------------------------------------------------
 * 是否使用精细脏矩形策略
 * 0:使用完整的矩形, 更低的内存占用
 * 1:使用精细的矩形, 更高的刷图性能
 * -------------------------------------------------------------------------- */
#ifndef WE_IMG_EX_USE_TIGHT_BBOX
#define WE_IMG_EX_USE_TIGHT_BBOX 1
#endif

typedef struct
{
    we_obj_t base;
    /* 控件基类。
     * 1. 基类的 x / y：控件包围盒左上角的绝对坐标
     * 2. 基类的 w / h：控件当前包围盒尺寸
     */

    const uint8_t *img;
    int16_t cx;
    /* 变换中心在屏幕上的 X 坐标。
     *
     * 这里不是控件左上角坐标，
     * 而是“这张图最终绕着屏幕上哪个点旋转 / 缩放”的中心点。
     *
     * 例如：
     * 做仪表盘指针时，这里通常就是表盘圆心的 X。
     */

    int16_t cy;
    /* 变换中心在屏幕上的 Y 坐标。
     *
     * 和 cx 配套使用，
     */

    int16_t pivot_ofs_x;
    /* 原图内部旋转中心，相对“图片几何中心”的 X 偏移。
     *
     * 作用：
     * 用来做偏心旋转。
     *
     * 如果是普通绕图片中心旋转：
     * pivot_ofs_x = 0
     *
     * 如果图片真正的旋转根部不在图片正中心，
     * 例如仪表盘指针根部在图片左侧，
     * 那么这里就需要写成负值或正值做补偿。
     *
     * 注意：
     * 这是“图片局部坐标系”里的偏移，
     * 不是屏幕坐标。
     */

    int16_t pivot_ofs_y;
    /* 和 pivot_ofs_x 配套使用。
     * 两者一起决定图片内部真正参与旋转的轴心位置。
     */

    int16_t angle;
    /* 角度
       目前单位精度是1度。
     */

    uint16_t scale_256;
    /* 当前缩放系数，使用 256 作为 1.0 倍的定点基准。
     *
     * 典型值：
     * 256 = 1.00 倍
     * 128 = 0.50 倍
     * 512 = 2.00 倍
     *
     * 这样做的目的，是避免在 MCU 上使用浮点缩放。
     */

    uint8_t opacity;
    /* 控件整体透明度，
       范围 0~255
     */
} we_img_ex_obj_t;

/**
 * @brief 初始化旋转缩放图像控件并计算初始包围盒。
 * @param obj 目标控件对象指针。
 * @param p_lcd GUI 运行时 LCD 上下文指针。
 * @param cx 变换中心 X 坐标（屏幕坐标）。
 * @param cy 变换中心 Y 坐标（屏幕坐标）。
 * @param img 图像资源数据指针（仅支持未压缩 RGB565）。
 * @param op 控件整体不透明度（0~255）。
 * @return 无。
 */
void we_img_ex_obj_init(we_img_ex_obj_t *obj, we_lcd_t *p_lcd, int16_t cx, int16_t cy, const uint8_t *img, uint8_t op);

/**
 * @brief 设置旋转缩放中心点并更新包围盒。
 * @param obj 目标控件对象指针。
 * @param cx 新的变换中心 X 坐标（屏幕坐标）。
 * @param cy 新的变换中心 Y 坐标（屏幕坐标）。
 * @return 无。
 */
void we_img_ex_obj_set_center(we_img_ex_obj_t *obj, int16_t cx, int16_t cy);

/**
 * @brief 设置源图旋转轴心相对图像中心的偏移。
 * @param obj 目标控件对象指针。
 * @param ofs_x 源图旋转轴心的 X 偏移（相对图像几何中心，像素）。
 * @param ofs_y 源图旋转轴心的 Y 偏移（相对图像几何中心，像素）。
 * @return 无。
 */
void we_img_ex_obj_set_pivot_offset(we_img_ex_obj_t *obj, int16_t ofs_x, int16_t ofs_y);

/**
 * @brief 设置旋转角度与缩放比例并更新包围盒。
 * @param obj 目标控件对象指针。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x）。
 * @return 无。
 */
void we_img_ex_obj_set_transform(we_img_ex_obj_t *obj, int16_t angle, uint16_t scale_256);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_img_ex_obj_set_opacity(we_img_ex_obj_t *obj, uint8_t opacity);

#endif

