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
│   tick │ timer │ task │ dirty-rect │ PFB render       │
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
// Create a 16ms periodic timer (≈60fps)
int8_t id = we_gui_timer_create(&mylcd, my_callback, 16U, 1U);

// Timer callback signature:
void my_callback(we_lcd_t *lcd, uint16_t elapsed_ms);

// Control:
we_gui_timer_stop(&mylcd, id);     // pause (clears accumulator)
we_gui_timer_start(&mylcd, id);    // resume
we_gui_timer_restart(&mylcd, id);  // reset + reactivate
we_gui_timer_delete(&mylcd, id);   // free the slot

// One-shot timer (fires once then auto-stops):
int8_t hide_id = we_gui_timer_create(&mylcd, hide_cb, 2000U, 0U);
```

Slots are limited by `WE_CFG_GUI_TIMER_MAX_NUM` (default 8).

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

// img_t is: { width, height, dat_type, *dat }
// Use pre-generated image arrays (e.g. img_RGB153)
we_img_obj_init(&my_img, &mylcd, 10, 50, &img_RGB153, 255);

we_img_obj_set_opacity(&my_img, 128);
we_img_obj_set_pos(&my_img, 20, 60);
we_img_obj_delete(&my_img);
```

Supported formats: RGB565/888/555/444/332, ARGB8888/6666/4444, RLE, QOI, indexed QOI.

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

Notable rendering notes:

- `toggle` track and thumb are drawn via the shared analytic round-rect fill renderer.
- `checkbox` box geometry is drawn via the shared analytic round-rect fill.
- `chart` waveform-body / feathering ideas reference Arm-2D, but the implementation has been rewritten for WeGui's ring-buffer, dirty-rectangle, PFB clipping and integer coordinate pipeline.

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
| `WE_CFG_GUI_TASK_MAX_NUM` | Internal task slots | `4` |
| `WE_CFG_GUI_TIMER_MAX_NUM` | User timer slots | `8` |
| `WE_CFG_ENABLE_INPUT_PORT_BIND` | Enable input port binding | `0` or `1` |
| `WE_CFG_ENABLE_STORAGE_PORT_BIND` | Enable storage port binding | `0` or `1` |

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
we_xxx_simple_demo_init(&mylcd);
we_gui_timer_create(&mylcd, we_xxx_simple_demo_tick, 16U, 1U);
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

### Existing Demos (demo_id for simulator)
| ID | Demo |
|----|------|
| 1 | label |
| 2 | btn |
| 3 | img |
| 4 | img_ex |
| 5 | arc |
| 6 | group |
| 7 | slideshow |
| 8 | concentric arc |
| 9 | key |
| 10 | checkbox |
| 11 | label_ex |
| 12 | chart |
| 13 | toggle |
| 14 | progress |
| 15 | msgbox |
| 16 | flash img |
| 17 | flash font |
| 18 | slider |
| 19 | dropdown |

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

    // 4. Register animation timer
    we_gui_timer_create(&mylcd, anim_tick, 16U, 1U);

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
