/*
        Copyright 2025 Lu Zhihao

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "we_gui_driver.h"
#include "image_res.h"
#include "we_render.h"

/* --------------------------------------------------------------------------
 * GUI 周期任务调度辅助
 *
 * 设计目标：
 * 1. 把容器动画这类周期逻辑从 we_gui_task_handler() 主体里拆出来；
 * 2. 后续如果继续增加光标闪烁、页面动画等逻辑，只需要注册任务回调；
 * 3. 使用固定数量数组，避免动态内存和复杂链表，适合低成本 MCU。
 * -------------------------------------------------------------------------- */
/**
 * @brief 执行一轮 GUI 内部任务回调分发
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param elapsed_ms 传入，本轮累计的已流逝时间，单位毫秒
 * @return 无
 * @note 仅分发已注册回调，不在此处做任务优先级或重入控制。
 */
static void _we_gui_run_tasks(we_lcd_t *p_lcd, uint16_t elapsed_ms)
{
    uint8_t i;

    if (p_lcd == NULL || elapsed_ms == 0U)
        return;

    for (i = 0; i < WE_CFG_GUI_TASK_MAX_NUM; i++)
    {
        if (p_lcd->task_list[i].cb != NULL)
        {
            p_lcd->task_list[i].cb(p_lcd, p_lcd->task_list[i].user_data, elapsed_ms);
        }
    }
}

/**
 * @brief 在 GUI 内部任务表中查找空闲槽位
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 成功返回空槽索引(0 ~ WE_CFG_GUI_TASK_MAX_NUM-1)，失败返回 -1
 */
static int8_t _we_gui_find_free_task_slot(we_lcd_t *p_lcd)
{
    uint8_t i;

    if (p_lcd == NULL)
        return -1;

    for (i = 0; i < WE_CFG_GUI_TASK_MAX_NUM; i++)
    {
        if (p_lcd->task_list[i].cb == NULL)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

/**
 * @brief 在 GUI 用户定时器表中查找空闲槽位
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 成功返回空槽索引(0 ~ WE_CFG_GUI_TIMER_MAX_NUM-1)，失败返回 -1
 */
static int8_t _we_gui_find_free_timer_slot(we_lcd_t *p_lcd)
{
    uint8_t i;

    if (p_lcd == NULL)
        return -1;

    for (i = 0; i < WE_CFG_GUI_TIMER_MAX_NUM; i++)
    {
        if (p_lcd->timer_list[i].cb == NULL)
        {
            return (int8_t)i;
        }
    }

    return -1;
}

/* --------------------------------------------------------------------------
 * PFB 双缓冲辅助
 *
 * 设计目标：
 * 1. 当 GRAM_DMA_BUFF_EN = 1 时，把用户传入的整块 GRAM 一分为二：
 *    - 一半给当前 CPU 绘制
 *    - 一半给底层 DMA/SPI 发送
 * 2. pfb_gram_base 始终保存整块缓冲的起始地址；
 * 3. pfb_gram 始终指向“当前正在被 GUI 绘制”的那一半。
 * -------------------------------------------------------------------------- */
/**
 * @brief 在 DMA 双缓冲模式下切换当前 PFB 工作区
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 仅在 GRAM_DMA_BUFF_EN == 1 时生效，非双缓冲模式下为空操作。
 */
static void _we_lcd_swap_pfb(we_lcd_t *p_lcd)
{
#if (GRAM_DMA_BUFF_EN == 1)
    /* pfb_gram 是 colour_t *，sizeof(colour_t) 随色深变化，
     * 指针步进天然覆盖 RGB565 和 RGB888，无需按色深分支。 */
    if (p_lcd->pfb_gram == p_lcd->pfb_gram_base)
    {
        p_lcd->pfb_gram = p_lcd->pfb_gram_base + p_lcd->pfb_size;
    }
    else
    {
        p_lcd->pfb_gram = p_lcd->pfb_gram_base;
    }
#else
    (void)p_lcd;
#endif
}

/* --------------------------------------------------------------------------
 * GUI 用户定时器运行器
 *
 * 设计目标：
 * 1. 对业务层提供“按周期触发”的时间接口，而不是暴露底层 task 队列；
 * 2. 支持单次定时器和周期定时器；
 * 3. 当主循环偶尔抖动时，允许按周期补偿回调次数，避免动画或逻辑明显变慢。
 * -------------------------------------------------------------------------- */
/**
 * @brief 推进并触发 GUI 用户定时器
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param elapsed_ms 传入，本轮累计的已流逝时间，单位毫秒
 * @return 无
 * @note 当 elapsed_ms 大于周期时会按周期补偿触发，避免时间驱动逻辑整体变慢。
 */
static void _we_gui_run_timers(we_lcd_t *p_lcd, uint16_t elapsed_ms)
{
    uint8_t i;

    if (p_lcd == NULL || elapsed_ms == 0U)
        return;

    for (i = 0; i < WE_CFG_GUI_TIMER_MAX_NUM; i++)
    {
        we_gui_timer_node_t *timer = &p_lcd->timer_list[i];
        uint32_t total_ms;

        if (timer->cb == NULL || timer->active == 0 || timer->period_ms == 0U)
            continue;

        total_ms = (uint32_t)timer->acc_ms + elapsed_ms;

        while (timer->cb != NULL && timer->active != 0 && timer->period_ms != 0U && total_ms >= timer->period_ms)
        {
            total_ms -= timer->period_ms;
            timer->cb(p_lcd, timer->period_ms);

            if (timer->cb == NULL || timer->active == 0)
                break;

            if (timer->repeat == 0U)
            {
                timer->active = 0U;
                total_ms = 0U;
                break;
            }
        }

        if (timer->cb != NULL && timer->active != 0U)
        {
            timer->acc_ms = (uint16_t)total_ms;
        }
    }
}

/* --------------------------------------------------------------------------
 * GUI 脏矩形刷新任务
 *
 * 设计目标：
 * 1. 把 we_gui_task_handler() 中“消费脏矩形并推屏”的逻辑单独收口；
 * 2. 保持主任务函数只负责调度顺序，降低主函数体积和阅读负担；
 * 3. 不把脏矩形刷新塞进任务表，避免打乱核心渲染阶段顺序。
 * -------------------------------------------------------------------------- */
/**
 * @brief 消费脏矩形并按块推送刷新
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
static void _we_gui_flush_dirty(we_lcd_t *p_lcd)
{
    uint8_t iter = 0;
    we_rect_t rect;

    if (p_lcd == NULL || p_lcd->dirty_mgr.count == 0)
    {
        return;
    }

    /* 遍历合并后的脏矩形，逐块推动底层刷新。 */
    while (we_dirty_get_next(&p_lcd->dirty_mgr, &rect, &iter))
    {
        uint16_t w = rect.x1 - rect.x0 + 1;
        uint16_t h = rect.y1 - rect.y0 + 1;

        we_push_pfb(p_lcd, rect.x0, rect.y0, w, h);
    }

    /* 只要这一轮真正消费了脏区并完成提交，就记作 1 帧渲染。 */
    p_lcd->stat_render_frames++;

    /* 所有脏区处理完成后，清空管理器，等待业务层下一轮标脏。 */
    we_dirty_clear(&p_lcd->dirty_mgr);
}

/**
 * @brief  在局部帧缓冲(PFB)中绘制一张原始 RGB565 图片
 * @param  p_lcd    当前 LCD 上下文
 * @param  x0       图片左上角屏幕坐标 X
 * @param  y0       图片左上角屏幕坐标 Y
 * @param  img      图片资源描述
 * @param  opacity  全局透明度，0 为完全透明，255 为完全不透明
 */
void we_img_render_rgb565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity)
{
    // 完全透明时直接返回，避免进入后续裁剪和逐像素循环。
    if (opacity == 0)
        return;

    /* ---------------- 1. 局部变量准备 ---------------- */
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);
    int16_t x1, y1;
    int16_t draw_x0, draw_y0;
    uint16_t ix_start, iy_start;
    uint16_t draw_width, draw_height;
    uint16_t src_stride, dst_stride;
    uint16_t x, y;

    uint8_t *src_line;
    colour_t *dst_line;
    uint8_t *p_src;
    colour_t *p_dst;

    /* ---------------- 2. 先计算图片完整包围盒，再做快速裁剪 ---------------- */
    x1 = x0 + img_w - 1;
    y1 = y0 + img_h - 1;

    // 如果整张图片与当前 PFB 切片没有交集，直接跳过。
    if ((x0 > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y0 > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
    {
        return;
    }

    /* ---------------- 3. 计算源图起始偏移和实际可绘制尺寸 ---------------- */
    // 当图片左侧或上侧超出当前 PFB 切片时，需要跳过对应数量的源像素。
    ix_start = (x0 < p_lcd->pfb_area.x0) ? (p_lcd->pfb_area.x0 - x0) : 0;
    iy_start = (y0 < p_lcd->pfb_y_start) ? (p_lcd->pfb_y_start - y0) : 0;

    // 实际绘制宽高 = 原始宽高 - 左上裁掉部分 - 右下超出部分。
    draw_width = img_w - ix_start - ((x1 > p_lcd->pfb_area.x1) ? (x1 - p_lcd->pfb_area.x1) : 0);
    draw_height = img_h - iy_start - ((y1 > p_lcd->pfb_y_end) ? (y1 - p_lcd->pfb_y_end) : 0);

    /* ---------------- 4. 预先计算源图/目标图起始指针与跨距 ---------------- */
    // RGB565 每像素 2 字节，因此源图偏移要乘 2。
    src_line = (uint8_t *)IMG_DAT_PIXELS(img) + (iy_start * img_w + ix_start) * 2;

    // 目标指针映射到当前 PFB 的局部坐标系。
    draw_x0 = x0 + ix_start;
    draw_y0 = y0 + iy_start;
    dst_line = p_lcd->pfb_gram + ((draw_y0 - p_lcd->pfb_y_start) * p_lcd->pfb_width) + (draw_x0 - p_lcd->pfb_area.x0);

    // 把每行跨距提前算好，避免在内层循环里重复乘法。
    src_stride = img_w * 2;        // 源图每行字节数
    dst_stride = p_lcd->pfb_width; // PFB 每行像素数

    /* ---------------- 5. 核心逐像素绘制循环 ----------------
     * opacity 在整次绘制内是常量，先在循环外分类，避免每像素重复判定：
     *   不透明 → 退化为带大小端交换的逐行拷贝（图标/背景最常见场景）
     *   半透明 → 走逐像素混色 */
    if (opacity >= 250U)
    {
        for (y = 0; y < draw_height; y++)
        {
            p_dst = dst_line;
            p_src = src_line;

            for (x = 0; x < draw_width; x++)
            {
                uint16_t cur_pixel = ((uint16_t)p_src[0] << 8) | p_src[1];
                we_store_color(p_dst, we_color_from_rgb565(cur_pixel));

                p_dst++;
                p_src += 2;
            }

            src_line += src_stride;
            dst_line += dst_stride;
        }
    }
    else
    {
        for (y = 0; y < draw_height; y++)
        {
            p_dst = dst_line;
            p_src = src_line;

            for (x = 0; x < draw_width; x++)
            {
                // 从源图按大端读取一个 RGB565 像素（byte[0]=高字节，byte[1]=低字节）。
                uint16_t cur_pixel = ((uint16_t)p_src[0] << 8) | p_src[1];
                colour_t fg = we_color_from_rgb565(cur_pixel);
                we_store_blended_color(p_dst, fg, opacity);

                p_dst++;
                p_src += 2; // 源图前进 1 个像素
            }

            // 一行绘制结束后，切到下一行起始地址。
            src_line += src_stride;
            dst_line += dst_stride;
        }
    }
}

/* --------------------------------------------------------------------------
 * 索引 QOI 绘制入口
 *
 * 当前工程只保留“索引 QOI”这一条图片解码路径。
 * 原始 QOI 已经在编译期开关里裁掉，目的有三点：
 * 1. 减少 Flash 占用
 * 2. 简化图片控件的格式分发逻辑
 * 3. 只保留当前实际会用到的解码能力
 * -------------------------------------------------------------------------- */
#if (WE_CFG_ENABLE_INDEXED_QOI == 1)
/**
 * @brief  在局部帧缓冲(PFB)中绘制索引 QOI 压缩的 RGB565 图片
 * @param  p_lcd    传入，当前 GUI 屏幕上下文指针
 * @param  x0       传入，图片左上角屏幕坐标 X
 * @param  y0       传入，图片左上角屏幕坐标 Y
 * @param  img      传入，索引 QOI 图片资源描述
 * @param  opacity  传入，全局透明度，0 为完全透明，255 为完全不透明
 * @return 无
 */
void we_img_render_indexed_qoi_rgb565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity)
{
    if (opacity == 0)
        return;

    /* ---------------- 1. 包围盒和裁剪参数准备 ---------------- */
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);
    int16_t x1 = x0 + img_w - 1;
    int16_t y1 = y0 + img_h - 1;

    // 如果整张图不在当前 PFB 切片内，直接返回。
    if ((x0 > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y0 > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
    {
        return;
    }

    uint16_t ix_start = (x0 < p_lcd->pfb_area.x0) ? (p_lcd->pfb_area.x0 - x0) : 0;
    uint16_t iy_start = (y0 < p_lcd->pfb_y_start) ? (p_lcd->pfb_y_start - y0) : 0;

    uint16_t draw_width = img_w - ix_start - ((x1 > p_lcd->pfb_area.x1) ? (x1 - p_lcd->pfb_area.x1) : 0);
    uint16_t draw_height = img_h - iy_start - ((y1 > p_lcd->pfb_y_end) ? (y1 - p_lcd->pfb_y_end) : 0);

    uint16_t clip_x_end = ix_start + draw_width;
    uint16_t clip_y_end = iy_start + draw_height;

    int16_t base_dest_x = x0 - p_lcd->pfb_area.x0;
    int16_t base_dest_y = y0 - p_lcd->pfb_y_start;
    uint16_t dst_stride = p_lcd->pfb_width;

    /* ---------------- 2. 解析索引 QOI 头部 (跳过6字节通用信息头) ---------------- */
    const uint8_t *dat = IMG_DAT_PIXELS(img);
    if (dat == 0)
        return;

    uint8_t head_size = dat[0];

    // 如需进一步做健壮性校验，可在这里检查头部长度。
    // if (head_size != 13) return;

    uint16_t interval = (dat[5] << 8) | dat[6];
    uint16_t u16_size = (dat[7] << 8) | dat[8];
    uint16_t u24_size = (dat[9] << 8) | dat[10];
    uint16_t u32_size = (dat[11] << 8) | dat[12];

    uint16_t num_u16 = u16_size / 2;
    uint16_t num_u24 = u24_size / 3;
    uint16_t num_u32 = u32_size / 4;
    uint32_t total_indices = num_u16 + num_u24 + num_u32;

    const uint8_t *idx_u16 = dat + head_size;
    const uint8_t *idx_u24 = idx_u16 + u16_size;
    const uint8_t *idx_u32 = idx_u24 + u24_size;
    const uint8_t *qoi_start = idx_u32 + u32_size;

    /* ---------------- 3. 根据索引表跳到最接近目标区域的解码起点 ---------------- */
    uint32_t first_needed_pixel = (uint32_t)iy_start * img_w + ix_start;
    uint32_t skip_intervals = (interval > 0) ? (first_needed_pixel / interval) : 0;

    uint32_t byte_offset = 0;

    if (skip_intervals > 0)
    {
        uint32_t target_idx = skip_intervals;

        // 索引越界保护，避免错误头部导致读取越界。
        if (target_idx >= total_indices)
        {
            target_idx = (total_indices > 0) ? (total_indices - 1) : 0;
            skip_intervals = target_idx;
        }

        if (target_idx < num_u16)
        {
            uint32_t i = target_idx * 2;
            byte_offset = (idx_u16[i] << 8) | idx_u16[i + 1];
        }
        else if (target_idx < num_u16 + num_u24)
        {
            uint32_t i = (target_idx - num_u16) * 3;
            byte_offset = (idx_u24[i] << 16) | (idx_u24[i + 1] << 8) | idx_u24[i + 2];
        }
        else
        {
            uint32_t i = (target_idx - num_u16 - num_u24) * 4;
            byte_offset = (idx_u32[i] << 24) | (idx_u32[i + 1] << 16) | (idx_u32[i + 2] << 8) | idx_u32[i + 3];
        }
    }

    uint32_t jump_pixel_idx = skip_intervals * interval;
    uint16_t cur_x = jump_pixel_idx % img_w;
    uint16_t cur_y = jump_pixel_idx / img_w;

    /* ---------------- 4. 初始化解码状态 ---------------- */
    const uint8_t *arry = qoi_start + byte_offset; // 跳到最近的字节偏移位置

    uint8_t flag;
    uint8_t r = 0, g = 0, b = 0;
    uint16_t cur_pixel = 0;

    /* opacity 整次绘制为常量，预先分类，省掉每像素混色分支 */
    uint8_t opaque = (uint8_t)(opacity >= 250U);
    /* 行指针缓存：同一行内复用，避免每像素重算 (cur_y*dst_stride) 乘法。
     * 仅在通过裁剪判定后才计算，保证偏移非负（不会出现越界回绕） */
    colour_t *row_dst = 0;
    int32_t row_dst_y = -1;

    /* 像素计数上限：图片总像素数。
     * 作为解码循环的安全终止条件，防止 byte_offset 损坏时越界读取。 */
    uint32_t max_pixels = (uint32_t)img_w * img_h;
    uint32_t decoded_pixels = jump_pixel_idx;

    /* ---------------- 5. 主解码循环 ---------------- */
    while (decoded_pixels < max_pixels)
    {
        flag = *arry++;

        if ((flag == 0xFF) || (flag == 0xFE))
        {
            uint8_t h = *arry++;
            uint8_t l = *arry++;
            cur_pixel = (h << 8) | l;
            r = h >> 3;
            g = ((h & 0x07) << 3) | (l >> 5);
            b = l & 0x1F;
        }
        else if ((flag & 0xC0) == 0x40)
        {
            r = (r + ((flag >> 4) & 0x03) - 2) & 0x1F;
            g = (g + ((flag >> 2) & 0x03) - 2) & 0x3F;
            b = (b + (flag & 0x03) - 2) & 0x1F;
            cur_pixel = (r << 11) | (g << 5) | b;
        }
        else if ((flag & 0xC0) == 0x80)
        {
            int8_t vg = (flag & 0x3F) - 32;
            uint8_t next_byte = *arry++;
            r = (r + vg - 8 + ((next_byte >> 4) & 0x0F)) & 0x1F;
            g = (g + vg) & 0x3F;
            b = (b + vg - 8 + (next_byte & 0x0F)) & 0x1F;
            cur_pixel = (r << 11) | (g << 5) | b;
        }
        else if ((flag & 0xC0) == 0xC0)
        {
            uint8_t run = (flag & 0x3F) + 1;

            colour_t fg = we_color_from_rgb565(cur_pixel);

            while (run--)
            {
                if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
                {
                    if (row_dst_y != (int32_t)cur_y)
                    {
                        row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                        row_dst_y = (int32_t)cur_y;
                    }
                    if (opaque)
                        we_store_color(row_dst + cur_x, fg);
                    else
                        we_store_blended_color(row_dst + cur_x, fg, opacity);
                }
                decoded_pixels++;
                cur_x++;
                if (cur_x >= img_w)
                {
                    cur_x = 0;
                    cur_y++;
                    if (cur_y >= clip_y_end)
                        return;
                }
            }
            continue;
        }

        if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
        {
            if (row_dst_y != (int32_t)cur_y)
            {
                row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                row_dst_y = (int32_t)cur_y;
            }
            colour_t fg = we_color_from_rgb565(cur_pixel);
            if (opaque)
                we_store_color(row_dst + cur_x, fg);
            else
                we_store_blended_color(row_dst + cur_x, fg, opacity);
        }
        decoded_pixels++;
        cur_x++;
        if (cur_x >= img_w)
        {
            cur_x = 0;
            cur_y++;
            if (cur_y >= clip_y_end)
                return;
        }
    }
}

/* --------------------------------------------------------------------------
 * ARGB8565 索引 QOI 绘制入口
 *
 * 与 RGB565 版本的差异：
 * 1. 像素格式为 24 位：[A(8)][R5G6B5_高(8)][R5G6B5_低(8)]
 * 2. 解码状态额外维护 cur_alpha（当前像素 alpha，0~255）
 * 3. 操作码扩展：
 *    - 0xFF：读 3 字节 [A][H][L]，更新 alpha + RGB565
 *    - 0xFE：读 2 字节 [H][L]，alpha 不变
 *    - 0x40 / 0x80：RGB delta，alpha 不变
 *    - 0xC0：RLE，混色时使用 cur_alpha × opacity / 255
 * -------------------------------------------------------------------------- */
/**
 * @brief  在局部帧缓冲(PFB)中绘制索引 QOI 压缩的 ARGB8565 图片
 * @param  p_lcd    传入，当前 GUI 屏幕上下文指针
 * @param  x0       传入，图片左上角屏幕坐标 X
 * @param  y0       传入，图片左上角屏幕坐标 Y
 * @param  img      传入，索引 QOI 图片资源描述
 * @param  opacity  传入，全局透明度，最终会与像素内 alpha 相乘
 * @return 无
 */
void we_img_render_indexed_qoi_argb8565(we_lcd_t *p_lcd, int16_t x0, int16_t y0, const uint8_t *img, uint8_t opacity)
{
    if (opacity == 0)
        return;

    /* ---------------- 1. 包围盒和裁剪参数准备 ---------------- */
    uint16_t img_w = IMG_DAT_WIDTH(img);
    uint16_t img_h = IMG_DAT_HEIGHT(img);
    int16_t x1 = x0 + img_w - 1;
    int16_t y1 = y0 + img_h - 1;

    if ((x0 > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y0 > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
        return;

    uint16_t ix_start = (x0 < p_lcd->pfb_area.x0) ? (p_lcd->pfb_area.x0 - x0) : 0;
    uint16_t iy_start = (y0 < p_lcd->pfb_y_start) ? (p_lcd->pfb_y_start - y0) : 0;

    uint16_t draw_width = img_w - ix_start - ((x1 > p_lcd->pfb_area.x1) ? (x1 - p_lcd->pfb_area.x1) : 0);
    uint16_t draw_height = img_h - iy_start - ((y1 > p_lcd->pfb_y_end) ? (y1 - p_lcd->pfb_y_end) : 0);

    uint16_t clip_x_end = ix_start + draw_width;
    uint16_t clip_y_end = iy_start + draw_height;

    int16_t base_dest_x = x0 - p_lcd->pfb_area.x0;
    int16_t base_dest_y = y0 - p_lcd->pfb_y_start;
    uint16_t dst_stride = p_lcd->pfb_width;

    /* ---------------- 2. 解析索引 QOI 头部 ---------------- */
    const uint8_t *dat = IMG_DAT_PIXELS(img);
    if (dat == 0)
        return;

    uint8_t head_size = dat[0];
    uint16_t interval = (dat[5] << 8) | dat[6];
    uint16_t u16_size = (dat[7] << 8) | dat[8];
    uint16_t u24_size = (dat[9] << 8) | dat[10];
    uint16_t u32_size = (dat[11] << 8) | dat[12];

    uint16_t num_u16 = u16_size / 2;
    uint16_t num_u24 = u24_size / 3;
    uint16_t num_u32 = u32_size / 4;
    uint32_t total_indices = num_u16 + num_u24 + num_u32;

    const uint8_t *idx_u16 = dat + head_size;
    const uint8_t *idx_u24 = idx_u16 + u16_size;
    const uint8_t *idx_u32 = idx_u24 + u24_size;
    const uint8_t *qoi_start = idx_u32 + u32_size;

    /* ---------------- 3. 根据索引表跳到解码起点 ---------------- */
    uint32_t first_needed_pixel = (uint32_t)iy_start * img_w + ix_start;
    uint32_t skip_intervals = (interval > 0) ? (first_needed_pixel / interval) : 0;
    uint32_t byte_offset = 0;

    if (skip_intervals > 0)
    {
        uint32_t target_idx = skip_intervals;
        if (target_idx >= total_indices)
        {
            target_idx = (total_indices > 0) ? (total_indices - 1) : 0;
            skip_intervals = target_idx;
        }

        if (target_idx < num_u16)
        {
            uint32_t i = target_idx * 2;
            byte_offset = (idx_u16[i] << 8) | idx_u16[i + 1];
        }
        else if (target_idx < num_u16 + num_u24)
        {
            uint32_t i = (target_idx - num_u16) * 3;
            byte_offset = (idx_u24[i] << 16) | (idx_u24[i + 1] << 8) | idx_u24[i + 2];
        }
        else
        {
            uint32_t i = (target_idx - num_u16 - num_u24) * 4;
            byte_offset = (idx_u32[i] << 24) | (idx_u32[i + 1] << 16) | (idx_u32[i + 2] << 8) | idx_u32[i + 3];
        }
    }

    uint32_t jump_pixel_idx = skip_intervals * interval;
    uint16_t cur_x = jump_pixel_idx % img_w;
    uint16_t cur_y = jump_pixel_idx / img_w;

    /* ---------------- 4. 初始化解码状态 ---------------- */
    const uint8_t *arry = qoi_start + byte_offset;

    uint8_t flag;
    uint8_t r = 0, g = 0, b = 0;
    uint8_t cur_alpha = 255; // ARGB8565 额外维护当前像素 alpha
    uint16_t cur_pixel = 0;

    /* final_alpha = cur_alpha*opacity/255，只在 cur_alpha 变化时重算，
     * 用 we_div255 近似去掉每像素一次软件除法（M0 ~90+ cycle） */
    uint8_t final_alpha = we_div255((uint32_t)cur_alpha * opacity);
    /* 行指针缓存：同一行内复用，避免每像素重算 (cur_y*dst_stride) 乘法。
     * 仅在通过裁剪判定后才计算，保证偏移非负（不会出现越界回绕） */
    colour_t *row_dst = 0;
    int32_t row_dst_y = -1;

    uint32_t max_pixels = (uint32_t)img_w * img_h;
    uint32_t decoded_pixels = jump_pixel_idx;

    /* ---------------- 5. 主解码循环 ---------------- */
    while (decoded_pixels < max_pixels)
    {
        flag = *arry++;

        if (flag == 0xFF)
        {
            /* 新像素：alpha + RGB565（3 字节） */
            cur_alpha = *arry++;
            final_alpha = we_div255((uint32_t)cur_alpha * opacity);
            uint8_t h = *arry++;
            uint8_t l = *arry++;
            cur_pixel = ((uint16_t)h << 8) | l;
            r = h >> 3;
            g = ((h & 0x07) << 3) | (l >> 5);
            b = l & 0x1F;
        }
        else if (flag == 0xFE)
        {
            /* 新 RGB565，alpha 不变（2 字节） */
            uint8_t h = *arry++;
            uint8_t l = *arry++;
            cur_pixel = ((uint16_t)h << 8) | l;
            r = h >> 3;
            g = ((h & 0x07) << 3) | (l >> 5);
            b = l & 0x1F;
        }
        else if ((flag & 0xC0) == 0x40)
        {
            /* RGB 小差值，alpha 不变 */
            r = (r + ((flag >> 4) & 0x03) - 2) & 0x1F;
            g = (g + ((flag >> 2) & 0x03) - 2) & 0x3F;
            b = (b + (flag & 0x03) - 2) & 0x1F;
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0) == 0x80)
        {
            /* RGB luma 差值，alpha 不变 */
            int8_t vg = (int8_t)(flag & 0x3F) - 32;
            uint8_t next_byte = *arry++;
            r = (r + vg - 8 + ((next_byte >> 4) & 0x0F)) & 0x1F;
            g = (g + vg) & 0x3F;
            b = (b + vg - 8 + (next_byte & 0x0F)) & 0x1F;
            cur_pixel = ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
        }
        else if ((flag & 0xC0) == 0xC0)
        {
            /* RLE：重复当前 ARGB 像素，final_alpha 已在变 alpha 时算好 */
            uint8_t run = (flag & 0x3F) + 1;
            colour_t fg = we_color_from_rgb565(cur_pixel);

            while (run--)
            {
                if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
                {
                    if (row_dst_y != (int32_t)cur_y)
                    {
                        row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                        row_dst_y = (int32_t)cur_y;
                    }
                    we_store_blended_color(row_dst + cur_x, fg, final_alpha);
                }
                decoded_pixels++;
                cur_x++;
                if (cur_x >= img_w)
                {
                    cur_x = 0;
                    cur_y++;
                    if (cur_y >= clip_y_end)
                        return;
                }
            }
            continue;
        }

        /* 单像素输出，final_alpha 已在变 alpha 时算好 */
        if (cur_y >= iy_start && cur_x >= ix_start && cur_x < clip_x_end)
        {
            if (row_dst_y != (int32_t)cur_y)
            {
                row_dst = p_lcd->pfb_gram + ((base_dest_y + cur_y) * dst_stride) + base_dest_x;
                row_dst_y = (int32_t)cur_y;
            }
            colour_t fg = we_color_from_rgb565(cur_pixel);
            we_store_blended_color(row_dst + cur_x, fg, final_alpha);
        }
        decoded_pixels++;
        cur_x++;
        if (cur_x >= img_w)
        {
            cur_x = 0;
            cur_y++;
            if (cur_y >= clip_y_end)
                return;
        }
    }
}
#endif

/**
 * @brief 在局部帧缓冲(PFB)中绘制实心矩形
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param x 传入，矩形左上角 X 坐标
 * @param y 传入，矩形左上角 Y 坐标
 * @param w 传入，矩形宽度
 * @param h 传入，矩形高度
 * @param color 传入，填充颜色
 * @param opacity 传入，全局透明度(0~255)
 * @return 无
 */
void we_fill_rect(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h, colour_t color, uint8_t opacity)
{
    int16_t x1, y1;
    int16_t draw_x_start, draw_y_start, draw_x_end, draw_y_end;
    uint16_t span;
    uint16_t dst_stride;
    colour_t *dst_line;
    int16_t py;

    if (w == 0 || h == 0 || opacity == 0)
        return;

    x1 = x + w - 1;
    y1 = y + h - 1;

    if ((x > p_lcd->pfb_area.x1) || (x1 < p_lcd->pfb_area.x0) || (y > p_lcd->pfb_y_end) || (y1 < p_lcd->pfb_y_start))
        return;

    draw_x_start = (x < p_lcd->pfb_area.x0) ? p_lcd->pfb_area.x0 : x;
    draw_y_start = (y < p_lcd->pfb_y_start) ? p_lcd->pfb_y_start : y;
    draw_x_end = (x1 > p_lcd->pfb_area.x1) ? p_lcd->pfb_area.x1 : x1;
    draw_y_end = (y1 > p_lcd->pfb_y_end) ? p_lcd->pfb_y_end : y1;

    span = (uint16_t)(draw_x_end - draw_x_start + 1);
    dst_stride = p_lcd->pfb_width;
    dst_line =
        p_lcd->pfb_gram + ((draw_y_start - p_lcd->pfb_y_start) * dst_stride) + (draw_x_start - p_lcd->pfb_area.x0);

    if (opacity >= 250U)
    {
#if (LCD_DEEP == DEEP_RGB565)
        uint16_t color_val = color.dat16;

        for (py = draw_y_start; py <= draw_y_end; py++)
        {
            colour_t *p_dst = dst_line;
            uint16_t blocks = span / 8U;
            uint16_t rem = span % 8U;

            while (blocks--)
            {
                p_dst[0].dat16 = color_val;
                p_dst[1].dat16 = color_val;
                p_dst[2].dat16 = color_val;
                p_dst[3].dat16 = color_val;
                p_dst[4].dat16 = color_val;
                p_dst[5].dat16 = color_val;
                p_dst[6].dat16 = color_val;
                p_dst[7].dat16 = color_val;
                p_dst += 8;
            }

            while (rem--)
            {
                p_dst->dat16 = color_val;
                p_dst++;
            }

            dst_line += dst_stride;
        }
#elif (LCD_DEEP == DEEP_RGB888)
        uint8_t r8 = color.rgb.r;
        uint8_t g8 = color.rgb.g;
        uint8_t b8 = color.rgb.b;

        for (py = draw_y_start; py <= draw_y_end; py++)
        {
            colour_t *p_dst = dst_line;
            uint16_t blocks = span / 4U;
            uint16_t rem = span % 4U;

            while (blocks--)
            {
                p_dst[0].rgb.r = r8;
                p_dst[0].rgb.g = g8;
                p_dst[0].rgb.b = b8;
                p_dst[1].rgb.r = r8;
                p_dst[1].rgb.g = g8;
                p_dst[1].rgb.b = b8;
                p_dst[2].rgb.r = r8;
                p_dst[2].rgb.g = g8;
                p_dst[2].rgb.b = b8;
                p_dst[3].rgb.r = r8;
                p_dst[3].rgb.g = g8;
                p_dst[3].rgb.b = b8;
                p_dst += 4;
            }

            while (rem--)
            {
                p_dst->rgb.r = r8;
                p_dst->rgb.g = g8;
                p_dst->rgb.b = b8;
                p_dst++;
            }

            dst_line += dst_stride;
        }
#else
        for (py = draw_y_start; py <= draw_y_end; py++)
        {
            colour_t *p_dst = dst_line;
            uint16_t count = span;

            while (count--)
            {
                we_store_color(p_dst, color);
                p_dst++;
            }

            dst_line += dst_stride;
        }
#endif
        return;
    }

    for (py = draw_y_start; py <= draw_y_end; py++)
    {
        colour_t *p_dst = dst_line;
        uint16_t count = span;

        while (count--)
        {
            we_store_blended_color(p_dst, color, opacity);
            p_dst++;
        }

        dst_line += dst_stride;
    }
}

/**
 * @brief 计算轴对齐 90 度四分之一圆在单个像素上的 4x4 coverage
 * @param x 传入，外接正方形左上角 X 坐标
 * @param y 传入，外接正方形左上角 Y 坐标
 * @param radius 传入，圆角半径
 * @param quadrant 传入，象限标识，取值见 WE_MASK_QUADRANT_xx
 * @param px 传入，目标像素 X 坐标
 * @param py 传入，目标像素 Y 坐标
 * @return 返回 0~16 coverage（16 表示满覆盖）
 */
static uint8_t _we_mask_quarter_circle_cov16(int16_t x, int16_t y, uint16_t radius,
                                             uint8_t quadrant, int16_t px, int16_t py)
{
    int32_t cx16;
    int32_t cy16;
    int32_t r16;
    int32_t r16_sq;
    int32_t near_dx;
    int32_t near_dy;
    int32_t far_dx;
    int32_t far_dy;
    int32_t near_d2;
    int32_t far_d2;
    uint8_t coverage = 0U;
    uint8_t syi;
    uint8_t sxi;
    static const uint8_t sample_ofs[4] = { 2U, 6U, 10U, 14U };

    if (radius == 0U)
        return 0U;

    switch (quadrant)
    {
    case WE_MASK_QUADRANT_LT:
        cx16 = (int32_t)(x + radius) * 16;
        cy16 = (int32_t)(y + radius) * 16;
        near_dx = (int32_t)(px + 1 - (x + radius)) * 16;
        near_dy = (int32_t)(py + 1 - (y + radius)) * 16;
        far_dx = (int32_t)(px - (x + radius)) * 16;
        far_dy = (int32_t)(py - (y + radius)) * 16;
        break;
    case WE_MASK_QUADRANT_RT:
        cx16 = (int32_t)x * 16;
        cy16 = (int32_t)(y + radius) * 16;
        near_dx = (int32_t)(px - x) * 16;
        near_dy = (int32_t)(py + 1 - (y + radius)) * 16;
        far_dx = (int32_t)(px + 1 - x) * 16;
        far_dy = (int32_t)(py - (y + radius)) * 16;
        break;
    case WE_MASK_QUADRANT_LB:
        cx16 = (int32_t)(x + radius) * 16;
        cy16 = (int32_t)y * 16;
        near_dx = (int32_t)(px + 1 - (x + radius)) * 16;
        near_dy = (int32_t)(py - y) * 16;
        far_dx = (int32_t)(px - (x + radius)) * 16;
        far_dy = (int32_t)(py + 1 - y) * 16;
        break;
    case WE_MASK_QUADRANT_RB:
    default:
        cx16 = (int32_t)x * 16;
        cy16 = (int32_t)y * 16;
        near_dx = (int32_t)(px - x) * 16;
        near_dy = (int32_t)(py - y) * 16;
        far_dx = (int32_t)(px + 1 - x) * 16;
        far_dy = (int32_t)(py + 1 - y) * 16;
        break;
    }

    r16 = (int32_t)radius * 16;
    r16_sq = r16 * r16;
    near_d2 = near_dx * near_dx + near_dy * near_dy;
    if (near_d2 >= r16_sq)
        return 0U;

    far_d2 = far_dx * far_dx + far_dy * far_dy;
    if (far_d2 <= r16_sq)
        return 16U;

    for (syi = 0U; syi < 4U; syi++)
    {
        int32_t sy = (int32_t)py * 16 + sample_ofs[syi];
        int32_t dy = sy - cy16;
        int32_t dy2 = dy * dy;

        for (sxi = 0U; sxi < 4U; sxi++)
        {
            int32_t sx = (int32_t)px * 16 + sample_ofs[sxi];
            int32_t dx = sx - cx16;
            int32_t d2 = dx * dx + dy2;
            if (d2 <= r16_sq)
                coverage++;
        }
    }

    return coverage;
}

/**
 * @brief 计算轴对齐 90 度四分之一圆在单个像素上的 alpha mask
 * @param x 传入，外接正方形左上角 X 坐标
 * @param y 传入，外接正方形左上角 Y 坐标
 * @param radius 传入，圆角半径
 * @param quadrant 传入，象限标识，取值见 WE_MASK_QUADRANT_xx
 * @param px 传入，目标像素 X 坐标
 * @param py 传入，目标像素 Y 坐标
 * @return 返回 0~255 alpha（255 表示满覆盖）
 */
uint8_t we_mask_quarter_circle_alpha(int16_t x, int16_t y, uint16_t radius,
                                     uint8_t quadrant, int16_t px, int16_t py)
{
    uint8_t cov16 = _we_mask_quarter_circle_cov16(x, y, radius, quadrant, px, py);

    if (cov16 == 0U)
        return 0U;
    if (cov16 >= 16U)
        return 255U;
    return (uint8_t)(((uint32_t)cov16 * 255U + 8U) >> 4);
}

uint8_t we_mask_round_rect_alpha(int16_t x, int16_t y, uint16_t w, uint16_t h,
                                 uint16_t radius, int16_t px, int16_t py)
{
    int16_t x1;
    int16_t y1;
    uint16_t r;

    if (w == 0U || h == 0U)
        return 0U;

    x1 = (int16_t)(x + (int16_t)w - 1);
    y1 = (int16_t)(y + (int16_t)h - 1);
    if (px < x || px > x1 || py < y || py > y1)
        return 0U;

    r = radius;
    if (r > w / 2U)
        r = (uint16_t)(w / 2U);
    if (r > h / 2U)
        r = (uint16_t)(h / 2U);

    if (r == 0U)
        return 255U;

    if ((px >= x + (int16_t)r && px <= x1 - (int16_t)r) ||
        (py >= y + (int16_t)r && py <= y1 - (int16_t)r))
        return 255U;

    if (px < x + (int16_t)r)
    {
        if (py < y + (int16_t)r)
            return we_mask_quarter_circle_alpha(x, y, r, WE_MASK_QUADRANT_LT, px, py);
        return we_mask_quarter_circle_alpha(x, y1 - (int16_t)r + 1, r, WE_MASK_QUADRANT_LB, px, py);
    }

    if (py < y + (int16_t)r)
        return we_mask_quarter_circle_alpha(x1 - (int16_t)r + 1, y, r, WE_MASK_QUADRANT_RT, px, py);

    return we_mask_quarter_circle_alpha(x1 - (int16_t)r + 1, y1 - (int16_t)r + 1, r, WE_MASK_QUADRANT_RB, px, py);
}

/**
 * @brief 绘制抗锯齿实心圆角矩形
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param x 传入，矩形左上角 X 坐标
 * @param y 传入，矩形左上角 Y 坐标
 * @param w 传入，矩形宽度
 * @param h 传入，矩形高度
 * @param radius 传入，圆角半径
 * @param color 传入，填充颜色
 * @param opacity 传入，全局透明度(0~255)
 * @return 无
 */
void we_draw_round_rect_analytic_fill(we_lcd_t *p_lcd, int16_t x, int16_t y,
                                      uint16_t w, uint16_t h, uint16_t radius,
                                      colour_t color, uint8_t opacity)
{
    uint16_t r;
    int16_t draw_x0;
    int16_t draw_y0;
    int16_t draw_x1;
    int16_t draw_y1;
    int16_t px;
    int16_t py;
    uint16_t stride;
    colour_t *row;

    if (p_lcd == NULL || w == 0U || h == 0U || opacity == 0U)
        return;

    r = radius;
    if (r > w / 2U)
        r = (uint16_t)(w / 2U);
    if (r > h / 2U)
        r = (uint16_t)(h / 2U);

    if (r == 0U)
    {
        we_fill_rect(p_lcd, x, y, w, h, color, opacity);
        return;
    }

    draw_x0 = x;
    draw_y0 = y;
    draw_x1 = (int16_t)(x + (int16_t)w - 1);
    draw_y1 = (int16_t)(y + (int16_t)h - 1);

    if (draw_x0 < (int16_t)p_lcd->pfb_area.x0) draw_x0 = (int16_t)p_lcd->pfb_area.x0;
    if (draw_y0 < (int16_t)p_lcd->pfb_y_start) draw_y0 = (int16_t)p_lcd->pfb_y_start;
    if (draw_x1 > (int16_t)p_lcd->pfb_area.x1) draw_x1 = (int16_t)p_lcd->pfb_area.x1;
    if (draw_y1 > (int16_t)p_lcd->pfb_y_end) draw_y1 = (int16_t)p_lcd->pfb_y_end;
    if (draw_x0 > draw_x1 || draw_y0 > draw_y1)
        return;

    stride = p_lcd->pfb_width;
    row = p_lcd->pfb_gram
        + (uint32_t)(draw_y0 - (int16_t)p_lcd->pfb_y_start) * stride
        + (uint32_t)(draw_x0 - (int16_t)p_lcd->pfb_area.x0);

    for (py = draw_y0; py <= draw_y1; py++, row += stride)
    {
        colour_t *p = row;

        for (px = draw_x0; px <= draw_x1; px++, p++)
        {
            uint8_t mask_alpha = we_mask_round_rect_alpha(x, y, w, h, r, px, py);
            if (mask_alpha == 0U)
                continue;
            if (mask_alpha >= 250U && opacity >= 250U)
            {
                we_store_color(p, color);
            }
            else
            {
                /* 用 we_div255 近似替换软件除法（M0 上每次 /255 约 90+ cycle）。
                 * 输入上限 255*255 = 65025，we_div255 结果天然 ≤255，无需再 clamp。 */
                uint8_t alpha = we_div255((uint32_t)opacity * mask_alpha);
                we_store_blended_color(p, color, alpha);
            }
        }
    }
}

/**
 * @brief 在 PFB 缓冲区内写入单个像素
 * @param p_lcd 传入：GUI 屏幕上下文指针
 * @param px 传入：像素 X 坐标 (屏幕绝对坐标)
 * @param py 传入：像素 Y 坐标
 * @param color 传入：像素颜色
 * @param opacity 传入：透明度 (0~255)
 */
void we_draw_pixel(we_lcd_t *p_lcd, int16_t px, int16_t py, colour_t color, uint8_t opacity)
{
    colour_t *dst;

    if (px < p_lcd->pfb_area.x0 || px > p_lcd->pfb_area.x1 || py < p_lcd->pfb_y_start || py > p_lcd->pfb_y_end)
        return;

    dst = p_lcd->pfb_gram + (py - p_lcd->pfb_y_start) * p_lcd->pfb_width + (px - p_lcd->pfb_area.x0);

    we_store_blended_color(dst, color, opacity);
}

/**
 * @brief 绘制线段（Xiaolin Wu 整数 Q8 抗锯齿，统一处理所有线宽）
 *
 * thickness == 1：标准 Wu，每步 2 像素（上下边缘各按小数分配 alpha）
 * thickness  > 1：Wu + 实心核，每步 T 个实心像素 + 2 个 AA 边缘像素
 *   lo = -(T-1)/2，hi = T/2；实心范围 [yi+lo, yi+hi]（恰好 T 个像素）
 *   AA 上边 yi+lo-1，AA 下边 yi+hi+1
 *
 * @param p_lcd     传入：GUI 屏幕上下文指针
 * @param x0        传入：起点 X
 * @param y0        传入：起点 Y
 * @param x1        传入：终点 X
 * @param y1        传入：终点 Y
 * @param thickness 传入：线宽 (像素)
 * @param color     传入：线段颜色
 * @param opacity   传入：透明度 (0~255)
 */
void we_draw_line(we_lcd_t *p_lcd, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, colour_t color,
                  uint8_t opacity)
{
    int16_t dx, dy, adx, ady, tmp, grad, x;
    int32_t y_fp;
    uint8_t steep;
    int16_t lo, hi;

    if (opacity == 0)
        return;

    dx = x1 - x0;
    dy = y1 - y0;
    adx = (dx < 0) ? -dx : dx;
    ady = (dy < 0) ? -dy : dy;

    /* 陡线交换 xy，保证沿主轴（较长轴）迭代 */
    steep = (ady > adx);
    if (steep)
    {
        tmp = x0;
        x0 = y0;
        y0 = tmp;
        tmp = x1;
        x1 = y1;
        y1 = tmp;
    }

    /* 保证从左到右 */
    if (x0 > x1)
    {
        tmp = x0;
        x0 = x1;
        x1 = tmp;
        tmp = y0;
        y0 = y1;
        y1 = tmp;
    }

    dx = x1 - x0;
    dy = y1 - y0;
    grad = (dx > 0) ? (int16_t)((int32_t)dy * 256 / dx) : 0;
    y_fp = (int32_t)y0 * 256;

    /* T=1：lo=0,hi=0 → AA 上边 yi，AA 下边 yi+1，实心区间为空（标准 Wu）
     * T>1：实心 [yi+lo, yi+hi] 共 T 像素，两侧各 1 个 AA 像素              */
    lo = -(int16_t)((thickness - 1U) / 2U);
    hi = (int16_t)(thickness / 2U);

    for (x = x0; x <= x1; x++)
    {
        int16_t yi = (int16_t)(y_fp >> 8);
        uint8_t frac = (uint8_t)(y_fp & 0xFFU);
        uint8_t a1 = (uint8_t)(((uint16_t)(255U - frac) * opacity) >> 8);
        uint8_t a2 = (uint8_t)(((uint16_t)frac * opacity) >> 8);
        int16_t t;

        if (steep)
        {
            we_draw_pixel(p_lcd, (int16_t)(yi + lo - 1), x, color, a1);
            for (t = lo; t <= hi; t++)
                we_draw_pixel(p_lcd, (int16_t)(yi + t), x, color, opacity);
            we_draw_pixel(p_lcd, (int16_t)(yi + hi + 1), x, color, a2);
        }
        else
        {
            we_draw_pixel(p_lcd, x, (int16_t)(yi + lo - 1), color, a1);
            for (t = lo; t <= hi; t++)
                we_draw_pixel(p_lcd, x, (int16_t)(yi + t), color, opacity);
            we_draw_pixel(p_lcd, x, (int16_t)(yi + hi + 1), color, a2);
        }

        y_fp += grad;
    }
}

#include <stdbool.h>
#include <stdint.h>

/* =========================================================================
 * 预计算 0~128 步(对应 0°~90°)正弦表，Q15 格式，1.0 = 32767
 * 系统统一使用 512 步/圈，四分之一圆 = 128 步
 * Flash 占用：129 * 2 = 258 Bytes
 * 优势：归一化只需 & 0x1FF，无除法；象限边界 128/256/384 均为 2 的幂次倍数
 * ========================================================================= */
static const int16_t we_sin_table_128[129] = {
    0,     402,   804,   1206,  1608,  2009,  2410,  2811,  3212,  3612,  4011,  4410,  4808,  5205,  5602,
    5997,  6393,  6787,  7180,  7572,  7963,  8352,  8742,  9130,  9515,  9899,  10280, 10659, 11038, 11415,
    11790, 12163, 12539, 12910, 13279, 13645, 14010, 14372, 14732, 15090, 15446, 15800, 16151, 16499, 16846,
    17189, 17530, 17869, 18204, 18537, 18868, 19195, 19519, 19841, 20159, 20475, 20787, 21096, 21403, 21705,
    22005, 22301, 22594, 22884, 23170, 23452, 23731, 24007, 24279, 24547, 24811, 25072, 25329, 25582, 25832,
    26077, 26319, 26556, 26790, 27019, 27245, 27466, 27683, 27896, 28105, 28310, 28510, 28706, 28898, 29085,
    29268, 29447, 29621, 29791, 29956, 30117, 30273, 30424, 30571, 30714, 30852, 30985, 31113, 31237, 31356,
    31470, 31580, 31685, 31785, 31880, 31971, 32057, 32137, 32213, 32285, 32351, 32412, 32469, 32521, 32567,
    32609, 32646, 32678, 32705, 32728, 32745, 32757, 32765, 32767};

/**
 * @brief  快速整数正弦函数，返回 Q15 格式结果
 * @param  angle 512 步制角度值，支持负数和超出 512 的输入
 * @retval 放大 32767 倍后的正弦值
 */
int16_t we_sin(int16_t angle)
{
    // 1. 归一化到 0~511，位与运算替代取模，零除法开销。
    int16_t norm_angle = angle & 0x1FF;
    if (norm_angle < 0)
    {
        norm_angle += 512;
    }

    // 2. 利用四象限对称性，只查 0~128 步的表即可。
    if (norm_angle <= 128)
    {
        return we_sin_table_128[norm_angle];
    }
    else if (norm_angle <= 256)
    {
        return we_sin_table_128[256 - norm_angle];
    }
    else if (norm_angle <= 384)
    {
        return -we_sin_table_128[norm_angle - 256];
    }
    else
    {
        return -we_sin_table_128[512 - norm_angle];
    }
}

/**
 * @brief  快速整数余弦函数，返回 Q15 格式结果
 * @param  angle 512 步制角度值
 * @retval 放大 32767 倍后的余弦值
 */
int16_t we_cos(int16_t angle)
{
    // 利用公式：cos(a) = sin(a + 90°)，90° 在 512 步制中 = WE_ANGLE(90.0f) = 128
    return we_sin(angle + 128);
}
/**
 * @brief  在当前 PFB 中绘制通用 Alpha 位图/蒙版，支持 A1/A2/A4/A8
 * @param  p_lcd     当前 LCD/PFB 上下文
 * @param  x, y      位图左上角屏幕坐标
 * @param  w, h      位图实际宽高
 * @param  src_data  位图像素数据
 * @param  bpp       位深，支持 1/2/4/8
 * @param  fg_color  前景色
 * @param  opacity   全局透明度
 */
void we_draw_alpha_mask(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h, const unsigned char *src_data,
                        uint8_t bpp, colour_t fg_color, uint8_t opacity)
{
    if (w == 0 || h == 0 || src_data == NULL || opacity == 0)
        return;

    // 1. 先算出位图完整物理边界。
    int16_t draw_x_end = x + w - 1;
    int16_t draw_y_end = y + h - 1;

    // 2. 与当前 PFB 切片求交，得到真正要处理的区域。
    int16_t clip_x_start = (x < p_lcd->pfb_area.x0) ? p_lcd->pfb_area.x0 : x;
    int16_t clip_y_start = (y < p_lcd->pfb_y_start) ? p_lcd->pfb_y_start : y;
    int16_t clip_x_end = (draw_x_end > p_lcd->pfb_area.x1) ? p_lcd->pfb_area.x1 : draw_x_end;
    int16_t clip_y_end = (draw_y_end > p_lcd->pfb_y_end) ? p_lcd->pfb_y_end : draw_y_end;

    // 如果与当前 PFB 没有交集，直接返回。
    if (clip_x_start > clip_x_end || clip_y_start > clip_y_end)
        return;

    uint8_t alpha_mask = (1 << bpp) - 1;

    colour_t *gram = (colour_t *)p_lcd->pfb_gram;
    uint16_t pfb_stride = p_lcd->pfb_width;

    // 3. 遍历裁剪后的有效区域。
    for (int16_t py = clip_y_start; py <= clip_y_end; py++)
    {
        colour_t *p_dst = gram + ((py - p_lcd->pfb_y_start) * pfb_stride) + (clip_x_start - p_lcd->pfb_area.x0);
        int16_t mask_y = py - y; // 位图内相对 Y

        for (int16_t px = clip_x_start; px <= clip_x_end; px++)
        {
            int16_t mask_x = px - x; // 位图内相对 X

            // 直接按绝对位偏移取出该像素的 alpha。
            uint32_t pixel_idx = mask_y * w + mask_x;
            uint32_t bit_pos = pixel_idx * bpp;
            uint32_t byte_idx = bit_pos >> 3;
            uint8_t shift = 8 - bpp - (bit_pos & 7);

            uint8_t a_raw = (src_data[byte_idx] >> shift) & alpha_mask;

            if (a_raw > 0)
            {
                // 把不同位深的 alpha 统一量化到 0~255。
                uint32_t alpha = 0;
                if (bpp == 8)
                    alpha = a_raw;
                else if (bpp == 4)
                    alpha = (a_raw << 4) | a_raw;
                else if (bpp == 2)
                    alpha = a_raw * 85;
                else if (bpp == 1)
                    alpha = 255;

                if (opacity != 255)
                    alpha = (alpha * opacity) >> 8;

                we_store_blended_color(p_dst, fg_color, (uint8_t)alpha);
            }
            p_dst++; // 目标指针前进 1 像素
        }
    }
}

/**
 * @brief  使用指定颜色填充当前整个局部帧缓冲(PFB/GRAM)
 * @param  p_lcd  当前 LCD 上下文
 * @param  c      目标颜色
 * @note   这里做了循环展开，优先兼顾速度和代码体积
 */
void we_fill_gram(we_lcd_t *p_lcd, colour_t c)
{
    // 这里必须按当前切片真实大小计算像素总数，不能直接使用 GRAM_NUM。
    uint32_t total_pixels = (uint32_t)p_lcd->pfb_width * (p_lcd->pfb_y_end - p_lcd->pfb_y_start + 1);

    uint32_t blocks, rem, i;

#if (LCD_DEEP == DEEP_OLED)
#error ("Not support DEEP_OLED yet!")
#elif (LCD_DEEP == DEEP_GRAY8)
#error ("Not support DEEP_GRAY8 yet!")
#elif (LCD_DEEP == DEEP_RGB332)
#error ("Not support DEEP_RGB332 yet!")

#elif (LCD_DEEP == DEEP_RGB565)
    // RGB565 直接用原生 16 位指针写入，避免非对齐访问问题。
    colour_t *gram16 = p_lcd->pfb_gram;
    uint16_t color_val = c.dat16; // 提前提取到局部变量，避免循环里重复解引用

    // 每次展开 8 像素，在 F103 这类 MCU 上通常是更稳的折中。
    blocks = total_pixels / 8;
    rem = total_pixels % 8;

    for (i = 0; i < blocks; i++)
    {
        gram16->dat16 = color_val;
        gram16++;
        gram16->dat16 = color_val;
        gram16++;
        gram16->dat16 = color_val;
        gram16++;
        gram16->dat16 = color_val;
        gram16++;
        gram16->dat16 = color_val;
        gram16++;
        gram16->dat16 = color_val;
        gram16++;
        gram16->dat16 = color_val;
        gram16++;
        gram16->dat16 = color_val;
        gram16++;
    }

    for (i = 0; i < rem; i++)
    {
        gram16->dat16 = color_val;
        gram16++;
    }

#elif (LCD_DEEP == DEEP_RGB888)
    colour_t *gram = p_lcd->pfb_gram;
    uint8_t r = c.rgb.r, g = c.rgb.g, b = c.rgb.b;

    blocks = total_pixels / 4;
    rem = total_pixels % 4;

    for (i = 0; i < blocks; i++)
    {
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
    }

    for (i = 0; i < rem; i++)
    {
        gram->rgb.r = r;
        gram->rgb.g = g;
        gram->rgb.b = b;
        gram++;
    }
#endif
}

/**
 * @brief 用当前屏幕背景色清空整个 PFB。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @return 无。
 * @note 本函数只是对 we_fill_gram() 的轻量封装，
 *       目的是让“按背景色清屏”这个语义更直观。
 */
void we_clear_gram(we_lcd_t *p_lcd) { we_fill_gram(p_lcd, p_lcd->bg_color); }

/**
 * @brief 立即执行一次整帧重绘。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @return 无。
 * @note 实现步骤：
 *       1. 先把当前 PFB 用背景色清空；
 *       2. 再遍历对象链表；
 *       3. 依次调用每个对象自己的 draw_cb 完成重绘。
 */
static void _we_engine_refresh(we_lcd_t *p_lcd)
{
    /* 1. 先清背景，保证本轮重绘从干净画布开始。 */
    we_fill_gram(p_lcd, p_lcd->bg_color);

    /* 2. 顺序遍历对象链表，让每个控件自己绘制自己。 */
    we_obj_t *curr = p_lcd->obj_list_head;
    while (curr != NULL)
    {
        if (curr->class_p != NULL && curr->class_p->draw_cb != NULL)
        {
            /* 3. 引擎层包围盒剔除（Culling）：
             * 只有控件的包围盒与当前 PFB 切片存在交集时，才调用 draw_cb。
             * 控件层自己也会做裁剪，但这里的剔除能消除函数调用本身的开销。
             *
             * 交集条件（AABB 相交判定）：
             * - X 方向：控件右边 > 切片左边 且 控件左边 <= 切片右边
             * - Y 方向：控件下边 > 切片上边 且 控件上边 <= 切片下边
             */
            if ((curr->x + curr->w > p_lcd->pfb_area.x0) && (curr->x <= p_lcd->pfb_area.x1) &&
                (curr->y + curr->h > p_lcd->pfb_y_start) && (curr->y <= p_lcd->pfb_y_end))
            {
                curr->class_p->draw_cb(curr);
            }
        }
        curr = curr->next;
    }
}

/**
 * @brief GUI 内部注册一个带上下文数据的周期任务。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @param cb 传入，任务回调函数指针。
 * @param user_data 传入，回调执行时要透传给任务的上下文指针。
 * @return 任务编号，成功时返回 0 ~ WE_CFG_GUI_TASK_MAX_NUM-1，失败返回 -1。
 * @note 该函数只负责在固定任务表中寻找空槽并写入任务信息，不做业务层判断。
 */
int8_t _we_gui_task_register_with_data(we_lcd_t *p_lcd, we_gui_task_cb_t cb, void *user_data)
{
    int8_t task_id;

    if (p_lcd == NULL || cb == NULL)
        return -1;

    task_id = _we_gui_find_free_task_slot(p_lcd);
    if (task_id < 0)
        return -1;

    p_lcd->task_list[(uint8_t)task_id].cb = cb;
    p_lcd->task_list[(uint8_t)task_id].user_data = user_data;
    return task_id;
}

/**
 * @brief 将指定矩形区域按 PFB 分块推送到底层显示端口。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @param x 传入，目标区域左上角 X 坐标，允许为负。
 * @param y 传入，目标区域左上角 Y 坐标，允许为负。
 * @param w 传入，目标区域原始宽度。
 * @param h 传入，目标区域原始高度。
 * @return 无。
 * @note 实现步骤：
 *       1. 先做整块区域的屏幕边界裁剪；
 *       2. 统计本次真正下发的块数和像素数；
 *       3. 根据 GRAM_NUM 计算单次最多能刷多少行；
 *       4. 分块重绘 PFB；
 *       5. 调用底层端口把当前块送到 LCD。
 */
void we_push_pfb(we_lcd_t *p_lcd, int16_t x, int16_t y, uint16_t w, uint16_t h)
{
    /* 1. 先做最基本的尺寸检查。 */
    if (w == 0 || h == 0)
        return;

    /* 2. 如果整块区域已经完全在屏幕外，直接跳过。 */
    if (x >= p_lcd->width || y >= p_lcd->height)
        return;
    if (x + (int16_t)w <= 0 || y + (int16_t)h <= 0)
        return;

    /* 3. 裁掉左边和上边越界的部分。 */
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }

    /* 4. 裁掉右边和下边越界的部分。 */
    if (x + w > p_lcd->width)
        w = p_lcd->width - x;
    if (y + h > p_lcd->height)
        h = p_lcd->height - y;

    /* 5. 记录这次真实下发到底层的块数和像素数。
     * 统计放在裁剪之后，才能更接近 LCD 真正处理的有效负载。 */
    p_lcd->stat_pfb_pushes++;
    p_lcd->stat_pushed_pixels += (uint32_t)w * h;

    /* 6. 预计算本次刷新的最终区域信息。 */
    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;

    /* 7. 根据当前宽度和 GRAM_NUM，算出单次最多能刷多少行。 */
    uint16_t max_lines = p_lcd->pfb_size / w;

    if (max_lines == 0)
        return;

    p_lcd->pfb_area.x0 = x;
    p_lcd->pfb_area.y0 = y;
    p_lcd->pfb_area.x1 = x1;
    p_lcd->pfb_area.y1 = y1;
    p_lcd->pfb_width = w;

    /* 8. 先告诉底层 LCD 本轮整块刷新的目标窗口。 */
    if (p_lcd->set_addr_cb == NULL)
        return;
    p_lcd->set_addr_cb((uint16_t)x, (uint16_t)y, x1, y1);

    uint16_t current_y = y;
    uint16_t lines_left = h;

    /* 9. 按块循环：
     *    每次只生成当前块的 PFB 内容，再立即推到底层端口。 */
    while (lines_left > 0)
    {
        uint16_t lines_this_chunk = (lines_left > max_lines) ? max_lines : lines_left;
        uint32_t pixels_to_push = (uint32_t)lines_this_chunk * w;

        p_lcd->pfb_y_start = current_y;
        p_lcd->pfb_y_end = current_y + lines_this_chunk - 1;

        /* 10. 先重绘当前块对应的 PFB 内容。 */
        _we_engine_refresh(p_lcd);

#if (WE_CFG_DEBUG_DIRTY_RECT == 1)
        colour_t debug_color;
#if (LCD_DEEP == DEEP_RGB565)
        debug_color.dat16 = 0xF800;
#elif (LCD_DEEP == DEEP_RGB888)
        debug_color.rgb.r = 255;
        debug_color.rgb.g = 0;
        debug_color.rgb.b = 0;
#endif

        colour_t *gram = (colour_t *)p_lcd->pfb_gram;

        for (uint16_t row = 0; row < lines_this_chunk; row++)
        {
            gram[row * w + 0] = debug_color;
            gram[row * w + (w - 1)] = debug_color;
        }

        if (current_y == y)
        {
            for (uint16_t col = 0; col < w; col++)
                gram[col] = debug_color;
        }

        if (current_y + lines_this_chunk - 1 == y + h - 1)
        {
            uint16_t last_row_idx = lines_this_chunk - 1;
            for (uint16_t col = 0; col < w; col++)
                gram[last_row_idx * w + col] = debug_color;
        }
#endif

#if (LCD_DEEP == DEEP_RGB565)
        p_lcd->flush_cb((uint16_t *)p_lcd->pfb_gram, pixels_to_push);
        _we_lcd_swap_pfb(p_lcd);
#elif (LCD_DEEP == DEEP_RGB888)
        p_lcd->flush_cb((uint8_t *)p_lcd->pfb_gram, pixels_to_push * 3U);
#endif
        current_y += lines_this_chunk;
        lines_left -= lines_this_chunk;
    }
}

/**
 * @brief 累计 GUI 主循环流逝时间
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param ms 传入，本次新增的流逝时间，单位毫秒
 * @return 无
 * @note 内部会对累计值做上限保护，避免卡顿后一次性补偿过大。
 */
void we_gui_tick_inc(we_lcd_t *p_lcd, uint16_t ms)
{
    uint32_t sum;

    if (p_lcd == NULL || ms == 0U)
        return;

    /* 单次累计时间需要封顶，避免主循环卡顿后一次性补偿过大，
     * 导致容器吸附或其他时间驱动动画突然跳变。 */
    sum = (uint32_t)p_lcd->tick_elapsed_ms + ms;
    if (sum > 200U)
        sum = 200U;

    p_lcd->tick_elapsed_ms = (uint16_t)sum;
}

/**
 * @brief GUI 内部注销一个已注册的周期任务。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @param task_id 传入，待注销的任务编号。
 * @return 无。
 */
void _we_gui_task_unregister(we_lcd_t *p_lcd, int8_t task_id)
{
    if (p_lcd == NULL)
        return;
    if (task_id < 0 || task_id >= WE_CFG_GUI_TASK_MAX_NUM)
        return;

    p_lcd->task_list[(uint8_t)task_id].cb = NULL;
    p_lcd->task_list[(uint8_t)task_id].user_data = NULL;
}

/**
 * @brief 创建并启动一个 GUI 定时器。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @param cb 传入，定时器回调函数。
 * @param period_ms 传入，触发周期，单位毫秒，必须大于 0。
 * @param repeat 传入，1 表示周期触发，0 表示单次触发。
 * @return 定时器编号，成功时返回 0 ~ WE_CFG_GUI_TIMER_MAX_NUM-1，失败返回 -1。
 */
int8_t we_gui_timer_create(we_lcd_t *p_lcd, we_gui_timer_cb_t cb, uint16_t period_ms, uint8_t repeat)
{
    int8_t timer_id;

    if (p_lcd == NULL || cb == NULL || period_ms == 0U)
        return -1;

    timer_id = _we_gui_find_free_timer_slot(p_lcd);
    if (timer_id < 0)
        return -1;

    p_lcd->timer_list[(uint8_t)timer_id].cb = cb;
    p_lcd->timer_list[(uint8_t)timer_id].period_ms = period_ms;
    p_lcd->timer_list[(uint8_t)timer_id].acc_ms = 0U;
    p_lcd->timer_list[(uint8_t)timer_id].repeat = (repeat != 0U) ? 1U : 0U;
    p_lcd->timer_list[(uint8_t)timer_id].active = 1U;
    return timer_id;
}

/**
 * @brief 启动指定 GUI 定时器并清零累计时间
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param timer_id 传入，待启动的定时器编号
 * @return 无
 */
void we_gui_timer_start(we_lcd_t *p_lcd, int8_t timer_id)
{
    if (p_lcd == NULL)
        return;
    if (timer_id < 0 || timer_id >= WE_CFG_GUI_TIMER_MAX_NUM)
        return;
    if (p_lcd->timer_list[(uint8_t)timer_id].cb == NULL)
        return;

    p_lcd->timer_list[(uint8_t)timer_id].active = 1U;
    p_lcd->timer_list[(uint8_t)timer_id].acc_ms = 0U;
}

/**
 * @brief 停止指定 GUI 定时器并清零累计时间
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param timer_id 传入，待停止的定时器编号
 * @return 无
 */
void we_gui_timer_stop(we_lcd_t *p_lcd, int8_t timer_id)
{
    if (p_lcd == NULL)
        return;
    if (timer_id < 0 || timer_id >= WE_CFG_GUI_TIMER_MAX_NUM)
        return;
    if (p_lcd->timer_list[(uint8_t)timer_id].cb == NULL)
        return;

    p_lcd->timer_list[(uint8_t)timer_id].active = 0U;
    p_lcd->timer_list[(uint8_t)timer_id].acc_ms = 0U;
}

/**
 * @brief 重启指定 GUI 定时器
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param timer_id 传入，待重启的定时器编号
 * @return 无
 * @note 重启会清零累计时间，并立即置为 active 状态。
 */
void we_gui_timer_restart(we_lcd_t *p_lcd, int8_t timer_id)
{
    if (p_lcd == NULL)
        return;
    if (timer_id < 0 || timer_id >= WE_CFG_GUI_TIMER_MAX_NUM)
        return;
    if (p_lcd->timer_list[(uint8_t)timer_id].cb == NULL)
        return;

    p_lcd->timer_list[(uint8_t)timer_id].acc_ms = 0U;
    p_lcd->timer_list[(uint8_t)timer_id].active = 1U;
}

/**
 * @brief 删除指定 GUI 定时器
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @param timer_id 传入，待删除的定时器编号
 * @return 无
 * @note 删除后该槽位可被后续 we_gui_timer_create() 复用。
 */
void we_gui_timer_delete(we_lcd_t *p_lcd, int8_t timer_id)
{
    if (p_lcd == NULL)
        return;
    if (timer_id < 0 || timer_id >= WE_CFG_GUI_TIMER_MAX_NUM)
        return;

    p_lcd->timer_list[(uint8_t)timer_id].cb = NULL;
    p_lcd->timer_list[(uint8_t)timer_id].period_ms = 0U;
    p_lcd->timer_list[(uint8_t)timer_id].acc_ms = 0U;
    p_lcd->timer_list[(uint8_t)timer_id].repeat = 0U;
    p_lcd->timer_list[(uint8_t)timer_id].active = 0U;
}

/**
 * @brief 执行一次 GUI 主任务处理
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 实现步骤：
 *       1. 先消费 tick 累计时间。
 *       2. 再推进 GUI 内部任务和用户定时器。
 *       3. 遍历当前脏矩形并逐块推送到底层显示端口。
 *       4. 本轮真正发生刷新后，统计一帧渲染并清空脏区。
 */
void we_gui_task_handler(we_lcd_t *p_lcd)
{
    uint16_t elapsed_ms = 0U;

    if (p_lcd == NULL)
        return;

    /* 1. 读取并分发输入事件。 */
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
    if (p_lcd->input_read_cb != NULL)
    {
        p_lcd->input_read_cb(&p_lcd->indev_data);
        if (p_lcd->indev_data.state != WE_TOUCH_STATE_NONE)
            we_gui_indev_handler(p_lcd, &p_lcd->indev_data);
    }
#else
    {
        extern void we_input_port_read(we_indev_data_t * data);
        we_input_port_read(&p_lcd->indev_data);
        if (p_lcd->indev_data.state != WE_TOUCH_STATE_NONE)
            we_gui_indev_handler(p_lcd, &p_lcd->indev_data);
    }
#endif

    /* 2. 消费累计时间，推进内部任务和用户定时器。 */
    elapsed_ms = p_lcd->tick_elapsed_ms;
    p_lcd->tick_elapsed_ms = 0U;
    _we_gui_run_tasks(p_lcd, elapsed_ms);
    _we_gui_run_timers(p_lcd, elapsed_ms);

    /* 3. 消费脏矩形并推屏。 */
    _we_gui_flush_dirty(p_lcd);
}

/**
 * @brief 通用对象移动接口，适用于所有继承 we_obj_t 的控件
 * @param obj 传入，目标对象指针
 * @param new_x 传入，新的绝对 X 坐标
 * @param new_y 传入，新的绝对 Y 坐标
 * @return 无
 * @note 实现步骤：
 *       1. 先把旧区域标脏，保证旧位置会被擦除。
 *       2. 再更新对象坐标。
 *       3. 最后把新区域标脏，保证新位置会被重新绘制。
 */
void we_obj_set_pos(we_obj_t *obj, int16_t new_x, int16_t new_y)
{
    if (obj == NULL || obj->lcd == NULL)
        return;
    if (obj->x == new_x && obj->y == new_y)
        return;

    // 1. 先把旧位置标脏，保证移动后旧区域会被重绘擦除。
    we_obj_invalidate(obj);

#if 0
    if (obj->parent != NULL)
    {
        we_container_obj_t *parent = (we_container_obj_t *)obj->parent;

        curr = parent->children_head;
        prev = NULL;
        while (curr != NULL)
        {
            if (curr == obj)
            {
                if (prev == NULL)
                    parent->children_head = curr->next;
                else
                    prev->next = curr->next;
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }

    // 2. 更新对象坐标。
#endif
    obj->x = new_x;
    obj->y = new_y;

    // 3. 再把新位置标脏，保证新区域会被绘制出来。
    we_obj_invalidate(obj);
}

/**
 * @brief 从指定链表头中摘除目标对象（公用 helper）
 * @param head_p 传入：链表头指针的地址（顶层链表为 &lcd->obj_list_head，
 *                                       子容器为 &parent->children_head）
 * @param obj 传入：待摘除对象指针
 * @return 无
 * @note 仅做摘除，不清状态。we_obj_delete 与 we_obj_bring_to_front 共用。
 */
static void _we_obj_unlink_from(we_obj_t **head_p, we_obj_t *obj)
{
    we_obj_t *curr = *head_p;
    we_obj_t *prev = NULL;

    while (curr != NULL)
    {
        if (curr == obj)
        {
            if (prev == NULL)
                *head_p = curr->next;
            else
                prev->next = curr->next;
            return;
        }
        prev = curr;
        curr = curr->next;
    }
}

/**
 * @brief 返回对象所在链表的头指针地址
 * @param obj 传入：目标对象
 * @return 链表头指针的地址（顶层或子容器）
 * @note 上层调用方保证 obj 与 obj->lcd 非空。
 */
static we_obj_t **_we_obj_owner_head(we_obj_t *obj)
{
    if (obj->parent != NULL)
    {
        we_child_owner_t *parent = (we_child_owner_t *)obj->parent;
        return &parent->children_head;
    }
    return &obj->lcd->obj_list_head;
}

/**
 * @brief 通用对象删除接口，把任意控件从渲染树中摘除
 * @param obj 传入，待删除对象指针
 * @return 无
 * @note 同时支持顶层链表和带 children_head 的复合容器（如 slideshow）。
 */
void we_obj_delete(we_obj_t *obj)
{
    if (obj == NULL || obj->lcd == NULL)
        return;

    // 1. 先把当前区域标脏，用于删除后的残影擦除。
    we_obj_invalidate(obj);

    // 2. 从所在链表中摘掉当前对象。
    _we_obj_unlink_from(_we_obj_owner_head(obj), obj);

    // 3. 清空对象状态，避免后续误用。
    obj->next = NULL;
    obj->parent = NULL;
    obj->class_p = NULL;
    obj->lcd = NULL;
}

void we_obj_bring_to_front(we_obj_t *obj)
{
    we_obj_t **head_p;
    we_obj_t *tail;

    if (obj == NULL || obj->lcd == NULL || obj->next == NULL)
        return;

    head_p = _we_obj_owner_head(obj);

    // 1. 先把自己从原链表中摘下来。
    _we_obj_unlink_from(head_p, obj);

    // 2. 再追加到链表尾部，使其位于绘制顺序最上层。
    tail = *head_p;
    if (tail == NULL)
    {
        *head_p = obj;
    }
    else
    {
        while (tail->next != NULL)
            tail = tail->next;
        tail->next = obj;
    }
    obj->next = NULL;
}

/**
 * @brief 基于父节点层层裁剪后的局部区域标脏函数
 * @param obj 传入，目标对象指针
 * @param x 传入，自定义脏区左上角 X 坐标
 * @param y 传入，自定义脏区左上角 Y 坐标
 * @param w 传入，自定义脏区宽度
 * @param h 传入，自定义脏区高度
 * @return 无
 * @note 这里的坐标使用屏幕绝对坐标。
 *       内部会沿 parent 链逐层求交，最终只把容器可视区内的部分提交给脏矩形管理器。
 */
void we_obj_invalidate_area(we_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (obj == NULL || obj->lcd == NULL)
        return;

    int16_t x0 = x;
    int16_t y0 = y;
    int16_t x1 = x + w - 1;
    int16_t y1 = y + h - 1;

    // 向上遍历所有父容器，逐层求交，保证标脏不会溢出容器可视区。
    we_obj_t *p = obj->parent;
    while (p)
    {
        x0 = WE_MAX(x0, p->x);
        y0 = WE_MAX(y0, p->y);
        x1 = WE_MIN(x1, p->x + p->w - 1);
        y1 = WE_MIN(y1, p->y + p->h - 1);
        p = p->parent;
    }

    // 只有最终仍有有效交集时，才真正提交给脏矩形管理器。
    if (x0 <= x1 && y0 <= y1)
    {
        we_dirty_invalidate(&obj->lcd->dirty_mgr, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
}

/**
 * @brief 基于父节点层层裁剪的智能标脏函数，默认标脏整个控件
 * @param obj 传入，目标对象指针
 * @return 无
 * @note 这样可以避免子控件标脏区域越过父容器边界，造成无意义重绘。
 */
void we_obj_invalidate(we_obj_t *obj)
{
    if (obj == NULL)
        return;
    we_obj_invalidate_area(obj, obj->x, obj->y, obj->w, obj->h);
}

/**
 * @brief 使用指定的 PFB 缓冲区和底层端口接口初始化一套 GUI 屏幕上下文
 * @param p_lcd 传入，待初始化的 GUI 屏幕上下文指针
 * @param bg 传入，默认背景色
 * @param gram_base 传入，用户指定的 PFB 缓冲区基址
 * @param gram_size 传入，用户提供的 PFB 总像素容量
 * @param set_addr_cb 传入，底层设窗回调
 * @param flush_cb 传入，底层刷屏回调
 * @return 无
 * @note 实现步骤：
 *       1. 选择本次初始化要绑定的 PFB 缓冲区和底层端口接口；
 *       2. 初始化背景色、对象链表、时间累计、任务表和统计字段；
 *       3. 初始化脏矩形管理器；
 *       4. 把整屏标记为脏区，确保首帧一定完整刷新。
 */
void we_lcd_init_with_port(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
                           we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb)
{
    uint8_t i;

    if (p_lcd == NULL)
        while (1)
            ;
    if (gram_base == NULL)
        while (1)
            ;
    if (set_addr_cb == NULL)
        while (1)
            ;
    if (flush_cb == NULL)
        while (1)
            ;

    p_lcd->width = SCREEN_WIDTH;
    p_lcd->height = SCREEN_HEIGHT;
    p_lcd->bg_color = bg;

    p_lcd->obj_list_head = NULL; // 对象链表初始为空
    p_lcd->tick_elapsed_ms = 0U; // GUI 时间累计从 0 开始

    for (i = 0; i < WE_CFG_GUI_TASK_MAX_NUM; i++)
    {
        p_lcd->task_list[i].cb = NULL;
        p_lcd->task_list[i].user_data = NULL;
    }
    for (i = 0; i < WE_CFG_GUI_TIMER_MAX_NUM; i++)
    {
        p_lcd->timer_list[i].cb = NULL;
        p_lcd->timer_list[i].period_ms = 0U;
        p_lcd->timer_list[i].acc_ms = 0U;
        p_lcd->timer_list[i].repeat = 0U;
        p_lcd->timer_list[i].active = 0U;
    }
    p_lcd->stat_render_frames = 0U;
    p_lcd->stat_pfb_pushes = 0U;
    p_lcd->stat_pushed_pixels = 0U;
    p_lcd->indev_data.x = 0;
    p_lcd->indev_data.y = 0;
    p_lcd->indev_data.state = WE_TOUCH_STATE_NONE;
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
    p_lcd->input_read_cb = NULL;
#endif
#if (WE_CFG_ENABLE_STORAGE_PORT_BIND == 1)
    p_lcd->storage_read_cb = NULL;
#endif
    /*
    p_lcd->pfb_gram_base = gram; // 记录 PFB 基址，后续双缓冲切换会用到
p_lcd->pfb_gram = gram;
    */
    p_lcd->pfb_gram_base = gram_base; // 记录 PFB 基址，后续双缓冲切换会用到
    p_lcd->pfb_gram = gram_base;
    /* PFB 长度约定：
     * 1. 非 DMA 双缓冲时，pfb_size = 用户注册的总长度；
     * 2. DMA 双缓冲时，用户仍然注册整块 USER_GRAM_NUM，
     *    内核内部自动按一半作为“当前绘制 PFB”长度使用，
     *    另一半留给底层异步发送使用。 */
#if (GRAM_DMA_BUFF_EN == 0)
    p_lcd->pfb_size = gram_size;
#else
    p_lcd->pfb_size = gram_size / 2U;
#endif
    p_lcd->set_addr_cb = set_addr_cb;
    p_lcd->flush_cb = flush_cb;

    we_dirty_init(&p_lcd->dirty_mgr);
    p_lcd->dirty_mgr.screen_w = p_lcd->width;
    p_lcd->dirty_mgr.screen_h = p_lcd->height;
    we_dirty_invalidate(&p_lcd->dirty_mgr, 0, 0, p_lcd->width, p_lcd->height);
}

/**
 * @brief 输入设备事件分发函数
 * @param lcd 传入，当前 GUI 屏幕上下文指针
 * @param data 传入，底层驱动采集到的触摸数据
 * @return 无
 * @note 释放阶段会按位移和阈值判定点击或方向滑动事件。
 */
void we_gui_indev_handler(we_lcd_t *lcd, we_indev_data_t *data)
{
    static we_obj_t *last_pressed_obj = NULL;
    static uint8_t had_stay = 0; /* 本次触摸序列中是否经历过 STAY（拖拽） */

    if (lcd == NULL || data == NULL)
        return;

    we_obj_t *target = NULL;
    we_obj_t *curr = lcd->obj_list_head;

    // 顺序遍历对象链表。后加入的对象通常位于更上层，
    // 所以最后一个命中的对象就是最终收到事件的对象。
    while (curr != NULL)
    {
        if (curr->class_p != NULL && curr->class_p->event_cb != NULL)
        {
            // 基础矩形命中检测
            if (data->x >= curr->x && data->x < (curr->x + curr->w) && data->y >= curr->y &&
                data->y < (curr->y + curr->h))
            {
                target = curr;
            }
        }
        curr = curr->next;
    }

    // 按触摸状态机分发事件。
    if (data->state == WE_TOUCH_STATE_PRESSED)
    {
        /* 记录按下坐标，用于释放时判定滑动方向 */
        lcd->gesture_press_x = data->x;
        lcd->gesture_press_y = data->y;
        had_stay = 0;

        if (target != NULL)
        {
            target->class_p->event_cb(target, WE_EVENT_PRESSED, data);
            last_pressed_obj = target;
        }
    }
    else if (data->state == WE_TOUCH_STATE_RELEASED)
    {
        if (last_pressed_obj != NULL)
        {
            last_pressed_obj->class_p->event_cb(last_pressed_obj, WE_EVENT_RELEASED, data);

            /* 滑动手势识别：仅在"快速划过"（无 STAY）时才生成 SWIPE。
             * 如果经历过 STAY（手指拖拽），控件已经通过 STAY 实时跟随了，
             * 松手后只需要吸附即可，不再额外派发 SWIPE 避免冲突。 */
            int16_t dx = data->x - lcd->gesture_press_x;
            int16_t dy = data->y - lcd->gesture_press_y;
            int16_t abs_dx = (dx >= 0) ? dx : -dx;
            int16_t abs_dy = (dy >= 0) ? dy : -dy;

            if (!had_stay && abs_dx > abs_dy && abs_dx >= WE_CFG_SWIPE_THRESHOLD)
            {
                /* 水平滑动 */
                we_event_t swipe = (dx < 0) ? WE_EVENT_SWIPE_LEFT : WE_EVENT_SWIPE_RIGHT;
                last_pressed_obj->class_p->event_cb(last_pressed_obj, swipe, data);
            }
            else if (!had_stay && abs_dy >= WE_CFG_SWIPE_THRESHOLD)
            {
                /* 垂直滑动 */
                we_event_t swipe = (dy < 0) ? WE_EVENT_SWIPE_UP : WE_EVENT_SWIPE_DOWN;
                last_pressed_obj->class_p->event_cb(last_pressed_obj, swipe, data);
            }
            else if (last_pressed_obj == target)
            {
                /* 位移不足或拖拽后松手，视为点击（拖拽后不在原控件上则不触发） */
                last_pressed_obj->class_p->event_cb(last_pressed_obj, WE_EVENT_CLICKED, data);
            }
        }
        last_pressed_obj = NULL;
    }
    else if (data->state == WE_TOUCH_STATE_STAY)
    {
        had_stay = 1;
        // 长按/拖拽期间持续分发 STAY 事件。
        if (last_pressed_obj != NULL && last_pressed_obj->class_p->event_cb != NULL)
        {
            last_pressed_obj->class_p->event_cb(last_pressed_obj, WE_EVENT_STAY, data);
        }
    }
}

/**
 * @brief 为指定 LCD 实例注册输入读取接口
 * @param p_lcd 传入，待绑定输入接口的 GUI 屏幕上下文指针
 * @param read_cb 传入，输入读取回调，传 NULL 表示当前 LCD 不启用输入采集
 * @return 无
 * @note 该接口只负责登记“如何读取输入”，主循环仍然可以自行决定轮询时机
 *       和是否把读取结果送入 we_gui_indev_handler()。
 */
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
void we_input_init_with_port(we_lcd_t *p_lcd, we_input_read_cb_t read_cb)
{
    if (p_lcd == NULL)
        return;

    p_lcd->input_read_cb = read_cb;
    p_lcd->indev_data.x = 0;
    p_lcd->indev_data.y = 0;
    p_lcd->indev_data.state = WE_TOUCH_STATE_NONE;
}
#endif

/**
 * @brief 为指定 LCD 实例注册外部存储读取接口
 * @param p_lcd 传入，待绑定存储接口的 GUI 屏幕上下文指针
 * @param read_cb 传入，存储读取回调，参数顺序为(数组指针, 存储地址, 读取数量)
 * @return 无
 */
#if (WE_CFG_ENABLE_STORAGE_PORT_BIND == 1)
void we_storage_init_with_port(we_lcd_t *p_lcd, we_storage_read_cb_t read_cb)
{
    if (p_lcd == NULL)
        return;

    p_lcd->storage_read_cb = read_cb;
}
#endif

/**
 * @brief 一次性完成 LCD / 输入 / 存储三路端口绑定
 * @param p_lcd 传入，待初始化的 GUI 屏幕上下文指针
 * @param bg 传入，默认背景色
 * @param gram_base 传入，用户指定的 PFB 缓冲区基址
 * @param gram_size 传入，用户提供的 PFB 总像素容量
 * @param set_addr_cb 传入，底层设窗回调
 * @param flush_cb 传入，底层刷屏回调
 * @param input_cb 传入，输入读取回调
 * @param storage_cb 传入，外部存储读取回调
 * @return 无
 * @note input_cb 和 storage_cb 在对应 WE_CFG_ENABLE_xxx_PORT_BIND == 0 时被忽略，可安全传 NULL。
 */
void we_gui_init(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
                 we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb, we_input_read_cb_t input_cb,
                 we_storage_read_cb_t storage_cb)
{
    we_lcd_init_with_port(p_lcd, bg, gram_base, gram_size, set_addr_cb, flush_cb);
#if (WE_CFG_ENABLE_INPUT_PORT_BIND == 1)
    we_input_init_with_port(p_lcd, input_cb);
#else
    (void)input_cb;
#endif
#if (WE_CFG_ENABLE_STORAGE_PORT_BIND == 1)
    we_storage_init_with_port(p_lcd, storage_cb);
#else
    (void)storage_cb;
#endif
}
