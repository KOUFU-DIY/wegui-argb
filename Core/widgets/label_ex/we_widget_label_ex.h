#ifndef WE_WIDGET_LABEL_EX_H
#define WE_WIDGET_LABEL_EX_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * label_ex 使用说明
 *
 * label_ex 是支持旋转和缩放的高级文字控件，渲染原理与 img_ex 相同：
 * 对屏幕包围盒内每个目标像素做逆映射，从字模 flash 数据直接采样后混色输出。
 * 无需额外 RAM 缓冲区，结构体本身仅缓存文字宽度用于包围盒计算。
 *
 * 使用步骤：
 * 1. 调用 we_label_ex_obj_init() 完成初始化。
 * 2. 文字内容变化时调用 we_label_ex_set_text()。
 * 3. 旋转/缩放单位与 img_ex 一致：angle 使用 512 步制（WE_ANGLE/WE_DEG），
 *    scale_256 以 256 为 1.0 倍。
 * -------------------------------------------------------------------------- */

/* --------------------------------------------------------------------------
 * 单个 label_ex 控件单次绘制时最多支持的字形数（栈缓存上限）。
 *
 * 旋转/缩放渲染的内层循环对每个屏幕像素都要查询"当前坐标命中哪个字形"。
 * 为了消除每像素重复 UTF-8 解码 + 字库 flash 查表，进入 draw_cb 时先把整段
 * 文字解析到栈数组，内层循环改为查表，命中后直接从已解析好的位图地址采样。
 *
 * 取值建议（单字形约占 16 字节栈，UTF-8 多字节字符仅计 1 字形）：
 *   - 16  → 仅用于极短数字/状态标签，如 "FPS:60.0"      约 256 B 栈
 *   - 32  → 默认值，覆盖一般标题/计数器/中短句         约 512 B 栈
 *   - 48  → 较长中英混排                                 约 768 B 栈
 *   - 64  → 整行长文本，使用前请确认 M0 主栈余量充足    约 1024 B 栈
 *
 * 文字字符数超过此宏时，超出部分不会被渲染（前 N 字仍可正常显示）。
 * 优先在 we_user_config.h 中覆盖，避免直接改本头文件。
 * -------------------------------------------------------------------------- */
#ifndef WE_CFG_LABEL_EX_MAX_GLYPHS
#define WE_CFG_LABEL_EX_MAX_GLYPHS 32
#endif

typedef struct
{
    we_obj_t base;              /* 基类，必须在首位 */

    const char *text;           /* UTF-8 字符串指针 */
    const unsigned char *font;  /* 字库数组指针 */
    colour_t color;             /* 文字前景色 */
    uint8_t opacity;            /* 整体透明度（0~255） */

    int16_t cx;
    /* 变换中心在屏幕上的 X 坐标。
     * 旋转和缩放都围绕这个点进行，同时也是文字视觉中心。
     */

    int16_t cy;
    /* 变换中心在屏幕上的 Y 坐标，与 cx 配套使用。 */

    int16_t angle;
    /* 旋转角度，512 步制（0~511 = 0°~<360°）。
     * 使用 WE_ANGLE(deg) 做浮点转换，或 WE_DEG(deg) 做整数编译期转换。
     */

    uint16_t scale_256;
    /* 缩放系数，256 = 1.0 倍，128 = 0.5 倍，512 = 2.0 倍。 */

    uint16_t text_w;
    /* 文字实际像素宽度缓存，init/set_text 时测量，用于包围盒计算。
     * 不占额外静态 RAM（仅 2 字节，内含于结构体）。
     */
} we_label_ex_obj_t;

/**
 * @brief 初始化可旋转缩放文本控件。
 * @param obj 控件对象指针。
 * @param lcd 所属 LCD 上下文。
 * @param cx 变换中心的屏幕 X 坐标。
 * @param cy 变换中心的屏幕 Y 坐标。
 * @param text 初始 UTF-8 文本。
 * @param font 字体资源指针。
 * @param color 文字颜色。
 * @param opacity 整体透明度（0~255）。
 */
void we_label_ex_obj_init(we_label_ex_obj_t *obj, we_lcd_t *lcd,
                          int16_t cx, int16_t cy,
                          const char *text, const unsigned char *font,
                          colour_t color, uint8_t opacity);

/**
 * @brief 设置文本内容并触发重排或重绘。
 * @param obj 目标控件对象指针。
 * @param new_text 新的文本字符串。
 * @return 无。
 */
void we_label_ex_set_text(we_label_ex_obj_t *obj, const char *new_text);

/**
 * @brief 设置变换中心坐标并重算包围盒。
 * @param obj 控件对象指针。
 * @param cx 新中心 X 坐标。
 * @param cy 新中心 Y 坐标。
 */
void we_label_ex_set_center(we_label_ex_obj_t *obj, int16_t cx, int16_t cy);

/**
 * @brief 设置旋转角度与缩放比例并重算包围盒。
 * @param obj 控件对象指针。
 * @param angle 旋转角度（512 分度制）。
 * @param scale_256 缩放比例（256=1.0x）。
 */
void we_label_ex_set_transform(we_label_ex_obj_t *obj, int16_t angle, uint16_t scale_256);

/**
 * @brief 设置绘制颜色并刷新显示。
 * @param obj 目标控件对象指针。
 * @param new_color 新的颜色值。
 * @return 无。
 */
void we_label_ex_set_color(we_label_ex_obj_t *obj, colour_t new_color);

/**
 * @brief 设置控件透明度并按需重绘。
 * @param obj 目标控件对象指针。
 * @param opacity 不透明度（0~255）。
 * @return 无。
 */
void we_label_ex_set_opacity(we_label_ex_obj_t *obj, uint8_t opacity);

/**
 * @brief 删除控件对象并从对象链表移除。
 * @param obj 控件对象指针。
 */
static inline void we_label_ex_obj_delete(we_label_ex_obj_t *obj)
{
we_obj_delete((we_obj_t *)obj);
}

#endif
