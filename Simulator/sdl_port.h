#ifndef SDL_PORT_H
#define SDL_PORT_H

#include "we_gui_driver.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define SIM_SCALE 1
#define SIM_MAX_FPS 30

    /**
     * @brief 初始化 SDL 显示资源（窗口、渲染器、纹理和屏幕缓冲）
     */
    void sim_lcd_init(void);

    /**
     * @brief 设置模拟 LCD 写入窗口地址
     * @param x0 起始 X 坐标
     * @param y0 起始 Y 坐标
     * @param x1 结束 X 坐标
     * @param y1 结束 Y 坐标
     */
    void sim_lcd_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

    /**
     * @brief 将屏幕缓冲刷新到 SDL 窗口并执行帧率限制
     */
    void sim_lcd_update(void);

#define lcd_set_addr sim_lcd_set_addr

#if (LCD_DEEP == DEEP_RGB565)
#define LCD_FLUSH_PORT lcd_rgb565_port
#elif (LCD_DEEP == DEEP_RGB888)
#define LCD_FLUSH_PORT lcd_rgb888_port
#endif

    /**
     * @brief 初始化显示硬件抽象层（模拟器实现）
     */
    void lcd_hw_init(void);

    /**
     * @brief 打开背光（模拟器为空实现）
     */
    void lcd_bl_on(void);

    /**
     * @brief 关闭背光（模拟器为空实现）
     */
    void lcd_bl_off(void);

    /**
     * @brief 毫秒级延时
     * @param ms 延时毫秒数
     */
    void lcd_delay_ms(uint32_t ms);

    /**
     * @brief 将 RGB565 像素块写入模拟器屏幕缓冲
     * @param gram 像素数据指针
     * @param pix_size 像素数量
     */
    void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size);

    /**
     * @brief 将 RGB888 像素块写入模拟器屏幕缓冲
     * @param gram 像素数据指针
     * @param pix_size 字节数量（3 字节/像素）
     */
    void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size);

    /**
     * @brief 初始化输入硬件抽象层（模拟器实现）
     */
    void input_hw_init(void);

    /**
     * @brief 读取一帧输入数据并上报给 GUI
     * @param data 输入数据输出指针
     */
    void we_input_port_read(we_indev_data_t *data);

    /**
     * @brief 初始化存储硬件抽象层（模拟器实现）
     */
    void storage_hw_init(void);

    /**
     * @brief 从模拟外挂 flash 读取数据
     * @param addr 读取起始地址
     * @param buf 目标缓冲区
     * @param len 读取长度（字节）
     */
    void we_storage_port_read(uint32_t addr, uint8_t buf[], uint32_t len);

    /**
     * @brief 处理 SDL 事件并生成触摸/键盘输入
     * @param lcd GUI 上下文，用于计算手势中心点
     * @return 1 表示继续运行，0 表示收到退出事件
     */
    int sim_handle_events(we_lcd_t *lcd);

#ifdef __cplusplus
}
#endif

#endif
