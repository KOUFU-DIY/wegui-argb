#include "sdl_port.h"
#include "we_gui_driver.h"
#include "we_user_config.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 模拟外挂 flash：直接读 exe 旁的 merged_bin.bin（tool/3.bin2c 产物，
 * CMake 构建后自动拷贝）。缺文件时读出 0xFF，等同已擦除的 flash。 */
static FILE *sim_flash_file = NULL;
static uint32_t sim_flash_size = 0U;

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static uint32_t *screen_buffer = NULL;

static struct
{
    uint16_t x0, y0, x1, y1;
} current_addr;

static uint32_t pixel_offset = 0;
static we_indev_data_t g_pending_input = {0};
static uint8_t g_has_pending_input = 0;

/**
 * @brief 初始化 SDL 显示资源（窗口、渲染器、纹理与屏幕缓冲）
 */
void sim_lcd_init(void)
{
    /* 屏幕缓冲先于 SDL 初始化分配：即使窗口/渲染器创建失败（如 autotest
     * 的 dummy 视频驱动裁剪环境），flush 端口与帧哈希仍可正常工作。 */
    screen_buffer = (uint32_t *)malloc(SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    if (screen_buffer != NULL)
    {
        memset(screen_buffer, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint32_t));
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0)
    {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return;
    }

    /* 强制使用最近邻采样，避免窗口放大后发糊。 */
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    window = SDL_CreateWindow("WeGui ARGB Simulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_WIDTH * SIM_SCALE, SCREEN_HEIGHT * SIM_SCALE,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_ALWAYS_ON_TOP);
    if (window == NULL)
    {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == NULL)
    {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }

    texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

}

/**
 * @brief 对当前屏幕缓冲做 FNV-1a 链式哈希（autotest 帧校验用）
 * @param h 传入：上一帧累计哈希（首帧传 2166136261u）
 * @return 融入本帧全部像素后的哈希值
 * @note 只依赖 screen_buffer 内容，与 SDL 渲染路径无关，dummy 驱动下同样有效。
 */
void sim_lcd_dump_ppm(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int i;

    if (fp == NULL || screen_buffer == NULL)
        return;
    fprintf(fp, "P6 %d %d 255 ", (int)SCREEN_WIDTH, (int)SCREEN_HEIGHT);
    for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
    {
        uint32_t px = screen_buffer[i];
        unsigned char rgb[3];

        rgb[0] = (unsigned char)(px >> 16);
        rgb[1] = (unsigned char)(px >> 8);
        rgb[2] = (unsigned char)px;
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
}

uint32_t sim_lcd_hash(uint32_t h)
{
    const uint8_t *pb = (const uint8_t *)screen_buffer;
    uint32_t n = (uint32_t)SCREEN_WIDTH * (uint32_t)SCREEN_HEIGHT * 4U;
    uint32_t i;

    if (pb == NULL)
        return h;
    for (i = 0U; i < n; i++)
    {
        h ^= pb[i];
        h *= 16777619u;
    }
    return h;
}

/**
 * @brief 设置模拟 LCD 写入窗口地址并重置窗口内写入偏移
 * @param x0 起始 X 坐标
 * @param y0 起始 Y 坐标
 * @param x1 结束 X 坐标
 * @param y1 结束 Y 坐标
 */
void sim_lcd_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    current_addr.x0 = x0;
    current_addr.y0 = y0;
    current_addr.x1 = x1;
    current_addr.y1 = y1;
    pixel_offset = 0;
}

/**
 * @brief 将单个 RGB565 像素转换为 ARGB8888
 * @param rgb565 输入 RGB565 颜色值
 * @return 转换后的 ARGB8888 颜色值（Alpha 固定 0xFF）
 */
static uint32_t rgb565_to_argb8888(uint16_t rgb565)
{
    uint8_t r = (uint8_t)((rgb565 >> 11) & 0x1F);
    uint8_t g = (uint8_t)((rgb565 >> 5) & 0x3F);
    uint8_t b = (uint8_t)(rgb565 & 0x1F);

    r = (uint8_t)((r << 3) | (r >> 2));
    g = (uint8_t)((g << 2) | (g >> 4));
    b = (uint8_t)((b << 3) | (b >> 2));

    return 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/**
 * @brief 将 RGB888 三通道颜色拼装为 ARGB8888
 * @param r 红色分量
 * @param g 绿色分量
 * @param b 蓝色分量
 * @return 拼装后的 ARGB8888 颜色值（Alpha 固定 0xFF）
 */
static uint32_t rgb888_to_argb8888(uint8_t r, uint8_t g, uint8_t b)
{
    return 0xFF000000U | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

/**
 * @brief 将 RGB565 像素块写入模拟器屏幕缓冲
 * @param gram RGB565 像素数据指针
 * @param pix_size 像素数量
 */
void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size)
{
    uint16_t rect_w;

    if (screen_buffer == NULL || gram == NULL)
    {
        return;
    }

    rect_w = (uint16_t)(current_addr.x1 - current_addr.x0 + 1U);

    for (uint32_t i = 0; i < pix_size; i++)
    {
        uint32_t total_idx = pixel_offset + i;
        uint16_t local_x = (uint16_t)(total_idx % rect_w);
        uint16_t local_y = (uint16_t)(total_idx / rect_w);
        uint16_t x = (uint16_t)(current_addr.x0 + local_x);
        uint16_t y = (uint16_t)(current_addr.y0 + local_y);

        if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
        {
            screen_buffer[y * SCREEN_WIDTH + x] = rgb565_to_argb8888(gram[i]);
        }
    }

    /* 这里只写模拟器屏幕缓冲，统一在 sim_lcd_update() 里刷到窗口。 */
    pixel_offset += pix_size;
}

/**
 * @brief 将 RGB888 像素块写入模拟器屏幕缓冲
 * @param gram RGB888 像素数据指针
 * @param pix_size 字节数量（3 字节/像素）
 */
void lcd_rgb888_port(uint8_t *gram, uint32_t pix_size)
{
    uint16_t rect_w;
    uint32_t pixel_count;

    if (screen_buffer == NULL || gram == NULL)
    {
        return;
    }

    rect_w = (uint16_t)(current_addr.x1 - current_addr.x0 + 1U);
    pixel_count = pix_size / 3U;

    for (uint32_t i = 0; i < pixel_count; i++)
    {
        uint32_t total_idx = pixel_offset + i;
        uint16_t local_x = (uint16_t)(total_idx % rect_w);
        uint16_t local_y = (uint16_t)(total_idx / rect_w);
        uint16_t x = (uint16_t)(current_addr.x0 + local_x);
        uint16_t y = (uint16_t)(current_addr.y0 + local_y);
        uint8_t *src = gram + i * 3U;

        if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
        {
            screen_buffer[y * SCREEN_WIDTH + x] = rgb888_to_argb8888(src[0], src[1], src[2]);
        }
    }

    pixel_offset += pixel_count;
}

/**
 * @brief 执行帧率限制并将屏幕缓冲刷新到 SDL 窗口
 */
void sim_lcd_update(void)
{
    static uint32_t last_frame_tick = 0;
    uint32_t current_tick;
    uint32_t target_frame_ms;
    uint32_t elapsed;

    if (renderer == NULL || texture == NULL || screen_buffer == NULL)
    {
        return;
    }

    /* 1. 自动维持目标帧率，避免模拟器吃满 CPU。 */
    current_tick = SDL_GetTicks();
    target_frame_ms = 1000U / SIM_MAX_FPS;
    elapsed = current_tick - last_frame_tick;

    if (elapsed < target_frame_ms)
    {
        SDL_Delay(target_frame_ms - elapsed);
    }
    else
    {
        SDL_Delay(1);
    }
    last_frame_tick = SDL_GetTicks();

    /* 2. 把屏幕缓冲统一刷新到 SDL 窗口。 */
    SDL_UpdateTexture(texture, NULL, screen_buffer, SCREEN_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/**
 * @brief 初始化显示硬件抽象层（转发到模拟 LCD 初始化）
 */
void lcd_hw_init(void) { sim_lcd_init(); }

/**
 * @brief 打开背光（模拟器为空实现）
 */
void lcd_bl_on(void) {}

/**
 * @brief 关闭背光（模拟器为空实现）
 */
void lcd_bl_off(void) {}

/**
 * @brief 毫秒级延时
 * @param ms 延时毫秒数
 */
void lcd_delay_ms(uint32_t ms) { SDL_Delay(ms); }

/**
 * @brief 初始化输入硬件抽象层（清空待上报输入状态）
 */
void input_hw_init(void)
{
    g_pending_input.x = 0;
    g_pending_input.y = 0;
    g_pending_input.state = WE_TOUCH_STATE_NONE;
    g_has_pending_input = 0;
}

/**
 * @brief 读取并输出一帧输入数据
 * @param data 输入数据输出指针
 */
void we_input_port_read(we_indev_data_t *data)
{
    if (data == NULL)
    {
        return;
    }

    if (g_has_pending_input)
    {
        *data = g_pending_input;
        g_has_pending_input = 0;
    }
    else
    {
        data->x = 0;
        data->y = 0;
        data->state = WE_TOUCH_STATE_NONE;
    }
}

/**
 * @brief 初始化存储硬件抽象层：打开 exe 旁的外挂 flash 镜像文件
 * @note 在 lcd_hw_init（内含 SDL_Init）之后调用；找不到文件不算致命错误，
 *       后续读取全部返回 0xFF（demo 15/16 会显示初始化失败提示）。
 */
void storage_hw_init(void)
{
    char *base = SDL_GetBasePath();

    if (base != NULL)
    {
        char path[512];

        snprintf(path, sizeof(path), "%smerged_bin.bin", base);
        SDL_free(base);
        sim_flash_file = fopen(path, "rb");
    }
    if (sim_flash_file == NULL)
        sim_flash_file = fopen("merged_bin.bin", "rb"); /* 兜底：当前目录 */

    if (sim_flash_file != NULL)
    {
        fseek(sim_flash_file, 0L, SEEK_END);
        sim_flash_size = (uint32_t)ftell(sim_flash_file);
    }
    else
    {
        SDL_Log("merged_bin.bin not found; external-flash reads return 0xFF");
    }
}

/**
 * @brief 从模拟外挂 flash 镜像读取数据（fseek + fread 直读 bin 文件）
 * @param addr 读取起始地址
 * @param buf 目标缓冲区
 * @param len 读取长度（字节）
 */
void we_storage_port_read(uint32_t addr, uint8_t buf[], uint32_t len)
{
    uint32_t got = 0U;

    if (sim_flash_file != NULL && addr < sim_flash_size)
    {
        uint32_t avail = sim_flash_size - addr;
        uint32_t n = (len < avail) ? len : avail;

        if (fseek(sim_flash_file, (long)addr, SEEK_SET) == 0)
            got = (uint32_t)fread(buf, 1U, n, sim_flash_file);
    }
    if (got < len)
        memset(&buf[got], 0xFF, (size_t)(len - got)); /* 越界/缺文件补已擦除态 */
}

/* WASD 键盘模拟滑动手势的状态机。
 * 按下 WASD 时先生成一帧 PRESSED（屏幕中心），
 * 下一帧再生成 RELEASED（偏移超过阈值），
 * 这样 we_gui_indev_handler 的滑动检测自然触发。 */
static uint8_t g_key_swipe_phase = 0; /* 0=空闲, 1=等下一帧发 RELEASED */
static int16_t g_key_release_x = 0;
static int16_t g_key_release_y = 0;

/* 滑动偏移量，需要超过 WE_CFG_SWIPE_THRESHOLD */
#define SIM_KEY_SWIPE_OFS (WE_CFG_SWIPE_THRESHOLD + 10)

/**
 * @brief 处理 SDL 事件并转换为 GUI 输入事件
 * @param lcd GUI 上下文，用于计算键盘手势的中心点
 * @return 1 表示继续主循环，0 表示退出主循环
 */
int sim_handle_events(we_lcd_t *lcd)
{
    SDL_Event e;
    static int is_mouse_down = 0;

    /* 键盘滑动第二阶段：上一帧发了 PRESSED，本帧补发 RELEASED */
    if (g_key_swipe_phase == 1)
    {
        g_pending_input.state = WE_TOUCH_STATE_RELEASED;
        g_pending_input.x = g_key_release_x;
        g_pending_input.y = g_key_release_y;
        g_has_pending_input = 1;
        g_key_swipe_phase = 0;
    }

    while (SDL_PollEvent(&e) != 0)
    {
        if (e.type == SDL_QUIT)
        {
            return 0;
        }

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
        /* 语义键映射 → 焦点导航：
         * 方向键 = 上/下/左/右（保留系统连发 = 按住连续移动/调值）
         * Tab / Shift+Tab = 后一个/前一个    Esc / 退格 = BACK
         * Enter / 小键盘 Enter / 空格 = OK：按下/松开双沿上报，
         * 控件按住期间保持按压态，松开才触发点击（滤掉系统连发）。 */
        if (e.type == SDL_KEYDOWN && !is_mouse_down)
        {
            uint8_t nav_key = WE_KEY_NONE;

            switch (e.key.keysym.sym)
            {
            case SDLK_UP:
                nav_key = WE_KEY_UP;
                break;
            case SDLK_DOWN:
                nav_key = WE_KEY_DOWN;
                break;
            case SDLK_LEFT:
                nav_key = WE_KEY_LEFT;
                break;
            case SDLK_RIGHT:
                nav_key = WE_KEY_RIGHT;
                break;
            case SDLK_TAB:
                nav_key = ((e.key.keysym.mod & KMOD_SHIFT) != 0) ? WE_KEY_PREV : WE_KEY_NEXT;
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                if (e.key.repeat == 0)
                    we_gui_key_press(lcd, WE_KEY_OK); /* 松开沿在 SDL_KEYUP 上报 */
                continue;
            case SDLK_ESCAPE:
            case SDLK_BACKSPACE:
                nav_key = WE_KEY_BACK;
                break;
            default:
                break;
            }

            if (nav_key != WE_KEY_NONE)
            {
                we_gui_key_press(lcd, nav_key);
                continue;
            }
        }

        /* OK 键松开沿：按住多久按压态就保持多久 */
        if (e.type == SDL_KEYUP)
        {
            switch (e.key.keysym.sym)
            {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
            case SDLK_SPACE:
                we_gui_key_release(lcd, WE_KEY_OK);
                continue;
            default:
                break;
            }
        }
#endif

        /* WASD 键盘 → 模拟滑动手势 */
        if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && !is_mouse_down)
        {
            int16_t cx = (int16_t)(lcd->width / 2);
            int16_t cy = (int16_t)(lcd->height / 2);
            int16_t rx = cx, ry = cy;
            uint8_t valid = 0;

            switch (e.key.keysym.sym)
            {
            case SDLK_a:
                rx = cx - SIM_KEY_SWIPE_OFS;
                valid = 1;
                break; /* 左滑 */
            case SDLK_d:
                rx = cx + SIM_KEY_SWIPE_OFS;
                valid = 1;
                break; /* 右滑 */
            case SDLK_w:
                ry = cy - SIM_KEY_SWIPE_OFS;
                valid = 1;
                break; /* 上滑 */
            case SDLK_s:
                ry = cy + SIM_KEY_SWIPE_OFS;
                valid = 1;
                break; /* 下滑 */
            default:
                break;
            }

            if (valid)
            {
                /* 本帧发 PRESSED（屏幕中心） */
                g_pending_input.state = WE_TOUCH_STATE_PRESSED;
                g_pending_input.x = cx;
                g_pending_input.y = cy;
                g_has_pending_input = 1;
                /* 记录下一帧的 RELEASED 目标 */
                g_key_release_x = rx;
                g_key_release_y = ry;
                g_key_swipe_phase = 1;
                continue;
            }
        }

        /* 鼠标触摸模拟 */
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
        {
            is_mouse_down = 1;
            g_pending_input.state = WE_TOUCH_STATE_PRESSED;
            g_pending_input.x = (uint16_t)(e.button.x / SIM_SCALE);
            g_pending_input.y = (uint16_t)(e.button.y / SIM_SCALE);
            g_has_pending_input = 1;
        }
        else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT)
        {
            is_mouse_down = 0;
            g_pending_input.state = WE_TOUCH_STATE_RELEASED;
            g_pending_input.x = (uint16_t)(e.button.x / SIM_SCALE);
            g_pending_input.y = (uint16_t)(e.button.y / SIM_SCALE);
            g_has_pending_input = 1;
        }
        else if (e.type == SDL_MOUSEMOTION && is_mouse_down)
        {
            g_pending_input.state = WE_TOUCH_STATE_STAY;
            g_pending_input.x = (uint16_t)(e.motion.x / SIM_SCALE);
            g_pending_input.y = (uint16_t)(e.motion.y / SIM_SCALE);
            g_has_pending_input = 1;
        }
    }

    return 1;
}
