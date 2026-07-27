#ifndef __WE_WIDGET_MASK_GROUP_H
#define __WE_WIDGET_MASK_GROUP_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * 蒙版容器控件（mask_group）
 *
 * 一个纯效果容器：子控件照常全速绘制，容器在每个 PFB 条带内做一次
 * 蒙版后处理合成，实现"挂进来就生效"的内容级裁剪/边框/渐隐：
 *   - 矩形硬裁剪：复用 PFB 窗口收窄（同 group/scroll_panel），零逐像素成本；
 *   - 四角独立轮廓：每角可各自配置 圆角 / 切角（45°）/ 直角（半径 0），
 *     几何口径与 box 完全一致，仅四个 K×K 角落方块做合成；
 *   - 边框：厚度 + 颜色（0 = 无边框），沿轮廓走；内容在边框内沿被裁剪，
 *     边框相当于容器的"相框"，渐变只作用于内容、不作用于边框；
 *   - 旋转线性 alpha 渐变：512 步制角度任意方向（横向 = 0，纵向 = 128），
 *     内环为每像素一次整数加法（DDA 增量），无除法、无浮点；
 *     圆锥/角向扫描渐变不提供（M0 逐像素 atan2 不可行）。
 *
 * 背板语义（v1 为纯色背板模式，零额外 RAM）：
 *   蒙版透明处向 backdrop 纯色还原。容器叠在纯色底上时效果完全正确；
 *   叠在图片/其他控件上时角落与渐隐区会显露背板色而非真实背景
 *   （真实背景需要快照缓冲，属后续扩展）。backdrop 默认取初始化时的屏幕底色。
 *
 * 与 group 相同的行为约定：
 *   - 子控件局部坐标 + 绝对坐标缓存，容器移动/翻页自动级联；
 *   - 透明度经 lcd->opa_scale 向子控件级联，完全透明时不拦截输入；
 *   - 命中转发：按压时锁定子控件，触摸序列事件按序转发（包围盒粒度）。
 *
 * 蒙版逐条带后处理的成本只在启用相应效果时支付：
 *   四角全直角 + 无边框 + 无渐变 → 完全等价于一个矩形裁剪 group，无后处理；
 *   仅圆角/切角          → 只触碰四个角落方块；
 *   边框                → 额外四条直边填充带 + 角落环带合成；
 *   启用渐变             → 容器 ∩ 条带区域整体一遍"加法 + 混色"。
 * -------------------------------------------------------------------------- */

/* 蒙版容器最多挂载的子控件数量 */
#ifndef WE_MASK_GROUP_CHILD_MAX
#define WE_MASK_GROUP_CHILD_MAX 12
#endif

#if WE_MASK_GROUP_CHILD_MAX > 32
#error "WE_MASK_GROUP_CHILD_MAX must be <= 32: slot occupancy is a uint32 bitmask (slot_used_mask)."
#endif

/* 渐变类型 */
typedef enum
{
    WE_MASK_GRAD_NONE = 0,  /* 无渐变：仅裁剪 */
    WE_MASK_GRAD_LINEAR     /* 旋转线性 alpha 渐变（横/纵为特例角度） */
} we_mask_grad_type_t;

/* 角落索引（与 WE_MASK_QUADRANT_xx / box 同序：左上/右上/左下/右下） */
typedef enum
{
    WE_MASK_GROUP_LT = 0,
    WE_MASK_GROUP_RT,
    WE_MASK_GROUP_LB,
    WE_MASK_GROUP_RB
} we_mask_group_corner_idx_t;

/* 角落样式（半径为 0 时无论样式如何都是直角） */
typedef enum
{
    WE_MASK_GROUP_CORNER_ROUND = 0, /* 圆角：四分之一圆抗锯齿 */
    WE_MASK_GROUP_CORNER_CHAMFER    /* 切角：45° 直线切边 */
} we_mask_group_corner_style_t;

typedef struct
{
    we_obj_t *child;
    int16_t local_x;
    int16_t local_y;
} we_mask_group_child_slot_t;

typedef struct
{
    we_obj_t base;              /* 前缀契约：与 we_child_owner_t 保持 base、children_head 顺序 */
    we_obj_t *children_head;

    uint8_t opacity;            /* 容器整体透明度（向子控件级联） */
    colour_t backdrop;          /* 纯色背板：蒙版透明处向该颜色还原 */

    uint8_t corner_styles;      /* 四角样式打包：每角 2bit（we_mask_group_corner_style_t），
                                 * 位移 = 角索引*2，按 we_mask_group_corner_idx_t 索引 */
    uint8_t corner_r[4];        /* 各角半径/切角尺寸（像素），0 = 直角，上限 255 */
    colour_t border_color;      /* 边框色 */
    uint8_t border_w;           /* 边框厚度（像素），0 = 无边框 */

    uint8_t grad_type;          /* we_mask_grad_type_t */
    int16_t grad_angle;         /* 渐变方向，512 步制（0 = 沿 +X，128 = 沿 +Y） */
    uint8_t grad_a0;            /* 渐变起点 alpha（投影最小端，255 = 完全可见） */
    uint8_t grad_a1;            /* 渐变终点 alpha（投影最大端） */

    uint32_t slot_used_mask; /* 槽位占用位图（bit i = child_slots[i] 在用） */
    we_mask_group_child_slot_t child_slots[WE_MASK_GROUP_CHILD_MAX];
    we_obj_t *last_pressed_child; /* 命中转发：本次触摸序列按到的子控件 */
} we_mask_group_obj_t;

/**
 * @brief 初始化蒙版容器并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X（屏幕绝对坐标）。
 * @param y 左上角 Y。
 * @param w 宽度（像素）。
 * @param h 高度（像素）。
 * @return 无。
 * @note 默认：四角直角、无边框、无渐变、不透明、backdrop = 当前屏幕底色；
 *       此时容器等价于一个零后处理成本的矩形裁剪 group。
 */
void we_mask_group_obj_init(we_mask_group_obj_t *obj, we_lcd_t *lcd,
                            int16_t x, int16_t y, int16_t w, int16_t h);

/**
 * @brief 删除蒙版容器：先逐个删除全部子控件并清空 slot，再删除容器自身。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_mask_group_obj_delete(we_mask_group_obj_t *obj);

/**
 * @brief 将子控件挂入容器（建立父子关系，局部坐标清零并刷新绝对位置）。
 * @param obj 控件对象指针。
 * @param child 目标子控件对象指针。
 * @return 无。
 * @note 自挂载/跨 lcd/重复挂载/槽满会被忽略。
 */
void we_mask_group_add_child(we_mask_group_obj_t *obj, we_obj_t *child);

/**
 * @brief 将子控件从容器中移除（去链并释放 slot）。
 * @param obj 控件对象指针。
 * @param child 目标子控件对象指针。
 * @return 无。
 */
void we_mask_group_remove_child(we_mask_group_obj_t *obj, we_obj_t *child);

/**
 * @brief 设置子控件在容器内的局部坐标并刷新其屏幕绝对位置。
 * @param obj 控件对象指针。
 * @param child 目标子控件对象指针。
 * @param local_x 相对容器左上角的局部 X（像素）。
 * @param local_y 相对容器左上角的局部 Y（像素）。
 * @return 无。
 */
void we_mask_group_set_child_pos(we_mask_group_obj_t *obj, we_obj_t *child,
                                 int16_t local_x, int16_t local_y);

/**
 * @brief 按各 slot 局部坐标重新刷新全部子控件的屏幕绝对位置。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_mask_group_relayout(we_mask_group_obj_t *obj);

/**
 * @brief 设置容器透明度（向子控件级联）并按需重绘。
 * @param obj 控件对象指针。
 * @param opacity 不透明度（0~255），0 时容器不绘制也不拦截输入。
 * @return 无。
 */
void we_mask_group_set_opacity(we_mask_group_obj_t *obj, uint8_t opacity);

/**
 * @brief 一键设置四角为同一半径的圆角并按需重绘。
 * @param obj 控件对象指针。
 * @param radius 半径（像素），0 = 纯矩形裁剪；上限 255，绘制时按宽高各一半自动钳制。
 * @return 无。
 */
void we_mask_group_set_radius(we_mask_group_obj_t *obj, uint16_t radius);

/**
 * @brief 单独配置一个角落的裁剪样式与半径并按需重绘。
 * @param obj 控件对象指针。
 * @param idx 角落索引（WE_MASK_GROUP_LT/RT/LB/RB）。
 * @param style 圆角或切角（WE_MASK_GROUP_CORNER_ROUND/CHAMFER）。
 * @param r 半径/切角尺寸（像素），0 = 直角，上限 255；绘制时按宽高各半自动钳制。
 * @return 无。
 */
void we_mask_group_set_corner(we_mask_group_obj_t *obj, we_mask_group_corner_idx_t idx,
                              we_mask_group_corner_style_t style, uint16_t r);

/**
 * @brief 设置边框厚度与颜色并按需重绘（width=0 关闭边框）。
 * @param obj 控件对象指针。
 * @param color 边框颜色。
 * @param width 边框厚度（像素），沿轮廓走；内容在边框内沿被裁剪。
 * @return 无。
 * @note 渐变只作用于内容，边框始终保持实色（容器自身 opacity 仍会使边框
 *       向背板色收敛）。
 */
void we_mask_group_set_border(we_mask_group_obj_t *obj, colour_t color, uint8_t width);

/**
 * @brief 设置纯色背板（蒙版透明处的还原色）并按需重绘。
 * @param obj 控件对象指针。
 * @param backdrop 背板颜色，应与容器身后的实际底色一致。
 * @return 无。
 */
void we_mask_group_set_backdrop(we_mask_group_obj_t *obj, colour_t backdrop);

/**
 * @brief 启用/更新旋转线性 alpha 渐变并按需重绘。
 * @param obj 控件对象指针。
 * @param angle 渐变方向，512 步制（0 = 横向 +X，128 = 纵向 +Y，可用 WE_DEG(45) 等）。
 * @param a0 投影最小端 alpha（255 = 完全可见，0 = 完全露出背板）。
 * @param a1 投影最大端 alpha。
 * @return 无。
 * @note 渐变沿 angle 方向线性过渡：内容 alpha 由 a0 渐变到 a1。
 */
void we_mask_group_set_gradient(we_mask_group_obj_t *obj, int16_t angle,
                                uint8_t a0, uint8_t a1);

/**
 * @brief 关闭渐变（保留圆角裁剪）并按需重绘。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_mask_group_clear_gradient(we_mask_group_obj_t *obj);

/**
 * @brief 设置容器左上角位置（子控件按局部坐标自动跟随）。
 * @param obj 控件对象指针。
 * @param x 新的 X 坐标。
 * @param y 新的 Y 坐标。
 */
static inline void we_mask_group_obj_set_pos(we_mask_group_obj_t *obj, int16_t x, int16_t y)
{
    we_obj_set_pos((we_obj_t *)obj, x, y);
}

#ifdef __cplusplus
}
#endif

#endif
