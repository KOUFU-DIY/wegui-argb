#ifndef LITE_PORT_H
#define LITE_PORT_H

#include "we_sim_port_config.h" /* 端口面：必须先于内核头（WE_PORT_FLUSH_ASYNC 覆盖默认值） */
#include "we_gui_driver.h"

/* --------------------------------------------------------------------------
 * SimLite 端口接口（纯 Win32/GDI 实现，见 win32_port.c）
 * 显示函数原型在 we_sim_port_config.h（经 we_hw_port.h 路由）。
 * -------------------------------------------------------------------------- */

/* 窗口放大倍数（帧缓冲固定 SCREEN_WIDTH x SCREEN_HEIGHT，GDI 拉伸显示） */
#ifndef LITE_SCALE
#define LITE_SCALE 1
#endif

/**
 * @brief 初始化输入抽象层（清空待上报触摸状态）
 */
void input_hw_init(void);

/**
 * @brief 初始化存储抽象层：打开 exe 旁的 merged_bin.bin（缺文件读 0xFF）
 */
void storage_hw_init(void);

/**
 * @brief 读取一帧触摸输入（鼠标模拟）
 * @param data 输出指针
 */
void we_input_port_read(we_indev_data_t *data);

/**
 * @brief 外挂 flash 读取端口（fseek/fread 直读镜像文件）
 * @param addr 起始地址
 * @param buf 目标缓冲
 * @param len 长度（字节）
 */
void we_storage_port_read(uint32_t addr, uint8_t buf[], uint32_t len);

/**
 * @brief 泵送窗口消息并转换为 GUI 输入
 * @param lcd GUI 上下文（键盘语义键注入用）
 * @return 1 = 继续主循环，0 = 窗口已关闭
 */
int lite_handle_events(we_lcd_t *lcd);

/**
 * @brief 把帧缓冲刷到窗口（headless 模式下为空操作）
 */
void lite_present(void);

/**
 * @brief 开关 headless 模式（debug/debug_main.c 的 --shot 自检用：只建帧缓冲不开窗；
 *        正式入口 main_lite.c 不调用）
 * @param on 1 = headless
 */
void lite_set_headless(int on);

/**
 * @brief 把当前帧缓冲导出为 P6 PPM（debug 构建自检目检用）
 * @param path 输出文件路径
 * @return 0 成功，-1 失败
 */
int lite_dump_ppm(const char *path);

/**
 * @brief 取 GUI 分辨率 ARGB8888 帧缓冲首地址（debug/lite_autotest.c 帧哈希用；
 *        布局与转换公式与原 SDL 模拟器 screen_buffer 完全一致，基准哈希可互换）
 * @return 帧缓冲指针（SCREEN_WIDTH x SCREEN_HEIGHT 个 uint32）
 */
const uint32_t *lite_fb(void);

/**
 * @brief 毫秒时基（GetTickCount 包装，主循环计算增量用）
 */
uint32_t lite_ticks_ms(void);

/**
 * @brief 毫秒休眠（主循环让出 CPU 用）
 * @param ms 毫秒数
 */
void lite_sleep_ms(uint32_t ms);

#endif /* LITE_PORT_H */
