# mask_group

## 功能
蒙版容器：子控件"挂进来即生效"地获得内容级蒙版效果——四角独立轮廓裁剪（每角可各自配置圆角 / 切角 45° / 直角）+ 沿轮廓的边框 + 任意角度旋转线性 alpha 渐变淡出。子控件本身零改动、按原有全速路径绘制，容器在每个 PFB 条带内做一次后处理合成。

## 适用场景
- 圆角/切角卡片里放图片/文字/色块，内容贴边处被轮廓与边框内沿平滑裁掉
- 面板内容沿某个方向渐隐（列表底部淡出、光影过渡等），边框作"相框"保持实色
- 需要"矩形裁剪 group"时（四角直角、无边框、无渐变，零后处理成本）

## 关键 API
- `we_mask_group_obj_init(...)`
- `we_mask_group_obj_delete(...)`
- `we_mask_group_add_child(...)` / `we_mask_group_remove_child(...)`
- `we_mask_group_set_child_pos(...)` / `we_mask_group_relayout(...)`
- `we_mask_group_set_opacity(...)`
- `we_mask_group_set_radius(...)` —— 一键四角同半径圆角，0 = 纯矩形裁剪
- `we_mask_group_set_corner(idx, style, r)` —— 单角样式/半径（WE_MASK_GROUP_LT/RT/LB/RB × ROUND/CHAMFER，r=0 直角）
- `we_mask_group_set_border(color, width)` —— 边框厚度与颜色，width=0 关闭
- `we_mask_group_set_backdrop(...)` —— 纯色背板（蒙版透明处的还原色）
- `we_mask_group_set_gradient(angle, a0, a1)` —— 512 步制角度旋转线性渐变（0 = 横向 +X，128 = 纵向 +Y）
- `we_mask_group_clear_gradient(...)`

## 可调宏
在 `we_user_config.h` 中可覆盖：
- `WE_MASK_GROUP_CHILD_MAX`（默认 12）

## 渲染模型与成本
- 子控件阶段与 group 完全一致：收窄 PFB 窗口（矩形硬裁剪）+ `opa_scale` 透明度级联
- 后处理阶段顺序：渐变 pass（内容渐隐）→ 边框直条 → 四角合成：
  - 四角直角 + 无边框 + 无渐变：直接跳过，零成本
  - 几何口径与 box 完全一致：K×K 角落方块（K = max(各角半径, 边框厚, 边框厚+内半径)），圆角带边框走 `we_mask_quarter_ring_alpha` 单遍子采样求外/内覆盖，切角按行分段整块写（alpha 仅 0/128/255，无逐像素函数调用），切角边框内缩 0.586·bw 保持等厚
  - 每像素按 内容·a_in + 边框·(a_out−a_in) + 背板·(255−a_out) 顺序合成（同 box 的顺序近似）
  - 渐变：容器 ∩ 条带整块一遍，内环每像素一次 int32 加法 + 混色（Q16 DDA，无除法、无浮点；进条带前 3 次 int64 除法一次性建参）
- 蒙版 alpha 是绝对坐标纯函数，脏矩形局部重绘可无缝拼接

## 事件与行为
- 命中转发同 group（按压锁定 + CLICKED 释放点复核），命中粒度为子控件包围盒
- 完全透明（opacity=0）的容器不拦截输入
- 所有 set 接口值未变时直接返回，不触发重绘

## 注意事项
- **纯色背板语义（v1）**：蒙版透明处向 `backdrop` 颜色还原（默认取 init 时的屏幕底色）。容器叠在纯色底上时结果精确；叠在图片/其他控件上时角落与渐隐区会显露背板色而非真实背景（真实背景恢复需要快照缓冲，v1 不做）
- 边框语义：内容在边框**内沿**被裁剪（同 CSS 盒模型），边框盖在裁剪线上；渐变只作用于内容，边框保持实色；容器自身 opacity 会让边框有效色向背板收敛（`bd_eff = blend(border, backdrop, opacity)`，每帧只算一次）
- 圆锥/角向扫描渐变刻意不提供（M0 逐像素 atan2 不可行）；"旋转渐变"指任意角度的线性渐变
- 渐变角度动画（每帧 set_gradient）会使整个容器区域每帧重绘，面板级尺寸在 M0 上可用，整屏尺寸慎用
- 子控件使用"相对容器左上角"的局部坐标；容器移动（set_pos）自动级联子控件
- 无动画节点，删除前不需要 we_anim_stop

## 对应 demo
- `Demo/preview/demo_mask_group.c`（DEMO_ID 121：左侧四角混合样式 + 边框 + 滑动文字裁剪，右侧切角/圆角对角混搭 + 细边框 + 旋转渐变色块矩阵）
