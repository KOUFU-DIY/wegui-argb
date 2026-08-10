#ifndef SIM_AUTOTEST_H
#define SIM_AUTOTEST_H

#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * headless 渲染回归脚手架（模拟器专用的测试设施，不是移植范例的一部分）
 *
 * 作用：以固定 16ms 步进跑 N 帧，对每帧屏幕缓冲做链式 FNV-1a 哈希，把结果
 * 写入文件后退出，供 Simulator/autotest.ps1 与 autotest/golden.txt 比对。
 * 固定步进 + 不读真实时钟 + 不用随机数 => 同样的代码必然得到同样的哈希，
 * 任何改变画面的回归都会被逐帧抓到。
 *
 * 用法（main 里只需两处，其余全在本模块内）：
 *   1. SDL 初始化之前 sim_autotest_parse_args()——命中 --autotest 时会切到
 *      dummy 视频驱动，免开窗；
 *   2. demo 初始化之后 cfg.frames > 0 则 sim_autotest_run() 并退出进程。
 *
 * 把模拟器代码搬进真实工程时，删掉 main 里那两处调用与本模块即可，
 * 对 GUI 内核和 demo 没有任何影响。
 * -------------------------------------------------------------------------- */

/* 编译开关：默认 0 = 整套脚手架不参与编译（本模块与 main 里的调用点同受
 * 此宏门控，日常构建的 exe 里没有任何回归测试代码，--autotest 参数被忽略）。
 * Simulator/autotest.ps1 跑基准回归时经 CMake 以 -DWE_SIM_AUTOTEST=1 自动
 * 开启，无需手动改动；想在日常构建里手动跑 --autotest，把下面默认值临时
 * 改成 1 重新构建即可。 */
#ifndef WE_SIM_AUTOTEST
#define WE_SIM_AUTOTEST 0
#endif

typedef struct
{
    int frames;              /* > 0 表示进入回归模式，数值为要跑的帧数 */
    const char *out_path;    /* 哈希结果输出文件 */
    const char *script_path; /* 输入注入脚本路径；NULL = 本轮不注入输入 */
} sim_autotest_cfg_t;

/**
 * @brief 解析回归模式命令行参数（--autotest N / --out file / --script file）
 * @param argc 传入：命令行参数个数
 * @param argv 传入：命令行参数数组
 * @param cfg 传出：解析结果（未命中 --autotest 时 frames 为 0）
 * @return 无
 * @note 必须在 SDL 初始化之前调用：命中回归模式时本函数会把视频驱动切成
 *       dummy，使后续 SDL_Init 不开窗口，可在无显示环境批量跑。
 */
void sim_autotest_parse_args(int argc, char *argv[], sim_autotest_cfg_t *cfg);

/**
 * @brief 跑完 cfg->frames 帧回归并把链式哈希写入结果文件
 * @param lcd 传入：已完成 we_gui_init 与 demo 初始化的屏幕上下文
 * @param cfg 传入：sim_autotest_parse_args 的解析结果
 * @param demo_id 传入：当前编译进来的 DEMO_ID（写进结果行用于对号）
 * @return 无
 * @note 需在 demo 初始化之后调用（跑的就是该 demo 的帧）。返回后调用方应
 *       直接退出进程，不要再进入交互主循环。
 *       诊断钩子：环境变量 WE_AUTOTEST_DUMP 置任意值输出逐帧哈希，
 *       WE_AUTOTEST_PPM=帧号,帧号,... 把指定帧抓成 PPM 图片。
 */
void sim_autotest_run(we_lcd_t *lcd, const sim_autotest_cfg_t *cfg, int demo_id);

#endif /* SIM_AUTOTEST_H */
