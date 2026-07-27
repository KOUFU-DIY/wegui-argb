# menu（preview 孵化区）

## 功能
多级菜单：顶部标题栏（当前页 title 居中 + 非根页左侧返回箭头 "<"）+
下方可滚动行区。行渲染沿用 list 风格：左对齐行文字 + 行底 1px 低透明度
分隔线 + 按压行高亮；带子页的行右侧画 ">" 提示；内容溢出时右缘常显细
滚动条。菜单树（页/行）全部由调用方以 static const 持有，控件只存指针。

导航为固定深度页面栈（`WE_MENU_STACK_MAX`，默认 6，含根页）：点击带
submenu 的行入栈进子页；点返回箭头 / 行区向右滑（快扫或慢拖均可）出栈；
每个栈帧记住本页滚动位置，返回时自动恢复。页面切换带 200ms 水平滑入
过渡：旧页与新页行内容整体 X 偏移经中央动画节点 + `we_ease_out_quad` +
`we_lerp` 推进（进子页新页从右滑入，返回从左滑入），行区经 PFB 窗口
收窄裁剪，滑动中的两页都不会渗出行区。

## 适用场景
- 设置界面 / 配置树（显示、声音、网络……逐级下钻）
- 层级不深（≤6 层）、每页行数不限的导航菜单
- 叶子行触发动作 ID、由业务层统一分发的场合

## 数据模型
```c
typedef struct we_menu_page_s we_menu_page_t;
typedef struct
{
    const char *label;             /* 行文本 */
    const we_menu_page_t *submenu; /* 非 NULL = 进入子页 */
    uint16_t action_id;            /* submenu==NULL 时点击回调用的动作 ID */
} we_menu_item_t;
struct we_menu_page_s
{
    const char *title;             /* 页标题（标题栏显示） */
    const we_menu_item_t *items;   /* 行数组（调用方持有） */
    uint16_t count;
};
```
树自底向上以 `static const` 定义（孙页 → 子页 → 根页），控件绝不拷贝。

## 关键 API
- `we_menu_obj_init(obj, lcd, x, y, w, h, root, font)`
- `we_menu_set_action_cb(obj, cb)`
  —— `void cb(void *menu, uint16_t action_id, const we_menu_item_t *item)`，
  点中 submenu==NULL 的叶子行时触发
- `we_menu_back(obj)` —— 返回上一级（带过渡；根页时无操作）
- `we_menu_reset(obj)` —— 复位到根页（清栈 + 根页滚动归零，无过渡）
- `we_menu_set_colors(obj, bg, title_bg, text, title_text, press, accent)`
  —— accent 同时作用于两种箭头与滚动条滑块；六色全部未变时直接返回
- `we_menu_obj_delete(obj)` —— 内部先摘惯性 + 过渡两个动画节点再摘链

## 可调宏
包含头文件前可覆盖：
- `WE_MENU_STACK_MAX`（页面栈深上限，默认 6）
- `WE_MENU_TRANS_MS`（页切换过渡时长，默认 200ms；0 = 关过渡直切）
- `WE_MENU_ROW_PAD`（行内上下边距，默认 7）
- `WE_MENU_TITLE_PAD`（标题栏上下边距，默认 8）
- `WE_MENU_TEXT_PAD`（行文字左内边距 / 分隔线内缩，默认 10）
- `WE_MENU_DEF_RADIUS`（面板圆角，默认 10）
- `WE_MENU_BACK_ZONE_W`（返回箭头点击热区宽，默认 44px）
- `WE_MENU_DRAG_THRESHOLD`（拖拽判定阈值，默认 6px）
- `WE_MENU_INERTIA_NUM` / `WE_MENU_INERTIA_DEN`（惯性衰减，默认 7/8）
- `WE_MENU_OVERSCROLL_LIMIT`（越界过冲上限，默认 24px）
- `WE_MENU_REBOUND_PULL_DIV` / `WE_MENU_REBOUND_MAX_STEP`（回弹拉力/单步上限）
- `WE_MENU_KICK_DIV` / `WE_MENU_KICK_MAX`（纵向快扫惯性初速换算/上限）
- `WE_MENU_SEP_OPA`（分隔线透明度，默认 46）
- `WE_MENU_SB_WIDTH` / `WE_MENU_SB_MARGIN` / `WE_MENU_SB_OPA`（滚动条）

## 事件与行为
- PRESSED 打断惯性；命中行进入按压高亮，命中返回热区（非根页、左上
  `WE_MENU_BACK_ZONE_W` × 标题栏高）进入箭头按压高亮；过渡期间忽略新按压
- STAY 纵向位移超阈值进入拖拽滚动（取消行按压态）；箭头按压中移出
  热区即取消
- RELEASED：
  - 箭头按压且释放仍在热区 → `we_menu_back`
  - 未拖拽且水平位移 ≥ `WE_CFG_SWIPE_THRESHOLD` 且横 > 纵 → 右移出栈
    返回（左移仅取消点击）；慢速右拖与快速右扫共用这一条路径，
    避免与内核 SWIPE_RIGHT 重复出栈（SWIPE 事件仅在无 STAY 时产生）
  - 未拖拽且释放点仍在按压行 → submenu 行入栈 / 叶子行触发 action 回调
  - 拖拽松手 → 以最近一次 STAY 步进为初速度做惯性（每步衰减 7/8）；
    无速度松手且处于过冲区（含按住定格后松开）→ 纯回弹
- SWIPE_UP/DOWN（无 STAY 的快扫，如 WASD 模拟）→ 按位移/`WE_MENU_KICK_DIV`
  折算惯性初速（上限 `WE_MENU_KICK_MAX`），补足拖拽路径测不到速度的场景
- 滚动允许越界过冲至多 `WE_MENU_OVERSCROLL_LIMIT`（默认 24px）橡皮筋，
  松手后经惯性同一动画节点回弹（每步拉回 过冲/`WE_MENU_REBOUND_PULL_DIV`，
  越界段惯性速度减半加速交棒；list 同款口径）；入栈/出栈前过冲吸回边界，
  过冲期间滚动条滑块按夹紧值计算不越轨
- 栈满时点击 submenu 行被忽略；所有事件返回 1（消费，不穿透）

## 渲染说明
- 行区内容经 PFB 窗口收窄（save/restore `pfb_area`/`pfb_y_start`/
  `pfb_y_end`/`pfb_gram`）裁剪在标题栏之下的行区矩形内
- 过渡期间先画滑出的旧页（偏移 = 新页偏移 ∓ 控件宽）再画滑入的新页；
  过渡由独立 `we_anim_t` 节点推进，惯性用另一个节点，两者互不干扰
- 标题栏 = 圆角矩形 + 底部补方角（顶角随面板圆角、底边取直）
- 滚动条在 PFB 恢复后叠加绘制，常显、过渡期间隐藏
- 零 malloc、渲染内环零浮点

## 注意事项
- 删除前必须走 `we_menu_obj_delete`（两个动画节点归控件所有，内核无法代摘）
- 菜单树生命周期须覆盖控件生命周期（推荐整棵 `static const`）
- 子页滚动从 0 开始；只有仍在栈上的页面记住滚动（出栈即丢弃）
- 过渡中再次导航会以当前页为新起点重启过渡（旧过渡直接落位，轻微跳变）
- 字体经 init 传入（init 时读取行高推导行高/标题栏高）

## 毕业前需优化
- [ ] 标脏按整控件包围盒（含过渡/惯性每步）；应改为行条带 / 过渡区精细标脏
- [ ] 标题栏文字/箭头无过渡动画（页切换时标题瞬变，可加淡入或同向滑动）
- [ ] 标题栏底部补方角在半透明主题下与圆角带二次混色（默认不透明无感）
- [ ] 按压高亮为内缩小圆角条，顶/底行不精确贴合面板大圆角
- [ ] 滚动条常显（无空闲淡出）、过渡期间直接隐藏而非渐隐
- [x] 越界橡皮筋回弹已对齐 list 的 OVERSCROLL/REBOUND 口径（2026-07）
- [ ] 行仅支持纯文本 + ">" 提示；图标/副标题/右侧值文本等富行样式未实现
- [ ] 长文本无省略号处理：行文本被 PFB 收窄裁剪在控件右缘（可能压住
  ">" 提示）；标题在收窄窗口之外绘制，超宽标题会溢出面板/与箭头重叠
- [ ] 栈满静默忽略导航，无任何提示回调

## 对应 demo
- `Demo/preview/demo_menu.c`（DEMO_ID 117：三层设置菜单树 + 动作回显）
