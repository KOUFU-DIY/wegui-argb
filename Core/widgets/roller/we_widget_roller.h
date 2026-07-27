#ifndef __WE_WIDGET_ROLLER_H
#define __WE_WIDGET_ROLLER_H

#include "we_gui_driver.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 本控件聚焦/按键支持开关（默认跟随全局 WE_CFG_ENABLE_KEY_INPUT）。
 * 置 0 单独裁剪滚轮的按键回调与可聚焦性，其余控件不受影响。
 * 键控换行依赖编辑态，WE_CFG_FOCUS_EDIT=0 时本支持整体关闭。 */
#ifndef WE_ROLLER_USE_KEY
#define WE_ROLLER_USE_KEY 1
#endif

/* --------------------------------------------------------------------------
 * 滚轮选值器（roller）—— preview 孵化区实验控件
 *
 * 垂直滚轮：选项沿 Y 轴排布，控件中央一行为"选中行"（背景圆角高亮条 +
 * 主色文字），上下行按距中心的行距做透明度递减（255/160/90/55/40 分档，
 * 档间按像素距离线性插值，滚动过程中亮度连续过渡）。
 *
 * 数据驱动：选项字符串数组由调用方持有（通常是 static const 数组），
 * 控件只保存 const 指针，绝不拷贝文本。
 *
 * 滚动模型：scroll_px 为像素级累计偏移（int32），选中第 i 项时
 * scroll_px == i * row_h；行高 row_h = 字体行高 + 2 * WE_ROLLER_ROW_PAD，
 * 控件高度 = visible_rows * row_h（visible_rows 强制为奇数）。
 *
 * 交互：
 *   - PRESSED 记录起点并打断进行中的吸附动画；
 *   - STAY 位移超阈值后进入跟手拖拽（滚动范围硬夹紧到首尾项），同时以
 *     步进位移持续估算速度（scroll_panel 同款约定：一次 STAY ≈ 一个
 *     16ms 调度周期）；指尖停驻（步进为 0）时速度每周期减半归零，
 *     防止"停顿后松手仍甩飞"；
 *   - RELEASED 时若测得速度 |v| >= WE_ROLLER_FLING_MIN_V 进入惯性甩动：
 *     吸附目标改为"按速度衰减外推的落点取整到行"（见宏注释的整数推导），
 *     并以 v 作为吸附动画的初速种子，滑过多行再减速吸附；
 *     慢速松手仍就近吸附最近行；
 *   - 轻点（未进入拖拽的 CLICKED）上/下方可见行：吸附动画滚到该行；
 *     点中心行不动作（点击直达）；
 *   - 快速轻扫（无 STAY 的 SWIPE_UP/DOWN）向对应方向翻 1 行；
 *   - 吸附完成且选中项发生变化时回调 changed_cb。
 *
 * 渲染与标脏：
 *   - 行文字经 PFB 窗口收窄裁剪在控件矩形内（scroll_panel 同款
 *     save/restore 套路），半露行不会渗出控件边界；
 *   - 滚动位移只标脏"文本列带"（可见窗内最大行宽 + 左右安全余量、
 *     水平居中、全控件高的竖条）：面板背景与中心高亮条在滚动中不变，
 *     文本又逐行水平居中，列带即内容裁剪矩形，面板圆角外的四角区域
 *     自然被排除；列带宽逼近控件宽时退回整件标脏（此时文本确实会
 *     进入圆角区）；
 *   - 吸附完成 commit 只标脏中心行条带；
 *   - 行宽经直接映射缓存（槽位 = 选项索引 & (WE_ROLLER_WCACHE_SIZE-1)，
 *     零除法零 malloc），滚动中每行至多重测一次/进窗；y 方向 bbox 对
 *     同一字体视作常量，缓存在结构体中（set_options / set_font 时刷新），
 *     绘制内环不再调用 we_get_text_width / we_get_text_bbox。
 *
 * 零 malloc、渲染内环零浮点。删除前必须 we_roller_obj_delete
 * （内部先 we_anim_stop 再 we_obj_delete）。
 * -------------------------------------------------------------------------- */

/* 默认可见行数（奇数；init 传 0 时使用） */
#ifndef WE_ROLLER_DEF_VISIBLE_ROWS
#define WE_ROLLER_DEF_VISIBLE_ROWS 5U
#endif

/* 行内上下边距（像素）：行高 = 字体行高 + 2 * PAD */
#ifndef WE_ROLLER_ROW_PAD
#define WE_ROLLER_ROW_PAD 6U
#endif

/* 面板背景圆角半径（像素） */
#ifndef WE_ROLLER_PANEL_RADIUS
#define WE_ROLLER_PANEL_RADIUS 10U
#endif

/* 中心高亮条圆角半径 / 左右内缩（像素） */
#ifndef WE_ROLLER_BAR_RADIUS
#define WE_ROLLER_BAR_RADIUS 8U
#endif
#ifndef WE_ROLLER_BAR_INSET
#define WE_ROLLER_BAR_INSET 4
#endif

/* 判定为拖拽（而非点击）的位移阈值（像素） */
#ifndef WE_ROLLER_DRAG_THRESHOLD
#define WE_ROLLER_DRAG_THRESHOLD 3
#endif

/* 吸附动画：纯整数"拉力 + 阻尼"缓动参数（对齐 slideshow COMPLEX 模式）。
 * 每帧：v += diff / PULL_DIV（按帧时长缩放）；v = v * DAMP_NUM / DAMP_DEN；
 * 单帧位移上限 MAX_STEP（16ms 基准，按帧时长缩放）。 */
#ifndef WE_ROLLER_SNAP_PULL_DIV
#define WE_ROLLER_SNAP_PULL_DIV 3
#endif
#ifndef WE_ROLLER_SNAP_DAMP_NUM
#define WE_ROLLER_SNAP_DAMP_NUM 3
#endif
#ifndef WE_ROLLER_SNAP_DAMP_DEN
#define WE_ROLLER_SNAP_DAMP_DEN 4
#endif
#ifndef WE_ROLLER_SNAP_MAX_STEP
#define WE_ROLLER_SNAP_MAX_STEP 24
#endif

/* --- 惯性甩动（松手继承拖拽速度）手感参数 ---
 *
 * 速度测量：STAY 期间 v = 上一触点 Y - 当前触点 Y（scroll 方向步进，
 * 单位 px/调度周期，16ms 基准，与 scroll_panel 的测速约定一致）。
 *
 * 整数外推推导：设松手后速度按每周期 f 几何衰减（v·f, v·f², v·f³ ...），
 * 总滑行距离为等比级数和 S = v·f / (1 - f)。取 f = 6/7 得 S = 6·v，
 * 即 落点 = scroll_px + v * PROJ_NUM / PROJ_DEN（纯整数，默认 6/1），
 * 落点夹紧量程后按 row_h 取整到行。减速过程本身不做逐帧速度积分，
 * 而是把落点交给现有拉力+阻尼吸附动画趋近（v 作为 snap_v 初速种子，
 * 保证松手瞬间速度连续），外推系数只决定"落在哪一行"。 */
#ifndef WE_ROLLER_FLING_MIN_V
#define WE_ROLLER_FLING_MIN_V 4 /* 触发惯性的速度阈值（px/周期），低于此就近吸附 */
#endif
#ifndef WE_ROLLER_FLING_PROJ_NUM
#define WE_ROLLER_FLING_PROJ_NUM 6 /* 外推系数分子：落点偏移 = v * NUM / DEN */
#endif
#ifndef WE_ROLLER_FLING_PROJ_DEN
#define WE_ROLLER_FLING_PROJ_DEN 1 /* 外推系数分母 */
#endif

/* 行宽缓存槽数：必须为 2 的幂（槽位 = 选项索引 & (SIZE-1)，无除法/取模）。
 * 直接映射缓存：>= 可见行数 + 2 时可见窗内无同帧互逐；更小仍正确、只是
 * 命中率下降。RAM 开销 = SIZE * 4 字节（宽度 uint16 + 索引 uint16）。 */
#ifndef WE_ROLLER_WCACHE_SIZE
#define WE_ROLLER_WCACHE_SIZE 16U
#endif

/* y 方向 bbox 常量化的扫描上限：set_options 时扫描前 N 个选项取墨迹
 * 纵向并集作为全字体共用 y_top/y_bot。权衡：全量扫描对超长选项表的
 * 绑定开销为 O(总字符数)，故设上限；N 之后若出现更高墨迹的字形，行内
 * 垂直居中可能偏 1~2px（数字/时间/档位类均匀选项完全无影响）。
 * 副作用（有意为之）：所有行共用同一垂直基准，滚动中不再出现旧实现
 * "逐行独立 bbox 居中"导致的基线抖动。 */
#ifndef WE_ROLLER_BBOX_SCAN_MAX
#define WE_ROLLER_BBOX_SCAN_MAX 32U
#endif

/* 滚动标脏文本列带的左右安全余量（像素）：覆盖字形 x_ofs 为负 /
 * 墨迹超出步进宽度（如斜体溢出）的边缘像素；使用大幅倾斜字体可调大 */
#ifndef WE_ROLLER_DIRTY_PAD
#define WE_ROLLER_DIRTY_PAD 4
#endif

struct we_roller_obj_t;

/**
 * @brief 选中项改变回调（吸附动画完成、选中项与上次不同才触发）。
 * @param obj 滚轮控件对象指针。
 * @param selected_idx 新选中项索引。
 * @return 无。
 */
typedef void (*we_roller_changed_cb_t)(struct we_roller_obj_t *obj,
                                       uint16_t selected_idx);

typedef struct we_roller_obj_t
{
    we_obj_t base;              /* 必须在首位：x/y/w/h 为控件外接矩形 */

    /* 4 字节对齐成员（指针/int32/动画节点）在前，消 padding */
    const char *const *options; /* 选项字符串数组（调用方持有，只存指针） */
    const unsigned char *font;  /* 字体资源（init 传入，可 set_font） */
    we_roller_changed_cb_t changed_cb; /* 吸附完成回调（可为 NULL） */
    int32_t scroll_px;          /* 像素级滚动偏移（选中第 i 项时 = i*row_h） */
    int32_t press_scroll;       /* 按下时 scroll_px */
    we_anim_t anim;             /* 吸附动画节点（归控件所有，删除前必须摘链） */
    int32_t snap_v;             /* 拉力 + 阻尼速度累计（甩动时以松手速度作种） */
    int32_t snap_target;        /* 吸附目标 scroll_px */

    /* 2 字节成员 */
    uint16_t option_cnt;        /* 选项个数 */
    uint16_t row_h;             /* 单行高度（像素，由字体行高推导） */
    int16_t sel_idx;            /* 当前已提交的选中项索引，-1 = 无选项 */
    int16_t press_y;            /* 按下时触摸 Y */
    int16_t last_y;             /* 上一次 STAY 触点 Y（步进测速基准） */
    int16_t fling_v;            /* 释放测速：px/调度周期，符号为 scroll 方向 */
    int16_t text_dy;            /* 派生缓存：行内垂直居中偏移 = row_h/2 - (top+bot)/2 */
    uint16_t disp_band_w;       /* 当前屏上文本列带宽（滚动精细标脏用，见 .c） */
    uint16_t wcache_w[WE_ROLLER_WCACHE_SIZE];   /* 行宽缓存（像素） */
    uint16_t wcache_idx[WE_ROLLER_WCACHE_SIZE]; /* 槽内选项索引，0xFFFF = 空 */
    colour_t bg_color;          /* 面板背景色 */
    colour_t bar_color;         /* 中心高亮条颜色 */
    colour_t text_color;        /* 非中心行文字色 */
    colour_t text_sel_color;    /* 中心行文字主色 */

    /* 1 字节成员与状态位域 */
    uint8_t visible_rows;       /* 可见行数（奇数） */
    uint8_t opacity;            /* 整体不透明度（0~255，默认 255） */
    int8_t text_y_top;          /* 字体 y bbox 常量缓存：墨迹顶部偏移 */
    int8_t text_y_bot;          /* 字体 y bbox 常量缓存：墨迹底部偏移 */
    uint8_t tracking : 1;       /* 本次触摸序列是否有效（PRESSED 起点） */
    uint8_t dragging : 1;       /* 是否已进入跟手拖拽 */
    uint8_t tap_armed : 1;      /* 本按压序列未进入拖拽（CLICKED 判定点击直达用） */
    uint8_t snap_animating : 1; /* 吸附动画进行中标志 */
} we_roller_obj_t;

/**
 * @brief 初始化滚轮控件并挂载到 LCD 对象链表。
 * @param obj 控件对象指针。
 * @param lcd GUI 运行时 LCD 上下文指针。
 * @param x 左上角 X 坐标（屏幕绝对坐标）。
 * @param y 左上角 Y 坐标。
 * @param w 控件宽度（像素）。
 * @param visible_rows 可见行数；0 = 默认 5；偶数自动 +1 保持奇数。
 * @param font 字体资源指针（必传；NULL 时不执行初始化）。
 * @return 无。
 * @note 控件高度 = visible_rows * 行高（字体行高 + 2*WE_ROLLER_ROW_PAD），
 *       由内部推导，不由调用方指定。默认深色主题配色、
 *       初始无选项（需再调 we_roller_set_options）。
 */
void we_roller_obj_init(we_roller_obj_t *obj, we_lcd_t *lcd,
                        int16_t x, int16_t y, int16_t w, uint8_t visible_rows,
                        const unsigned char *font);

/**
 * @brief 绑定选项字符串数组（控件只保存指针，不复制内容）。
 * @param obj 控件对象指针。
 * @param options 字符串指针数组，需在控件生命周期内保持有效。
 * @param count 选项个数。
 * @return 无。
 * @note 绑定后选中项复位为 0（count 为 0 时复位为 -1），并打断进行中的
 *       吸附动画；数组指针与个数均未变化时直接返回。同时刷新文字测量
 *       缓存（y bbox 常量 + 清空行宽缓存），故就地修改数组内的字符串
 *       内容后需重新调用本接口（先绑 NULL 再绑回可强制刷新）。
 */
void we_roller_set_options(we_roller_obj_t *obj,
                           const char *const *options, uint16_t count);

/**
 * @brief 立即定位到指定选项（无动画，不触发 changed_cb）。
 * @param obj 控件对象指针。
 * @param index 目标选项索引（越界时钳制到最后一项）。
 * @return 无。
 */
void we_roller_set_selected(we_roller_obj_t *obj, uint16_t index);

/**
 * @brief 获取当前选中项索引。
 * @param obj 控件对象指针。
 * @return 选中项索引；无选项时返回 -1。
 * @note 拖拽/吸附动画期间返回的是最后一次提交的索引。
 */
int16_t we_roller_get_selected(const we_roller_obj_t *obj);

/**
 * @brief 设置选中项改变回调（吸附完成且索引变化时触发）。
 * @param obj 控件对象指针。
 * @param cb 回调函数指针，NULL 表示不回调。
 * @return 无。
 */
void we_roller_set_changed_cb(we_roller_obj_t *obj, we_roller_changed_cb_t cb);

/**
 * @brief 设置字体资源，按新字体行高重推导行高与控件高度。
 * @param obj 控件对象指针。
 * @param font 字体资源指针（NULL 或与当前相同直接返回）。
 * @return 无。
 * @note 流程：先标脏旧区（旧高度）→ 换字体并重算 row_h / 控件高 /
 *       文字测量缓存 → 标脏新区。scroll_px 按新旧行高比例换算保持
 *       选中行（行对齐位置 i*old_row_h 精确映射到 i*new_row_h，
 *       整数分解 idx*new + rem*new/old 避免中间量溢出）；换算后若
 *       处于行间（此前正在拖拽/动画中）则立即就近吸附。打断进行中
 *       的触摸序列与吸附动画。
 */
void we_roller_set_font(we_roller_obj_t *obj, const unsigned char *font);

/**
 * @brief 删除滚轮控件：先摘除吸附动画节点（we_anim_stop）再摘链。
 * @param obj 控件对象指针。
 * @return 无。
 */
void we_roller_obj_delete(we_roller_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* __WE_WIDGET_ROLLER_H */
