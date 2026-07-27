#include "we_widget_qrcode.h"
#include "we_render.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * qrcode —— 二维码控件（preview 孵化区）
 *
 * 显示模型：
 *   底色一次 we_fill_rect 铺满整控件（含 4 模块静区与全部亮模块），
 *   暗模块逐行做横向 run 合并，每段连续暗模块一次 we_fill_rect；
 *   反色模式仅交换前景/底色映射，位矩阵不动。
 *
 * 编码在 we_qrcode_set_text() 内同步完成（we_qr_encode），位矩阵拷入
 * 控件自身定长成员，多实例互不干扰（编码器 static 工作区仅在调用瞬间占用）。
 *
 * 错误占位：编码失败（内容超容量）时画"底色 + 两条对角圆头粗线（叉）"，
 * 尺寸保持不变，下一次 set_text 成功后恢复正常显示。
 * -------------------------------------------------------------------------- */

/**
 * @brief 比较两个颜色值是否相等（按当前色深逐通道比较）。
 * @param a 传入：颜色 A。
 * @param b 传入：颜色 B。
 * @return 1 表示相等，0 表示不等。
 */
static uint8_t _qrcode_color_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#else
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 计算指定矩阵边长对应的控件像素边长（含两侧静区）。
 * @param obj 传入：控件对象指针。
 * @param modules 传入：矩阵边长（模块数）。
 * @return 控件像素边长。
 */
static int16_t _qrcode_px_size(const we_qrcode_obj_t *obj, uint8_t modules)
{
    return (int16_t)(((int16_t)modules + 2 * WE_QRCODE_QUIET_ZONE) * (int16_t)obj->module_px);
}

/**
 * @brief 绘制编码失败占位：两条对角圆头粗线组成的叉。
 * @param obj 传入：控件对象指针。
 * @param fg 传入：叉线颜色。
 * @return 无。
 */
static void _qrcode_draw_error(we_qrcode_obj_t *obj, colour_t fg)
{
    we_lcd_t *lcd = obj->base.lcd;
    int16_t bx = obj->base.x;
    int16_t by = obj->base.y;
    int16_t bw = obj->base.w;
    int16_t bh = obj->base.h;
    int16_t margin = (int16_t)(bw / 5);
    uint8_t thick = (uint8_t)(obj->module_px * 2U);

    if (thick < 3U)
        thick = 3U;
    we_draw_line_round(lcd, (int16_t)(bx + margin), (int16_t)(by + margin),
                       (int16_t)(bx + bw - 1 - margin), (int16_t)(by + bh - 1 - margin),
                       thick, fg, 255U);
    we_draw_line_round(lcd, (int16_t)(bx + bw - 1 - margin), (int16_t)(by + margin),
                       (int16_t)(bx + margin), (int16_t)(by + bh - 1 - margin),
                       thick, fg, 255U);
}

/**
 * @brief 控件绘制回调：底色整铺 + 暗模块横向 run 合并填充。
 * @param ptr 传入：控件对象指针。
 * @return 无。
 */
static void _qrcode_draw_cb(void *ptr)
{
    we_qrcode_obj_t *obj = (we_qrcode_obj_t *)ptr;
    we_lcd_t *lcd;
    colour_t fg;
    colour_t bg;
    int16_t bx;
    int16_t by;
    int16_t mpx;
    uint8_t r;

    if (obj == NULL)
        return;
    lcd = obj->base.lcd;
    if (lcd == NULL || obj->base.w <= 0 || obj->base.h <= 0)
        return;

    /* 反色仅交换颜色映射（位矩阵保持"1=数据暗模块"语义） */
    fg = obj->invert ? obj->light_color : obj->dark_color;
    bg = obj->invert ? obj->dark_color : obj->light_color;
    bx = obj->base.x;
    by = obj->base.y;
    mpx = (int16_t)obj->module_px;

    /* 1. 底色整铺：覆盖静区与全部亮模块 */
    we_fill_rect(lcd, bx, by, (uint16_t)obj->base.w, (uint16_t)obj->base.h, bg, 255U);

    /* 2. 编码失败：叉占位 */
    if (obj->err_flag != 0U)
    {
        _qrcode_draw_error(obj, fg);
        return;
    }
    if (obj->qr_size == 0U)
        return; /* 尚未设置内容：纯底色面板 */

    /* 3. 暗模块：逐行扫描，连续暗模块合并为一次 we_fill_rect */
    for (r = 0U; r < obj->qr_size; r++)
    {
        int16_t py = (int16_t)(by + (WE_QRCODE_QUIET_ZONE + (int16_t)r) * mpx);
        uint8_t c = 0U;

        while (c < obj->qr_size)
        {
            /* 显式加 const 限定：二维数组的隐式 const 转换 AC5 报 #167 */
            if (we_qr_bit_get((const uint8_t(*)[WE_QR_ROW_BYTES])obj->bits, r, c) != 0U)
            {
                uint8_t run0 = c;

                while (c < obj->qr_size &&
                       we_qr_bit_get((const uint8_t(*)[WE_QR_ROW_BYTES])obj->bits, r, c) != 0U)
                    c++;
                we_fill_rect(lcd,
                             (int16_t)(bx + (WE_QRCODE_QUIET_ZONE + (int16_t)run0) * mpx),
                             py,
                             (uint16_t)((int16_t)(c - run0) * mpx),
                             (uint16_t)mpx, fg, 255U);
            }
            else
            {
                c++;
            }
        }
    }
}

/**
 * @brief 控件事件回调：装饰性控件，不消费任何输入。
 * @param ptr 传入：控件对象指针。
 * @param event 传入：输入事件类型。
 * @param data 传入：输入数据。
 * @return 恒返回 0（穿透给背后控件）。
 */
static uint8_t _qrcode_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    (void)ptr;
    (void)event;
    (void)data;
    return 0U;
}

static const we_class_t _qrcode_class = {
    .draw_cb = _qrcode_draw_cb,
    .event_cb = _qrcode_event_cb,
    .set_pos_cb = NULL, /* 几何全部由 base.x/y 推导，默认移动逻辑即正确 */
};

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化二维码控件并挂载到 LCD 对象链表。
 * @param obj 传入：控件对象指针。
 * @param lcd 传入：GUI 运行时 LCD 上下文指针。
 * @param x 传入：左上角 X 坐标（屏幕绝对坐标，含静区）。
 * @param y 传入：左上角 Y 坐标。
 * @param module_px 传入：每模块像素边长（钳制到 2~6）。
 * @return 无。
 */
void we_qrcode_obj_init(we_qrcode_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, uint8_t module_px)
{
    if (obj == NULL || lcd == NULL)
        return;

    if (module_px < 2U)
        module_px = 2U;
    if (module_px > 6U)
        module_px = 6U;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.class_p = &_qrcode_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;

    obj->module_px = module_px;
    obj->qr_size = 0U;
    obj->invert = 0U;
    obj->err_flag = 0U;
    obj->text_len = 0U;
    obj->text[0] = '\0';
    memset(obj->bits, 0, sizeof(obj->bits));

    obj->dark_color = RGB888TODEV(18, 20, 26);    /* 近黑码色 */
    obj->light_color = RGB888TODEV(240, 243, 248); /* 近白底色 */

    /* 初始按版本 1 占位尺寸；set_text 成功后自动改为实际版本尺寸 */
    obj->base.w = _qrcode_px_size(obj, 21U);
    obj->base.h = obj->base.w;

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置二维码内容（ASCII byte mode），内容变化才重新编码重绘。
 * @param obj 传入：控件对象指针。
 * @param str 传入：待编码字符串（NUL 结尾；NULL 按空串处理）。
 * @return 0 表示编码成功，-1 表示失败（超过 WE_QR_TEXT_MAX 字节）。
 */
int8_t we_qrcode_set_text(we_qrcode_obj_t *obj, const char *str)
{
    size_t n;
    int16_t new_px;

    if (obj == NULL || obj->base.lcd == NULL)
        return -1;
    if (str == NULL)
        str = "";
    n = strlen(str);

    /* 超容量：不存内容，text_len 记 255 哨兵（重复超长串不重复标脏） */
    if (n > (size_t)WE_QR_TEXT_MAX)
    {
        if (obj->err_flag != 0U && obj->text_len == 255U)
            return -1;
        obj->err_flag = 1U;
        obj->text_len = 255U;
        we_obj_invalidate((we_obj_t *)obj);
        return -1;
    }

    /* 内容未变：直接返回（存量文本必然编码成功，见下方失败路径不存文本） */
    if (obj->err_flag == 0U && obj->text_len == (uint8_t)n &&
        memcmp(obj->text, str, n) == 0)
        return 0;

    if (we_qr_encode((const uint8_t *)str, (uint16_t)n, obj->bits, &obj->qr_size) != 0)
    {
        /* 防御路径：n <= 62 时正常不会到这里 */
        obj->err_flag = 1U;
        obj->text_len = 255U;
        we_obj_invalidate((we_obj_t *)obj);
        return -1;
    }

    obj->err_flag = 0U;
    obj->text_len = (uint8_t)n;
    memcpy(obj->text, str, n);
    obj->text[n] = '\0';

    /* 版本变化会改变控件尺寸：先标脏旧区域，再改尺寸标脏新区域 */
    new_px = _qrcode_px_size(obj, obj->qr_size);
    if (new_px != obj->base.w || new_px != obj->base.h)
    {
        we_obj_invalidate((we_obj_t *)obj);
        obj->base.w = new_px;
        obj->base.h = new_px;
    }
    we_obj_invalidate((we_obj_t *)obj);
    return 0;
}

/**
 * @brief 设置暗模块颜色与底色。
 * @param obj 传入：控件对象指针。
 * @param dark 传入：暗模块颜色。
 * @param light 传入：底色（静区 + 亮模块）。
 * @return 无。
 */
void we_qrcode_set_colors(we_qrcode_obj_t *obj, colour_t dark, colour_t light)
{
    if (obj == NULL)
        return;
    if (_qrcode_color_eq(obj->dark_color, dark) &&
        _qrcode_color_eq(obj->light_color, light))
        return;

    obj->dark_color = dark;
    obj->light_color = light;
    we_obj_invalidate((we_obj_t *)obj);
}

/**
 * @brief 设置反色显示（深底浅码）。
 * @param obj 传入：控件对象指针。
 * @param invert 传入：0=正常，非0=反色。
 * @return 无。
 */
void we_qrcode_set_invert(we_qrcode_obj_t *obj, uint8_t invert)
{
    uint8_t val;

    if (obj == NULL)
        return;
    val = (invert != 0U) ? 1U : 0U;
    if (obj->invert == val)
        return;

    obj->invert = val;
    we_obj_invalidate((we_obj_t *)obj);
}
