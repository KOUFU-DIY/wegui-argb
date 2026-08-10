#ifndef WE_USER_CONFIG_H
#define WE_USER_CONFIG_H

/* GUI 统一用户配置入口。
 * Core 层和各控件层都优先从这里读取可调宏，避免后续直接改控件头文件。 */

/* ----------------------------- 版本 ----------------------------- */
/* 框架版本号（字符串），与 README / 对外发布保持一致 */
#define WE_GUI_VERSION "V0.2.3"

#define DEEP_RGB565 (4) /* RGB565 */
#define DEEP_RGB888 (5) /* RGB888 */

/* -------------------------- 屏幕设置 -------------------------- */
/* LCD 输出色深 */
#define LCD_DEEP (DEEP_RGB565)

/* 屏幕宽高 */
#define SCREEN_WIDTH (280)
#define SCREEN_HEIGHT (240)

/* 屏幕显存
 * 最小设置 1 行；
 * DMA 模式建议至少 2 行。 */
#define USER_GRAM_NUM (SCREEN_WIDTH * 8)

/* DMA 双 BUF 模式开关 */
#define GRAM_DMA_BUFF_EN (1)

/* 刷新区域像素对齐粒度（QSPI 彩屏 / SSD1306 页式 OLED 等硬件需求）。
 * 部分屏幕对刷新窗口坐标有硬件粒度要求：
 *   - QSPI 接口彩屏常要求 set_addr 的 x/y 起止坐标为 2 或 4 的倍数；
 *   - SSD1306 等页式单色 OLED 以 8 行为一页，y 向必须按 8 对齐。
 * 语义：脏矩形入库时（Core/dirty_driver.c 的 we_dirty_invalidate）把矩形
 * 扩张到对齐边界——x0/y0 向下取整到对齐倍数，x1/y1（包含端点）向上取整到
 * 对齐倍数-1。扩出的边缘随本矩形整块走正常渲染路径重绘，因此推屏时
 * set_addr 收到的窗口天然对齐，且像素流与窗口严格一致。
 * 取值必须为 2 的幂；平台端口不定义时默认 1（不对齐），
 * 现有平台零行为、零开销变化。 */
#define WE_LCD_FLUSH_ALIGN_X (1)
#define WE_LCD_FLUSH_ALIGN_Y (1)


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

/* 控件压力性能测试开关
 * 0: 关闭
 * 1: 打开 */
#define WE_CFG_DEBUG_PERF_STRESS (0)

/* ------------------------- 功能裁剪开关 ------------------------- */
/* 1: 保留索引 QOI 解码
 * 0: 裁掉索引 QOI 相关绘图函数和图片控件分发路径 */
#define WE_CFG_ENABLE_INDEXED_QOI (1)

/* --------------------------- GUI 定时器配置 --------------------------- */
/* 用户定时器已改为调用方持有的侵入式节点（we_gui_timer_t）：无槽位上限 */

/* -------------------------- 输入接口 -------------------------- */
/* 输入接口(按键或触摸)开关
 * 0: 关闭，节省空间
 * 1: 打开 */
#define WE_CFG_ENABLE_INPUT_PORT_BIND (1)

/* 滑动手势识别阈值（像素）
 * 位移超过此值才判定为 swipe，而不是普通点击。 */
#define WE_CFG_SWIPE_THRESHOLD (30)

/* 手势接管阈值（像素）：按压后位移超过此值，内核沿祖先链发起
 * WE_EVENT_DRAG_BEGIN 接管询问，可滚动容器从子控件手中接管手势（默认 8） */
// #define WE_CFG_DRAG_THRESHOLD (8)

/* 层次输入裁剪（默认 1）：置 0 后命中测试不下钻容器子树、拖拽接管询问
 * 剔除——控件全部平铺、不在滚动容器里放交互控件的产品可省 ROM 与栈 */
// #define WE_CFG_ENABLE_NESTED_INPUT (1)

/* 顶层链 + 模态裁剪（默认 1）：置 0 后 attach_to_top 退化为 bring_to_front、
 * modal 三函数退化为空 stub——不用 msgbox/toast/dropdown 弹层语义的产品
 * 可整段裁掉（弹层控件仍可编译，只是失去保证置顶与吞键语义） */
// #define WE_CFG_ENABLE_TOP_LAYER (1)

/* ----------------------- 聚焦与按键导航 ----------------------- */
/* 全局聚焦 + 按键导航总开关
 * 0: 关闭，键注入/焦点管理/矩形光标全部编译剔除，纯触摸工程零成本
 * 1: 打开，端口经 we_gui_key_press/release（双沿）或 we_gui_key_inject
 *    （tap）注入 上/下/左/右/前/后/OK/返回 语义键控制焦点：方向键按
 *    包围盒中心空间四向就近移动、前后键线性环序，OK 进入容器或触发
 *    控件（按住期间保持按压态，松开触发），BACK 退出容器，顶层再按
 *    清除焦点 */
#define WE_CFG_ENABLE_KEY_INPUT (1)

/* 聚焦裁剪子开关（默认全开；面向极低配产品的按需瘦身）
 * WE_CFG_FOCUS_EDIT   0=剔除编辑态：值类控件（slider/stepper/roller/list）
 *                     的按键支持整体关闭，只留按钮/开关类点击语义
 * WE_CFG_FOCUS_NESTED 0=剔除容器下钻/上退：焦点环只含顶层可聚焦控件
 * 另有逐控件开关 WE_BTN_USE_KEY / WE_CHECKBOX_USE_KEY / WE_TOGGLE_USE_KEY /
 * WE_INDICATOR_USE_KEY / WE_SLIDER_USE_KEY / WE_STEPPER_USE_KEY /
 * WE_ROLLER_USE_KEY / WE_LIST_USE_KEY / WE_SCROLL_PANEL_USE_KEY /
 * WE_DROPDOWN_USE_KEY（默认 1，置 0 单独裁掉该控件的按键回调与
 * 可聚焦性，定义处见各控件头文件） */
// #define WE_CFG_FOCUS_EDIT (0)
// #define WE_CFG_FOCUS_NESTED (0)

/* 焦点光标外观与手感（不定义时使用默认值） */
// #define WE_CFG_FOCUS_CURSOR_THICKNESS (2) /* 光标框线宽（像素） */
// #define WE_CFG_FOCUS_CURSOR_GAP (2)       /* 光标框与控件包围盒的间隙（像素） */
// #define WE_CFG_FOCUS_CURSOR_R (92)        /* 光标颜色 RGB888 */
// #define WE_CFG_FOCUS_CURSOR_G (181)
// #define WE_CFG_FOCUS_CURSOR_B (255)
// #define WE_CFG_FOCUS_FLASH_MS (90U)       /* OK 最短按压窗口（毫秒，≤255） */
// #define WE_CFG_KEY_QUEUE_LEN (8)          /* 语义键环形队列深度（2 的幂，容量=深度-1，默认 8） */

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
// #define WE_CHART_DIRTY_BLOCK_W (16)

/* 柔边衰减曲线
 * 0: 线性衰减
 * 1: 二次衰减（内侧更厚实，外侧掉得更快） */
// #define WE_CFG_CHART_AA_CURVE (0)

/* ------------------------- group 控件 ------------------------- */

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
/* 消息框淡入/淡出动画总时长（毫秒） */
// #define WE_MSGBOX_ANIM_DURATION_MS (220U)

/* 是否启用消息框淡入/淡出动画
 * 0: 直接显示/隐藏
 * 1: 带动画（透明度淡入淡出） */
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

/* ---------------------- scroll_panel 控件 ---------------------- */
/* 面板最多挂载的子控件数量 */

/* 右缘滚动条宽度（像素） */
// #define WE_SCROLL_PANEL_SCROLLBAR_W (4U)

/* 判定为拖拽滚动的位移阈值（像素） */
// #define WE_SCROLL_PANEL_DRAG_THRESHOLD (8)

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

/* ------------------------- slider 控件 ------------------------- */
/* 轨道厚度（像素） */
// #define WE_SLIDER_TRACK_THICKNESS (8U)

/* 滑块直径（像素） */
// #define WE_SLIDER_THUMB_SIZE (22U)

/* 是否编译竖直方向滑条支持
 * 0: 仅水平，省代码
 * 1: 水平 + 竖直 */
// #define WE_SLIDER_ENABLE_VERTICAL (1)

/* ------------------------- stepper 控件 ------------------------- */
/* 按住连续步进：首次重复前的延迟（STAY 次数，约 16ms/次 → 25≈400ms） */
// #define WE_STEPPER_HOLD_DELAY (25U)

/* 按住连续步进：达到延迟后每隔多少次 STAY 再步进一次（7≈112ms） */
// #define WE_STEPPER_HOLD_INTERVAL (7U)

/* 支持的最大小数位数（决定内部查表与缓冲大小） */
// #define WE_STEPPER_MAX_DECIMALS (4U)

/* ----------------------- indicator 控件 ----------------------- */
/* 是否编译亮灭过渡动画（运行时仍可单独关闭某盏灯） */
// #define WE_INDICATOR_USE_ANIM (1)

/* 默认动画时长（毫秒），可 we_indicator_set_anim 运行时修改 */
// #define WE_INDICATOR_ANIM_MS (250U)

/* 光晕相对核心圆的额外半径占比（256 制） */
// #define WE_INDICATOR_GLOW_RATIO (80U)

/* 光晕峰值透明度（0~255） */
// #define WE_INDICATOR_GLOW_ALPHA (120U)

/* 点亮颜色 RGB */
// #define WE_INDICATOR_ON_R (52)
// #define WE_INDICATOR_ON_G (199)
// #define WE_INDICATOR_ON_B (89)

/* 熄灭颜色 RGB */
// #define WE_INDICATOR_OFF_R (60)
// #define WE_INDICATOR_OFF_G (60)
// #define WE_INDICATOR_OFF_B (66)

/* ------------------------ dropdown 控件 ------------------------ */
/* 展开列表默认最多可见行数 */
// #define WE_DROPDOWN_DEF_MAX_VISIBLE (4)

/* 滚动条空闲淡出：全显保持时长（毫秒） */
// #define WE_DROPDOWN_SB_HOLD_MS (600U)

/* 滚动条空闲淡出：渐隐时长（毫秒） */
// #define WE_DROPDOWN_SB_FADE_MS (400U)

/* 滚动条常驻最低透明度（0~255） */
// #define WE_DROPDOWN_SB_IDLE_ALPHA (40U)

/* 拖拽允许的最大越界像素（橡皮筋过冲） */
// #define WE_DROPDOWN_OVERSCROLL_LIMIT (24)

/* 回弹拉力除数（越小回弹越强） */
// #define WE_DROPDOWN_REBOUND_PULL_DIV (3)

/* 回弹阶段单帧最大位移 */
// #define WE_DROPDOWN_REBOUND_MAX_STEP (24)

/* -------------------------- line 控件 -------------------------- */
/* 是否编译线段动画（0 时 we_line_anim_* 退化为立即生效 stub） */
// #define WE_LINE_USE_ANIM (1)

/* 默认动画时长（毫秒） */
// #define WE_LINE_ANIM_MS (300U)

/* 默认线宽（像素） */
// #define WE_LINE_DEF_WIDTH (3U)

/* 默认线色 RGB */
// #define WE_LINE_DEF_R (88)
// #define WE_LINE_DEF_G (166)
// #define WE_LINE_DEF_B (240)

/* -------------------------- box 控件 -------------------------- */
/* 是否编译填充色/透明度动画（默认关；0 时 we_box_anim_* 为立即生效 stub） */
// #define WE_BOX_USE_ANIM (0)

/* 默认动画时长（毫秒，仅 USE_ANIM=1 时有效） */
// #define WE_BOX_ANIM_MS (300U)

/* 默认圆角半径（像素） */
// #define WE_BOX_DEF_RADIUS (8U)

/* 默认填充色 RGB */
// #define WE_BOX_DEF_R (38)
// #define WE_BOX_DEF_G (46)
// #define WE_BOX_DEF_B (60)

/* ------------------------- gauge 控件 ------------------------- */
/* 默认起始角 / 扫过角（512 步制，WE_DEG 换算） */
// #define WE_GAUGE_DEF_START (WE_DEG(135))
// #define WE_GAUGE_DEF_SWEEP (WE_DEG(270))

/* 默认刻度数量与几何（长度/线宽均为像素） */
// #define WE_GAUGE_DEF_TICK_CNT (11U)
// #define WE_GAUGE_DEF_TICK_LEN (9U)
// #define WE_GAUGE_TICK_W (2U)

/* 刻度数量上限（决定端点缓存数组大小） */
// #define WE_GAUGE_TICK_MAX (16U)

/* 判定为小表盘的直径阈值（小表盘自动精简刻度） */
// #define WE_GAUGE_SMALL_SIZE (40)

/* 指针线宽 / 中心帽直径（像素） */
// #define WE_GAUGE_DEF_PTR_W (4U)
// #define WE_GAUGE_DEF_CAP_W (10U)

/* 指针长度占半径比（Q8：256 = 到达刻度环） */
// #define WE_GAUGE_PTR_LEN_Q8 (184)

/* 刻度颜色 RGB */
// #define WE_GAUGE_TICK_R (148)
// #define WE_GAUGE_TICK_G (162)
// #define WE_GAUGE_TICK_B (184)

/* 指针颜色 RGB */
// #define WE_GAUGE_PTR_R (255)
// #define WE_GAUGE_PTR_G (96)
// #define WE_GAUGE_PTR_B (84)

/* -------------------------- list 控件 -------------------------- */
/* 行内上下留白（行高 = 字体行高 + 2*该值） */
// #define WE_LIST_ROW_PAD (7U)

/* 行文字左缩进（像素） */
// #define WE_LIST_TEXT_PAD (10)

/* 面板默认圆角半径 */
// #define WE_LIST_DEF_RADIUS (10U)

/* 判定为拖拽滚动的位移阈值（像素） */
// #define WE_LIST_DRAG_THRESHOLD (6)

/* 惯性摩擦分子/分母（每帧速度衰减到 NUM/DEN） */
// #define WE_LIST_INERTIA_NUM (7)
// #define WE_LIST_INERTIA_DEN (8)

/* 快速轻扫（无 STAY）测速时间片（毫秒） */
// #define WE_LIST_SWIPE_SLICE_MS (128)

/* 拖拽允许的最大越界像素（橡皮筋过冲） */
// #define WE_LIST_OVERSCROLL_LIMIT (24)

/* 回弹拉力除数 / 单帧最大位移 */
// #define WE_LIST_REBOUND_PULL_DIV (3)
// #define WE_LIST_REBOUND_MAX_STEP (24)

/* 行底分隔线透明度（0~255） */
// #define WE_LIST_SEP_OPA (46U)

/* 按压高亮条左右内缩与圆角 */
// #define WE_LIST_PRESS_INSET (2)
// #define WE_LIST_PRESS_RADIUS (6U)

/* 滚动条宽度 / 右缘间距（像素） */
// #define WE_LIST_SB_WIDTH (4)
// #define WE_LIST_SB_MARGIN (3)

/* 滚动条峰值透明度 / 全显保持时长 / 渐隐时长 / 常驻最低透明度 */
// #define WE_LIST_SB_OPA (255U)
// #define WE_LIST_SB_HOLD_MS (600U)
// #define WE_LIST_SB_FADE_MS (400U)
// #define WE_LIST_SB_IDLE_ALPHA (80U)

/* ------------------------- roller 控件 ------------------------- */
/* 默认可见行数（奇数；偶数会自动 +1） */
// #define WE_ROLLER_DEF_VISIBLE_ROWS (5U)

/* 行内上下留白（行高 = 字体行高 + 2*该值） */
// #define WE_ROLLER_ROW_PAD (6U)

/* 面板圆角 / 中心高亮条圆角与左右内缩 */
// #define WE_ROLLER_PANEL_RADIUS (10U)
// #define WE_ROLLER_BAR_RADIUS (8U)
// #define WE_ROLLER_BAR_INSET (4)

/* 判定为拖拽的位移阈值（像素） */
// #define WE_ROLLER_DRAG_THRESHOLD (3)

/* 吸附动画：拉力除数 / 阻尼分子分母 / 单帧最大位移 */
// #define WE_ROLLER_SNAP_PULL_DIV (3)
// #define WE_ROLLER_SNAP_DAMP_NUM (3)
// #define WE_ROLLER_SNAP_DAMP_DEN (4)
// #define WE_ROLLER_SNAP_MAX_STEP (24)

/* 惯性甩动：触发速度阈值（px/周期）与落点外推系数（NUM/DEN） */
// #define WE_ROLLER_FLING_MIN_V (4)
// #define WE_ROLLER_FLING_PROJ_NUM (6)
// #define WE_ROLLER_FLING_PROJ_DEN (1)

/* 行宽缓存槽数（2 的幂，≥ 可见行数 + 2） */
// #define WE_ROLLER_WCACHE_SIZE (16U)

/* 字体 y-bbox 常量扫描的最多选项数 */
// #define WE_ROLLER_BBOX_SCAN_MAX (32U)

/* 滚动标脏列带的左右余量（像素） */
// #define WE_ROLLER_DIRTY_PAD (4)

/* ------------------------ marquee 控件 ------------------------ */
/* 默认滚动速度（像素/秒），可 we_marquee_set_speed 运行时修改 */
// #define WE_MARQUEE_DEF_SPEED (30U)

/* 默认接缝停顿时长（毫秒） */
// #define WE_MARQUEE_DEF_PAUSE (800U)

/* 无缝循环两段文本之间的间隔（像素） */
// #define WE_MARQUEE_GAP (40)

/* 上下留白（控件高 = 字体行高 + 2*该值） */
// #define WE_MARQUEE_PAD_Y (2)

/* 速度上限（像素/秒） */
// #define WE_MARQUEE_SPEED_MAX (2000U)

/* ------------------------- toast 控件 ------------------------- */
/* 左右边距（控件宽 = 屏宽 - 2*该值） */
// #define WE_TOAST_MARGIN_X (10)

/* 停靠位置：滑入后的顶部 Y */
// #define WE_TOAST_DOCK_Y (8)

/* 文字上下留白（控件高 = 字体行高 + 2*该值） */
// #define WE_TOAST_PAD_Y (8)

/* 文字左右内缩（超宽截断计算用） */
// #define WE_TOAST_TEXT_PAD (4)

/* 滑入/滑出动画时长（毫秒） */
// #define WE_TOAST_ANIM_MS (200U)

/* 横幅圆角半径 */
// #define WE_TOAST_RADIUS (8U)

/* 默认停留时长（毫秒，show 传 0 时使用） */
// #define WE_TOAST_DEF_DURATION (1500U)

#endif
