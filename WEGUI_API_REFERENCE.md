# WeGui API Reference

> Lightweight embedded GUI framework for multiple MCU / SoC platforms.
> Dual-target: STM32F103 hardware (Keil AC5) + SDL2 PC simulator (CMake + MinGW).

---

## 1. Architecture Overview

```
┌──────────────────────────────────────────────────────┐
│                    User Application                   │
│          (main.c / main_sim.c / demo_xxx.c)          │
├──────────────────────────────────────────────────────┤
│            Widget Layer (Core/widgets/...)            │
│   label │ btn │ img │ img_ex │ arc │ group │ ...      │
├──────────────────────────────────────────────────────┤
│          GUI Kernel (Core/we_gui_driver.h)            │
│   tick │ timer │ anim │ dirty-rect │ PFB render       │
├──────────────────────────────────────────────────────┤
│         Platform Port Config (per-target header)      │
│   LCD size │ color depth │ flush callback │ GPIO      │
└──────────────────────────────────────────────────────┘
```

### Directory Layout

| Path | Description |
|------|-------------|
| `Core/` | Platform-independent kernel, widgets, dirty-rect engine, fonts |
| `Demo/` | Demo applications, each widget has its own `demo_xxx.c` |
| `STM32F103/` | Hardware MCU entry, Keil project, LCD SPI port |
| `Simulator/` | SDL2 simulator entry, SDL port, simulator config |

### Key Concepts

- **PFB (Partial Frame Buffer)**: Rendering uses a small line buffer, not full framebuffer. Memory-efficient for MCUs.
- **Dirty Rectangle**: Only changed regions are redrawn. Strategy 0=full, 1=single merged box, 2=multi-rect (up to `WE_CFG_DIRTY_MAX_NUM`).
- **512-step Angle System**: Full circle = 512 steps. 90° = 128. Normalize with `& 0x1FF`. Use `WE_DEG(degrees)` macro.
- **Q8 Fixed-point Scale**: `scale_256 = 256` means 1.0x. `128` = 0.5x, `512` = 2.0x.
- **Object Linked List**: All widgets register into `lcd->obj_list_head` chain on init. Draw order = insertion order.

---

## 2. Initialization & Main Loop

### 2.1 All-in-one Init

```c
#include "we_gui_driver.h"

we_lcd_t mylcd;
colour_t user_gram[USER_GRAM_NUM];

// Single call initializes LCD + input + storage ports
we_gui_init(&mylcd,
    RGB888TODEV(10, 14, 20),      // background color
    user_gram, USER_GRAM_NUM,      // PFB buffer and size
    lcd_set_addr,                  // set-window callback
    LCD_FLUSH_PORT,                // flush callback
    we_input_port_read,            // input read callback (NULL if unused)
    we_storage_port_read);         // storage read callback (NULL if unused)
```

Or use individual init functions:

```c
we_lcd_init_with_port(&mylcd, bg, gram, gram_size, set_addr_cb, flush_cb);
we_input_init_with_port(&mylcd, input_cb);    // requires WE_CFG_ENABLE_INPUT_PORT_BIND == 1
we_storage_init_with_port(&mylcd, storage_cb); // requires WE_CFG_ENABLE_STORAGE_PORT_BIND == 1
```

### 2.2 Main Loop Pattern

```c
// STM32:
while (1) {
    if (sys1ms_stick >= 1U) {
        uint16_t ms = sys1ms_stick;
        sys1ms_stick = 0;
        we_gui_tick_inc(&mylcd, ms);   // feed elapsed time
    }
    we_gui_task_handler(&mylcd);       // input + timers + render + flush
}

// Simulator (additional SDL call):
while (sim_handle_events(&mylcd)) {
    uint32_t delta = SDL_GetTicks() - last_tick;
    if (delta > 0U) {
        we_gui_tick_inc(&mylcd, (uint16_t)delta);
        last_tick += delta;
    }
    we_gui_task_handler(&mylcd);
    sim_lcd_update();                  // push PFB to SDL window
}
```

### 2.3 Color Macro

```c
colour_t c = RGB888TODEV(r, g, b);  // converts 8-bit RGB to device colour_t
```

`colour_t` is a union — RGB565 uses `uint16_t dat16`, RGB888 uses `struct { uint8_t r, g, b; } rgb`.

---

## 3. Timer API

Demo animations and periodic logic use GUI timers:

```c
// 节点由调用方持有（静态或生命周期覆盖使用期）——零槽位、数量无上限、不会失败
static we_gui_timer_t anim_timer;
we_gui_timer_create(&mylcd, &anim_timer, my_callback, 16U, 1U); // 16ms 周期 ≈ 60fps

// Timer callback signature:
void my_callback(we_lcd_t *lcd, uint16_t elapsed_ms);

// Control:
we_gui_timer_stop(&mylcd, &anim_timer);     // pause (clears accumulator)
we_gui_timer_start(&mylcd, &anim_timer);    // resume
we_gui_timer_restart(&mylcd, &anim_timer);  // reset + reactivate
we_gui_timer_delete(&mylcd, &anim_timer);   // unlink（节点可复用）

// One-shot timer (fires once then auto-stops):
static we_gui_timer_t hide_timer;
we_gui_timer_create(&mylcd, &hide_timer, hide_cb, 2000U, 0U);
```

定时器与中央动画引擎同构：侵入式链表节点，create 幂等（已挂链仅刷新参数），
无 `WE_CFG_GUI_TIMER_MAX_NUM` 上限（该宏已移除）。

### 3.1 中央动画引擎 (we_anim_t)

控件动画统一由中央动画引擎驱动，**不占用任何槽位**、数量无上限、不会注册失败。
动画节点内嵌在控件结构体中（零堆分配），`we_gui_task_handler()` 每个调度周期遍历链表并调用 step 回调：

```c
// 节点（通常内嵌在控件结构体里）
typedef struct we_anim_s {
    struct we_anim_s *next;
    void (*step_cb)(void *owner, uint16_t elapsed_ms);
    void *owner;
} we_anim_t;

// 挂入动画链表（已在链上则仅更新回调；不会失败）
we_anim_start(&mylcd, &obj->anim, my_step_cb, obj);

// 摘链（不在链上则为空操作）
we_anim_stop(&mylcd, &obj->anim);
```

约定：
1. 动画到达目标态后在 step_cb 内自行 `we_anim_stop` 摘链（空闲零开销）；
2. step_cb 内只允许摘除自身节点；
3. **删除带动画的控件前必须先 `we_anim_stop`**（节点归控件所有，内核无法代摘）——
   各控件的 `we_xxx_obj_delete()` 已内置此调用，自定义删除路径须自行保证。

内置使用者：`toggle`、`progress`、`indicator`、`msgbox`、`slideshow`、`scroll_panel`、
`dropdown`（滚动条淡出）。

---

## 4. Widget API

All widgets follow the same pattern:
1. Declare a **static** object variable
2. Call `we_xxx_obj_init(...)` — registers into render chain automatically
3. Use `we_xxx_set_yyy(...)` to modify — auto-marks dirty for redraw

### 4.1 Label (Text)

```c
#include "we_widget_label.h"

static we_label_obj_t my_label;

// Init
we_label_obj_init(&my_label, &mylcd,
    10, 10,                           // x, y
    "Hello WeGui",                    // UTF-8 text (supports \n for multiline)
    we_font_consolas_18,              // font array pointer
    RGB888TODEV(255, 255, 255),       // text color
    255);                             // opacity (0~255)

// Modify at runtime
we_label_set_text(&my_label, "New text");
we_label_set_color(&my_label, RGB888TODEV(255, 0, 0));
we_label_set_opacity(&my_label, 128);
we_label_obj_set_pos(&my_label, 20, 30);
we_label_obj_delete(&my_label);       // remove from render chain
```

**Font**: `we_font_consolas_18` is the built-in font. `font[0]` = line height (19px). Font supports UTF-8, uses binary search for glyph lookup.

### 4.2 Button

```c
#include "we_widget_btn.h"

static we_btn_obj_t my_btn;

// Init (with NULL event callback = use internal default)
we_btn_obj_init(&my_btn, &mylcd,
    50, 80,                      // x, y
    120, 40,                     // width, height
    "Click Me",                  // button text
    we_font_consolas_18,         // font
    NULL);                       // event callback (NULL = default)

// State control
we_btn_set_state(&my_btn, WE_BTN_STATE_NORMAL);    // 0
we_btn_set_state(&my_btn, WE_BTN_STATE_SELECTED);  // 1
we_btn_set_state(&my_btn, WE_BTN_STATE_PRESSED);   // 2
we_btn_set_state(&my_btn, WE_BTN_STATE_DISABLED);  // 3

// Appearance
we_btn_set_text(&my_btn, "New Label");
we_btn_set_radius(&my_btn, 12);       // corner radius

// Custom style (only when WE_BTN_USE_CUSTOM_STYLE == 1)
we_btn_set_style(&my_btn, WE_BTN_STATE_NORMAL,
    RGB888TODEV(40, 40, 60),          // bg
    RGB888TODEV(80, 80, 120),         // border
    RGB888TODEV(255, 255, 255),       // text
    2);                               // border width
```

**Custom event callback**:
```c
static uint8_t my_event_cb(void *obj, we_event_t event, we_indev_data_t *data) {
    if (event == WE_EVENT_PRESSED) { /* handle press */ }
    if (event == WE_EVENT_RELEASED) { /* handle release */ }
    if (event == WE_EVENT_CLICKED) { /* handle click */ }
    if (event == WE_EVENT_SWIPE_LEFT) { /* handle swipe left */ }
    return 1; // 1 = consume event, 0 = pass through
}
we_btn_obj_init(&my_btn, &mylcd, x, y, w, h, "text", font, my_event_cb);
```

### 4.3 Image

```c
#include "we_widget_img.h"

static we_img_obj_t my_img;

// 资源为 image_res.h v2 格式数组，由 tool/2.img2c 例程生成（res_img.h）
we_img_obj_init(&my_img, &mylcd, 10, 50, demo_rgb565_raw_be_64x80, 255);

we_img_obj_set_opacity(&my_img, 128);
we_img_obj_set_pos(&my_img, 20, 60);

// A1/A2/A4/A8 透明位图专用：前景色上色（默认白色，其余格式忽略）
we_img_obj_set_color(&my_img, RGB888TODEV(120, 230, 205));

we_img_obj_delete(&my_img);
```

Supported formats: RGB565 raw, ARGB8565 raw, RGB565/ARGB8565 indexed QOI (`WE_CFG_ENABLE_INDEXED_QOI` trims), A1/A2/A4/A8 alpha bitmaps (tinted via `we_img_obj_set_color`). Unsupported formats are rejected at `we_img_obj_init`.

### 4.4 Image Ex (Rotation/Scale)

```c
#include "we_widget_img_ex.h"

static we_img_ex_obj_t my_img_ex;

// Init: cx, cy = screen rotation center
we_img_ex_obj_init(&my_img_ex, &mylcd, 140, 120, &img_RGB153, 255);

// Rotate + scale
we_img_ex_obj_set_transform(&my_img_ex,
    WE_DEG(45),       // angle: 45° in 512-step system
    256);              // scale: 1.0x (256 = 1.0)

// Eccentric pivot (offset from image geometric center)
we_img_ex_obj_set_pivot_offset(&my_img_ex, -20, 0);

// Move rotation center
we_img_ex_obj_set_center(&my_img_ex, 160, 130);

we_img_ex_obj_set_opacity(&my_img_ex, 200);
```

**Important**: img_ex only supports **uncompressed RGB565 raw images** for rotation/scale. No RLE/QOI.

### 4.5 Arc (Progress Ring)

```c
#include "we_widget_arc.h"

static we_arc_obj_t my_arc;

// Init
we_arc_obj_init(&my_arc, &mylcd,
    140, 120,                           // center x, y
    60,                                 // outer radius
    10,                                 // thickness
    WE_DEG(135),                        // start angle (512-step)
    WE_DEG(405),                        // end angle (can exceed 360)
    RGB888TODEV(0, 200, 150),           // foreground color
    RGB888TODEV(40, 50, 60),            // background track color
    255);                               // opacity

// Set progress value (0~255, 0=empty, 255=full)
we_arc_set_value(&my_arc, 180);

we_arc_set_opacity(&my_arc, 128);
we_arc_obj_set_pos(&my_arc, 100, 100);
we_arc_obj_delete(&my_arc);
```

### 4.6 Slideshow / Group

The `group` widget hosts child widgets in a shared local coordinate system. The `slideshow` widget is built on `group` and adds paged scrolling and swipe-to-snap behavior. Refer to:

- `Core/widgets/group/widget.md`
- `Core/widgets/slideshow/widget.md`

for the canonical API list of these composite widgets.

### 4.7 Other widgets

For per-widget API and behavior, see the corresponding `widget.md` under:

- `Core/widgets/checkbox/widget.md`
- `Core/widgets/label_ex/widget.md`
- `Core/widgets/chart/widget.md`
- `Core/widgets/toggle/widget.md`
- `Core/widgets/progress/widget.md`
- `Core/widgets/msgbox/widget.md`
- `Core/widgets/img_flash/widget.md`
- `Core/widgets/font_flash/widget.md`
- `Core/widgets/slider/widget.md`
- `Core/widgets/scroll_panel/widget.md`
- `Core/widgets/dropdown/widget.md`
- `Core/widgets/stepper/widget.md`
- `Core/widgets/indicator/widget.md`
- `Core/widgets/gauge/widget.md`
- `Core/widgets/list/widget.md`
- `Core/widgets/roller/widget.md`
- `Core/widgets/marquee/widget.md`
- `Core/widgets/toast/widget.md`

Experimental (preview zone) widgets keep the same convention under `Core/widgets_preview/<name>/widget.md` — e.g. `Core/widgets_preview/mask_group/widget.md`; they are simulator-only and may be removed at any time.

Notable rendering notes:

- `toggle` track and thumb are drawn via the shared analytic round-rect fill renderer.
- `checkbox` box geometry is drawn via the shared analytic round-rect fill.
- `chart` waveform-body / feathering ideas reference Arm-2D, but the implementation has been rewritten for WeGui's ring-buffer, dirty-rectangle, PFB clipping and integer coordinate pipeline.
- `dropdown` expanded list uses pixel-level free scrolling; the scrollbar auto-fades to a residual minimum opacity (`WE_DROPDOWN_SB_IDLE_ALPHA`) after idle, driven by the central animation engine (no timer slot). Its expanded list is an embedded top-layer object registered as the modal (`we_modal_open`), so only one modal popup is open screen-wide.
- `stepper` stores its value as fixed-point `int32` (real value = `value / 10^decimals`); hold-to-repeat reuses the `STAY` event and consumes no timer slot.
- `indicator` animates its on/off color transition (optional glow) via the central animation engine (see 3.1, no timer slot); default is read-only, opt into click-toggle with `we_indicator_set_clickable()`.
- All widget animations (`toggle`/`progress`/`indicator`/`msgbox`/`slideshow`/`scroll_panel`/`dropdown`) run on the central animation engine — none of them consume timer slots.
- `slider`/`toggle`/`checkbox` support a value-changed callback (`we_xxx_set_changed_cb`), fired only on user interaction (programmatic setters do not fire it); `dropdown`/`stepper` had equivalent callbacks already.
- A bare `group` now hit-forwards touch events to its children (press-lock + click re-verification), so interactive widgets inside a plain group receive input; a fully transparent group does not intercept input.
- Container opacity propagates to children: `we_group_set_opacity` (and slideshow/scroll_panel opacity) fades the whole subtree. Mechanism: containers multiply into the lcd-level `opa_scale` around their children pass; every primitive applies it once at entry (`we_opa_apply`) — zero per-pixel cost when no fade is active, nesting composes automatically.
- `gauge` redraws differentially: a value change invalidates only the old + new pointer footprints (static tick ring never redrawn; equal quantized angles submit nothing). Tick geometry is cached at init/`set_range`/`set_tick_count`, and value→angle uses a Q16 slope pre-divided in `set_range` — zero trig and zero mul/div in the draw callback.
- `list` injects inertia from both drag release and no-STAY fast swipes, allows ±24px rubber-band overscroll with rebound, and fades its scrollbar after 600ms idle to a resident low alpha (dropdown idiom). Dirty marking is per-interaction: one row strip on press/release, the content clip rect on scroll, the scrollbar strip on fade.
- `roller` snaps to the nearest row on slow release and flings on fast release (velocity-projected landing row, release velocity seeds the snap animation). Scrolling dirty-marks only the centered text column band, and text measurement is fully cached (font-constant y-bbox + row-width cache) — zero measurement calls in the draw loop.
- `marquee` scrolls as a seamless two-segment loop on one central anim node and draws through a windowed glyph loop: glyphs left of the window only advance the cursor (no bitmap fetch), and drawing stops past the right edge. Static (non-scrolling) when the text fits; input passes through (`event_cb` NULL).
- `toast` is a non-modal slide-in/stay/slide-out banner on one central anim node; it never blocks input and does not take the modal slot (a toast can float above an open keyboard). Each animation step submits a single union of old + new bboxes; over-wide text is tail-truncated with "..." (prefix drawn zero-copy via PFB right-edge narrowing).
- (preview zone) `mask_group` — incubating in `Core/widgets_preview/mask_group/` — is an effect container: children draw at full speed through the normal narrowed-PFB pass (same idiom as group/scroll_panel), then the container runs a per-stripe post-pass in order gradient → border strips → corner compositing toward a solid backdrop color. Corners are **per-corner independently configurable** round/chamfer/square (`we_mask_group_set_corner(idx, style, r)`, geometry identical to box: K×K corner squares, single-pass `we_mask_quarter_ring_alpha` outer+inner coverage for bordered round corners, chamfer per-row spans with alpha 0/128/255 and 0.586·bw inner inset). A border (`we_mask_group_set_border(color, width)`) clips content at its inner edge (CSS-box-like picture frame); the gradient fades content only — the border stays solid, and container opacity converges it toward the backdrop via a once-per-frame `bd_eff`. Rotated linear gradient uses the 512-step angle system (`0` = +X, `128` = +Y); the inner loop is one int32 add per pixel (Q16 DDA, per-stripe setup uses 3 int64 divisions). All-square corners with no border and no gradient degenerates to a zero-cost rect-clip group. Solid-backdrop semantics: masked-out pixels are restored to `backdrop` (defaults to the LCD bg color), so stacking the container on top of images/other widgets will reveal the backdrop color in masked areas (true backdrop capture would need a snapshot buffer — not in v1). Conic/sweep gradients are intentionally not offered (per-pixel atan2 is not viable on M0).

---

## 5. Easing Functions (we_motion.h)

Pure Q8 fixed-point easing library. `t ∈ [0, 256]`, output ∈ `[0, 256]` (except `out_back` which can overshoot).

```c
#include "we_motion.h"

uint16_t val = we_ease_out_quad(t);  // t=0→0, t=256→256

// Available functions:
we_ease_linear(t);        // Linear
we_ease_in_quad(t);       // Slow start
we_ease_out_quad(t);      // Slow end (most common)
we_ease_in_out_quad(t);   // Slow-fast-slow
we_ease_out_cubic(t);     // Faster than quad
we_ease_in_out_sine(t);   // Smoothest (uses we_cos)
we_ease_out_bounce(t);    // Bounces 3 times
we_ease_out_back(t);      // Overshoots then returns (>256 briefly)

// Combine with we_lerp for property animation:
int32_t x = we_lerp(start_x, end_x, we_ease_out_quad(t));
```

**`we_lerp`** (in `we_gui_driver.h`):
```c
static inline int32_t we_lerp(int32_t from, int32_t to, uint16_t t);
// from + (to - from) * t / 256
```

**`we_ease_fn_t`** function pointer type:
```c
typedef uint16_t (*we_ease_fn_t)(uint16_t t);
```

---

## 6. Text Layout Utilities

```c
// Measure text width in pixels (single line, stops at \n)
uint16_t w = we_get_text_width(we_font_consolas_18, "Hello");

// Measure actual visible vertical bounds (for precise centering)
int8_t y_top, y_bot;
we_get_text_bbox(we_font_consolas_18, "Hello", &y_top, &y_bot);
// y_top = topmost glyph edge offset from cursor_y
// y_bot = bottommost glyph edge offset from cursor_y
// Visual height = y_bot - y_top

// Vertical centering formula:
int16_t cursor_y = center_y - (y_top + y_bot) / 2;

// Draw string (supports \n for multiline)
we_draw_string(&mylcd, x, y, we_font_consolas_18, "Hello\nWorld",
               RGB888TODEV(255, 255, 255), 255);
```

### Font Format

Font array layout:
- `[0]`: line height (pixels, e.g., 19 for Consolas 18)
- `[1]`: flags (bit7=ASCII fast path) | bpp (low 4 bits, typically 4)
- `[2-3]`: glyph count (uint16 LE)
- `[4]`: ASCII range start
- `[5]`: ASCII range count
- `[6..]`: Unicode index + glyph metadata + bitmap data

---

## 7. Low-level Drawing API

```c
// Fill entire PFB with solid color
we_fill_gram(&mylcd, RGB888TODEV(0, 0, 0));

// Clear PFB to zero
we_clear_gram(&mylcd);

// Draw filled rounded rectangle
we_draw_round_rect_analytic_fill(&mylcd, x, y, w, h, radius, color, opacity);

// Draw alpha mask (used internally for font rendering)
we_draw_alpha_mask(&mylcd, x, y, w, h, mask_data, bpp, fg_color, opacity);

// Push a PFB region to LCD hardware
we_push_pfb(&mylcd, x, y, w, h);
```

---

## 8. Object Base Class API

Every widget inherits from `we_obj_t`:

```c
typedef struct we_obj_t {
    struct we_lcd_t *lcd;
    int16_t x, y;
    uint16_t w, h;
    const we_class_t *class_p;   // draw_cb + event_cb
    struct we_obj_t *next;
    struct we_obj_t *parent;
} we_obj_t;
```

```c
// Move any widget
we_obj_set_pos((we_obj_t *)&my_label, new_x, new_y);

// Remove from render chain
we_obj_delete((we_obj_t *)&my_label);

// Manually mark widget dirty (trigger redraw)
we_obj_invalidate((we_obj_t *)&my_label);

// Mark a custom area dirty
we_obj_invalidate_area((we_obj_t *)&my_label, x, y, w, h);
```

---

## 9. Math Utilities

```c
// Trigonometry (512-step angle, returns Q15: -32768~32767)
int16_t s = we_sin(angle_512);
int16_t c = we_cos(angle_512);

// Angle conversion
int16_t a = WE_DEG(90);         // compile-time integer: 90° → 128
int16_t b = WE_ANGLE(45.5f);    // runtime float: 45.5° → 65

// Linear interpolation
int32_t v = we_lerp(0, 1000, 128);  // = 500 (128/256 = 0.5)

// Common macros
WE_ABS(x)
WE_MIN(a, b)
WE_MAX(a, b)
```

---

## 10. Input System

Input events are dispatched by `we_gui_task_handler` automatically.

```c
typedef struct {
    int16_t x, y;
    uint8_t state;   // we_touch_state_t
} we_indev_data_t;

// Touch states:
WE_TOUCH_STATE_NONE      // 0: no input
WE_TOUCH_STATE_PRESSED   // 1: just pressed
WE_TOUCH_STATE_RELEASED  // 2: just released
WE_TOUCH_STATE_STAY      // 3: held down

// Events dispatched to widgets:
WE_EVENT_PRESSED    // finger down on widget
WE_EVENT_RELEASED   // finger up
WE_EVENT_CLICKED    // pressed + released on same widget
WE_EVENT_STAY       // held
WE_EVENT_VALUE_CHG   // value changed
WE_EVENT_SCROLLED    // external scroll
WE_EVENT_SWIPE_LEFT  // swipe left gesture
WE_EVENT_SWIPE_RIGHT // swipe right gesture
WE_EVENT_SWIPE_UP    // swipe up gesture
WE_EVENT_SWIPE_DOWN  // swipe down gesture
```

**Swipe gesture detection**: Built into `we_gui_indev_handler`. On RELEASED, if the displacement from press point exceeds `WE_CFG_SWIPE_THRESHOLD` (default 30px), a swipe event is dispatched instead of CLICKED. The dominant axis (|dx| vs |dy|) determines horizontal or vertical swipe. Container widgets automatically handle swipe events to snap to the next page in the swipe direction.

Hit testing: the **last** widget in the linked list that contains the touch point receives the event (painter's algorithm — top-most widget wins).

---

## 11. Platform Port Configuration

Each platform must define these macros before including `we_gui_config.h`:

| Macro | Description | Example |
|-------|-------------|---------|
| `LCD_DEEP` | Color depth | `DEEP_RGB565` or `DEEP_RGB888` |
| `SCREEN_WIDTH` | Screen width in pixels | `280` |
| `SCREEN_HEIGHT` | Screen height in pixels | `240` |
| `GRAM_DMA_BUFF_EN` | DMA double-buffer enable | `0` or `1` |
| `USER_GRAM_NUM` | PFB buffer size in pixels | `SCREEN_WIDTH * 2` |
| `WE_CFG_DIRTY_STRATEGY` | Dirty rect strategy | `0`, `1`, or `2` |
| `WE_CFG_DIRTY_MAX_NUM` | Max dirty rects (strategy 2) | `8` |
| `WE_CFG_DEBUG_DIRTY_RECT` | Show dirty rects in red | `0` or `1` |
| `WE_CFG_ENABLE_INDEXED_QOI` | Enable indexed QOI decode | `0` or `1` |
| `WE_CFG_GUI_TIMER_MAX_NUM` | User timer slots | `8` |
| `WE_CFG_ENABLE_INPUT_PORT_BIND` | Enable input port binding | `0` or `1` |
| `WE_CFG_ENABLE_STORAGE_PORT_BIND` | Enable storage port binding | `0` or `1` |

Optional macros (defaulted by `we_gui_config.h` when the platform omits them):

| Macro | Description | Default |
|-------|-------------|---------|
| `WE_LCD_FLUSH_ALIGN_X` | Flush-window X alignment granularity (power of two). Dirty rects are expanded at intake (`we_dirty_invalidate`) so every `set_addr` window has `x0 % A == 0` and `(x1 + 1) % A == 0`. For QSPI panels requiring x multiples of 2/4. Requires `SCREEN_WIDTH % A == 0`. | `1` |
| `WE_LCD_FLUSH_ALIGN_Y` | Flush-window Y alignment granularity (power of two), same intake expansion for y. For QSPI panels (2/4) or SSD1306-class page OLEDs (`8`). Requires `SCREEN_HEIGHT % A == 0` and PFB rows (`USER_GRAM_NUM / SCREEN_WIDTH`) `% A == 0` so PFB chunking inherits the alignment. | `1` |

See `Demo/we_lcd_port_template.h` for worked QSPI and SSD1306 (RGB565→1bpp page packing) port examples.

Config header chain: `we_port.h` → selects platform config → `we_gui_config.h` validates all macros.

---

## 12. Demo Pattern (How to Write a Demo)

Every demo follows the same structure:

```c
// Demo/demo_xxx.c
#include "simple_widget_demos.h"
#include "demo_common.h"
#include "we_widget_xxx.h"

// 1. Static variables (widgets + animation state)
static we_label_obj_t xxx_title;
static we_label_obj_t xxx_fps;
static uint32_t xxx_fps_timer;
static uint32_t xxx_last_frames;
static char xxx_fps_buf[16];

// 2. Init function
void we_xxx_simple_demo_init(we_lcd_t *lcd) {
    // Create widgets with scaled positions
    int16_t margin_x = we_demo_scale_x(lcd, 10);
    we_label_obj_init(&xxx_title, lcd, margin_x, 10,
                      "XXX DEMO", we_font_consolas_18,
                      RGB888TODEV(236, 241, 248), 255);
    // ... more widgets ...
}

// 3. Tick function (called by timer at 16ms interval)
void we_xxx_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick) {
    if (lcd == NULL || ms_tick == 0U) return;
    // Animation logic here...
    we_demo_update_fps(lcd, &xxx_fps, &xxx_fps_timer,
                       &xxx_last_frames, xxx_fps_buf, ms_tick);
}
```

Declare in `Demo/simple_widget_demos.h`:
```c
void we_xxx_simple_demo_init(we_lcd_t *lcd);
void we_xxx_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);
```

Register in main:
```c
static we_gui_timer_t demo_timer;
we_xxx_simple_demo_init(&mylcd);
we_gui_timer_create(&mylcd, &demo_timer, we_xxx_simple_demo_tick, 16U, 1U);
```

### Demo Scale Helpers

```c
int16_t we_demo_scale_x(const we_lcd_t *lcd, int16_t base_x);   // scale X for screen width
int16_t we_demo_scale_y(const we_lcd_t *lcd, int16_t base_y);   // scale Y for screen height
int16_t we_demo_scale_w(const we_lcd_t *lcd, int16_t base_w);   // scale width
int16_t we_demo_scale_h(const we_lcd_t *lcd, int16_t base_h);   // scale height
int16_t we_demo_right_x(const we_lcd_t *lcd, int16_t margin, int16_t obj_w);  // right-aligned X
int16_t we_demo_bottom_y(const we_lcd_t *lcd, int16_t margin, int16_t obj_h); // bottom-aligned Y
```

---

## 13. Available Resources

### Built-in Font
- `we_font_consolas_18` — Consolas 18pt, line height 19px, 4bpp anti-aliased, ASCII 0x20~0x7E (95 glyphs)

### Built-in Images
- `img_RGB153`, `img_RGB608`, `img_RGB567`, `img_RGB535`
- `img_RGB186`, `img_RGB345`, `img_RGB128`, `img_RGB129`
- `img_BG`

### Existing Demos (DEMO_ID)

Numbering is unified across all three targets — `1..28` are identical. The simulator
additionally defines `0 = showcase` (simulator-only, needs 800×480). Select a demo by
editing the `#define DEMO_ID` line near the top of `main`.

| ID | Demo |
|----|------|
| 0 | showcase (simulator-only, 800×480) |
| 1 | label |
| 2 | btn |
| 3 | img |
| 4 | img_ex |
| 5 | arc |
| 6 | group |
| 7 | slideshow |
| 8 | concentric arc |
| 9 | checkbox |
| 10 | label_ex |
| 11 | chart |
| 12 | toggle |
| 13 | progress |
| 14 | msgbox |
| 15 | flash img |
| 16 | flash font |
| 17 | slider |
| 18 | scroll_panel |
| 19 | dropdown |
| 20 | stepper |
| 21 | indicator |
| 22 | line |
| 23 | box |
| 24 | gauge |
| 25 | list |
| 26 | roller |
| 27 | marquee |
| 28 | toast |

---

## 14. Build Commands

### Simulator (wrapper-script preferred)
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/build_sim.ps1" -Clean
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/run_latest_sim.ps1"
```

### STM32 (Keil MDK-ARM AC5)
```powershell
UV4.exe -r "STM32F103\MDK-ARM\Project.uvprojx" -t "WeGui_ARGB"
```

---

## 15. Quick Start: Minimal Application

```c
#include "we_gui_driver.h"
#include "we_widget_label.h"
#include "we_widget_btn.h"

static we_lcd_t mylcd;
static colour_t user_gram[USER_GRAM_NUM];
static we_label_obj_t hello;
static we_btn_obj_t my_btn;

static void anim_tick(we_lcd_t *lcd, uint16_t ms) {
    // your animation logic here
}

int main(void) {
    // 1. Platform hardware init
    lcd_hw_init();

    // 2. GUI init
    we_gui_init(&mylcd, RGB888TODEV(10, 14, 20),
                user_gram, USER_GRAM_NUM,
                lcd_set_addr, LCD_FLUSH_PORT, NULL, NULL);

    // 3. Create widgets
    we_label_obj_init(&hello, &mylcd, 10, 10, "Hello WeGui!",
                      we_font_consolas_18,
                      RGB888TODEV(255, 255, 255), 255);

    we_btn_obj_init(&my_btn, &mylcd, 50, 50, 100, 36,
                    "Press", we_font_consolas_18, NULL);

    // 4. Register animation timer（节点调用方持有）
    static we_gui_timer_t anim_timer;
    we_gui_timer_create(&mylcd, &anim_timer, anim_tick, 16U, 1U);

    // 5. Main loop
    while (1) {
        if (sys1ms_stick >= 1U) {
            uint16_t ms = sys1ms_stick;
            sys1ms_stick = 0;
            we_gui_tick_inc(&mylcd, ms);
        }
        we_gui_task_handler(&mylcd);
    }
}
```

## 16. Focus & Key Navigation (V0.2.2+)

按键导航子系统完整说明见 `CLAUDE.md` 的 "Focus and Key Navigation" 章节与
`Core/we_gui_driver.h` 注释；此处为 API 速查。总开关 `WE_CFG_ENABLE_KEY_INPUT`
（共享配置=1；内核默认 0，纯触摸工程零成本剔除）。

### 端口注入（唯一入口，禁止轮询）

```c
we_gui_key_press(&mylcd, WE_KEY_OK);    // 按下沿（双沿端口；OK 按住保持按压态）
we_gui_key_release(&mylcd, WE_KEY_OK);  // 松开沿（仅 OK 消费，其余键可不上报）
we_gui_key_inject(&mylcd, WE_KEY_NEXT); // 完整 tap（简单端口；OK 经最短按压窗口）
```

语义键：`WE_KEY_UP/DOWN/LEFT/RIGHT/PREV/NEXT/OK/BACK`（并入 `we_event_t` 的
0x10 段）。注入 ISR 安全（SPSC 环形队列，深度 `WE_CFG_KEY_QUEUE_LEN`=8）。
`WE_KEY_EVT_*`（0x20 段）为内核→控件通知，端口禁止注入。

### 焦点管理

```c
we_focus_set(&mylcd, (we_obj_t *)&my_btn); // NULL = 清除焦点
we_obj_t *cur = we_focus_get(&mylcd);
uint8_t ok = we_focus_candidate(obj);      // 结构性候选判定（容器空子树拒停靠）
we_focus_edit_enter/exit/active(&mylcd);   // 编辑态（WE_CFG_FOCUS_EDIT=0 时为空 stub）
```

可聚焦性 = 类描述符 `class_flags` 带 `WE_CLASS_FLAG_FOCUSABLE`（2C 起 key_cb
已并入统一 `event_cb`：0x10/0x20 两段事件码只发给 FOCUSABLE 类）。裁剪宏：
`WE_CFG_FOCUS_EDIT` / `WE_CFG_FOCUS_NESTED` / 各控件 `WE_<NAME>_USE_KEY`。
光标外观：`WE_CFG_FOCUS_CURSOR_THICKNESS/GAP/R/G/B`、`WE_CFG_FOCUS_FLASH_MS`。
光标显隐随输入来源：按键活动或 `we_focus_set`/`we_focus_edit_enter` 亮出，
触摸按下收起（触摸的焦点跟随仍生效，只是不画环）。

## 17. Kernel Object & Layer API (refactor additions)

```c
/* 对象树（内核唯一持链人——控件不得手工摘挂链表） */
void we_obj_detach(we_obj_t *obj);                    /* 摘链保活（lcd/class_p 保留） */
void we_obj_set_parent(we_obj_t *obj, we_obj_t *p);   /* 改挂父子（非容器父=回顶层） */
void we_obj_delete(we_obj_t *obj);                    /* 唯一删除入口：delete_cb →
                                                         CHILD_OWNER 后序递归删子 →
                                                         内核回收按压/焦点/弹层/动画引用 */
void we_obj_attach_to_top(we_lcd_t *lcd, we_obj_t *o);/* 顶层链：保证置顶（toast/msgbox），
                                                         不打乱普通层 Z 序 */

/* 输入（内核统一派发） */
void we_indev_grab(we_lcd_t *lcd, we_obj_t *obj);     /* 祖先容器接管手势（原按压对象
                                                         收 RELEASED 回弹，不触发点击） */
/* 容器只需应答 WE_EVENT_HIT_TEST（0=连子树跳过）与 WE_EVENT_DRAG_BEGIN
 * （拖拽接管询问，越 WE_CFG_DRAG_THRESHOLD=8 时沿祖先链询问一轮） */

/* 滚动物理组件（Core/we_scroll.h，list 已迁移为样板） */
void    we_scroll_press(we_scroll_t *sc, int16_t c);
uint8_t we_scroll_stay(we_scroll_t *sc, const we_scroll_cfg_t *cfg, int16_t c, int32_t max);
uint8_t we_scroll_release(we_scroll_t *sc, int32_t max);      /* 非0=需启动惯性/回弹动画 */
uint8_t we_scroll_swipe(we_scroll_t *sc, const we_scroll_cfg_t *cfg, int16_t c, int32_t max);
uint8_t we_scroll_anim_step(we_scroll_t *sc, const we_scroll_cfg_t *cfg, uint16_t ms, int32_t max);
uint8_t we_scroll_set(we_scroll_t *sc, int32_t pos, int32_t max);
```

断言钩子：`WE_ASSERT(expr)`（`we_gui_config.h` 默认空实现，用户配置可覆盖）。
