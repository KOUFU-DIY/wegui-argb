/**
 * @file  debug_main.c
 * @brief SimLite 开发者入口（替换 main_lite.c，仅 -Dev/--dev 构建编译）
 *
 * 正式入口 main_lite.c 与硬件目标同构（编译期 DEMO_ID 宏选 demo，无任何
 * 调试设施）；本文件是开发期工具，把运行时选 demo 与 headless 抓帧自检
 * 集中到 debug/ 下，产物名 wegui_lite_dev，与正式 wegui_lite 并存：
 *
 *   wegui_lite_dev [demo_id]                  运行指定 demo（默认 33，免重编切换）
 *   wegui_lite_dev [demo_id] --shot N out.ppm  headless 渲染 N 帧导出 PPM（自检）
 *   wegui_lite_dev [demo_id] --autotest N [--out f] [--script s.evt]
 *                                             headless 基准哈希回归（SimLite/autotest.ps1 驱动）
 *   wegui_lite_dev --list                     列出全部可用 demo
 *
 * 构建：build_lite.ps1 -Dev [-Run -Demo N]   /   build_lite.sh --dev
 */

#include "lite_port.h" /* 端口面：必须最先（we_sim_port_config.h 先于内核头） */
#include "lite_autotest.h"
#include "sim_demo_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

we_lcd_t mylcd;
colour_t user_gram[USER_GRAM_NUM];

/**
 * @brief 开发者入口：解析参数、初始化端口与 GUI、运行选定 demo
 * @param argc 参数数量
 * @param argv 参数数组
 * @return 0 正常退出
 */
int main(int argc, char *argv[])
{
    static we_gui_timer_t demo_timer; /* demo 周期定时器节点（调用方持有） */
    const sim_demo_entry_t *entry;
    lite_autotest_cfg_t at;
    int demo_id = 33;
    long shot_frames = 0;
    const char *shot_path = NULL;
    uint32_t last_tick;
    int i;

    /* 回归参数解析必须早于 lcd_hw_init（命中时切 headless 免开窗） */
    lite_autotest_parse_args(argc, argv, &at);

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--list") == 0)
        {
            int n, k;
            const sim_demo_entry_t *t = sim_demo_table(&n);

            for (k = 0; k < n; k++)
                printf("%3d  %s\n", t[k].id, t[k].name);
            return 0;
        }
        if (strcmp(argv[i], "--shot") == 0 && i + 2 < argc)
        {
            shot_frames = strtol(argv[i + 1], NULL, 10);
            shot_path = argv[i + 2];
            i += 2;
            continue;
        }
        if ((strcmp(argv[i], "--autotest") == 0 || strcmp(argv[i], "--out") == 0 ||
             strcmp(argv[i], "--script") == 0) && i + 1 < argc)
        {
            i += 1; /* 值已由 lite_autotest_parse_args 消费，跳过防误当 demo_id */
            continue;
        }
        demo_id = (int)strtol(argv[i], NULL, 10);
    }

    if (shot_path != NULL)
        lite_set_headless(1);

    lcd_hw_init();
    input_hw_init();
    storage_hw_init();

    we_gui_init(&mylcd, RGB888TODEV(10, 14, 20), user_gram, USER_GRAM_NUM, lcd_set_addr, LCD_FLUSH_PORT,
                we_input_port_read, we_storage_port_read);

    entry = sim_demo_find(demo_id);
    if (entry == NULL)
    {
        printf("unknown demo id %d, fallback to 1 (use --list)\n", demo_id);
        entry = sim_demo_find(1);
    }
    printf("demo %d: %s\n", entry->id, entry->name);

    entry->init(&mylcd);
    if (entry->tick != NULL)
        we_gui_timer_create(&mylcd, &demo_timer, entry->tick, 16U, 1U);

    /* --autotest 基准哈希回归：跑完 N 帧写出链式哈希即退出，不进交互主循环 */
    if (at.frames > 0)
    {
        lite_autotest_run(&mylcd, &at, entry->id);
        return 0;
    }

    /* --shot 自检：固定 16ms 步进渲染 N 帧后导出帧缓冲，不开窗口 */
    if (shot_path != NULL)
    {
        long f;

        for (f = 0; f < shot_frames; f++)
        {
            we_gui_tick_inc(&mylcd, 16U);
            we_gui_task_handler(&mylcd);
        }
        if (lite_dump_ppm(shot_path) != 0)
        {
            printf("dump failed: %s\n", shot_path);
            return 1;
        }
        printf("shot %ld frames -> %s\n", shot_frames, shot_path);
        return 0;
    }

    last_tick = lite_ticks_ms();

    while (lite_handle_events(&mylcd))
    {
        uint32_t now = lite_ticks_ms();
        uint32_t delta = now - last_tick;
        uint32_t frame_ms;

        if (delta > 0U)
        {
            uint16_t ms = (uint16_t)((delta > 100U) ? 16U : delta);

            we_gui_tick_inc(&mylcd, ms);
            last_tick = now;
        }

        we_gui_task_handler(&mylcd);
        lite_present();

        frame_ms = lite_ticks_ms() - now;
        if (frame_ms < 15U)
            lite_sleep_ms(15U - frame_ms); /* ~60fps，让出 CPU */
        else
            lite_sleep_ms(1U);
    }

    return 0;
}
