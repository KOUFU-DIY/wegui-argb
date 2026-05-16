#ifndef WE_USER_CONFIG_H
#define WE_USER_CONFIG_H

/* GUI 统一用户配置入口。
 * Core 层和各控件层都优先从这里读取可调宏，避免后续直接改控件头文件。 */

#define DEEP_RGB565 (4) /* RGB565 */
#define DEEP_RGB888 (5) /* RGB888 */

/* -------------------------- 屏幕设置 -------------------------- */
/* LCD 输出色深 */
#define LCD_DEEP (DEEP_RGB565)

/* 屏幕宽高 */
#define SCREEN_WIDTH (240)
#define SCREEN_HEIGHT (240)

/* 屏幕显存
 * 最小设置 1 行；
 * DMA 模式建议至少 2 行。 */
#define USER_GRAM_NUM (SCREEN_WIDTH * 8)

/* DMA 双 BUF 模式开关 */
#define GRAM_DMA_BUFF_EN (1)

/* ------------------------- 脏矩形配置 ------------------------- */
/* 脏矩形策略
 * 0: 全屏刷新
 * 1: 单一最小包围盒
 * 2: 智能合并多脏矩形 */
#define WE_CFG_DIRTY_STRATEGY (2)

/* 多脏矩形模式下的数量上限，推荐 4~16 */
#define WE_CFG_DIRTY_MAX_NUM (10)

/* 脏矩形调试开关
 * 0: 关闭
 * 1: 打开，脏区会用红框标记 */
#define WE_CFG_DEBUG_DIRTY_RECT (0)

/* ------------------------- 功能裁剪开关 ------------------------- */
/* 1: 保留索引 QOI 解码
 * 0: 裁掉索引 QOI 相关绘图函数和图片控件分发路径 */
#define WE_CFG_ENABLE_INDEXED_QOI (1)

/* ------------------------- GUI 周期任务配置 ------------------------- */
/* GUI 内部任务槽数量上限
 * 说明：这组槽位只给 GUI 内部使用，例如控件动画任务。 */
#define WE_CFG_GUI_TASK_MAX_NUM (4)

/* --------------------------- GUI 定时器配置 --------------------------- */
/* 面向用户开放的 GUI 定时器数量上限 */
#define WE_CFG_GUI_TIMER_MAX_NUM (8)

/* -------------------------- 输入接口 -------------------------- */
/* 输入接口(按键或触摸)开关
 * 0: 关闭，节省空间
 * 1: 打开 */
#define WE_CFG_ENABLE_INPUT_PORT_BIND (1)

/* 滑动手势识别阈值（像素）
 * 位移超过此值才判定为 swipe，而不是普通点击。 */
#define WE_CFG_SWIPE_THRESHOLD (30)

/* ------------------------ 外部储存接口 ------------------------ */
/* 外部储存接口开关
 * 0: 关闭，节省空间
 * 1: 打开 */
#define WE_CFG_ENABLE_STORAGE_PORT_BIND (1)

/* ======================== 控件可编辑默认宏 ======================== */
/* 下面这些默认值从当前控件头文件拷贝出来。
 * 后续如需调优，优先改这里，不要直接改 Core/we_widget_*.h。 */

/* -------------------------- arc 控件 -------------------------- */
/* 圆弧控件脏矩形模式
 * 0: 精细脏矩形，刷新效率更高
 * 1: 简易脏矩形，代码更小 */
// #define WE_ARC_OPT_MODE (0)

/* -------------------------- btn 控件 -------------------------- */
/* 是否启用按钮自定义样式
 * 0: 所有按钮统一使用内置样式，更省 RAM
 * 1: 每个按钮保存一套独立样式，更灵活 */
// #define WE_BTN_USE_CUSTOM_STYLE (0)

/* 按钮圆角边框绘制模式
 * 0: 精细模式，观感更好
 * 1: 紧凑模式，代码更小
 * 当前默认边框厚度已取消，该开关主要保留兼容用途。 */
// #define WE_BTN_DRAW_MODE (0)

/* ------------------------- chart 控件 ------------------------- */
/* 背景网格列数
 * 设为 0 可关闭竖向网格。 */
// #define WE_CHART_GRID_COLS (10)

/* 背景网格行数
 * 设为 0 可关闭横向网格。 */
// #define WE_CHART_GRID_ROWS (10)

/* 背景网格颜色 RGB */
// #define WE_CHART_GRID_R (50)
// #define WE_CHART_GRID_G (50)
// #define WE_CHART_GRID_B (50)

/* 波形柔边高度
 * 0: 关闭柔边
 * N: 主体上下各保留 N 像素柔边 */
// #define WE_CHART_AA_MAX (2)

/* chart push 标脏策略
 * 0: 每次 push 整个控件包围盒标脏
 * 1: 每次 push 按列块联合包络标脏 */
// #define WE_CFG_CHART_DIRTY_MODE (1)

/* 联合包络标脏时的列块宽度
 * 1: 最细逐列标脏
 * N: 每 N 列合成一个 dirty block */
 #define WE_CHART_DIRTY_BLOCK_W (16)

/* 柔边衰减曲线
 * 0: 线性衰减
 * 1: 二次衰减（内侧更厚实，外侧掉得更快） */
// #define WE_CFG_CHART_AA_CURVE (0)

/* ------------------------- group 控件 ------------------------- */
/* 控件组最多挂载的子控件数量 */
// #define WE_GROUP_CHILD_MAX (24)

/* ------------------------ img_ex 控件 ------------------------ */
/* 图片旋转缩放取样模式
 * 0: 最近邻，性能更高
 * 1: 双线性，质量更高 */
// #define WE_IMG_EX_SAMPLE_MODE (0)

/* 是否开启 img_ex 边缘羽化抗锯齿
 * 0: 关闭，性能更高
 * 1: 打开，边缘更柔和 */
// #define WE_IMG_EX_ENABLE_EDGE_AA (1)

/* 是否假定 img_ex 始终不透明
 * 0: 保留整体透明度功能
 * 1: 认为始终不透明，可减少部分混色开销 */
// #define WE_IMG_EX_ASSUME_OPAQUE (0)

/* 是否使用精细包围盒
 * 0: 使用较保守的大矩形，代码更省
 * 1: 使用精细包围盒，减少无效刷新 */
// #define WE_IMG_EX_USE_TIGHT_BBOX (1)

/* ----------------------- label_ex 控件 ----------------------- */
/* 单次绘制时栈缓存的字形数上限
 * 单字形约 16 字节栈；UTF-8 多字节字符算 1 字形；超出部分不会渲染。
 * 取值建议：
 *   16 → 短数字/状态标签         (~256B 栈)
 *   32 → 默认，一般标题/计数器   (~512B 栈)
 *   48 → 较长中英混排            (~768B 栈)
 *   64 → 整行长文本，注意 M0 栈余量 (~1024B 栈) */
// #define WE_CFG_LABEL_EX_MAX_GLYPHS (32)

/* ---------------------- 外挂 flash 图片控件 ---------------------- */
/* 外挂图片流式读取块大小（字节）
 * 块越大，读次数越少；块越小，RAM 占用越低。 */
// #define WE_FLASH_IMG_CHUNK (128U)

/* ---------------------- 外挂 flash 字体控件 ---------------------- */
/* 单字形 scratch 缓冲上限（字节）
 * 默认按“整屏宽度、4bpp 单行”估算。 */
// #define WE_FLASH_FONT_SCRATCH_MAX ((((SCREEN_WIDTH) * 4U) + 7U) >> 3U)

/* ------------------------- msgbox 控件 ------------------------- */
/* 顶部滑入消息框动画总时长（毫秒） */
// #define WE_MSGBOX_ANIM_DURATION_MS (220U)

/* 是否启用消息框滑入/滑出动画
 * 0: 直接显示/隐藏
 * 1: 带动画 */
// #define WE_MSGBOX_USE_ANIM (1)

/* 消息框距离屏幕边缘的安全留白 */
// #define WE_MSGBOX_EDGE_MARGIN (12)

/* 消息框面板圆角半径 */
// #define WE_MSGBOX_RADIUS (14)

/* 消息框内部按钮圆角半径 */
// #define WE_MSGBOX_BTN_RADIUS (10)

/* ----------------------- slideshow 控件 ----------------------- */
/* 幻灯片吸附动画模式
 * 0: 松手后立即吸附
 * 1: 固定步长吸附
 * 2: 拉力 + 阻尼整数缓动 */
// #define WE_SLIDESHOW_SNAP_ANIM_MODE (2)

/* 固定步长吸附时，每帧最大移动步长 */
// #define WE_SLIDESHOW_SNAP_SIMPLE_STEP (8)

/* 复杂缓动模式：目标拉力除数
 * 数值越小，吸附拉力越强。 */
// #define WE_SLIDESHOW_SNAP_COMPLEX_PULL_DIV (3)

/* 复杂缓动模式：速度阻尼分子 */
// #define WE_SLIDESHOW_SNAP_COMPLEX_DAMP_NUM (3)

/* 复杂缓动模式：速度阻尼分母
 * 实际阻尼约为 DAMP_NUM / DAMP_DEN。 */
// #define WE_SLIDESHOW_SNAP_COMPLEX_DAMP_DEN (4)

/* 复杂缓动模式：单帧最大位移限制 */
// #define WE_SLIDESHOW_SNAP_COMPLEX_MAX_STEP (24)

/* 幻灯片最大页数 */
// #define WE_SLIDESHOW_PAGE_MAX (8)

/* 幻灯片内最多挂载的子控件数量（所有页面合计） */
// #define WE_SLIDESHOW_CHILD_MAX (24)

/* ---------------------- scroll_panel 控件 ---------------------- */
/* 是否启用滚动惯性
 * 0: 松手后立即停止
 * 1: 按释放瞬间速度继续滑行 */
// #define WE_SCROLL_PANEL_USE_INERTIA (1)

/* 是否启用边界回弹
 * 0: 越界后直接硬夹紧
 * 1: 允许少量越界并自动回弹 */
// #define WE_SCROLL_PANEL_USE_REBOUND (1)

/* 惯性摩擦分子 */
// #define WE_SCROLL_PANEL_INERTIA_FRICTION_NUM (7)

/* 惯性摩擦分母
 * 实际每帧速度约衰减到 NUM / DEN。 */
// #define WE_SCROLL_PANEL_INERTIA_FRICTION_DEN (8)

/* 回弹拉力除数
 * 数值越小，回弹越强。 */
// #define WE_SCROLL_PANEL_REBOUND_PULL_DIV (3)

/* 回弹阶段单帧最大位移 */
// #define WE_SCROLL_PANEL_REBOUND_MAX_STEP (24)

/* 允许的最大越界像素 */
// #define WE_SCROLL_PANEL_OVERSCROLL_LIMIT (32)

/* 释放速度放大分子 */
// #define WE_SCROLL_PANEL_VELOCITY_GAIN_NUM (1)

/* 释放速度放大分母 */
// #define WE_SCROLL_PANEL_VELOCITY_GAIN_DEN (1)

/* ----------------------- progress 控件 ----------------------- */
/* 数值过渡动画时长（毫秒） */
// #define WE_PROGRESS_ANIM_DURATION_MS (220U)

/* 进度条默认圆角半径；0 表示直角 */
// #define WE_PROGRESS_RADIUS (8)

/* 是否复用 btn 圆角皮肤绘制
 * 1: 复用 btn 的圆角边缘算法，过渡更平滑
 * 0: 使用通用 round rect 绘制 */
// #define WE_PROGRESS_USE_BTN_SKIN (1)

/* ------------------------- toggle 控件 ------------------------- */
/* 是否启用拨动开关动画
 * 0: 直接跳变
 * 1: 平滑滑动切换 */
// #define WE_TOGGLE_USE_ANIM (1)

/* 动画总步数 */
// #define WE_TOGGLE_ANIM_STEPS (8)

/* 动画每步时间间隔（毫秒） */
// #define WE_TOGGLE_ANIM_STEP_MS (16U)

/* 滑块与轨道边缘的间距 */
// #define WE_TOGGLE_THUMB_PAD (2)

/* ON 状态轨道颜色 */
// #define WE_TOGGLE_COLOR_ON_R (52)
// #define WE_TOGGLE_COLOR_ON_G (199)
// #define WE_TOGGLE_COLOR_ON_B (89)

/* OFF 状态轨道颜色 */
// #define WE_TOGGLE_COLOR_OFF_R (120)
// #define WE_TOGGLE_COLOR_OFF_G (120)
// #define WE_TOGGLE_COLOR_OFF_B (128)

/* 滑块颜色 */
// #define WE_TOGGLE_COLOR_THUMB_R (255)
// #define WE_TOGGLE_COLOR_THUMB_G (255)
// #define WE_TOGGLE_COLOR_THUMB_B (255)

/* 按下时额外变暗强度
 * 0: 不变暗
 * 255: 变成全黑 */
// #define WE_TOGGLE_PRESS_DARKEN (40)

#endif
