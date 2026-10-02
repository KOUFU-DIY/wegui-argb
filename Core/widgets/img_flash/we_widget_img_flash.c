
#include "we_widget_img_flash.h"
#include "wegui.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * IMG_RGB565 Flash 渲染器（无压缩）
 *
 * 逐行直接寻址：每行可见像素地址 = flash_addr + 6 + (y * img_w + ix_start) * 2。
 * 内核已把窗口收窄到本控件包围盒内，窗口左上角即 pfb_gram 起点。
 * -------------------------------------------------------------------------- */

/**
 * @brief 渲染未压缩 RGB565 Flash 图片到当前 PFB 切片。
 * @param p_lcd 当前 LCD 上下文。
 * @param obj 图片控件实例。
 * @param opacity 图片整体透明度（容器级联后，> 5）。
 */
static void _flash_render_rgb565(we_lcd_t *p_lcd,
                                  we_flash_img_obj_t *obj,
                                  uint8_t opacity)
{
    uint16_t  img_w  = (uint16_t)obj->base.w;
    uint16_t  draw_w = (uint16_t)(p_lcd->clip.x1 - p_lcd->clip.x0 + 1);
    uint16_t  draw_h = (uint16_t)(p_lcd->clip.y1 - p_lcd->clip.y0 + 1);
    uint32_t  addr   = obj->flash_addr + 6U +
                       ((uint32_t)(p_lcd->clip.y0 - obj->base.y) * img_w + (uint32_t)(p_lcd->clip.x0 - obj->base.x)) * 2U;
    colour_t *row    = p_lcd->pfb_gram;
    uint16_t  y;

#if (LCD_DEEP == DEEP_RGB565)
    if (opacity >= 250U)
    {
        /* 不透明：直接读进 PFB 行，再原地把大端字节拼成像素（每个像素只覆盖它自己
         * 的 2 个源字节，与 CPU 字节序无关）。图宽正好等于窗口宽时各行在外挂与 PFB
         * 里都首尾相接，整片一次读完 */
        uint32_t n_px   = draw_w;
        uint16_t n_rows = draw_h;

        if (draw_w == img_w && draw_w == p_lcd->pfb_width)
        {
            n_px *= draw_h;
            n_rows = 1U;
        }
        for (y = 0U; y < n_rows; y++, row += p_lcd->pfb_width, addr += (uint32_t)img_w * 2U)
        {
            const uint8_t *b = (const uint8_t *)row;
            uint32_t i;

            we_gui_storage(p_lcd)(addr, (uint8_t *)row, n_px * 2U);
            for (i = 0U; i < n_px; i++)
                row[i].dat16 = (uint16_t)(((uint16_t)b[2U * i] << 8) | b[2U * i + 1U]);
        }
        return;
    }
#endif

    /* 半透明（或非 RGB565 色深）：分块读到栈上再逐像素混色，每块最多 WE_FLASH_IMG_CHUNK/2 个像素 */
    for (y = 0U; y < draw_h; y++, row += p_lcd->pfb_width, addr += (uint32_t)img_w * 2U)
    {
        uint8_t   chunk[WE_FLASH_IMG_CHUNK];
        uint32_t  a    = addr;
        colour_t *dst  = row;
        uint16_t  left = draw_w;

        while (left > 0U)
        {
            uint16_t n = (left < (uint16_t)(WE_FLASH_IMG_CHUNK / 2U)) ? left : (uint16_t)(WE_FLASH_IMG_CHUNK / 2U);
            uint16_t i;

            we_gui_storage(p_lcd)(a, chunk, (uint32_t)n * 2U);
            for (i = 0U; i < n; i++)
            {
                uint16_t raw = (uint16_t)(((uint16_t)chunk[2U * i] << 8) | chunk[2U * i + 1U]);

                we_store_blended_color(dst++, we_color_from_rgb565(raw), opacity);
            }
            a += (uint32_t)n * 2U;
            left = (uint16_t)(left - n);
        }
    }
}

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
/* --------------------------------------------------------------------------
 * 索引 QOI 外挂读取状态
 *
 * 解码经两个窗口读外挂：索引窗口缓存一段连续的行索引项，数据窗口缓存一段
 * 连续的 QOI 数据流。请求落在窗口已载区间内直接取，否则从请求地址起整块
 * 重装。窗口按 flash 绝对地址命中：外挂数据运行期只读，不同图片的地址区间
 * 互不重叠，按地址命中即可保证正确。V3 的非去重行按行序紧挨存放，相邻行的
 * 索引项与数据都会连续命中同一窗口，去重行回指近处时同样命中。
 *
 * 头（m16、调色盘条目数）在 init 时读进对象；调色盘按图片资源地址缓存一份。
 * 状态记下所属的外挂读取回调：多块屏接不同存储器时互不串用。
 * WE_FLASH_IMG_CACHE = 1：整套状态放静态 RAM，跨切片、跨帧保留（单槽）；
 * WE_FLASH_IMG_CACHE = 0：状态放栈上，只在单次绘制调用内有效。
 * -------------------------------------------------------------------------- */
typedef struct
{
    uint32_t addr; /* 窗口首字节的 flash 绝对地址 */
    uint16_t len;  /* 窗口有效字节数，0 = 空 */
} flash_win_t;

typedef struct
{
    we_storage_read_cb_t owner;    /* 读取状态所属的外挂读取回调 */
    uint32_t    pal_key;           /* 调色盘所属图片的资源头地址 */
    uint8_t     pal_valid;         /* 1 = 调色盘有效 */
    uint8_t     pal[64U * 3U];     /* 调色盘原始字节：RGB565 每项 2 字节，ARGB8565 每项 3 字节 */
    flash_win_t idx_win;           /* 行索引窗口 */
    flash_win_t dat_win;           /* 数据窗口 */
    uint8_t     idx_buf[WE_FLASH_IMG_IDX_WIN];
    uint8_t     dat_buf[WE_FLASH_IMG_CHUNK];
} flash_qoi_state_t;

#if (WE_FLASH_IMG_CACHE == 1)
static flash_qoi_state_t s_qoi_state;
#endif

/* 数据流读取器：逐字节经数据窗口读取 */
typedef struct
{
    we_storage_read_cb_t cb;
    flash_qoi_state_t   *st;
    uint32_t             pos; /* 下一个要读的字节的 flash 绝对地址 */
} flash_stream_t;

/**
 * @brief 作废全部读取状态（调色盘、两个窗口）。
 * @param st 读取状态。
 */
static void _flash_qoi_state_reset(flash_qoi_state_t *st)
{
    st->owner       = NULL;
    st->pal_valid   = 0U;
    st->idx_win.len = 0U;
    st->dat_win.len = 0U;
}

/**
 * @brief 从数据流读取下一个字节：落在数据窗口内直接取，否则从当前位置重装窗口。
 * @param s 数据流读取器。
 * @return 读到的字节。
 */
static uint8_t flash_stream_get(flash_stream_t *s)
{
    flash_qoi_state_t *st = s->st;
    uint32_t o = s->pos - st->dat_win.addr; /* pos 在窗口前方时回绕成大数，同样判为未命中 */

    if (o >= st->dat_win.len)
    {
        s->cb(s->pos, st->dat_buf, WE_FLASH_IMG_CHUNK);
        st->dat_win.addr = s->pos;
        st->dat_win.len  = WE_FLASH_IMG_CHUNK;
        o = 0U;
    }
    s->pos++;
    return st->dat_buf[o];
}

/**
 * @brief 取第 row 行在 QOI 数据流中的字节偏移（经行索引窗口）。
 * @param st 读取状态。
 * @param cb 外部存储读取回调。
 * @param idx_base 行索引表起点（flash 绝对地址）。
 * @param m16 u16 行索引表项数（其后各行用 u32 表项）。
 * @param row 行号。
 * @return 行数据相对 QOI 数据流起点的偏移。
 * @note 行号顺序递增，窗口一次重装即覆盖后续多行的索引项。
 */
static uint32_t _flash_qoi_row_off(flash_qoi_state_t *st, we_storage_read_cb_t cb,
                                   uint32_t idx_base, uint16_t m16, uint16_t row)
{
    uint32_t       ep;
    uint16_t       el;
    const uint8_t *e;

    if (row < m16)
    {
        ep = idx_base + ((uint32_t)row << 1);
        el = 2U;
    }
    else
    {
        ep = idx_base + ((uint32_t)m16 << 1) + ((uint32_t)(row - m16) << 2);
        el = 4U;
    }

    if ((st->idx_win.len == 0U) || (ep < st->idx_win.addr) ||
        ((ep + el) > (st->idx_win.addr + st->idx_win.len)))
    {
        cb(ep, st->idx_buf, WE_FLASH_IMG_IDX_WIN);
        st->idx_win.addr = ep;
        st->idx_win.len  = WE_FLASH_IMG_IDX_WIN;
    }

    e = st->idx_buf + (ep - st->idx_win.addr);
    if (el == 2U)
        return ((uint32_t)e[0] << 8) | e[1];
    return ((uint32_t)e[0] << 24) | ((uint32_t)e[1] << 16) | ((uint32_t)e[2] << 8) | e[3];
}

/* --------------------------------------------------------------------------
 * 索引 QOI Flash 渲染器（RGB565 / ARGB8565 共用）
 *
 * 与 we_img_render_indexed_qoi_* 逻辑相同（V3 行索引 + 行去重），差别仅在于
 * 字节经读取窗口来自外挂 flash，而非内存指针。每个可见行：经索引窗口取行偏移，
 * 定位数据流，解码一行，行内越过裁剪右缘立即停止。
 *
 * ARGB8565 与 RGB565 的差异：
 *   0x00..0x3F → 静态调色盘 op，条目 3 字节 [A][H][L]（含 Alpha）
 *   0xFF → 读 3 字节 [A][H][L]，更新 alpha + RGB565
 *   0xFE → 读 2 字节 [H][L]，alpha 不变
 *   输出时 alpha = 像素 alpha × opacity / 255（RGB565 恒为 opacity）
 * V3 行首 op 只会是调色盘 op 或 0xFF 原始全量，两者都不依赖上文，
 * 每行定位后即自包含重建完整状态（包括 alpha；调色盘全局有效）。
 * -------------------------------------------------------------------------- */

/**
 * @brief 渲染索引 QOI 格式 Flash 图片到当前 PFB 切片。
 * @param p_lcd 当前 LCD 上下文。
 * @param obj 图片控件实例。
 * @param opacity 图片整体透明度（容器级联后，> 5）。
 * @param px_bytes 每像素字节数：RGB565 = 2，ARGB8565 = 3（决定调色盘项长与 0xFF 的载荷）。
 */
static void _flash_render_indexed_qoi(we_lcd_t *p_lcd, we_flash_img_obj_t *obj,
                                      uint8_t opacity, uint8_t px_bytes)
{
#if (WE_FLASH_IMG_CACHE == 1)
    flash_qoi_state_t *st = &s_qoi_state;
#else
    flash_qoi_state_t  st_local;
    flash_qoi_state_t *st = &st_local;
#endif
    flash_stream_t  s;
    uint16_t        img_h      = (uint16_t)obj->base.h;
    uint16_t        m16        = obj->m16;
    uint8_t         pal_count  = obj->pal_count;
    uint16_t        ix_start   = (uint16_t)(p_lcd->clip.x0 - obj->base.x);
    uint16_t        clip_x_end = (uint16_t)(p_lcd->clip.x1 - obj->base.x + 1);
    uint16_t        cur_y      = (uint16_t)(p_lcd->clip.y0 - obj->base.y);
    uint16_t        clip_y_end = (uint16_t)(p_lcd->clip.y1 - obj->base.y + 1);
    colour_t       *row_dst    = p_lcd->pfb_gram - ix_start; /* 行内以图片列号寻址 */
    /* payload 布局：[14字节索引头][u16行索引表][u32行索引表][静态调色盘][QOI数据流] */
    uint32_t        idx_base   = obj->flash_addr + 6U + 14U;
    uint32_t        pal_addr   = idx_base + ((uint32_t)m16 << 1) + ((uint32_t)(img_h - m16) << 2);
    uint32_t        dat_base   = pal_addr + (uint32_t)pal_count * px_bytes;
    /* 资源头没有总长度字段：用"最坏 4 字节/像素 + 余量"的保守流长上界拦截损坏索引表 */
    uint32_t        stream_max = (((uint32_t)obj->base.w * img_h) << 2) + 16U;

    s.cb = we_gui_storage(p_lcd);
    s.st = st;
#if (WE_FLASH_IMG_CACHE == 0)
    _flash_qoi_state_reset(st);
#endif
    if (st->owner != s.cb)
    {
        _flash_qoi_state_reset(st);
        st->owner = s.cb;
    }
    if (pal_count > 0U && (st->pal_valid == 0U || st->pal_key != obj->flash_addr))
    {
        s.cb(pal_addr, st->pal, (uint32_t)pal_count * px_bytes);
        st->pal_key   = obj->flash_addr;
        st->pal_valid = 1U;
    }

    /* ---- 行循环：逐可见行经行索引空降解码（RUN 不跨行、行自包含） ---- */
    for (; cur_y < clip_y_end; cur_y++, row_dst += p_lcd->pfb_width)
    {
        uint32_t row_off     = _flash_qoi_row_off(st, s.cb, idx_base, m16, cur_y);
        uint16_t cur_x       = 0U;
        uint8_t  r = 0, g = 0, b = 0;
        uint16_t cur_pixel   = 0U;
        uint8_t  final_alpha = opacity; /* ARGB8565 行首 op 必然重建 alpha，这里只是缺省值 */

        if (row_off >= stream_max)
            return; /* 损坏索引表 */

        s.pos = dat_base + row_off;
        while ((cur_x < clip_x_end) && ((s.pos - dat_base) < stream_max))
        {
            uint8_t flag = flash_stream_get(&s);

            if (flag >= 0xFEU)
            {
                /* 0xFF / 0xFE：原始像素（ARGB8565 的 0xFF 先带 1 字节 alpha） */
                uint8_t h;
                uint8_t l;

                if (flag == 0xFFU && px_bytes == 3U)
                    final_alpha = we_div255((uint32_t)flash_stream_get(&s) * opacity);
                h = flash_stream_get(&s);
                l = flash_stream_get(&s);
                cur_pixel = ((uint16_t)h << 8) | l;
                r = h >> 3;
                g = (uint8_t)(((h & 0x07U) << 3) | (l >> 5));
                b = l & 0x1FU;
            }
            else if ((flag & 0xC0U) == 0x40U)
            {
                r = (uint8_t)((r + ((flag >> 4) & 0x03U) - 2U) & 0x1FU);
                g = (uint8_t)((g + ((flag >> 2) & 0x03U) - 2U) & 0x3FU);
                b = (uint8_t)((b + ( flag       & 0x03U) - 2U) & 0x1FU);
                cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
            }
            else if ((flag & 0xC0U) == 0x80U)
            {
                int8_t  vg = (int8_t)((flag & 0x3FU) - 32U);
                uint8_t nb = flash_stream_get(&s);
                r = (uint8_t)((r + vg - 8 + ((nb >> 4) & 0x0FU)) & 0x1FU);
                g = (uint8_t)((g + vg)                            & 0x3FU);
                b = (uint8_t)((b + vg - 8 + (nb & 0x0FU))        & 0x1FU);
                cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
            }
            else if (flag < 0x40U)
            {
                /* 0x00..0x3F：静态调色盘 op，查缓存的调色盘（ARGB8565 条目先带 alpha） */
                const uint8_t *e;

                if (flag >= pal_count)
                    return; /* 超出条目数视为损坏流 */
                e = &st->pal[(uint16_t)flag * px_bytes];
                if (px_bytes == 3U)
                    final_alpha = we_div255((uint32_t)(*e++) * opacity);
                cur_pixel = ((uint16_t)e[0] << 8) | e[1];
                r = e[0] >> 3;
                g = (uint8_t)(((e[0] & 0x07U) << 3) | (e[1] >> 5));
                b = e[1] & 0x1FU;
            }
            else /* 0xC0..0xFD：RUN（不跨行）：先跳过裁剪左缘以外的部分，再连续填到右缘 */
            {
                uint16_t end = (uint16_t)(cur_x + (flag & 0x3FU) + 1U);

                if (end > clip_x_end)
                    end = clip_x_end; /* 行内右缘截断：本行剩余像素不可见，填完即结束本行 */
                if (end > ix_start)
                {
                    colour_t fg = we_color_from_rgb565(cur_pixel);
                    uint16_t x  = (cur_x > ix_start) ? cur_x : ix_start;

                    for (; x < end; x++)
                        we_store_blended_color(row_dst + x, fg, final_alpha);
                }
                cur_x = end;
                continue;
            }

            if (cur_x >= ix_start)
                we_store_blended_color(row_dst + cur_x, we_color_from_rgb565(cur_pixel), final_alpha);
            cur_x++;
        }
    }
}
#endif /* WE_CFG_ENABLE_INDEXED_QOI */

/* --------------------------------------------------------------------------
 * 控件绘制回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 图片控件绘制回调：按资源格式选择对应 Flash 渲染路径。
 * @param ptr 回调透传的 we_flash_img_obj_t 指针。
 * @note 格式在 init 时校验过，只会是已支持的格式。
 */
static void _flash_img_draw_cb(void *ptr)
{
    we_flash_img_obj_t *obj = (we_flash_img_obj_t *)ptr;
    /* 容器透明度级联：入口算一次有效透明度，<= 5 时什么也画不出，连外挂读取也省掉 */
    uint8_t eff_op = we_opa_apply(obj->base.lcd, obj->opacity);

    if (eff_op <= 5U)
        return;

#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    if (obj->fmt != IMG_RGB565)
    {
        _flash_render_indexed_qoi(obj->base.lcd, obj, eff_op, (obj->fmt == IMG_ARGB8565_INDEXQOI) ? 3U : 2U);
        return;
    }
#endif
    _flash_render_rgb565(obj->base.lcd, obj, eff_op);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 初始化外部 Flash 图片控件并加入 LCD 对象链表。
 * @param obj 待初始化的图片控件实例。
 * @param lcd 当前 LCD 上下文，端口需提供 storage_read。
 * @param x 图片左上角 X 坐标。
 * @param y 图片左上角 Y 坐标。
 * @param flash_addr 图片资源头在外部 Flash 的地址。
 * @param opacity 图片整体透明度（0~255）。
 * @return 1 表示初始化成功，0 表示参数或资源头不合法。
 * @note 索引 QOI 的 14 字节索引头与资源头一次读出并在此校验（只支持 V3 流），
 *       绘制时不再重读。
 */
uint8_t we_flash_img_obj_init(we_flash_img_obj_t *obj, we_lcd_t *lcd,
                               int16_t x, int16_t y,
                               uint32_t flash_addr, uint8_t opacity)
{
    static const we_class_t _flash_img_class = {.draw_cb = _flash_img_draw_cb, .event_cb = NULL, .set_pos_cb = NULL};

    uint8_t  hdr[6U + 14U]; /* 资源头 [res_type][format][w_H][w_L][h_H][h_L] + 索引 QOI 索引头 */
    uint16_t img_w;
    uint16_t img_h;

    if (obj == NULL || lcd == NULL || we_gui_storage(lcd) == NULL)
    {
        return 0U;
    }

#if (WE_CFG_ENABLE_INDEXED_QOI == 1) && (WE_FLASH_IMG_CACHE == 1)
    /* 外挂内容可能已被改写：初始化任何外挂图时都清空读取缓存 */
    _flash_qoi_state_reset(&s_qoi_state);
#endif

    we_gui_storage(lcd)(flash_addr, hdr, sizeof(hdr));

    if (hdr[0] != (uint8_t)FILE_TYPE_IMG)
    {
        return 0U;
    }

    /* 仅接受已支持的格式 */
    if (hdr[1] != (uint8_t)IMG_RGB565
#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
        && hdr[1] != (uint8_t)IMG_RGB565_INDEXQOI
        && hdr[1] != (uint8_t)IMG_ARGB8565_INDEXQOI
#endif
       )
    {
        return 0U;
    }

    /* 宽或高为 0 视为损坏资源 / 错误的 flash 地址 */
    img_w = (uint16_t)(((uint16_t)hdr[2] << 8) | hdr[3]);
    img_h = (uint16_t)(((uint16_t)hdr[4] << 8) | hdr[5]);
    if (img_w == 0U || img_h == 0U)
    {
        return 0U;
    }

    obj->m16       = 0U;
    obj->pal_count = 0U;
#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
    if (hdr[1] != (uint8_t)IMG_RGB565)
    {
        const uint8_t *inner = &hdr[6];
        uint16_t m16 = (uint16_t)(((uint16_t)inner[5] << 8) | inner[6]);

        /* V3 索引头固定 14 字节，byte0=0x0F 兼作版本标识（V2 的 0x0E、V1 的
         * 0x0D 均不支持）；调色盘条目数上限 64，m16 不超过行数，否则视为损坏流。
         * 像素数上限保证解码的流长上界（4 字节/像素）不溢出 32 位 */
        if ((inner[0] != 0x0FU) || (inner[13] > 64U) || (m16 > img_h) ||
            ((uint32_t)img_w * img_h > ((0xFFFFFFFFUL - 16UL) >> 2)))
        {
            return 0U;
        }
        obj->m16       = m16;
        obj->pal_count = inner[13];
    }
#endif

    /* 填充 we_obj_t 基类字段；w/h 复用存储图片宽高 */
    obj->fmt           = hdr[1];
    obj->flash_addr    = flash_addr;
    obj->opacity       = opacity;
    obj->base.lcd      = lcd;
    obj->base.x        = x;
    obj->base.y        = y;
    obj->base.w        = (int16_t)img_w;
    obj->base.h        = (int16_t)img_h;
    obj->base.class_p  = &_flash_img_class;
    obj->base.next     = NULL;
    obj->base.parent   = NULL;

    /* 挂入 LCD 控件链表 */
    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);

    if (opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);

    return 1U;
}

/**
 * @brief 设置图片控件透明度并触发重绘。
 * @param obj 图片控件实例。
 * @param opacity 新透明度（0~255）。
 */
void we_flash_img_obj_set_opacity(we_flash_img_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;
    obj->opacity = opacity;
    we_obj_invalidate((we_obj_t *)obj);
}
