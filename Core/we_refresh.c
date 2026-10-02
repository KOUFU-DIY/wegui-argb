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
/**
 * @file  we_refresh.c
 * @brief 刷新管线：消费脏矩形、按 PFB 容量分块、逐块重绘并推送到显示端口。
 */
#include "we_internal.h"

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
 * @note 仅在双缓冲生效（pfb_double，初始化时按端口是否异步决定）时切换，否则为空操作。
 */
static void _we_lcd_swap_pfb(we_lcd_t *p_lcd)
{
#if (GRAM_DMA_BUFF_EN == 1)
    if (p_lcd->pfb_double == 0U)
        return;
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
 * GUI 脏矩形刷新任务
 *
 * 设计目标：
 * 1. 把 we_gui_run() 中“消费脏矩形并推屏”的逻辑单独收口；
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
#if (WE_CFG_ENABLE_RENDER_STATS == 1)
    p_lcd->stat_render_frames++;
#endif

    /* 所有脏区处理完成后，清空管理器，等待业务层下一轮标脏。 */
    we_dirty_clear(&p_lcd->dirty_mgr);
}

#if (WE_CFG_DEBUG_PERF_STRESS == 1)
/* --------------------------------------------------------------------------
 * 控件性能压测：每帧强制标脏所有顶层控件
 *
 * 仅在 WE_CFG_DEBUG_PERF_STRESS == 1 时编译进来。开启后，主任务每轮都会把
 * 当前对象链表里的每个控件按其自身区域重新标脏，使 GUI 持续全量重绘，
 * 从而暴露当前页面控件的最坏情况渲染吞吐，配合 FPS / stat 计数器观察性能。
 * -------------------------------------------------------------------------- */
/**
 * @brief 强制把所有顶层控件按自身区域标脏（性能压测专用）
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 * @note 只遍历顶层 obj_list_head；子容器内的子控件会随父容器整块重绘，
 *       因此无需递归进入 children_head。
 */
static void _we_gui_perf_stress_invalidate(we_lcd_t *p_lcd)
{
    we_obj_t *curr;

    if (p_lcd == NULL)
        return;

    for (curr = p_lcd->obj_list_head; curr != NULL; curr = curr->next)
    {
        we_obj_invalidate(curr);
    }
}
#endif

/**
 * @brief 用当前屏幕背景色清空整个 PFB。
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @return 无。
 * @note 本函数只是对 we_fill_gram() 的轻量封装，
 *       目的是让“按背景色清屏”这个语义更直观。
 */
void we_clear_gram(we_lcd_t *p_lcd) { we_fill_gram(p_lcd, p_lcd->bg_color); }

/**
 * @brief 绘制一个对象及其子树
 * @param p_lcd 传入，GUI 屏幕上下文指针
 * @param obj 传入，待绘制对象
 * @return 无
 * @note 顺序：窗口收窄到对象包围盒 → draw_cb → 子控件（容器：窗口再收窄到
 *       content_cb 给出的子控件区，透明度级联乘入 opa_scale）→ 窗口回到包围盒
 *       → draw_post_cb → 恢复窗口。控件因此画不到自己的包围盒之外（删改时不会
 *       留下残影），包围盒与当前窗口无交集时整棵子树直接跳过。
 *       递归深度 = 容器嵌套深度。
 */
static void _we_draw_obj(we_lcd_t *p_lcd, we_obj_t *obj)
{
    const we_class_t *cls = obj->class_p;
    we_pfb_clip_t sv;
    we_rect_t box;

    if (cls == NULL)
        return;

    box.x0 = obj->x;
    box.y0 = obj->y;
    box.x1 = (int16_t)(obj->x + obj->w - 1);
    box.y1 = (int16_t)(obj->y + obj->h - 1);
    /* 绝大多数对象与当前切片不相交，先用一次包围盒比较挡掉 */
    if (box.x0 > p_lcd->clip.x1 || box.x1 < p_lcd->clip.x0 ||
        box.y0 > p_lcd->clip.y1 || box.y1 < p_lcd->clip.y0)
        return;
    we_pfb_clip_save(p_lcd, &sv);
    if (!we_pfb_clip_to(p_lcd, &sv, box.x0, box.y0, box.x1, box.y1))
        return;

    if (cls->draw_cb != NULL)
        cls->draw_cb(obj);

    if ((cls->class_flags & WE_CLASS_FLAG_CHILD_OWNER) != 0U)
    {
        uint8_t opa = 255U;

        /* box 复用为子控件区：默认 = 包围盒，content_cb 只能在包围盒以内收窄 */
        if (cls->content_cb != NULL)
            opa = cls->content_cb(obj, &box);
        if (opa != 0U)
        {
            we_pfb_clip_t bv; /* 本对象包围盒窗口，画完子控件回到这里给 draw_post_cb */

            we_pfb_clip_save(p_lcd, &bv);
            if (we_pfb_clip_to(p_lcd, &bv, box.x0, box.y0, box.x1, box.y1))
            {
                uint8_t old_scale = p_lcd->opa_scale;
                we_obj_t *child;

                p_lcd->opa_scale = we_opa_apply(p_lcd, opa); /* 嵌套容器自动链乘 */
                for (child = ((we_child_owner_t *)obj)->children_head; child != NULL; child = child->next)
                    _we_draw_obj(p_lcd, child);
                p_lcd->opa_scale = old_scale;
                we_pfb_clip_restore(p_lcd, &bv);
            }
        }
        if (cls->draw_post_cb != NULL)
            cls->draw_post_cb(obj);
    }

    we_pfb_clip_restore(p_lcd, &sv);
}
/**
 * @brief 重绘当前切片：背景 → 普通层 → 焦点光标 → 顶层
 * @param p_lcd 传入，GUI 屏幕上下文指针。
 * @return 无。
 */
static void _we_engine_refresh(we_lcd_t *p_lcd)
{
    we_obj_t *curr;

    we_fill_gram(p_lcd, p_lcd->bg_color);

    for (curr = p_lcd->obj_list_head; curr != NULL; curr = curr->next)
        _we_draw_obj(p_lcd, curr);

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /* 焦点矩形光标：普通对象之后补画，悬浮在普通层之上，
     * 但不压住顶层（toast/msgbox）与弹窗。 */
    _we_focus_draw_cursor(p_lcd);
#endif

#if (WE_CFG_ENABLE_TOP_LAYER == 1)
    /* 顶层对象（toast/msgbox 等"保证置顶"层）：普通层与光标之后绘制。 */
    for (curr = p_lcd->top_list_head; curr != NULL; curr = curr->next)
        _we_draw_obj(p_lcd, curr);
#endif
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
 *       3. 根据 pfb_size（当前 PFB 像素容量，真双缓冲时为整块的一半）计算单次最多能刷多少行；
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

    /* 5. 未绑定端口（未初始化）时直接跳过，避免空指针调用。 */
    if (p_lcd->port == NULL)
        return;

    /* 6. 预计算本次刷新的最终区域信息。 */
    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;

    /* 7. 根据当前宽度和 pfb_size（当前 PFB 像素容量，真双缓冲时为整块的一半），算出单次最多能刷多少行；
     *    再向下对齐到 Y 对齐粒度——窄矩形下切片行数会被 pfb_size/w 放大到非对齐值，
     *    页打包类端口依赖每个切片都是整对齐块（ALIGN_Y=1 时本步恒等，零变化）。 */
    uint16_t max_lines = p_lcd->pfb_size / w;
    max_lines = (uint16_t)(max_lines & (uint16_t)~(WE_LCD_FLUSH_ALIGN_Y - 1));

    if (max_lines == 0)
        return;

    p_lcd->clip.x0 = x;
    p_lcd->clip.x1 = x1;
    p_lcd->pfb_width = w;

    /* 8. 先告诉底层 LCD 本轮整块刷新的目标窗口。 */
    p_lcd->port->set_addr((uint16_t)x, (uint16_t)y, x1, y1);

    uint16_t current_y = y;
    uint16_t lines_left = h;

    /* 9. 按块循环：
     *    每次只生成当前块的 PFB 内容，再立即推到底层端口。 */
    while (lines_left > 0)
    {
        uint16_t lines_this_chunk = (lines_left > max_lines) ? max_lines : lines_left;
        uint32_t pixels_to_push = (uint32_t)lines_this_chunk * w;

        p_lcd->clip.y0 = current_y;
        p_lcd->clip.y1 = current_y + lines_this_chunk - 1;

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
#elif (LCD_DEEP == DEEP_GRAY8)
        debug_color.dat8 = 255; /* 灰度屏用最亮标记脏区边框 */
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

        /* 11. 推送当前块（数量单位统一为像素），双缓冲时随即换到另一半继续画。 */
#if (LCD_DEEP == DEEP_RGB565)
        p_lcd->port->flush((uint16_t *)p_lcd->pfb_gram, pixels_to_push);
#else
        p_lcd->port->flush((uint8_t *)p_lcd->pfb_gram, pixels_to_push);
#endif
        _we_lcd_swap_pfb(p_lcd);
        current_y += lines_this_chunk;
        lines_left -= lines_this_chunk;
    }
}

/**
 * @brief 刷新阶段：压测标脏（调试开关）后消费脏矩形并推屏
 * @param p_lcd 传入，当前 GUI 屏幕上下文指针
 * @return 无
 */
void _we_refresh_run(we_lcd_t *p_lcd)
{
#if (WE_CFG_DEBUG_PERF_STRESS == 1)
    /* 性能压测：在刷新前强制标脏所有控件，制造每帧全量重绘负载。 */
    _we_gui_perf_stress_invalidate(p_lcd);
#endif
    _we_gui_flush_dirty(p_lcd);
}
