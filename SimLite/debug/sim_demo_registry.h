#ifndef SIM_DEMO_REGISTRY_H
#define SIM_DEMO_REGISTRY_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * demo 运行时注册表（debug_main.c 专用：开发期用命令行参数选 demo 免重编；
 * 正式入口 main_lite.c 走编译期 DEMO_ID 宏，与硬件目标一致，不含本表。
 * 编号与四个目标的 DEMO_ID 完全一致，0=showcase 因需 800x480 不收录）
 * -------------------------------------------------------------------------- */

typedef struct
{
    int id;                                       /* DEMO_ID（与硬件目标一致） */
    const char *name;                             /* ASCII 名（控制台打印用） */
    void (*init)(we_lcd_t *lcd);                  /* demo 初始化 */
    void (*tick)(we_lcd_t *lcd, uint16_t ms_tick);/* 周期 tick（NULL = 无） */
} sim_demo_entry_t;

/**
 * @brief 按 DEMO_ID 查找注册表项
 * @param id DEMO_ID
 * @return 表项指针，未收录返回 NULL
 */
const sim_demo_entry_t *sim_demo_find(int id);

/**
 * @brief 取完整注册表（列出可用 demo 用）
 * @param count 传出：表项数量
 * @return 表首指针
 */
const sim_demo_entry_t *sim_demo_table(int *count);

#endif /* SIM_DEMO_REGISTRY_H */
