#ifndef LITE_AUTOTEST_H
#define LITE_AUTOTEST_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * headless 渲染回归脚手架（debug 构建专用，正式构建不编译本目录）
 *
 * 作用：以固定 16ms 步进跑 N 帧，对每帧 ARGB8888 帧缓冲做链式 FNV-1a 哈希，
 * 把结果写入文件后退出，供 SimLite/autotest.ps1 与 autotest/golden.txt 比对。
 * 固定步进 + 不读真实时钟 + 不用随机数 => 同样的代码必然得到同样的哈希，
 * 任何改变画面的回归都会被逐帧抓到。
 *
 * 帧缓冲布局与 565->ARGB8888 位扩展公式与原 SDL 模拟器完全一致，
 * 因此基准哈希与旧 Simulator/autotest 的 golden 值可直接互换。
 *
 * 用法（debug_main 里只需两处）：
 *   1. lcd_hw_init() 之前 lite_autotest_parse_args()——命中 --autotest 时
 *      切 headless，不开窗口；
 *   2. demo 初始化之后 cfg.frames > 0 则 lite_autotest_run() 并退出进程。
 * -------------------------------------------------------------------------- */

typedef struct
{
    int frames;              /* > 0 表示进入回归模式，数值为要跑的帧数 */
    const char *out_path;    /* 哈希结果输出文件 */
    const char *script_path; /* 输入注入脚本路径；NULL = 本轮不注入输入 */
} lite_autotest_cfg_t;

/**
 * @brief 解析回归模式命令行参数（--autotest N / --out file / --script file）
 * @param argc 传入：命令行参数个数
 * @param argv 传入：命令行参数数组
 * @param cfg 传出：解析结果（未命中 --autotest 时 frames 为 0）
 * @return 无
 * @note 必须在 lcd_hw_init 之前调用：命中回归模式时会切 headless，
 *       后续初始化不开窗口，可在无显示环境批量跑。
 */
void lite_autotest_parse_args(int argc, char *argv[], lite_autotest_cfg_t *cfg);

/**
 * @brief 跑完 cfg->frames 帧回归并把链式哈希写入结果文件
 * @param lcd 传入：已完成 we_gui_init 与 demo 初始化的屏幕上下文
 * @param cfg 传入：lite_autotest_parse_args 的解析结果
 * @param demo_id 传入：当前运行的 demo id（写进结果行用于对号）
 * @return 无
 * @note 需在 demo 初始化之后调用（跑的就是该 demo 的帧）。返回后调用方应
 *       直接退出进程，不要再进入交互主循环。
 *       诊断钩子：环境变量 WE_AUTOTEST_DUMP 置任意值输出逐帧哈希，
 *       WE_AUTOTEST_PPM=帧号,帧号,... 把指定帧抓成 PPM 图片。
 */
void lite_autotest_run(we_lcd_t *lcd, const lite_autotest_cfg_t *cfg, int demo_id);

#endif /* LITE_AUTOTEST_H */
