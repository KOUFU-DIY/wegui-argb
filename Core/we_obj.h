#ifndef WE_OBJ_H
#define WE_OBJ_H

/* --------------------------------------------------------------------------
 * 对象树：挂接/摘链/改挂父子、几何修改、标脏、删除与引用回收、
 * 顶层链与模态。
 * -------------------------------------------------------------------------- */

#include "we_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 修改对象绝对坐标
     * @param obj 传入：目标对象指针
     * @param new_x 传入：新的 X 坐标
     * @param new_y 传入：新的 Y 坐标
     * @return 无
     */
    void we_obj_set_pos(we_obj_t *obj, int16_t new_x, int16_t new_y);
    /**
     * @brief 修改对象包围盒尺寸（左上角不动）
     * @param obj 传入：目标对象指针
     * @param new_w 传入：新的宽度（像素）
     * @param new_h 传入：新的高度（像素）
     * @return 无
     * @note 与 we_obj_set_pos 同款：新旧区域与焦点光标环的标脏都由内核负责，
     *       控件改尺寸走这里即可，不必自己算环的几何。
     */
    void we_obj_set_size(we_obj_t *obj, int16_t new_w, int16_t new_h);
    /**
     * @brief 将对象追加到指定对象链表尾部
     * @param head_p 传入：链表头指针地址
     * @param obj 传入：待追加对象
     * @return 无
     */
    void we_obj_append_to_list(we_obj_t **head_p, we_obj_t *obj);
    /**
     * @brief 将对象追加到 LCD 顶层对象链表尾部
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param obj 传入：待追加对象
     * @return 无
     */
    void we_obj_attach_to_lcd(we_lcd_t *lcd, we_obj_t *obj);
    /**
     * @brief 把对象挂到 LCD 顶层链表（保证绘制在普通层与焦点光标之上）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param obj 传入：目标对象（可来自普通层：会先摘链再挂入）
     * @return 无
     * @note 顶层内部仍按链序绘制（后挂者更靠上）。toast/msgbox 用它置顶：
     *       不打乱普通层的用户 Z 序，也不会被后来的 bring_to_front 盖住。
     *       WE_CFG_ENABLE_TOP_LAYER=0 裁剪档下本函数退化为挂到普通层链尾
     *       （未挂链的对象也会挂上；失去"保证置顶"，接口保持可链接）。
     */
    void we_obj_attach_to_top(we_lcd_t *lcd, we_obj_t *obj);
    /**
     * @brief 声明一个顶层对象为模态（弹层语义：吞下全部输入与按键）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param obj 传入：模态对象（应已挂顶层链）
     * @return 无
     * @note 模态期间：触摸命中限定顶层链，未命中即视为命中模态对象本身
     *       （点外收起逻辑留在控件事件回调里）；语义键按下沿以裸键值、
     *       松开沿以 键值|WE_KEY_RELEASE_FLAG 直送其 event_cb。
     *       已有旧模态时先向旧模态发 WE_EVENT_MODAL_CLOSE（互斥策略——
     *       模态之间默认替换，非模态顶层对象如 toast 不受影响可共存）。
     *       WE_CFG_ENABLE_TOP_LAYER=0 裁剪档下 modal 三函数退化为空 stub
     *       （we_modal_get 恒 NULL），弹层失去吞键与全屏命中语义。
     */
    void we_modal_open(we_lcd_t *lcd, we_obj_t *obj);
    /**
     * @brief 解除模态（owner 匹配才生效；不摘对象、不标脏，由调用方收尾）
     */
    void we_modal_close(we_lcd_t *lcd, we_obj_t *obj);
    /**
     * @brief 查询当前模态对象（NULL = 无模态）
     */
    we_obj_t *we_modal_get(we_lcd_t *lcd);
    /**
     * @brief 把对象从其当前所属链表摘出（父容器 children_head 或 LCD 顶层链表）
     * @param obj 传入：目标对象指针
     * @return 无
     * @note 摘链后清空 next 与 parent，但保留 lcd / class_p，对象仍然可用——
     *       供改挂父子关系、或把对象挂上顶层链之前调用。
     *       不标脏：调用方通常紧接着重新挂链或自行标脏。
     */
    void we_obj_detach(we_obj_t *obj);
    /**
     * @brief 改挂对象的父子关系（先摘链，再挂到新宿主链表尾部）
     * @param obj 传入：目标对象指针
     * @param parent 传入：新父容器；NULL 表示挂回 LCD 顶层链表
     * @return 无
     * @note parent 必须是带 WE_CLASS_FLAG_CHILD_OWNER 的复合容器，否则本调用
     *       退化为“挂回顶层”，不会把对象挂到一个没有 children_head 的对象上。
     *       只维护链表与 parent 指针；容器自身的槽位表与局部坐标由容器的
     *       add_child 负责。
     */
    void we_obj_set_parent(we_obj_t *obj, we_obj_t *parent);
    /**
     * @brief 删除一个对象
     * @param obj 传入：待删除对象指针
     * @return 无
     * @note 唯一删除入口：先回调 class_p->delete_cb，再后序递归删除复合容器
     *       的子控件，最后由内核统一回收按压 / 焦点 / 弹层 / 动画节点四类引用。
     */
    void we_obj_delete(we_obj_t *obj);
    /**
     * @brief 将对象提升到同级链表最前端（最后绘制）
     * @param obj 传入：目标对象指针
     * @return 无
     * @note 在对象当前所在的链内重排：父容器 children_head、LCD 顶层链
     *       （toast/msgbox 等）或普通层链；不会把对象换到别的链上。
     */
    void we_obj_bring_to_front(we_obj_t *obj);
    /**
     * @brief 按对象当前区域执行智能标脏
     * @param obj 传入：目标对象指针
     * @return 无
     * @note 标脏区域会沿 parent 链逐层裁剪，避免越界刷新。
     */
    void we_obj_invalidate(we_obj_t *obj);
    /**
     * @brief 按对象局部区域执行智能标脏
     * @param obj 传入：目标对象指针
     * @param x 传入：局部区域左上角 X 坐标（相对对象坐标系）
     * @param y 传入：局部区域左上角 Y 坐标（相对对象坐标系）
     * @param w 传入：局部区域宽度
     * @param h 传入：局部区域高度
     * @return 无
     * @note 标脏区域会沿 parent 链逐层裁剪，适合局部内容更新。
     */
    void we_obj_invalidate_area(we_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h);
    /**
     * @brief 按对象区域标脏，但排除其中一个安全空洞矩形
     * @param obj 传入：目标对象指针
     * @param x 传入：整体区域左上角 X 坐标（屏幕绝对坐标）
     * @param y 传入：整体区域左上角 Y 坐标（屏幕绝对坐标）
     * @param w 传入：整体区域宽度
     * @param h 传入：整体区域高度
     * @param ex 传入：排除区域左上角 X 坐标（屏幕绝对坐标）
     * @param ey 传入：排除区域左上角 Y 坐标（屏幕绝对坐标）
     * @param ew 传入：排除区域宽度
     * @param eh 传入：排除区域高度
     * @return 无
     * @note 标脏区域会沿 parent 链逐层裁剪；该接口只拆分本次新增脏区，
     *       不会从已有 dirty list 中删除任何区域。
     */
    void we_obj_invalidate_area_exclude(we_obj_t *obj, int16_t x, int16_t y, int16_t w, int16_t h,
                                        int16_t ex, int16_t ey, int16_t ew, int16_t eh);

#ifdef __cplusplus
}
#endif

#endif /* WE_OBJ_H */
