#ifndef WE_FOCUS_H
#define WE_FOCUS_H

/* --------------------------------------------------------------------------
 * 语义键注入、焦点管理与编辑态（WE_CFG_ENABLE_KEY_INPUT 控制是否编译）。
 * -------------------------------------------------------------------------- */

#include "we_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if (WE_CFG_ENABLE_KEY_INPUT == 1)
    /**
     * @brief 注入语义按键的按下沿（端口在消抖后的按下沿调用）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param key 传入：WE_KEY_UP..WE_KEY_BACK 语义键值（WE_KEY_EVT_* 通知类禁止注入）
     * @return 无
     * @note 方向/前后/BACK 键按下沿即触发，长按连发由端口重复注入本函数实现；
     *       OK 键按下沿进入按压态，需配合 we_gui_key_release 在松开时触发动作。
     */
    void we_gui_key_press(we_lcd_t *lcd, uint8_t key);
    /**
     * @brief 注入语义按键的松开沿（端口在按键释放时调用）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param key 传入：WE_KEY_UP..WE_KEY_BACK 语义键值
     * @return 无
     * @note 目前只有 OK 键消费松开沿（回弹按压态并触发点击），
     *       其余键的松开沿会被静默忽略，端口可只对 OK 上报松开。
     */
    void we_gui_key_release(we_lcd_t *lcd, uint8_t key);
    /**
     * @brief 注入一次完整按键（按下沿 + 松开沿，tap 语义）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param key 传入：WE_KEY_UP..WE_KEY_BACK 语义键值
     * @return 无
     * @note 面向只上报"按了一下"的简单端口（三键板/编码器等）；两个边沿
     *       同周期消费，OK 按压视觉不驻留，要按住手感请用双沿上报。
     */
    void we_gui_key_inject(we_lcd_t *lcd, uint8_t key);
    /**
     * @brief 程序化设置/清除焦点
     * @param lcd 传入：GUI 屏幕上下文指针
     * @param obj 传入：目标对象；NULL 表示清除焦点
     * @return 无
     * @note 目标须可聚焦（类带 key_cb，或为子树含可聚焦控件的复合容器）；
     *       交互控件可在 WE_KEY_EVT_FOCUS 查询中按实例拒绝（如 DISABLED）。
     */
    void we_focus_set(we_lcd_t *lcd, we_obj_t *obj);
    /**
     * @brief 查询当前焦点对象
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 焦点对象指针，无焦点返回 NULL
     */
    we_obj_t *we_focus_get(we_lcd_t *lcd);
    /**
     * @brief 结构性判定对象是否可成为焦点候选（不触发 FOCUS 实例查询）
     * @param obj 传入：待判定对象
     * @return 1 表示候选（自带 key_cb 的交互控件，或子树含候选的复合容器）
     * @note 供容器控件在 WE_KEY_EVT_FOCUS 查询里判断"子树里有没有可聚焦
     *       内容"（如 scroll_panel：空面板拒绝聚焦避免死停靠点）。
     */
    uint8_t we_focus_candidate(we_obj_t *obj);
#if (WE_CFG_FOCUS_EDIT == 1)
    /**
     * @brief 进入编辑态（焦点光标换编辑色，方向键交焦点控件调值）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 无
     * @note 由值类控件在 key_cb 的 WE_KEY_OK 分支调用；无焦点时空操作。
     *       编辑态下未被控件消费的 OK/BACK 由管理器退出编辑，其余导航键吞掉。
     */
    void we_focus_edit_enter(we_lcd_t *lcd);
    /**
     * @brief 退出编辑态（焦点光标恢复导航色）
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 无
     */
    void we_focus_edit_exit(we_lcd_t *lcd);
    /**
     * @brief 查询当前是否处于编辑态
     * @param lcd 传入：GUI 屏幕上下文指针
     * @return 1 表示编辑态，0 表示导航态
     */
    uint8_t we_focus_edit_active(we_lcd_t *lcd);
#else
    /* WE_CFG_FOCUS_EDIT=0：编辑态整体编译剔除，API 退化为空操作 stub，
     * 应用层代码无需条件编译即可两态通编。 */
    static inline void we_focus_edit_enter(we_lcd_t *lcd) { (void)lcd; }
    static inline void we_focus_edit_exit(we_lcd_t *lcd) { (void)lcd; }
    static inline uint8_t we_focus_edit_active(we_lcd_t *lcd)
    {
        (void)lcd;
        return 0U;
    }
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* WE_FOCUS_H */
