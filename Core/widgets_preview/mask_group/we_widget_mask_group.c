/**
 * @file  we_widget_mask_group.c
 * @brief 蒙版容器控件（mask_group）实现
 *
 * 渲染分两阶段：
 *   1. 子控件阶段：完全复用 group 的套路——收窄 PFB 窗口（矩形硬裁剪）+
 *      opa_scale 透明度级联，子控件按原有全速路径绘制，原语零改动；
 *   2. 蒙版后处理阶段：恢复 PFB 窗口后，对（容器 ∩ 当前条带）区域合成：
 *      2a. 渐变 pass（可选）：内容整体向背板做 alpha 渐隐，整行 DDA 增量
 *          推进（每像素一次 int32 加法 + 混色），Q16 定点步进在进条带前
 *          用 3 次 int64 除法一次性算好，内环无除法、无浮点、无函数调用；
 *      2b. 边框直条（可选）：四条 K 段间的直边用有效边框色整块覆盖
 *          （内容在边框内沿被裁剪，边框如"相框"盖在裁剪线上）；
 *      2c. 四角合成：几何口径与 box 完全一致（外轮廓 = 裁剪线，内轮廓 =
 *          内缩 bw 的同形轮廓；切角内缩 0.586·bw 保持等厚）。圆角走
 *          we_mask_quarter_ring_alpha 单遍子采样（外/内覆盖一次求出），
 *          切角按行分段整块写（alpha 仅 0/128/255，无逐像素函数调用）。
 *          每像素按 [内容·a_in + 边框·(a_out-a_in) + 背板·(255-a_out)]
 *          顺序合成（与 box 相同的顺序近似，仅 AA 过渡带有亚像素误差）。
 *
 * 边框与渐变的关系：渐变只作用于内容层（2a 在 2b/2c 之前执行），
 * 边框保持实色；容器自身 opacity 会让边框有效色向背板收敛
 * （bd_eff = blend(border, backdrop, opacity)，每帧只算一次）。
 *
 * 背板语义：蒙版透明处向 backdrop 纯色还原。容器叠在纯色底上时结果精确；
 * 叠在图片/控件上时会露出背板色（真实背景恢复需要快照缓冲，v1 不做）。
 *
 * 蒙版 alpha 是绝对坐标的纯函数，任意脏矩形局部重绘都能无缝拼接。
 * 所有 set 接口在目标值与当前值相同时直接返回，不触发重绘。
 */

#include "we_widget_mask_group.h"
#include "we_render.h"
/* 槽位占用位图：used 标志抽为容器级 uint32 位图后槽体 12B->8B 零填充，
 * WE_MASK_GROUP_CHILD_MAX(<=32) 个槽共省 4*N+4 字节 RAM。 */
#define _MG_SLOT_USED(o, i) ((((o)->slot_used_mask >> (i)) & 1U) != 0U)
#define _MG_SLOT_SET(o, i) ((o)->slot_used_mask |= ((uint32_t)1U << (i)))
#define _MG_SLOT_CLR(o, i) ((o)->slot_used_mask &= ~((uint32_t)1U << (i)))


/* 切角边框的内轮廓收缩系数（与 box 保持一致）：
 * 45° 切角若同样减 bw，对角段的垂直厚度只有 bw/√2（视觉偏细）。
 * 改为收缩 (2-√2)·bw ≈ 0.586·bw 可让对角段与直边段等厚。Q8：0.586*256 ≈ 150。 */
#define _MG_CHAMFER_INSET(bw) ((uint16_t)(((uint32_t)(bw) * 150U) >> 8))

/* 从打包字节里取一个角的样式（每角 2bit，位移 = 角索引*2） */
#define _MG_STYLE(o, i) ((uint8_t)(((o)->corner_styles >> ((uint8_t)(i) * 2U)) & 0x3U))

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 颜色相等比较（RGB565/RGB888），供 setter 的"值未变则跳过"守卫使用。
 */
static __inline uint8_t _mg_colour_eq(colour_t a, colour_t b)
{
#if (LCD_DEEP == DEEP_RGB565)
    return (uint8_t)(a.dat16 == b.dat16);
#elif (LCD_DEEP == DEEP_RGB888)
    return (uint8_t)(a.rgb.r == b.rgb.r && a.rgb.g == b.rgb.g && a.rgb.b == b.rgb.b);
#endif
}

/**
 * @brief 在容器 slot 表中查找指定子控件的记录。
 * @param obj 控件对象指针。
 * @param child 目标子控件对象指针。
 * @return 对应 slot 指针；未找到返回 NULL。
 */
static we_mask_group_child_slot_t *_mask_group_find_slot(we_mask_group_obj_t *obj, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return NULL;

    for (i = 0; i < WE_MASK_GROUP_CHILD_MAX; i++)
    {
        if (_MG_SLOT_USED(obj, i) && obj->child_slots[i].child == child)
            return &obj->child_slots[i];
    }

    return NULL;
}

/**
 * @brief 把对象从其当前所属链表（父容器 children_head 或顶层 obj_list_head）摘除。
 * @param obj 目标控件对象指针。
 * @return 无。
 * @note 与 group 的实现保持一致，供改挂父子关系前使用。
 */
static void _mask_group_detach_obj(we_obj_t *obj)
{
    we_obj_t *curr;
    we_obj_t *prev;

    if (obj == NULL || obj->lcd == NULL)
        return;

    if (obj->parent != NULL)
    {
        we_child_owner_t *parent = (we_child_owner_t *)obj->parent;
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
    else
    {
        curr = obj->lcd->obj_list_head;
        prev = NULL;
        while (curr != NULL)
        {
            if (curr == obj)
            {
                if (prev == NULL)
                    obj->lcd->obj_list_head = curr->next;
                else
                    prev->next = curr->next;
                break;
            }
            prev = curr;
            curr = curr->next;
        }
    }

    obj->next = NULL;
    obj->parent = NULL;
}

/**
 * @brief 按 slot 局部坐标叠加容器绝对坐标，刷新该子控件的屏幕绝对位置。
 * @param obj 控件对象指针。
 * @param slot 子控件槽位记录指针。
 * @return 无。
 */
static void _mask_group_update_child_abs(we_mask_group_obj_t *obj, we_mask_group_child_slot_t *slot)
{
    if (obj == NULL || slot == NULL ||
        !_MG_SLOT_USED(obj, (uint16_t)(slot - obj->child_slots)) || slot->child == NULL)
        return;

    we_obj_set_pos(slot->child,
                   (int16_t)(obj->base.x + slot->local_x),
                   (int16_t)(obj->base.y + slot->local_y));
}

/* --------------------------------------------------------------------------
 * 蒙版后处理
 * -------------------------------------------------------------------------- */

/* 一次蒙版合成所需的几何快照（口径与 box 的 _box_geo_t 一致） */
typedef struct
{
    int16_t  x, y, w, h;     /* 外轮廓矩形（= 容器矩形） */
    int16_t  wi, hi;         /* 内轮廓矩形宽高（内缩 bw；<=0 表示无内轮廓） */
    uint16_t r_out[4];       /* 各角外半径（已钳制） */
    uint16_t r_in[4];        /* 各角内半径（已钳制） */
    uint8_t  bw;             /* 边框厚度（已钳制） */
    uint16_t k;              /* 角落方块边长 */
    colour_t bd_eff;         /* 有效边框色（border 按容器 opacity 向背板收敛） */
} _mg_geo_t;

/**
 * @brief 构建蒙版几何快照：钳制各角半径/边框厚度，求内轮廓与角落方块边长 K。
 * @param obj 控件对象指针。
 * @param g 传出：几何快照。
 * @return 无。
 */
static void _mg_build_geo(const we_mask_group_obj_t *obj, _mg_geo_t *g)
{
    int16_t w = obj->base.w;
    int16_t h = obj->base.h;
    uint16_t half = (uint16_t)((w < h ? w : h) / 2);
    uint16_t k;
    uint8_t i;

    g->x = obj->base.x;
    g->y = obj->base.y;
    g->w = w;
    g->h = h;

    g->bw = obj->border_w;
    if ((uint16_t)g->bw > half)
        g->bw = (uint8_t)half;

    k = g->bw;
    for (i = 0U; i < 4U; i++)
    {
        uint16_t r = obj->corner_r[i];
        if (r > (uint16_t)(w / 2))
            r = (uint16_t)(w / 2);
        if (r > (uint16_t)(h / 2))
            r = (uint16_t)(h / 2);
        g->r_out[i] = r;
        if (r > k)
            k = r;
    }

    /* 内轮廓：内缩 bw 的同形轮廓（圆角减 bw；切角减 0.586·bw 保持等厚） */
    g->wi = (int16_t)(w - 2 * (int16_t)g->bw);
    g->hi = (int16_t)(h - 2 * (int16_t)g->bw);
    for (i = 0U; i < 4U; i++)
    {
        uint16_t r  = g->r_out[i];
        uint16_t in = (_MG_STYLE(obj, i) == (uint8_t)WE_MASK_GROUP_CORNER_ROUND)
                        ? (uint16_t)g->bw : _MG_CHAMFER_INSET(g->bw);
        uint16_t ri = (r > in) ? (uint16_t)(r - in) : 0U;
        if (g->wi <= 0 || g->hi <= 0)
            ri = 0U; /* 边框吃满整盒：无内轮廓 */
        else
        {
            if (ri > (uint16_t)(g->wi / 2))
                ri = (uint16_t)(g->wi / 2);
            if (ri > (uint16_t)(g->hi / 2))
                ri = (uint16_t)(g->hi / 2);
        }
        g->r_in[i] = ri;

        if ((uint16_t)(g->bw + ri) > k)
            k = (uint16_t)(g->bw + ri);
    }

    if (k > half)
        k = half;
    g->k = k;

    /* 有效边框色一帧只算一次：容器半透时边框向背板收敛 */
    g->bd_eff = we_colour_blend(obj->border_color, obj->backdrop, obj->opacity);
}

/**
 * @brief 渐变 pass：对（容器 ∩ 当前条带）区域做内容→背板的线性 alpha 渐隐。
 * @param obj 控件对象指针。
 * @param rx0/ry0/rx1/rx1 已与条带求交的处理区域（屏幕绝对坐标）。
 * @return 无。
 * @note 投影 p(x,y) = cos·(x-x0) + sin·(y-y0)（Q15），alpha 沿全矩形投影
 *       范围从 a0 线性过渡到 a1；步进用 int64 除法一次算好（Q16），
 *       内环每像素一次加法 + 混色。m=255 保留内容（跳过写入）。
 */
static void _mg_pass_gradient(const we_mask_group_obj_t *obj,
                              int16_t rx0, int16_t ry0, int16_t rx1, int16_t ry1)
{
    we_lcd_t *lcd = obj->base.lcd;
    int32_t acc_row = (int32_t)obj->grad_a0 << 16;
    int32_t step_x = 0;
    int32_t step_y = 0;
    int16_t px;
    int16_t py;
    uint16_t stride;
    colour_t *row;

    {
        int32_t c = we_cos(obj->grad_angle);
        int32_t s = we_sin(obj->grad_angle);
        int32_t pw = c * (int32_t)(obj->base.w - 1);
        int32_t ph = s * (int32_t)(obj->base.h - 1);
        int32_t pmin = (pw < 0 ? pw : 0) + (ph < 0 ? ph : 0);
        int32_t pmax = (pw > 0 ? pw : 0) + (ph > 0 ? ph : 0);
        int32_t span = pmax - pmin;
        int32_t da = (int32_t)obj->grad_a1 - (int32_t)obj->grad_a0;

        if (span > 0 && da != 0)
        {
            int32_t p00 = c * (int32_t)(rx0 - obj->base.x) + s * (int32_t)(ry0 - obj->base.y);

            step_x = (int32_t)((((int64_t)da * c) << 16) / span);
            step_y = (int32_t)((((int64_t)da * s) << 16) / span);
            acc_row += (int32_t)((((int64_t)da * (p00 - pmin)) << 16) / span);
        }
    }

    stride = lcd->pfb_width;
    row = lcd->pfb_gram
        + (uint32_t)(ry0 - (int16_t)lcd->pfb_y_start) * stride
        + (uint32_t)(rx0 - (int16_t)lcd->pfb_area.x0);

    for (py = ry0; py <= ry1; py++, row += stride, acc_row += step_y)
    {
        int32_t acc = acc_row;
        colour_t *p = row;

        for (px = rx0; px <= rx1; px++, p++, acc += step_x)
        {
            int32_t mv = acc >> 16;

            if (mv >= 250)
                continue; /* 内容完全保留 */
            if (mv <= 5)
            {
                we_store_color(p, obj->backdrop);
                continue;
            }
            *p = we_colour_blend(*p, obj->backdrop, (uint8_t)mv);
        }
    }
}

/**
 * @brief 圆角角落：对一个 K×K 方块执行"背板还原 + 边框环 + 内容保留"合成。
 * @param obj 控件对象指针。
 * @param g 几何快照。
 * @param bx/by 角落方块左上角（屏幕绝对坐标）。
 * @param q 角落象限（WE_MASK_QUADRANT_xx）。
 * @return 无。
 * @note 方块内平直区零 mask 开销；AA 区带边框时内外覆盖共享一遍子采样。
 *       内容层为当前 PFB 像素（渐变 pass 已先行作用于内容）。
 */
static void _mg_corner_round(const we_mask_group_obj_t *obj, const _mg_geo_t *g,
                             int16_t bx, int16_t by, uint8_t q)
{
    we_lcd_t *lcd = obj->base.lcd;
    int16_t cx0 = bx;
    int16_t cy0 = by;
    int16_t cx1 = (int16_t)(bx + (int16_t)g->k - 1);
    int16_t cy1 = (int16_t)(by + (int16_t)g->k - 1);
    int16_t x1  = (int16_t)(g->x + g->w - 1);
    int16_t y1  = (int16_t)(g->y + g->h - 1);
    uint16_t r  = g->r_out[q];
    uint16_t ri = g->r_in[q];
    int16_t sx;
    int16_t sy;
    uint8_t has_in;
    int16_t px;
    int16_t py;
    uint16_t stride;
    colour_t *row;

    /* 外圆角 r×r 外接方块左上角 */
    sx = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB)
           ? g->x : (int16_t)(x1 - (int16_t)r + 1);
    sy = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT)
           ? g->y : (int16_t)(y1 - (int16_t)r + 1);
    has_in = (uint8_t)((g->bw > 0U) && (g->wi > 0) && (g->hi > 0));

    if (cx0 < (int16_t)lcd->pfb_area.x0) cx0 = (int16_t)lcd->pfb_area.x0;
    if (cy0 < (int16_t)lcd->pfb_y_start) cy0 = (int16_t)lcd->pfb_y_start;
    if (cx1 > (int16_t)lcd->pfb_area.x1) cx1 = (int16_t)lcd->pfb_area.x1;
    if (cy1 > (int16_t)lcd->pfb_y_end) cy1 = (int16_t)lcd->pfb_y_end;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    stride = lcd->pfb_width;
    row = lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)lcd->pfb_y_start) * stride
        + (uint32_t)(cx0 - (int16_t)lcd->pfb_area.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        int16_t v = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT)
                      ? (int16_t)(py - g->y) : (int16_t)(y1 - py);
        colour_t *p = row;

        for (px = cx0; px <= cx1; px++, p++)
        {
            int16_t u = (q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB)
                          ? (int16_t)(px - g->x) : (int16_t)(x1 - px);
            uint8_t a_out;
            uint8_t a_in;

            if (u >= (int16_t)r || v >= (int16_t)r)
            {
                /* 圆角方块外的平直区：按内矩形边界分 边框/内容，零 mask 开销 */
                if (g->bw != 0U &&
                    !(has_in && u >= (int16_t)g->bw && v >= (int16_t)g->bw))
                {
                    we_store_color(p, g->bd_eff);
                }
                continue; /* 内容区：保留 */
            }

            /* 圆角 AA 区：带边框时内外覆盖共享一遍 4x4 子采样 */
            if (g->bw == 0U)
            {
                a_out = we_mask_quarter_circle_alpha(sx, sy, r, q, px, py);
                a_in  = a_out; /* 无边框：全部记作内容 */
            }
            else
            {
                a_out = we_mask_quarter_ring_alpha(sx, sy, r, ri, q, px, py, &a_in);
            }

            if (a_out == 0U)
            {
                we_store_color(p, obj->backdrop); /* 轮廓外：还原背板 */
                continue;
            }

            {
                uint8_t a_bd = (uint8_t)(a_out - a_in);
                colour_t base;

                if (a_in >= 250U && a_bd <= 5U)
                    continue; /* 内容完全保留 */

                /* 内容 over 背板（a_in 覆盖），再 边框 over 结果（a_bd 覆盖） */
                if (a_in >= 250U)
                    base = *p;
                else if (a_in <= 5U)
                    base = obj->backdrop;
                else
                    base = we_colour_blend(*p, obj->backdrop, a_in);

                if (a_bd >= 250U)
                    we_store_color(p, g->bd_eff);
                else if (a_bd > 5U)
                    *p = we_colour_blend(g->bd_eff, base, a_bd);
                else
                    *p = base;
            }
        }
    }
}

/**
 * @brief 把切角行扫描的 u 坐标区间 [ua, ub] 映射为像素列区间并钳制。
 * @param bx 角落方块左上角 X；k 方块边长。
 * @param left 非 0 表示左侧角落（u 随 px 递增），否则右侧（u 随 px 递减）。
 * @param cx0/cx1 本行已裁剪的列范围（屏幕绝对坐标）。
 * @param pxa/pxb 传出：像素列区间；*pxa > *pxb 表示区间为空。
 * @return 无。
 */
static void _mg_u_span(int16_t bx, uint16_t k, uint8_t left,
                       int16_t ua, int16_t ub, int16_t cx0, int16_t cx1,
                       int16_t *pxa, int16_t *pxb)
{
    if (ua < 0)
        ua = 0;
    if (ub > (int16_t)(k - 1U))
        ub = (int16_t)(k - 1U);
    if (ua > ub)
    {
        *pxa = 1;
        *pxb = 0;
        return;
    }

    if (left)
    {
        *pxa = (int16_t)(bx + ua);
        *pxb = (int16_t)(bx + ub);
    }
    else
    {
        *pxa = (int16_t)(bx + ((int16_t)(k - 1U) - ub));
        *pxb = (int16_t)(bx + ((int16_t)(k - 1U) - ua));
    }
    if (*pxa < cx0) *pxa = cx0;
    if (*pxb > cx1) *pxb = cx1;
}

/**
 * @brief 切角角落：按行分段对一个 K×K 方块做蒙版合成（无逐像素函数调用）。
 * @param obj 控件对象指针。
 * @param g 几何快照。
 * @param bx/by 角落方块左上角（屏幕绝对坐标）。
 * @param q 角落象限（WE_MASK_QUADRANT_xx）。
 * @return 无。
 * @note 以角顶点为原点的 (u,v) 坐标里，外切线为 u+v = r-1（该像素 alpha=128），
 *       内切线为 u'+v' = r_in-1（u'=u-bw, v'=v-bw）。每行按
 *       背板段 / 外 AA 像素 / 边框段 / 内 AA 像素 / 内容段 五段处理，
 *       内容段不写入（保留子控件像素）。
 */
static void _mg_corner_chamfer(const we_mask_group_obj_t *obj, const _mg_geo_t *g,
                               int16_t bx, int16_t by, uint8_t q)
{
    we_lcd_t *lcd = obj->base.lcd;
    int16_t cx0 = bx;
    int16_t cy0 = by;
    int16_t cx1 = (int16_t)(bx + (int16_t)g->k - 1);
    int16_t cy1 = (int16_t)(by + (int16_t)g->k - 1);
    int16_t y1  = (int16_t)(g->y + g->h - 1);
    uint8_t left = (uint8_t)(q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_LB);
    uint8_t top  = (uint8_t)(q == WE_MASK_QUADRANT_LT || q == WE_MASK_QUADRANT_RT);
    uint8_t has_in = (uint8_t)((g->bw > 0U) && (g->wi > 0) && (g->hi > 0));
    int16_t py;
    int16_t pxa;
    int16_t pxb;
    uint16_t stride;
    colour_t *row;

    if (cx0 < (int16_t)lcd->pfb_area.x0) cx0 = (int16_t)lcd->pfb_area.x0;
    if (cy0 < (int16_t)lcd->pfb_y_start) cy0 = (int16_t)lcd->pfb_y_start;
    if (cx1 > (int16_t)lcd->pfb_area.x1) cx1 = (int16_t)lcd->pfb_area.x1;
    if (cy1 > (int16_t)lcd->pfb_y_end) cy1 = (int16_t)lcd->pfb_y_end;
    if (cx0 > cx1 || cy0 > cy1)
        return;

    stride = lcd->pfb_width;
    row = lcd->pfb_gram
        + (uint32_t)(cy0 - (int16_t)lcd->pfb_y_start) * stride
        + (uint32_t)(cx0 - (int16_t)lcd->pfb_area.x0);

    for (py = cy0; py <= cy1; py++, row += stride)
    {
        int16_t v  = top ? (int16_t)(py - g->y) : (int16_t)(y1 - py);
        int16_t u0 = (int16_t)((int16_t)g->r_out[q] - 1 - v); /* 外切线 AA 像素 */
        int16_t in_aa = -1;                                   /* 内切线 AA 像素，<0 无 */
        int16_t fill_from;                                    /* 内容段起始 u */
        colour_t *p;
        int16_t px;

        /* --- 背板段 [0, u0-1]：轮廓外，整段还原背板 --- */
        _mg_u_span(bx, g->k, left, 0, (int16_t)(u0 - 1), cx0, cx1, &pxa, &pxb);
        if (pxa <= pxb)
        {
            p = row + (pxa - cx0);
            for (px = pxa; px <= pxb; px++, p++)
                we_store_color(p, obj->backdrop);
        }

        if (g->bw == 0U)
        {
            /* 无边框：外 AA 像素 = 内容/背板各半，其后整段内容（保留） */
            _mg_u_span(bx, g->k, left, u0, u0, cx0, cx1, &pxa, &pxb);
            if (pxa <= pxb)
            {
                p = row + (pxa - cx0);
                for (px = pxa; px <= pxb; px++, p++)
                    *p = we_colour_blend(*p, obj->backdrop, 128U);
            }
            continue;
        }

        if (!has_in || v < (int16_t)g->bw)
        {
            fill_from = (int16_t)g->k; /* 本行无内容段（全边框带） */
        }
        else
        {
            int16_t u1 = (int16_t)((int16_t)g->r_in[q] - 1 - (v - (int16_t)g->bw));
            if (u1 >= 0)
            {
                in_aa     = (int16_t)((int16_t)g->bw + u1);
                fill_from = (int16_t)(in_aa + 1);
            }
            else
            {
                fill_from = (int16_t)g->bw; /* 内切线已过，内容从内矩形左沿开始 */
            }
        }

        /* --- 外切线 AA 像素：边框/背板各半 --- */
        _mg_u_span(bx, g->k, left, u0, u0, cx0, cx1, &pxa, &pxb);
        if (pxa <= pxb)
        {
            p = row + (pxa - cx0);
            for (px = pxa; px <= pxb; px++, p++)
                *p = we_colour_blend(g->bd_eff, obj->backdrop, 128U);
        }

        /* --- 边框实心段 --- */
        _mg_u_span(bx, g->k, left, (int16_t)(u0 + 1),
                   (int16_t)((in_aa >= 0 ? in_aa : fill_from) - 1),
                   cx0, cx1, &pxa, &pxb);
        if (pxa <= pxb)
        {
            p = row + (pxa - cx0);
            for (px = pxa; px <= pxb; px++, p++)
                we_store_color(p, g->bd_eff);
        }

        /* --- 内切线 AA 像素：边框/内容各半 --- */
        if (in_aa >= 0)
        {
            _mg_u_span(bx, g->k, left, in_aa, in_aa, cx0, cx1, &pxa, &pxb);
            if (pxa <= pxb)
            {
                p = row + (pxa - cx0);
                for (px = pxa; px <= pxb; px++, p++)
                    *p = we_colour_blend(g->bd_eff, *p, 128U);
            }
        }
        /* --- 内容段 [fill_from, k-1]：保留，不写入 --- */
    }
}

/**
 * @brief 对（容器 ∩ 当前 PFB 条带）区域执行蒙版合成。
 * @param obj 控件对象指针。
 * @return 无。
 * @note 调用时 PFB 窗口必须已恢复为进入容器前的状态。
 *       顺序：渐变 pass（内容渐隐）→ 边框直条 → 四角合成。
 *       全部几何均为绝对坐标纯函数，脏矩形局部重绘可无缝拼接。
 */
static void _mask_group_apply_mask(we_mask_group_obj_t *obj)
{
    we_lcd_t *lcd = obj->base.lcd;
    _mg_geo_t g;
    uint8_t use_grad = (uint8_t)(obj->grad_type == WE_MASK_GRAD_LINEAR);
    int16_t x1;
    int16_t y1;
    int16_t rx0;
    int16_t ry0;
    int16_t rx1;
    int16_t ry1;

    _mg_build_geo(obj, &g);

    if (!use_grad && g.bw == 0U && g.k == 0U)
        return; /* 纯矩形裁剪：无后处理成本 */

    x1 = (int16_t)(g.x + g.w - 1);
    y1 = (int16_t)(g.y + g.h - 1);

    /* 与当前 PFB 条带求交 */
    rx0 = WE_MAX((int16_t)lcd->pfb_area.x0, g.x);
    ry0 = WE_MAX((int16_t)lcd->pfb_y_start, g.y);
    rx1 = WE_MIN((int16_t)lcd->pfb_area.x1, x1);
    ry1 = WE_MIN((int16_t)lcd->pfb_y_end, y1);
    if (rx0 > rx1 || ry0 > ry1)
        return;

    /* --- 1. 渐变 pass：内容整体渐隐（在边框/角落合成之前作用于内容层） --- */
    if (use_grad)
        _mg_pass_gradient(obj, rx0, ry0, rx1, ry1);

    /* --- 2. 边框直条：四条 K 段之间的直边整块覆盖（内容在边框内沿被裁剪） --- */
    if (g.bw > 0U)
    {
        int16_t kk = (int16_t)g.k;

        if ((int16_t)(g.w - 2 * kk) > 0)
        {
            int16_t xm = (int16_t)(g.x + kk);
            uint16_t wm = (uint16_t)(g.w - 2 * kk);

            we_fill_rect(lcd, xm, g.y, wm, g.bw, g.bd_eff, 255U);
            we_fill_rect(lcd, xm, (int16_t)(y1 - (int16_t)g.bw + 1), wm, g.bw,
                         g.bd_eff, 255U);
        }
        if ((int16_t)(g.h - 2 * kk) > 0)
        {
            int16_t ym = (int16_t)(g.y + kk);
            uint16_t hm = (uint16_t)(g.h - 2 * kk);

            we_fill_rect(lcd, g.x, ym, g.bw, hm, g.bd_eff, 255U);
            we_fill_rect(lcd, (int16_t)(x1 - (int16_t)g.bw + 1), ym, g.bw, hm,
                         g.bd_eff, 255U);
        }
    }

    /* --- 3. 四个 K×K 角落方块合成（圆角单遍子采样 / 切角行扫描） --- */
    if (g.k > 0U)
    {
        int16_t bxs[4];
        int16_t bys[4];
        uint8_t i;

        bxs[0] = g.x;
        bys[0] = g.y;
        bxs[1] = (int16_t)(x1 - (int16_t)g.k + 1);
        bys[1] = g.y;
        bxs[2] = g.x;
        bys[2] = (int16_t)(y1 - (int16_t)g.k + 1);
        bxs[3] = (int16_t)(x1 - (int16_t)g.k + 1);
        bys[3] = (int16_t)(y1 - (int16_t)g.k + 1);

        for (i = 0U; i < 4U; i++)
        {
            if (_MG_STYLE(obj, i) == (uint8_t)WE_MASK_GROUP_CORNER_CHAMFER)
                _mg_corner_chamfer(obj, &g, bxs[i], bys[i], i);
            else
                _mg_corner_round(obj, &g, bxs[i], bys[i], i);
        }
    }
}

/* --------------------------------------------------------------------------
 * 类回调
 * -------------------------------------------------------------------------- */

/**
 * @brief 控件绘制回调：子控件阶段（PFB 收窄 + 透明度级联）+ 蒙版后处理阶段。
 * @param ptr 回调透传对象指针。
 * @return 无。
 */
static void _mask_group_draw_cb(void *ptr)
{
    we_mask_group_obj_t *obj = (we_mask_group_obj_t *)ptr;
    we_lcd_t *lcd = obj->base.lcd;

    if (obj->opacity == 0U)
        return;

    {
        we_area_t old_pfb_area = lcd->pfb_area;
        uint16_t old_y_start = lcd->pfb_y_start;
        uint16_t old_y_end = lcd->pfb_y_end;
        colour_t *old_gram = lcd->pfb_gram;
        uint8_t old_scale = lcd->opa_scale;
        int16_t new_x0 = WE_MAX(old_pfb_area.x0, obj->base.x);
        int16_t new_y0 = WE_MAX((int16_t)old_y_start, obj->base.y);
        int16_t new_x1 = WE_MIN(old_pfb_area.x1, (int16_t)(obj->base.x + obj->base.w - 1));
        int16_t new_y1 = WE_MIN((int16_t)old_y_end, (int16_t)(obj->base.y + obj->base.h - 1));

        /* 子控件透明度级联：把本容器 opacity 乘进全局乘子（嵌套自动链乘） */
        lcd->opa_scale = we_opa_apply(lcd, obj->opacity);

        if (new_x0 <= new_x1 && new_y0 <= new_y1)
        {
            we_obj_t *child = obj->children_head;

            lcd->pfb_area.x0 = new_x0;
            lcd->pfb_area.x1 = new_x1;
            lcd->pfb_y_start = (uint16_t)new_y0;
            lcd->pfb_y_end = (uint16_t)new_y1;
            lcd->pfb_gram = old_gram + (new_y0 - (int16_t)old_y_start) * lcd->pfb_width +
                            (new_x0 - old_pfb_area.x0);

            while (child != NULL)
            {
                if (child->class_p && child->class_p->draw_cb &&
                    (child->x + child->w > lcd->pfb_area.x0) && (child->x <= lcd->pfb_area.x1) &&
                    (child->y + child->h > (int16_t)lcd->pfb_y_start) &&
                    (child->y <= (int16_t)lcd->pfb_y_end))
                {
                    child->class_p->draw_cb(child);
                }
                child = child->next;
            }
        }

        lcd->opa_scale = old_scale;
        lcd->pfb_area = old_pfb_area;
        lcd->pfb_y_start = old_y_start;
        lcd->pfb_y_end = old_y_end;
        lcd->pfb_gram = old_gram;
    }

    /* 蒙版后处理：圆角裁剪 + 渐变（radius=0 且无渐变时内部直接返回） */
    _mask_group_apply_mask(obj);
}

/**
 * @brief 在容器内查找命中坐标的可交互子控件（后挂者层级更高，取最后命中者）。
 * @param obj 控件对象指针。
 * @param x 屏幕绝对 X 坐标。
 * @param y 屏幕绝对 Y 坐标。
 * @return 命中的子控件指针；无命中返回 NULL。
 */
static we_obj_t *_mask_group_hit_child(we_mask_group_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_t *child = obj->children_head;
    we_obj_t *target = NULL;

    while (child != NULL)
    {
        if (child->class_p != NULL && child->class_p->event_cb != NULL &&
            x >= child->x && x < (child->x + child->w) &&
            y >= child->y && y < (child->y + child->h))
        {
            target = child;
        }
        child = child->next;
    }
    return target;
}

/**
 * @brief 容器事件回调：按压时锁定子控件，后续触摸序列事件按序转发（同 group）。
 * @param ptr 回调透传对象指针。
 * @param event 输入事件类型。
 * @param data 输入设备事件数据指针。
 * @return 1 已处理，0 未处理。
 */
static uint8_t _mask_group_event_cb(void *ptr, we_event_t event, we_indev_data_t *data)
{
    we_mask_group_obj_t *obj = (we_mask_group_obj_t *)ptr;
    we_obj_t *child;

    if (obj == NULL || data == NULL || obj->opacity == 0U)
        return 0U; /* 完全透明（淡出隐藏）的容器不拦截输入 */

    if (event == WE_EVENT_PRESSED)
    {
        child = _mask_group_hit_child(obj, data->x, data->y);
        obj->last_pressed_child = child;
        if (child != NULL)
        {
            child->class_p->event_cb(child, WE_EVENT_PRESSED, data);
            return 1U;
        }
        /* 未命中交互子控件时返回 0：让外层容器把这次按压用于拖拽等手势 */
        return 0U;
    }

    /* 仅转发触摸序列事件；SCROLLED 等广播事件不属于转发范围 */
    if (event != WE_EVENT_RELEASED && event != WE_EVENT_STAY && event != WE_EVENT_CLICKED &&
        event != WE_EVENT_SWIPE_LEFT && event != WE_EVENT_SWIPE_RIGHT &&
        event != WE_EVENT_SWIPE_UP && event != WE_EVENT_SWIPE_DOWN)
        return 0U;

    child = obj->last_pressed_child;
    if (child == NULL)
        return 0U;
    if (child->class_p == NULL || child->class_p->event_cb == NULL)
    {
        /* 子控件已在按压期间被删除/失效，丢弃引用 */
        obj->last_pressed_child = NULL;
        return 0U;
    }

    if (event == WE_EVENT_CLICKED)
    {
        /* 点击需复核释放点仍落在原子控件上，按下后拖出再松手不触发 */
        if (data->x >= child->x && data->x < (child->x + child->w) &&
            data->y >= child->y && data->y < (child->y + child->h))
            child->class_p->event_cb(child, WE_EVENT_CLICKED, data);
        obj->last_pressed_child = NULL;
        return 1U;
    }

    child->class_p->event_cb(child, event, data);
    return 1U;
}

/**
 * @brief 容器移动回调：平移外框的同时按局部坐标同步全部子控件。
 * @param ptr 回调透传对象指针。
 * @param new_x 新的左上角 X 坐标。
 * @param new_y 新的左上角 Y 坐标。
 * @return 无。
 */
static void _mask_group_set_pos_cb(void *ptr, int16_t new_x, int16_t new_y)
{
    we_mask_group_obj_t *obj = (we_mask_group_obj_t *)ptr;

    we_obj_invalidate((we_obj_t *)obj);
    obj->base.x = new_x;
    obj->base.y = new_y;
    we_mask_group_relayout(obj); /* 按 slot 局部坐标刷新全部子控件绝对位置 */
    we_obj_invalidate((we_obj_t *)obj);
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

void we_mask_group_obj_init(we_mask_group_obj_t *obj, we_lcd_t *lcd,
                            int16_t x, int16_t y, int16_t w, int16_t h)
{
    static const we_class_t _mask_group_class = {
        .draw_cb = _mask_group_draw_cb,
        .event_cb = _mask_group_event_cb,
        .set_pos_cb = _mask_group_set_pos_cb
    };
    uint16_t i;

    if (obj == NULL || lcd == NULL)
        return;

    obj->base.lcd = lcd;
    obj->base.x = x;
    obj->base.y = y;
    obj->base.w = w;
    obj->base.h = h;
    obj->base.class_p = &_mask_group_class;
    obj->base.next = NULL;
    obj->base.parent = NULL;
    obj->children_head = NULL;

    obj->opacity = 255U;
    obj->backdrop = lcd->bg_color;
    obj->corner_styles = 0U;
    obj->corner_r[0] = 0U;
    obj->corner_r[1] = 0U;
    obj->corner_r[2] = 0U;
    obj->corner_r[3] = 0U;
    obj->border_color = lcd->bg_color;
    obj->border_w = 0U;
    obj->grad_type = (uint8_t)WE_MASK_GRAD_NONE;
    obj->grad_angle = 0;
    obj->grad_a0 = 255U;
    obj->grad_a1 = 255U;
    obj->last_pressed_child = NULL;

    obj->slot_used_mask = 0U; /* 全部槽位空闲 */

    we_obj_attach_to_lcd(lcd, (we_obj_t *)obj);
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mask_group_obj_delete(we_mask_group_obj_t *obj)
{
    we_obj_t *child;
    we_obj_t *next;
    uint16_t i;

    if (obj == NULL || obj->base.lcd == NULL)
        return;

    child = obj->children_head;
    while (child != NULL)
    {
        next = child->next;
        we_obj_delete(child);
        child = next;
    }

    obj->children_head = NULL;
    obj->last_pressed_child = NULL;
    obj->slot_used_mask = 0U; /* 全部槽位空闲 */

    we_obj_delete((we_obj_t *)obj);
}

void we_mask_group_add_child(we_mask_group_obj_t *obj, we_obj_t *child)
{
    uint16_t i;

    if (obj == NULL || child == NULL)
        return;
    if (child == (we_obj_t *)obj)
        return;
    if (child->lcd != obj->base.lcd)
        return;
    if (_mask_group_find_slot(obj, child) != NULL)
        return;

    for (i = 0; i < WE_MASK_GROUP_CHILD_MAX; i++)
    {
        if (!_MG_SLOT_USED(obj, i))
        {
            _mask_group_detach_obj(child);
            child->next = NULL;
            child->parent = (we_obj_t *)obj;

            we_obj_append_to_list(&obj->children_head, child);

            obj->child_slots[i].child = child;
            obj->child_slots[i].local_x = 0;
            obj->child_slots[i].local_y = 0;
            _MG_SLOT_SET(obj, i);
            _mask_group_update_child_abs(obj, &obj->child_slots[i]);
            return;
        }
    }
}

void we_mask_group_remove_child(we_mask_group_obj_t *obj, we_obj_t *child)
{
    we_mask_group_child_slot_t *slot = _mask_group_find_slot(obj, child);

    if (slot == NULL)
        return;

    _mask_group_detach_obj(child);
    _MG_SLOT_CLR(obj, (uint16_t)(slot - obj->child_slots));
    slot->child = NULL;

    /* 被移除的子控件若正处于按压转发状态，同步丢弃引用 */
    if (obj->last_pressed_child == child)
        obj->last_pressed_child = NULL;
}

void we_mask_group_set_child_pos(we_mask_group_obj_t *obj, we_obj_t *child,
                                 int16_t local_x, int16_t local_y)
{
    we_mask_group_child_slot_t *slot = _mask_group_find_slot(obj, child);

    if (slot == NULL)
        return;

    slot->local_x = local_x;
    slot->local_y = local_y;
    _mask_group_update_child_abs(obj, slot);
}

void we_mask_group_relayout(we_mask_group_obj_t *obj)
{
    uint16_t i;

    if (obj == NULL)
        return;

    for (i = 0; i < WE_MASK_GROUP_CHILD_MAX; i++)
    {
        if (_MG_SLOT_USED(obj, i))
            _mask_group_update_child_abs(obj, &obj->child_slots[i]);
    }
}

void we_mask_group_set_opacity(we_mask_group_obj_t *obj, uint8_t opacity)
{
    if (obj == NULL || obj->opacity == opacity)
        return;

    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
    obj->opacity = opacity;
    if (obj->opacity > 0U)
        we_obj_invalidate((we_obj_t *)obj);
}

void we_mask_group_set_radius(we_mask_group_obj_t *obj, uint16_t radius)
{
    uint8_t r8;
    uint8_t i;

    if (obj == NULL)
        return;

    r8 = (radius > 255U) ? 255U : (uint8_t)radius;
    if (obj->corner_styles == 0U &&
        obj->corner_r[0] == r8 && obj->corner_r[1] == r8 &&
        obj->corner_r[2] == r8 && obj->corner_r[3] == r8)
        return;

    obj->corner_styles = 0U; /* 四角全部圆角 */
    for (i = 0U; i < 4U; i++)
        obj->corner_r[i] = r8;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mask_group_set_corner(we_mask_group_obj_t *obj, we_mask_group_corner_idx_t idx,
                              we_mask_group_corner_style_t style, uint16_t r)
{
    uint8_t r8;
    uint8_t shift;
    uint8_t new_styles;

    if (obj == NULL || (uint8_t)idx > (uint8_t)WE_MASK_GROUP_RB)
        return;

    r8 = (r > 255U) ? 255U : (uint8_t)r;
    shift = (uint8_t)((uint8_t)idx * 2U);
    new_styles = (uint8_t)((obj->corner_styles & (uint8_t)~(0x3U << shift)) |
                           (uint8_t)(((uint8_t)style & 0x3U) << shift));

    if (obj->corner_styles == new_styles && obj->corner_r[idx] == r8)
        return;

    obj->corner_styles = new_styles;
    obj->corner_r[idx] = r8;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mask_group_set_border(we_mask_group_obj_t *obj, colour_t color, uint8_t width)
{
    if (obj == NULL)
        return;
    if (obj->border_w == width && _mg_colour_eq(obj->border_color, color))
        return;

    obj->border_color = color;
    obj->border_w = width;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mask_group_set_backdrop(we_mask_group_obj_t *obj, colour_t backdrop)
{
    if (obj == NULL || _mg_colour_eq(obj->backdrop, backdrop))
        return;

    obj->backdrop = backdrop;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mask_group_set_gradient(we_mask_group_obj_t *obj, int16_t angle,
                                uint8_t a0, uint8_t a1)
{
    if (obj == NULL)
        return;
    if (obj->grad_type == (uint8_t)WE_MASK_GRAD_LINEAR &&
        obj->grad_angle == angle && obj->grad_a0 == a0 && obj->grad_a1 == a1)
        return;

    obj->grad_type = (uint8_t)WE_MASK_GRAD_LINEAR;
    obj->grad_angle = angle;
    obj->grad_a0 = a0;
    obj->grad_a1 = a1;
    we_obj_invalidate((we_obj_t *)obj);
}

void we_mask_group_clear_gradient(we_mask_group_obj_t *obj)
{
    if (obj == NULL || obj->grad_type == (uint8_t)WE_MASK_GRAD_NONE)
        return;

    obj->grad_type = (uint8_t)WE_MASK_GRAD_NONE;
    we_obj_invalidate((we_obj_t *)obj);
}
