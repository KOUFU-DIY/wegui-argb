# AGENTS.md

This file provides guidance to code agents working with this repository.

## Project Overview

Lightweight embedded GUI framework for multiple MCU / SoC platforms, with dual-target support: STM32F103 hardware (Keil MDK-ARM AC5) and SDL2 PC simulator (CMake + MinGW/Ninja). The `Core` and `Demo` directories are shared between both targets; only the platform port layer differs.

## Build Commands

### STM32F103 Hardware (Keil MDK-ARM AC5)

```powershell
UV4.exe -r "STM32F103\MDK-ARM\Project.uvprojx" -t "WeGui_ARGB"
```

Build log: `STM32F103/MDK-ARM/Objects/Project.build_log.htm`

Flash via CMSIS-DAP / DAPLink (pyOCD usable). If flashed content appears stale, verify build log timestamp before reflashing.

### Simulator (wrapper-script preferred)

```powershell
# Preferred: use the repository build wrapper
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/build_sim.ps1" -Clean

# Run latest build
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/run_latest_sim.ps1"
```

`Simulator/build_sim.ps1` will prefer `ninja + gcc + g++` when available, and fall back to `mingw32-make + gcc + g++` otherwise.

VS Code tasks in `.vscode/tasks.json` follow the same wrapper-based flow.

## Architecture

### Directory Layout

- `Core/` — Platform-independent GUI kernel, widget implementations, dirty-rect engine
- `Demo/` — Demo applications; each widget type has its own `demo_xxx.c`; `simple_widget_demos.h` declares all entry points
- `STM32F103/` — Hardware MCU entry (`main.c`), Keil project, LCD SPI port implementations
- `Simulator/` — SDL2 simulator entry (`main_sim.c`), SDL port (`sdl_port.c/h`), and simulator config (`we_sim_port_config.h`)

### Platform Port Config Chain

`STM32F103/Lcd_Port/we_port.h` routes to the correct platform config header based on preprocessor defines:
- `WE_SIMULATOR` → `Simulator/we_sim_port_config.h`
- `WE_PLATFORM_CMS32C030` → CMS32C030 config
- default → `STM32F103/Lcd_Port/stm32f103_hw_config.h`

`stm32f103_hw_config.h` selects LCD IC (`LCD_IC`: `_ST7735`, `_ST7789V3`, `_ST7789VW`, `_ST7796S`, `_GC9A01`) and port type (`LCD_PORT`: `_HARD_4SPI`, `_DMA_4SPI`, etc.). It defines `lcd_set_addr`, `lcd_ic_init`, and `LCD_FLUSH_PORT` macros that connect to the concrete driver.

`Core/we_gui_config.h` enforces that every required config macro (`LCD_DEEP`, `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `WE_CFG_DIRTY_STRATEGY`, etc.) is defined by the platform config — it will `#error` if any is missing.

For porting to a new MCU, copy `Demo/we_lcd_port_template.h/.c` as the starting point.

### Initialization API

Primary init (bundles LCD + input + storage in one call):

```c
we_gui_init(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
            we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb,
            we_input_read_cb_t input_cb,      // NULL if unused
            we_storage_read_cb_t storage_cb); // NULL if unused
```

Lower-level alternative (LCD only, then bind input/storage separately):

```c
we_lcd_init_with_port(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base,
                      uint16_t gram_size,
                      we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb);
// optional, guarded by config flags:
we_input_init_with_port(we_lcd_t*, we_input_read_cb_t);    // WE_CFG_ENABLE_INPUT_PORT_BIND
we_storage_init_with_port(we_lcd_t*, we_storage_read_cb_t); // WE_CFG_ENABLE_STORAGE_PORT_BIND
```

Hardware-side naming conventions: `lcd_hw_init()`, `lcd_rgb565_port()`, `lcd_rgb888_port()`.

### Main Loop Pattern

Demo ticks are driven by GUI timers, not called directly in the main loop:

```c
lcd_hw_init();
we_gui_init(&mylcd, RGB888TODEV(10,14,20), user_gram, USER_GRAM_NUM,
            lcd_set_addr, LCD_FLUSH_PORT, input_cb, storage_cb);
we_xxx_simple_demo_init(&mylcd);
we_gui_timer_create(&mylcd, we_xxx_simple_demo_tick, 16U, 1U); // 16ms periodic

while (1) {
    we_gui_tick_inc(&mylcd, ms);   // pass actual elapsed ms (from SysTick/SDL delta)
    we_gui_task_handler(&mylcd);   // drives timers + rendering
}
```

Input is polled automatically inside `we_gui_task_handler` via the registered callback. Call `we_gui_indev_handler()` directly only when managing input state manually.

Simulator additionally calls `sim_lcd_update()` after `we_gui_task_handler`.

### Timer API

```c
int8_t id = we_gui_timer_create(lcd, cb, period_ms, repeat); // repeat=1 periodic, 0 one-shot
we_gui_timer_stop(lcd, id);
we_gui_timer_start(lcd, id);
we_gui_timer_restart(lcd, id);   // reset accumulator + reactivate
we_gui_timer_delete(lcd, id);
```

Fixed array: `WE_CFG_GUI_TASK_MAX_NUM` internal task slots, `WE_CFG_GUI_TIMER_MAX_NUM` user timer slots. Both are configured per-platform.

### Widgets

Widget types in `Core/`: `label`, `btn`, `img`, `img_ex`, `arc`, `group`, `checkbox`, `label_ex`, `chart`, `toggle`, `progress`, `msgbox`, `img_flash`, `font_flash`, `slideshow`, `slider`.

**img_ex semantics:**
- Angle unit is **512-step** (0–511 = full circle); 90° = 128, 180° = 256, 270° = 384. Use `WE_ANGLE(deg)` for float conversion or `WE_DEG(deg)` for integer compile-time constants. Normalized internally with `& 0x1FF` (no division).
- `cx/cy` = screen transform center; `pivot_ofs_x/y` = source image local pivot offset — these describe different coordinate systems and must not be merged

**label_ex**: rotatable/scalable text widget (uses same 512-step angle system as `img_ex`).

**chart**: scrolling waveform chart with circular buffer and optional grid overlay. Waveform-body + feathering thinking references Arm-2D, but the implementation has been rewritten for WeGui's ring-buffer, dirty-rect, PFB clipping, and integer-coordinate pipeline.

**group**: lightweight child-container used to manage a set of child widgets with local coordinates, opacity propagation, and coordinated movement. Also used as the structural base for composite controls.

**slideshow**: paged composite widget built on top of `group`; each page uses local coordinates and supports swipe/page snapping behavior.

**toggle**: iOS-style animated switch. Track and thumb both reuse the shared analytic round-rect fill renderer.

**checkbox**: box geometry now uses the shared analytic round-rect fill; check mark and label drawing remain unchanged.

**progress**: horizontal progress bar using a direct `0~255` target value model with smooth animated display transitions and fine-grained dirty updates.

**msgbox**: modal popup that slides in from the top, with one-button or two-button layout.

**img_flash / font_flash**: image / font assets stored on external SPI flash. Both require a registered storage read callback to function.

**Motion system** (`Core/we_motion.h`): provides easing/interpolation helpers used by widget animations.

### Dirty Rectangle Strategy (`WE_CFG_DIRTY_STRATEGY` in platform config)

- `0`: full-screen redraw
- `1`: one merged bounding box
- `2`: multi-rect (default; up to `WE_CFG_DIRTY_MAX_NUM` rects)

Enable `WE_CFG_DEBUG_DIRTY_RECT` to overlay dirty regions in red during development.

`we_lcd_t` tracks rendering stats: `stat_render_frames`, `stat_pfb_pushes`, `stat_pushed_pixels`.

### Input & Gesture

`we_indev_data_t indev_data` lives inside `we_lcd_t`. Do not relocate it unless redesigning input.

**Swipe gesture detection** is built into `we_gui_indev_handler`. On RELEASED, if displacement from press exceeds `WE_CFG_SWIPE_THRESHOLD` (default 30px, overridable in platform config), a directional swipe event (`WE_EVENT_SWIPE_LEFT/RIGHT/UP/DOWN`) is dispatched instead of `WE_EVENT_CLICKED`. Container widgets automatically handle swipe events to snap to the next page.

## Demo Style

Each demo follows the unified pattern:

```c
void we_xxx_simple_demo_init(we_lcd_t *lcd);
void we_xxx_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);
```

One demo = one group of static variables + one init + one tick. Each demo has its own `Demo/demo_xxx.c` file; all declarations are in `Demo/simple_widget_demos.h`.

**Selecting active demo:**
- **STM32** (`STM32F103/main.c`): switch to the desired demo path in the current entry-file logic, then rebuild/flash
- **Simulator** (`Simulator/main_sim.c`): change `demo_id` (1–18) at the top of `main`

The `we_timer_page_message_demo` additionally creates its own internal timers for auto page-switching and message hide.

## Code Style

- Comments are in Chinese — use targeted edits only; do not do bulk text replacement (risk of mojibake)
- Prefer direct, readable code with minimal abstraction layers
- Prefer static variables in demos over complex state shells
- Keep demo code easy to copy into new projects

## Key Files to Read First

When starting a new session involving this repo:

1. `STM32F103/main.c` — hardware entry and demo selection
2. `Simulator/main_sim.c` — simulator entry
3. `Demo/simple_widget_demos.h` — all demo entry point declarations
4. `Core/we_gui_driver.h` — core API surface
