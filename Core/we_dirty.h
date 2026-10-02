#ifndef WE_DIRTY_H
#define WE_DIRTY_H

/* --------------------------------------------------------------------------
 * 脏矩形管理器（内核内部使用）：控件标脏请走 we_obj_invalidate 系列，
 * 它们会先按父容器裁剪再交给这里。
 * -------------------------------------------------------------------------- */

#include "we_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 初始化脏矩形管理器
     * @param mgr 传入：待初始化的脏矩形管理器指针
     * @return 无
     */
    void we_dirty_init(we_dirty_mgr_t *mgr);
    /**
     * @brief 登记一个脏矩形区域
     * @param mgr 传入：脏矩形管理器指针
     * @param x 传入：区域左上角 X 坐标
     * @param y 传入：区域左上角 Y 坐标
     * @param w 传入：区域宽度
     * @param h 传入：区域高度
     * @return 无
     */
    void we_dirty_invalidate(we_dirty_mgr_t *mgr, int16_t x, int16_t y, int16_t w, int16_t h);
    /**
     * @brief 登记一个矩形脏区，但排除其中一个安全空洞矩形
     * @param mgr 传入：脏矩形管理器指针
     * @param x 传入：整体区域左上角 X 坐标
     * @param y 传入：整体区域左上角 Y 坐标
     * @param w 传入：整体区域宽度
     * @param h 传入：整体区域高度
     * @param ex 传入：排除区域左上角 X 坐标
     * @param ey 传入：排除区域左上角 Y 坐标
     * @param ew 传入：排除区域宽度
     * @param eh 传入：排除区域高度
     * @return 无
     * @note 该函数只在新增脏区时把 bbox-hole 拆成最多 4 个矩形提交，
     *       不会从已有 dirty list 中删除任何区域。
     */
    void we_dirty_invalidate_exclude(we_dirty_mgr_t *mgr, int16_t x, int16_t y, int16_t w, int16_t h,
                                     int16_t ex, int16_t ey, int16_t ew, int16_t eh);
    /**
     * @brief 取出下一个待刷新的脏矩形
     * @param mgr 传入：脏矩形管理器指针
     * @param out_rect 传出：取出的脏矩形结果
     * @param iterator 传入传出：遍历迭代器
     * @return 1 表示成功取到脏矩形，0 表示已经遍历结束
     */
    uint8_t we_dirty_get_next(we_dirty_mgr_t *mgr, we_rect_t *out_rect, uint8_t *iterator);
    /**
     * @brief 清空所有脏矩形
     * @param mgr 传入：脏矩形管理器指针
     * @return 无
     */
    void we_dirty_clear(we_dirty_mgr_t *mgr);

#ifdef __cplusplus
}
#endif

#endif /* WE_DIRTY_H */
