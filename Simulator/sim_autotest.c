#include "sim_autotest.h"

#if (WE_SIM_AUTOTEST == 1) /* 默认 0：整个文件不参与编译，见头文件开关说明 */

#include "sdl_port.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 脚本事件容量：一条轨迹的按下/移动/抬起与按键注入合计上限 */
#define _SIM_AT_EV_MAX 128

/* 脚本命令编码 */
enum
{
    _EV_DOWN = 1,
    _EV_MOVE,
    _EV_UP,
    _EV_KINJ,
    _EV_KPRESS,
    _EV_KREL
};

typedef struct
{
    uint16_t frame;
    uint8_t cmd;
    int16_t x;
    int16_t y;
} _sim_at_ev_t;

static _sim_at_ev_t _sim_at_evs[_SIM_AT_EV_MAX];

/**
 * @brief 把语义键名解析成键值
 * @param name 传入：键名字符串（UP/DOWN/LEFT/RIGHT/PREV/NEXT/OK/BACK）
 * @return 键值；无法识别返回 0
 */
static uint8_t _sim_at_key_from_name(const char *name)
{
    if (strcmp(name, "UP") == 0)
        return WE_KEY_UP;
    if (strcmp(name, "DOWN") == 0)
        return WE_KEY_DOWN;
    if (strcmp(name, "LEFT") == 0)
        return WE_KEY_LEFT;
    if (strcmp(name, "RIGHT") == 0)
        return WE_KEY_RIGHT;
    if (strcmp(name, "PREV") == 0)
        return WE_KEY_PREV;
    if (strcmp(name, "NEXT") == 0)
        return WE_KEY_NEXT;
    if (strcmp(name, "OK") == 0)
        return WE_KEY_OK;
    if (strcmp(name, "BACK") == 0)
        return WE_KEY_BACK;
    return 0U;
}

/**
 * @brief 载入输入注入脚本
 * @param path 传入：脚本文件路径
 * @param name_out 传出：脚本文件名（去掉目录，写进结果行）
 * @return 解析出的事件条数
 * @note 行格式 `<frame> <cmd> [args]`，cmd = down x y / move x y / up /
 *       kinject K / kpress K / krelease K；'#' 开头为注释行。
 */
static int _sim_at_load_script(const char *path, const char **name_out)
{
    char line[96];
    FILE *sf = fopen(path, "r");
    const char *bs = strrchr(path, '\\');
    const char *fs = strrchr(path, '/');
    int ev_cnt = 0;

    *name_out = (bs != NULL && (fs == NULL || bs > fs)) ? (bs + 1)
                : (fs != NULL)                          ? (fs + 1)
                                                        : path;
    if (sf == NULL)
        return 0;

    while (ev_cnt < _SIM_AT_EV_MAX && fgets(line, sizeof(line), sf) != NULL)
    {
        char cmd[16];
        char karg[16];
        int frame;
        int x = 0;
        int y = 0;

        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        if (sscanf(line, "%d %15s %d %d", &frame, cmd, &x, &y) < 2)
            continue;

        _sim_at_evs[ev_cnt].frame = (uint16_t)frame;
        _sim_at_evs[ev_cnt].x = (int16_t)x;
        _sim_at_evs[ev_cnt].y = (int16_t)y;
        if (strcmp(cmd, "down") == 0)
            _sim_at_evs[ev_cnt].cmd = _EV_DOWN;
        else if (strcmp(cmd, "move") == 0)
            _sim_at_evs[ev_cnt].cmd = _EV_MOVE;
        else if (strcmp(cmd, "up") == 0)
            _sim_at_evs[ev_cnt].cmd = _EV_UP;
        else
        {
            uint8_t k = 0U;

            if (sscanf(line, "%*d %*s %15s", karg) == 1)
                k = _sim_at_key_from_name(karg);
            if (k == 0U)
                continue;
            _sim_at_evs[ev_cnt].x = (int16_t)k;
            if (strcmp(cmd, "kinject") == 0)
                _sim_at_evs[ev_cnt].cmd = _EV_KINJ;
            else if (strcmp(cmd, "kpress") == 0)
                _sim_at_evs[ev_cnt].cmd = _EV_KPRESS;
            else if (strcmp(cmd, "krelease") == 0)
                _sim_at_evs[ev_cnt].cmd = _EV_KREL;
            else
                continue;
        }
        ev_cnt++;
    }
    fclose(sf);
    return ev_cnt;
}

/**
 * @brief 按诊断环境变量抓取当前帧
 * @param frame 传入：当前帧号
 * @param crc 传入：截至本帧的链式哈希
 * @return 无
 * @note WE_AUTOTEST_PPM=帧号列表 抓图；WE_AUTOTEST_DUMP 输出逐帧哈希。
 */
static void _sim_at_diagnostics(int frame, uint32_t crc)
{
    const char *pf = getenv("WE_AUTOTEST_PPM");

    if (pf != NULL)
    {
        char tok[512];
        char *t;

        strncpy(tok, pf, sizeof(tok) - 1U);
        tok[sizeof(tok) - 1U] = 0;
        for (t = strtok(tok, ","); t != NULL; t = strtok(NULL, ","))
        {
            if (atoi(t) == frame)
            {
                char name[64];

                sprintf(name, "frame_%03d.ppm", frame);
                sim_lcd_dump_ppm(name);
            }
        }
    }
    if (getenv("WE_AUTOTEST_DUMP") != NULL)
    {
        FILE *df = fopen("autotest_frames.txt", (frame == 0) ? "w" : "a");

        if (df != NULL)
        {
            fprintf(df, "%d %08X\n", frame, (unsigned int)crc);
            fclose(df);
        }
    }
}

void sim_autotest_parse_args(int argc, char *argv[], sim_autotest_cfg_t *cfg)
{
    int argi;

    if (cfg == NULL)
        return;

    cfg->frames = 0;
    cfg->out_path = "autotest_crc.txt";
    cfg->script_path = NULL;

    for (argi = 1; argi < argc; argi++)
    {
        if (strcmp(argv[argi], "--autotest") == 0 && (argi + 1) < argc)
            cfg->frames = atoi(argv[++argi]);
        else if (strcmp(argv[argi], "--out") == 0 && (argi + 1) < argc)
            cfg->out_path = argv[++argi];
        else if (strcmp(argv[argi], "--script") == 0 && (argi + 1) < argc)
            cfg->script_path = argv[++argi];
    }

    /* 结果经文件回传：exe 链接了 -mwindows，没有控制台，stdout 不可靠。
     * dummy 视频驱动免开窗，便于脚本批量跑。 */
    if (cfg->frames > 0)
        SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
}

void sim_autotest_run(we_lcd_t *lcd, const sim_autotest_cfg_t *cfg, int demo_id)
{
    uint32_t crc = 2166136261u; /* FNV-1a 初始基准值 */
    we_indev_data_t sim_touch;
    uint8_t touch_active = 0U;
    const char *script_name = "";
    int ev_cnt = 0;
    FILE *fp;
    int f;

    if (lcd == NULL || cfg == NULL || cfg->frames <= 0)
        return;

    /* 脚本化输入注入：down 之后每帧自动补 STAY（与真实端口行为一致），
     * move 只改坐标，up 收尾。固定 16ms 步进下完全确定，可把拖拽/惯性/
     * 焦点导航/弹层开关的轨迹一并记入基准哈希。 */
    if (cfg->script_path != NULL)
        ev_cnt = _sim_at_load_script(cfg->script_path, &script_name);

    sim_touch.x = 0;
    sim_touch.y = 0;
    sim_touch.state = WE_TOUCH_STATE_NONE;

    for (f = 0; f < cfg->frames; f++)
    {
        int e;
        uint8_t touched_this_frame = 0U;

        for (e = 0; e < ev_cnt; e++)
        {
            if (_sim_at_evs[e].frame != (uint16_t)f)
                continue;
            switch (_sim_at_evs[e].cmd)
            {
            case _EV_DOWN:
                sim_touch.x = _sim_at_evs[e].x;
                sim_touch.y = _sim_at_evs[e].y;
                sim_touch.state = WE_TOUCH_STATE_PRESSED;
                we_gui_indev_handler(lcd, &sim_touch);
                touch_active = 1U;
                touched_this_frame = 1U;
                break;
            case _EV_MOVE:
                sim_touch.x = _sim_at_evs[e].x;
                sim_touch.y = _sim_at_evs[e].y;
                break;
            case _EV_UP:
                sim_touch.state = WE_TOUCH_STATE_RELEASED;
                we_gui_indev_handler(lcd, &sim_touch);
                touch_active = 0U;
                touched_this_frame = 1U;
                break;
#if (WE_CFG_ENABLE_KEY_INPUT == 1)
            case _EV_KINJ:
                we_gui_key_inject(lcd, (uint8_t)_sim_at_evs[e].x);
                break;
            case _EV_KPRESS:
                we_gui_key_press(lcd, (uint8_t)_sim_at_evs[e].x);
                break;
            case _EV_KREL:
                we_gui_key_release(lcd, (uint8_t)_sim_at_evs[e].x);
                break;
#endif /* 纯触摸档：脚本照常解析，键事件落到 default 被忽略 */
            default:
                break;
            }
        }
        if (touch_active && !touched_this_frame)
        {
            sim_touch.state = WE_TOUCH_STATE_STAY;
            we_gui_indev_handler(lcd, &sim_touch);
        }

        we_gui_tick_inc(lcd, 16U);
        we_gui_task_handler(lcd);
        crc = sim_lcd_hash(crc);
        _sim_at_diagnostics(f, crc);
    }

    fp = fopen(cfg->out_path, "w");
    if (fp != NULL)
    {
        if (cfg->script_path != NULL)
            fprintf(fp, "id=%d frames=%d script=%s crc=%08X\n",
                    demo_id, cfg->frames, script_name, (unsigned int)crc);
        else
            fprintf(fp, "id=%d frames=%d crc=%08X\n",
                    demo_id, cfg->frames, (unsigned int)crc);
        fclose(fp);
    }
}

#endif /* WE_SIM_AUTOTEST */
