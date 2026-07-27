#ifndef __WE_WIDGET_STEPPER_H
#define __WE_WIDGET_STEPPER_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * 数值步进控件（stepper / spinbox）
 *
 * 左 [-] / 中 数值 / 右 [+] 三段式布局，适合 MCU 设置项（温度、音量、阈值…）。
 *   - 点击左/右区按 step 减/加，到边界禁用对应按钮（wrap=1 则回绕不禁用）；
 *   - 按住不放触发连续步进（复用 STAY 事件，不占用 timer slot）；
 *   - 数值统一用“定点 int32”存储：真实值 = value / 10^decimals，
 *     小数显示在 draw 时才拆分，避免 Cortex-M0 软浮点开销与符号歧义。
 *
 * 例：温度 16.0~30.0、步进 0.5、1 位小数 →
 *     decimals=1, min=160, max=300, step=5, init=230。
 * -------------------------------------------------------------------------- */

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪步进器的按键回调与可聚焦性，其余控件不受影响。
 * 键控步进依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_STEPPER_USE_KEY
#define WE_STEPPER_USE_KEY 1
#endif

/* 连续步进：按住后首次重复前的延迟（STAY 次数，约 16ms/次 → 25≈400ms） */
#ifndef WE_STEPPER_HOLD_DELAY
#define WE_STEPPER_HOLD_DELAY 25U
#endif

/* 连续步进：达到延迟后每隔多少次 STAY 再步进一次（7≈112ms） */
#ifndef WE_STEPPER_HOLD_INTERVAL
#define WE_STEPPER_HOLD_INTERVAL 7U
#endif

/* 支持的最大小数位数（决定内部查表与缓冲大小） */
#ifndef WE_STEPPER_MAX_DECIMALS
#define WE_STEPPER_MAX_DECIMALS 4U
#endif

struct we_stepper_obj_t;

/* 数值改变回调（value 为定点值，真实值 = value / 10^decimals） */
typedef void (*we_stepper_changed_cb_t)(struct we_stepper_obj_t *obj, int32_t value);

typedef struct we_stepper_obj_t
{
    we_obj_t base;
    int32_t value;     /* 当前定点值 */
    int32_t min_value; /* 定点下限 */
    int32_t max_value; /* 定点上限 */
    int32_t step;      /* 单次步进的定点增量（>0） */
    const unsigned char *font;
    we_stepper_changed_cb_t changed_cb;
    uint16_t radius;   /* 底框圆角 */
    uint16_t hold_cnt; /* 按住期间累计的 STAY 次数 */
    char buf[16];      /* 数值文本缓冲（draw 时刷新） */
    uint8_t decimals;  /* 小数位数，0 = 纯整数 */
    int8_t active_side; /* 当前按住的一侧：-1=减，+1=加，0=无 */
    uint8_t wrap : 1;    /* 1：到边界回绕；0：到边界禁用对应按钮 */
    uint8_t enabled : 1; /* 是否可交互 */
    uint8_t pressed : 1; /* 是否处于按下态（用于按钮高亮） */
} we_stepper_obj_t;

/**
 * @brief 初始化数值步进控件并挂载到 LCD 对象链表。
 * @param obj 目标控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标。
 * @param y 左上角 Y 坐标。
 * @param w 控件总宽度（含左右按钮区与中间数值区）。
 * @param h 控件高度（左右按钮区为 h×h 方形）。
 * @param font 字体资源指针。
 * @param decimals 小数位数（0~WE_STEPPER_MAX_DECIMALS）。
 * @param min_value 定点下限。
 * @param max_value 定点上限。
 * @param step 单次步进的定点增量（<=0 时强制为 1）。
 * @param init_value 初始定点值（自动夹紧到 min/max）。
 * @return 无。
 */
void we_stepper_obj_init(we_stepper_obj_t *obj, we_lcd_t *lcd,
                         int16_t x, int16_t y, uint16_t w, uint16_t h,
                         const unsigned char *font, uint8_t decimals,
                         int32_t min_value, int32_t max_value,
                         int32_t step, int32_t init_value);

/**
 * @brief 设置当前定点值（自动夹紧并按需触发回调/重绘）。
 * @param obj 控件对象指针。
 * @param value 新定点值。
 * @return 无。
 */
void we_stepper_set_value(we_stepper_obj_t *obj, int32_t value);

/**
 * @brief 获取当前定点值（真实值 = 返回值 / 10^decimals）。
 * @param obj 控件对象指针。
 * @return 当前定点值；obj 为 NULL 时返回 0。
 */
int32_t we_stepper_get_value(const we_stepper_obj_t *obj);

/**
 * @brief 设置步进增量。
 * @param obj 控件对象指针。
 * @param step 定点增量（<=0 时强制为 1）。
 * @return 无。
 */
void we_stepper_set_step(we_stepper_obj_t *obj, int32_t step);

/**
 * @brief 设置取值范围（自动夹紧当前值）。
 * @param obj 控件对象指针。
 * @param min_value 定点下限。
 * @param max_value 定点上限。
 * @return 无。
 */
void we_stepper_set_range(we_stepper_obj_t *obj, int32_t min_value, int32_t max_value);

/**
 * @brief 设置到边界是否回绕。
 * @param obj 控件对象指针。
 * @param wrap 非 0 回绕，0 禁用边界按钮。
 * @return 无。
 */
void we_stepper_set_wrap(we_stepper_obj_t *obj, uint8_t wrap);

/**
 * @brief 设置是否可交互。
 * @param obj 控件对象指针。
 * @param enabled 非 0 可交互。
 * @return 无。
 */
void we_stepper_set_enabled(we_stepper_obj_t *obj, uint8_t enabled);

/**
 * @brief 设置数值改变回调。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针。
 * @return 无。
 */
void we_stepper_set_changed_cb(we_stepper_obj_t *obj, we_stepper_changed_cb_t cb);

/**
 * @brief 删除控件并从对象链表移除。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_stepper_obj_delete(we_stepper_obj_t *obj);

#endif
