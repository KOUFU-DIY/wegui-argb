/*
 * WE-GUI 核心引擎：多策略智能脏矩形管理器 (Dirty Rectangle Manager)
 * 包含：视口裁剪、面积启发式合并、互斥切割、全局最优压缩
 */

#include "we_gui_driver.h" // 确保你的头文件里包含了 WE_CFG_DIRTY_STRATEGY 等宏定义和 we_dirty_mgr_t 结构体

/* =========================================================================
 * 1. 基础几何辅助函数 (各策略共用)
 * ========================================================================= */

/**
 * @brief 计算矩形面积
 * @param r 传入：矩形指针
 * @return 矩形面积（像素数）
 */
static uint32_t rect_area(we_rect_t *r) { return (uint32_t)(r->x1 - r->x0 + 1) * (r->y1 - r->y0 + 1); }

/**
 * @brief 计算两个矩形的并集包围盒
 * @param r1 传入：矩形 1 指针
 * @param r2 传入：矩形 2 指针
 * @return 并集后的包围矩形
 */
static we_rect_t get_union_rect(we_rect_t *r1, we_rect_t *r2)
{
    we_rect_t res;
    res.x0 = (r1->x0 < r2->x0) ? r1->x0 : r2->x0;
    res.y0 = (r1->y0 < r2->y0) ? r1->y0 : r2->y0;
    res.x1 = (r1->x1 > r2->x1) ? r1->x1 : r2->x1;
    res.y1 = (r1->y1 > r2->y1) ? r1->y1 : r2->y1;
    return res;
}

/**
 * @brief 判断矩形是否有效（宽高均非负）
 * @param r 传入：矩形指针
 * @return 1 表示有效，0 表示为空
 */
static uint8_t rect_is_valid(we_rect_t *r)
{
    return (r->x0 <= r->x1 && r->y0 <= r->y1) ? 1U : 0U;
}

/**
 * @brief 判断一个矩形是否被另一个矩形完全包含
 * @param outer 传入：外层矩形指针
 * @param inner 传入：内层矩形指针
 * @return 1 表示完全包含，0 表示否
 */
static uint8_t rect_contains(we_rect_t *outer, we_rect_t *inner)
{
    return (outer->x0 <= inner->x0 && outer->y0 <= inner->y0 && outer->x1 >= inner->x1 && outer->y1 >= inner->y1) ? 1U : 0U;
}

/**
 * @brief 计算两个矩形的交集
 * @param a 传入：矩形 A 指针
 * @param b 传入：矩形 B 指针
 * @param out 传出：交集矩形
 * @return 1 表示存在交集，0 表示无交集
 */
static uint8_t rect_intersect(we_rect_t *a, we_rect_t *b, we_rect_t *out)
{
    out->x0 = (a->x0 > b->x0) ? a->x0 : b->x0;
    out->y0 = (a->y0 > b->y0) ? a->y0 : b->y0;
    out->x1 = (a->x1 < b->x1) ? a->x1 : b->x1;
    out->y1 = (a->y1 < b->y1) ? a->y1 : b->y1;
    return rect_is_valid(out);
}

/**
 * @brief 对单端被覆盖的矩形执行缩短裁剪
 * @param base 传入：用于裁剪的已存在矩形
 * @param target 传入传出：待缩短的目标矩形
 * @return 1 表示发生了裁剪，0 表示未变化
 */
static uint8_t rect_trim_one_side(we_rect_t *base, we_rect_t *target)
{
    we_rect_t inter;

    if (!rect_intersect(base, target, &inter))
        return 0U;

    if (rect_contains(base, target))
    {
        target->x1 = (int16_t)(target->x0 - 1);
        target->y1 = (int16_t)(target->y0 - 1);
        return 1U;
    }

    if (inter.y0 == target->y0 && inter.y1 == target->y1)
    {
        if (inter.x0 == target->x0)
        {
            target->x0 = (int16_t)(inter.x1 + 1);
            return 1U;
        }
        if (inter.x1 == target->x1)
        {
            target->x1 = (int16_t)(inter.x0 - 1);
            return 1U;
        }
    }

    if (inter.x0 == target->x0 && inter.x1 == target->x1)
    {
        if (inter.y0 == target->y0)
        {
            target->y0 = (int16_t)(inter.y1 + 1);
            return 1U;
        }
        if (inter.y1 == target->y1)
        {
            target->y1 = (int16_t)(inter.y0 - 1);
            return 1U;
        }
    }

    return 0U;
}

/* =========================================================================
 * 2. 高阶调度引擎 (策略 2 用)
 * ========================================================================= */
#if (WE_CFG_DIRTY_STRATEGY >= 2)
/**
 * @brief 智能添加脏矩形并执行必要合并
 * @param mgr 传入：脏矩形管理器指针
 * @param new_r 传入：新提交的脏矩形
 * @return 无
 */
static void we_dirty_add_rect(we_dirty_mgr_t *mgr, we_rect_t *new_r)
{
    uint32_t new_area;

    for (uint8_t i = 0; i < mgr->count;)
    {
        if (rect_contains(&mgr->rects[i], new_r))
        {
            return;
        }

        if (rect_contains(new_r, &mgr->rects[i]))
        {
            mgr->rects[i] = mgr->rects[mgr->count - 1];
            mgr->count--;
            continue;
        }

        if (rect_trim_one_side(&mgr->rects[i], new_r))
        {
            if (!rect_is_valid(new_r))
                return;
        }

        i++;
    }

    new_area = rect_area(new_r);

    // =========================================================
    // 1. 尝试【无损愈合/吸收】(数学降维公式)
    // =========================================================
    for (uint8_t i = 0; i < mgr->count; i++)
    {
        we_rect_t u = get_union_rect(&mgr->rects[i], new_r);
        if (rect_area(&u) <= rect_area(&mgr->rects[i]) + new_area)
        {
            mgr->rects[i] = u;
            return; // 吸收成功，直接退出！
        }
    }

    // =========================================================
    // 2. 坑位未满，直接加入
    // =========================================================
    if (mgr->count < WE_CFG_DIRTY_MAX_NUM)
    {
        mgr->rects[mgr->count++] = *new_r;
        return;
    }

    // =========================================================
    // 3. ?? 坑位爆满！触发【全局最优融合 (Global Optimal Compact)】
    // 将 new_r 也拉进来，5个人一起相亲，寻找合并浪费最小的“天选组合”
    // =========================================================
    uint8_t best_i = 0, best_j = 1;
    int32_t min_waste = 0x7FFFFFFF;
    we_rect_t best_union;

    // 构建虚拟数组：把 new_r 放在最后
    we_rect_t v_rects[WE_CFG_DIRTY_MAX_NUM + 1];
    for (uint8_t i = 0; i < WE_CFG_DIRTY_MAX_NUM; i++)
        v_rects[i] = mgr->rects[i];
    v_rects[WE_CFG_DIRTY_MAX_NUM] = *new_r;

    // O(N^2) 遍历，找出最小代价组合
    for (uint8_t i = 0; i < WE_CFG_DIRTY_MAX_NUM; i++)
    {
        for (uint8_t j = i + 1; j <= WE_CFG_DIRTY_MAX_NUM; j++)
        { // ?? 注意这里是 j <= MAX_NUM
            we_rect_t u = get_union_rect(&v_rects[i], &v_rects[j]);
            int32_t waste = (int32_t)rect_area(&u) - (int32_t)rect_area(&v_rects[i]) - (int32_t)rect_area(&v_rects[j]);

            if (waste < min_waste)
            {
                min_waste = waste;
                best_i = i;
                best_j = j;
                best_union = u;
            }
        }
    }

    // 无论如何，best_i 必然是原来的某个旧矩形。将其更新为融合后的大框。
    mgr->rects[best_i] = best_union;

    // 接下来处理空洞与 new_r 的归宿
    if (best_j < WE_CFG_DIRTY_MAX_NUM)
    {
        // 如果天选组合是两个【旧矩形】，说明它们融为一体腾出了 best_j 这个位置！
        // 用数组末尾的旧矩形去填补这个坑，然后把 new_r 稳稳地放在末尾。
        mgr->rects[best_j] = mgr->rects[WE_CFG_DIRTY_MAX_NUM - 1];
        mgr->rects[WE_CFG_DIRTY_MAX_NUM - 1] = *new_r;
    }
    else
    {
        // 如果 best_j == WE_CFG_DIRTY_MAX_NUM，说明是【new_r】主动牺牲了自己，
        // 它完美融入了旧矩形 best_i 中。数组的坑位没有任何变动，直接结束！
    }
}
#endif

/* =========================================================================
 * 4. 脏矩形 API 接口层
 * ========================================================================= */

/**
 * @brief 初始化脏矩形管理器
 * @param mgr 传入：脏矩形管理器指针
 * @return 无
 */
void we_dirty_init(we_dirty_mgr_t *mgr) { mgr->count = 0; }

/**
 * @brief 清空脏矩形队列
 * @param mgr 传入：脏矩形管理器指针
 * @return 无
 */
void we_dirty_clear(we_dirty_mgr_t *mgr) { mgr->count = 0; }

/**
 * @brief 取出下一个待处理脏矩形
 * @param mgr 传入：脏矩形管理器指针
 * @param out_rect 传出：当前迭代位置对应的脏矩形
 * @param iterator 传入传出：迭代器位置
 * @return 1 表示成功取到，0 表示已无更多脏矩形
 */
uint8_t we_dirty_get_next(we_dirty_mgr_t *mgr, we_rect_t *out_rect, uint8_t *iterator)
{
    if (*iterator < mgr->count)
    {
        *out_rect = mgr->rects[*iterator];
        (*iterator)++;
        return 1;
    }
    return 0;
}

/**
 * @brief 提交一个局部重绘请求并按策略登记脏区
 * @param mgr 传入：脏矩形管理器指针
 * @param x 传入：请求区域左上角 X 坐标
 * @param y 传入：请求区域左上角 Y 坐标
 * @param w 传入：请求区域宽度
 * @param h 传入：请求区域高度
 * @return 无
 */
void we_dirty_invalidate(we_dirty_mgr_t *mgr, int16_t x, int16_t y, int16_t w, int16_t h)
{
    if (w <= 0 || h <= 0)
        return;

    // --- 终极防线：完全越界剔除 ---
    int16_t sw = (int16_t)mgr->screen_w;
    int16_t sh = (int16_t)mgr->screen_h;
    if (x >= sw || x + w <= 0 || y >= sh || y + h <= 0)
        return;

    // --- 局部越界裁剪：砍掉超出屏幕边缘的部分 ---
    int16_t x0 = (x < 0) ? 0 : x;
    int16_t y0 = (y < 0) ? 0 : y;
    int16_t x1 = (x + w - 1 > sw - 1) ? sw - 1 : x + w - 1;
    int16_t y1 = (y + h - 1 > sh - 1) ? sh - 1 : y + h - 1;

    we_rect_t new_r = {x0, y0, x1, y1};

#if (WE_CFG_DIRTY_STRATEGY == 0)
    // 策略 0：全局重绘
    mgr->count = 1;
    mgr->rects[0] = (we_rect_t){0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1};

#elif (WE_CFG_DIRTY_STRATEGY == 1)
    // 策略 1：单面包围盒
    if (mgr->count == 0)
    {
        mgr->rects[0] = new_r;
        mgr->count = 1;
    }
    else
    {
        mgr->rects[0] = get_union_rect(&mgr->rects[0], &new_r);
    }

#elif (WE_CFG_DIRTY_STRATEGY == 2)
    // 策略 2：基于极简面积运算的智能合并
    we_dirty_add_rect(mgr, &new_r);
#endif
}
