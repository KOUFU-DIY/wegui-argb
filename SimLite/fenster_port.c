/**
 * @file  fenster_port.c
 * @brief SimLite 端口实现 —— 基于 fenster 单头库（Win32 GDI / macOS Cocoa /
 *        Linux X11），无 SDL 依赖，可用 TCC（Windows）或系统 cc 编译。
 *
 * 结构：
 *   - gui_fb：GUI 分辨率 ARGB 帧缓冲，LCD flush 端口写这里；
 *   - win_buf：放大 LITE_SCALE 倍的窗口缓冲，lite_present() 最近邻放大
 *     后交给 fenster 贴窗；--shot 自检模式不开窗，直接导出 gui_fb；
 *   - 输入：fenster 只给按键/鼠标"状态"，端口逐帧 diff 出边沿——鼠标
 *     映射触摸（按下/拖动/抬起），键盘映射语义键（方向/Tab/OK/BACK，
 *     OK 双沿；方向与 Tab 带长按自动重复），WASD 两相注入模拟滑动。
 */

#include "lite_port.h"
#include "fenster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 帧缓冲与窗口 ---------------- */
static uint32_t gui_fb[SCREEN_WIDTH * SCREEN_HEIGHT];
static uint32_t *win_buf = NULL;
static int g_headless = 0;
static int g_quit = 0;
static int g_window_open = 0;

static struct fenster g_f = {
    .title = "WeGui SimLite",
    .width = SCREEN_WIDTH * LITE_SCALE,
    .height = SCREEN_HEIGHT * LITE_SCALE,
    .buf = NULL,
};

static struct
{
    uint16_t x0, y0, x1, y1;
} current_addr;
static uint32_t pixel_offset = 0;

/* 触摸（鼠标）待上报槽 */
static we_indev_data_t g_pending_input;
static uint8_t g_has_pending_input = 0;
static int g_prev_mouse = 0;

void lite_set_headless(int on) { g_headless = on; }

void lite_lcd_init(void)
{
    memset(gui_fb, 0, sizeof(gui_fb));
    if (g_headless)
        return;

    win_buf = (uint32_t *)malloc((size_t)g_f.width * (size_t)g_f.height * 4U);
    if (win_buf == NULL)
        return;
    memset(win_buf, 0, (size_t)g_f.width * (size_t)g_f.height * 4U);
    g_f.buf = win_buf;

    if (fenster_open(&g_f) == 0)
        g_window_open = 1;

#if defined(_WIN32)
    /* fenster 把 width/height 当整窗尺寸用，客户区会被边框吃掉一圈；
     * 这里去掉可拉伸边框与最大化按钮（帧缓冲固定尺寸，拉伸只会糊），
     * 再按客户区尺寸回调一次窗口大小（保证像素 1:1 完整可见），
     * 并置顶显示（与旧 SDL 模拟器 ALWAYS_ON_TOP 的习惯一致）。 */
    if (g_window_open)
    {
        RECT r;
        LONG style = GetWindowLong(g_f.hwnd, GWL_STYLE);

        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        SetWindowLong(g_f.hwnd, GWL_STYLE, style);

        r.left = 0;
        r.top = 0;
        r.right = g_f.width;
        r.bottom = g_f.height;
        AdjustWindowRectEx(&r, (DWORD)style, FALSE, WS_EX_CLIENTEDGE);
        SetWindowPos(g_f.hwnd, HWND_TOPMOST, 0, 0, r.right - r.left, r.bottom - r.top,
                     SWP_NOMOVE | SWP_FRAMECHANGED);
    }
#endif
}

void lcd_hw_init(void) { lite_lcd_init(); }
void lcd_bl_on(void) {}
void lcd_bl_off(void) {}
void lcd_delay_ms(uint32_t ms) { fenster_sleep((int64_t)ms); }

void lite_lcd_set_addr(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    current_addr.x0 = x0;
    current_addr.y0 = y0;
    current_addr.x1 = x1;
    current_addr.y1 = y1;
    pixel_offset = 0;
}

/**
 * @brief RGB565 -> ARGB8888（位复制扩展，无偏）
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

void lcd_rgb565_port(uint16_t *gram, uint32_t pix_size)
{
    uint16_t rect_w;
    uint32_t i;

    if (gram == NULL)
        return;

    rect_w = (uint16_t)(current_addr.x1 - current_addr.x0 + 1U);

    for (i = 0; i < pix_size; i++)
    {
        uint32_t total_idx = pixel_offset + i;
        uint16_t local_x = (uint16_t)(total_idx % rect_w);
        uint16_t local_y = (uint16_t)(total_idx / rect_w);
        uint16_t x = (uint16_t)(current_addr.x0 + local_x);
        uint16_t y = (uint16_t)(current_addr.y0 + local_y);

        if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT)
            gui_fb[(uint32_t)y * SCREEN_WIDTH + x] = rgb565_to_argb8888(gram[i]);
    }

    pixel_offset += pix_size;
}

void lite_present(void)
{
    int gy;

    if (g_headless || !g_window_open || win_buf == NULL)
        return;

    /* gui_fb -> win_buf 最近邻整数放大 */
    for (gy = 0; gy < SCREEN_HEIGHT; gy++)
    {
        const uint32_t *src = &gui_fb[(uint32_t)gy * SCREEN_WIDTH];
        uint32_t *row0 = &win_buf[(uint32_t)gy * LITE_SCALE * (uint32_t)g_f.width];
        int gx, s;

        for (gx = 0; gx < SCREEN_WIDTH; gx++)
        {
            uint32_t c = src[gx];
            for (s = 0; s < LITE_SCALE; s++)
                row0[gx * LITE_SCALE + s] = c;
        }
        for (s = 1; s < LITE_SCALE; s++)
            memcpy(row0 + (uint32_t)s * g_f.width, row0, (size_t)g_f.width * 4U);
    }

    if (fenster_loop(&g_f) != 0)
        g_quit = 1;
}

int lite_dump_ppm(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int i;

    if (fp == NULL)
        return -1;
    fprintf(fp, "P6 %d %d 255 ", (int)SCREEN_WIDTH, (int)SCREEN_HEIGHT);
    for (i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
    {
        unsigned char rgb[3];

        rgb[0] = (unsigned char)(gui_fb[i] >> 16);
        rgb[1] = (unsigned char)(gui_fb[i] >> 8);
        rgb[2] = (unsigned char)gui_fb[i];
        fwrite(rgb, 1, 3, fp);
    }
    fclose(fp);
    return 0;
}

uint32_t lite_ticks_ms(void) { return (uint32_t)fenster_time(); }
void lite_sleep_ms(uint32_t ms) { fenster_sleep((int64_t)ms); }

const uint32_t *lite_fb(void) { return gui_fb; }

/* ---------------- 输入 ---------------- */

void input_hw_init(void)
{
    memset(&g_pending_input, 0, sizeof(g_pending_input));
    g_pending_input.state = WE_TOUCH_STATE_NONE;
    g_has_pending_input = 0;
    g_prev_mouse = 0;
}

void we_input_port_read(we_indev_data_t *data)
{
    if (data == NULL)
        return;

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

/* fenster 统一键码（三平台一致）：方向 17..20、Enter=10、Tab=9、Esc=27、
 * 退格=8、空格=32、字母为大写 ASCII */
#define FK_UP 17
#define FK_DOWN 18
#define FK_RIGHT 19
#define FK_LEFT 20
#define FK_ENTER 10
#define FK_TAB 9
#define FK_ESC 27
#define FK_BKSP 8
#define FK_SPACE 32

/* WASD 键盘滑动两相状态（同 SDL 端口方案） */
static uint8_t g_key_swipe_phase = 0;
static int16_t g_key_release_x = 0;
static int16_t g_key_release_y = 0;
#define LITE_KEY_SWIPE_OFS (WE_CFG_SWIPE_THRESHOLD + 10)

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
/* 方向/Tab 长按自动重复（fenster 只给状态，重复是端口的事） */
static int g_rep_code = 0;        /* 正在长按的 fenster 键码 */
static uint8_t g_rep_navkey = 0;  /* 对应语义键 */
static uint32_t g_rep_next = 0;   /* 下次重复时刻 */
#define LITE_REP_DELAY_MS 350U
#define LITE_REP_RATE_MS 90U
#endif

static int g_prev_keys[256];

int lite_handle_events(we_lcd_t *lcd)
{
    int mouse_down;
    int16_t mx, my;

    if (g_quit)
        return 0;
    if (g_headless || !g_window_open)
        return 1;

    /* WASD 滑动第二相：上一帧发了 PRESSED，本帧补 RELEASED */
    if (g_key_swipe_phase == 1)
    {
        g_pending_input.state = WE_TOUCH_STATE_RELEASED;
        g_pending_input.x = g_key_release_x;
        g_pending_input.y = g_key_release_y;
        g_has_pending_input = 1;
        g_key_swipe_phase = 0;
    }

    /* ---- 鼠标 -> 触摸（坐标除以放大倍数并钳到屏内） ---- */
    mouse_down = g_f.mouse & 1;
    mx = (int16_t)(g_f.x / LITE_SCALE);
    my = (int16_t)(g_f.y / LITE_SCALE);
    if (mx < 0)
        mx = 0;
    if (my < 0)
        my = 0;
    if (mx >= SCREEN_WIDTH)
        mx = SCREEN_WIDTH - 1;
    if (my >= SCREEN_HEIGHT)
        my = SCREEN_HEIGHT - 1;

    if (mouse_down && !g_prev_mouse)
    {
        g_pending_input.state = WE_TOUCH_STATE_PRESSED;
        g_pending_input.x = mx;
        g_pending_input.y = my;
        g_has_pending_input = 1;
    }
    else if (!mouse_down && g_prev_mouse)
    {
        g_pending_input.state = WE_TOUCH_STATE_RELEASED;
        g_pending_input.x = mx;
        g_pending_input.y = my;
        g_has_pending_input = 1;
    }
    else if (mouse_down)
    {
        g_pending_input.state = WE_TOUCH_STATE_STAY;
        g_pending_input.x = mx;
        g_pending_input.y = my;
        g_has_pending_input = 1;
    }
    g_prev_mouse = mouse_down;

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* ---- 键盘 -> 语义键（按状态 diff 出边沿） ---- */
    {
        struct
        {
            int code;
            uint8_t key;
        } nav[] = {
            {FK_UP, WE_KEY_UP},     {FK_DOWN, WE_KEY_DOWN}, {FK_LEFT, WE_KEY_LEFT},
            {FK_RIGHT, WE_KEY_RIGHT}, {FK_ESC, WE_KEY_BACK},  {FK_BKSP, WE_KEY_BACK},
        };
        uint32_t now = lite_ticks_ms();
        size_t i;

        for (i = 0; i < sizeof(nav) / sizeof(nav[0]); i++)
        {
            if (g_f.keys[nav[i].code] && !g_prev_keys[nav[i].code] && !mouse_down)
            {
                we_gui_key_press(lcd, nav[i].key);
                g_rep_code = nav[i].code;
                g_rep_navkey = nav[i].key;
                g_rep_next = now + LITE_REP_DELAY_MS;
            }
        }

        /* Tab / Shift+Tab（fenster mod bit1 = shift） */
        if (g_f.keys[FK_TAB] && !g_prev_keys[FK_TAB] && !mouse_down)
        {
            uint8_t k = ((g_f.mod & 2) != 0) ? WE_KEY_PREV : WE_KEY_NEXT;

            we_gui_key_press(lcd, k);
            g_rep_code = FK_TAB;
            g_rep_navkey = k;
            g_rep_next = now + LITE_REP_DELAY_MS;
        }

        /* 长按自动重复（OK 不重复，保持按压语义） */
        if (g_rep_code != 0)
        {
            if (!g_f.keys[g_rep_code])
            {
                g_rep_code = 0;
            }
            else if (now >= g_rep_next)
            {
                we_gui_key_press(lcd, g_rep_navkey);
                g_rep_next = now + LITE_REP_RATE_MS;
            }
        }

        /* OK：按下沿/松开沿双上报（Enter 与空格） */
        if ((g_f.keys[FK_ENTER] && !g_prev_keys[FK_ENTER]) ||
            (g_f.keys[FK_SPACE] && !g_prev_keys[FK_SPACE]))
            we_gui_key_press(lcd, WE_KEY_OK);
        if ((!g_f.keys[FK_ENTER] && g_prev_keys[FK_ENTER]) ||
            (!g_f.keys[FK_SPACE] && g_prev_keys[FK_SPACE]))
            we_gui_key_release(lcd, WE_KEY_OK);
    }
#endif

    /* ---- WASD -> 模拟滑动（按下沿触发两相注入） ---- */
    if (!mouse_down && g_key_swipe_phase == 0)
    {
        int16_t cx = (int16_t)(lcd->width / 2);
        int16_t cy = (int16_t)(lcd->height / 2);
        int16_t rx = cx, ry = cy;
        uint8_t valid = 0;

        if (g_f.keys['A'] && !g_prev_keys['A'])
        {
            rx = (int16_t)(cx - LITE_KEY_SWIPE_OFS);
            valid = 1;
        }
        else if (g_f.keys['D'] && !g_prev_keys['D'])
        {
            rx = (int16_t)(cx + LITE_KEY_SWIPE_OFS);
            valid = 1;
        }
        else if (g_f.keys['W'] && !g_prev_keys['W'])
        {
            ry = (int16_t)(cy - LITE_KEY_SWIPE_OFS);
            valid = 1;
        }
        else if (g_f.keys['S'] && !g_prev_keys['S'])
        {
            ry = (int16_t)(cy + LITE_KEY_SWIPE_OFS);
            valid = 1;
        }

        if (valid)
        {
            g_pending_input.state = WE_TOUCH_STATE_PRESSED;
            g_pending_input.x = cx;
            g_pending_input.y = cy;
            g_has_pending_input = 1;
            g_key_release_x = rx;
            g_key_release_y = ry;
            g_key_swipe_phase = 1;
        }
    }

    memcpy(g_prev_keys, g_f.keys, sizeof(g_prev_keys));
    return 1;
}

/* ---------------- 存储（外挂 flash 镜像） ---------------- */
static FILE *lite_flash_file = NULL;
static uint32_t lite_flash_size = 0U;

void storage_hw_init(void)
{
#if defined(_WIN32)
    /* 先找 exe 同目录，双击运行也能定位镜像 */
    char path[512];
    DWORD n = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));

    if (n > 0 && n < sizeof(path))
    {
        char *slash = strrchr(path, '\\');

        if (slash != NULL)
        {
            slash[1] = '\0';
            strncat(path, "merged_bin.bin", sizeof(path) - strlen(path) - 1U);
            lite_flash_file = fopen(path, "rb");
        }
    }
#endif
    if (lite_flash_file == NULL)
        lite_flash_file = fopen("merged_bin.bin", "rb"); /* 兜底：当前目录 */

    if (lite_flash_file != NULL)
    {
        fseek(lite_flash_file, 0L, SEEK_END);
        lite_flash_size = (uint32_t)ftell(lite_flash_file);
    }
    else
    {
        printf("merged_bin.bin not found; external-flash reads return 0xFF\n");
    }
}

void we_storage_port_read(uint32_t addr, uint8_t buf[], uint32_t len)
{
    uint32_t got = 0U;

    if (lite_flash_file != NULL && addr < lite_flash_size)
    {
        uint32_t avail = lite_flash_size - addr;
        uint32_t n = (len < avail) ? len : avail;

        if (fseek(lite_flash_file, (long)addr, SEEK_SET) == 0)
            got = (uint32_t)fread(buf, 1U, n, lite_flash_file);
    }
    if (got < len)
        memset(&buf[got], 0xFF, (size_t)(len - got));
}
