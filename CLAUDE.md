# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Lightweight embedded GUI framework for STM32 ARM Cortex-M MCUs with dual-target support: STM32F103 hardware (Keil MDK-ARM AC5) and SDL2 PC simulator (CMake + MinGW). The `Core` and `Demo` directories are shared between both targets; only the platform port layer differs.

Full API reference: `WEGUI_API_REFERENCE.md`.

## Build Commands

### STM32F103 Hardware (Keil MDK-ARM AC5)

```powershell
UV4.exe -r "STM32F103\MDK-ARM\Project.uvprojx" -t "WeGui_RGB"
```

Build log: `STM32F103/MDK-ARM/Objects/Project.build_log.htm`

Flash via CMSIS-DAP / DAPLink (pyOCD usable). If flashed content appears stale, verify build log timestamp before reflashing.

### Simulator (CMake + MinGW)

```powershell
# Preferred: use the repository build wrapper
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/build_sim.ps1" -Clean

# Run latest build
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/run_latest_sim.ps1"
```

VS Code tasks in `.vscode/tasks.json` currently contain local MinGW / Keil examples. For a public clone, adjust those tool paths to match your own environment if you use the provided tasks.

If the simulator build is stale or broken, delete `Simulator/build/` contents and reconfigure.

### Tests / Validation

There is **no standalone automated test suite or lint target** in this repository. Validation is done by building one of the two targets and running a demo:

- **Simulator smoke test**: build `wegui_sim`, run it, and verify the selected demo renders and animates correctly.
- **Hardware smoke test**: build the Keil target, flash it, and verify the selected demo on the LCD.

Closest equivalent to a "single test": run one demo in isolation.

- **Simulator**: change `demo_id` at the top of `Simulator/main_sim.c`, rebuild, then run `wegui_sim`.
- **STM32** (`STM32F103/main.c`): switch to the desired demo path in the current entry-file logic, then rebuild/flash.

VS Code tasks in `.vscode/tasks.json` automate these steps (`sim: configure`, `sim: build`, `sim: build and run`, `stm32: build (AC5)`, `stm32: rebuild (AC5)`).

## Architecture

### Directory Layout

- `Core/` — Platform-independent GUI kernel, widget implementations, dirty-rect engine
- `Demo/` — Demo applications; each widget type has its own `demo_xxx.c`; `simple_widget_demos.h` declares all entry points
- `STM32F103/` — Hardware MCU entry (`main.c`), Keil project, LCD SPI port implementations
- `Simulator/` — SDL2 simulator entry (`main_sim.c`), SDL port (`sdl_port.c/h`), and simulator config (`we_sim_port_config.h`)

### Platform Port Config Chain

`we_user_config.h` (project root) is the unified config entry point — edit this first when tuning screen size, PFB rows, dirty strategy, or timer slot counts.

`Core/we_gui_driver.h` includes `we_user_config.h` directly, while the platform routing header (`we_hw_port.h` at project root, or `STM32F103/Lcd_Port/we_port.h` for STM32-only builds) selects the correct platform config based on preprocessor defines:
- `WE_SIMULATOR` → `Simulator/we_sim_port_config.h`
- `WE_PLATFORM_CMS32C030` → CMS32C030 config
- `WE_PLATFORM_CW32L012` → CW32L012 config
- `WE_PLATFORM_AD15N` → AD15N config
- default → `STM32F103/Lcd_Port/stm32f103_hw_config.h`

`stm32f103_hw_config.h` selects LCD IC (`LCD_IC`: `_ST7735`, `_ST7789V3`, `_ST7789VW`, `_ST7796S`, `_GC9A01`) and port type (`LCD_PORT`: `_HARD_4SPI`, `_DMA_4SPI`, etc.). It defines `lcd_set_addr`, `lcd_ic_init`, and `LCD_FLUSH_PORT` macros that connect to the concrete driver.

`Core/we_gui_config.h` enforces that every required config macro (`LCD_DEEP`, `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `WE_CFG_DIRTY_STRATEGY`, etc.) is defined by the platform config — it will `#error` if any is missing.

For porting to a new MCU, copy `Demo/we_lcd_port_template.h/.c` as the starting point.

### Core Runtime Model

The runtime is centered on a single `we_lcd_t` instance, defined in `Core/we_gui_driver.h`. That struct owns:
- the partial frame buffer (PFB) and flush callbacks,
- the dirty-rectangle manager,
- the root linked list of GUI objects,
- GUI internal task slots and user timer slots,
- input state and optional storage binding,
- render statistics counters.

This is the main architectural boundary: widgets and demos mutate object state, mark regions dirty, and `we_gui_task_handler()` consumes that state to redraw via the currently bound LCD port.

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

Both simulator and STM32 entry points follow the same four-stage control flow:
1. initialize hardware/ports,
2. call `we_gui_init(...)` once to bind LCD/input/storage,
3. initialize exactly one demo and register its periodic tick with `we_gui_timer_create(...)`,
4. loop over `we_gui_tick_inc(...)` + `we_gui_task_handler(...)`.

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

Widget types in `Core/`: `label`, `btn`, `img`, `img_ex`, `arc`, `group`, `checkbox`, `label_ex`, `chart`, `toggle`, `progress`, `msgbox`, `img_flash`, `font_flash`, `slideshow`.

**img_ex semantics:**
- Angle unit is **512-step** (0–511 = full circle); 90° = 128, 180° = 256, 270° = 384. Use `WE_ANGLE(deg)` for float conversion or `WE_DEG(deg)` for integer compile-time constants. Normalized internally with `& 0x1FF` (no division).
- Scale unit is **256-step** (256 = 1.0×, 128 = 0.5×, 512 = 2.0×).
- `cx/cy` = screen transform center; `pivot_ofs_x/y` = source image local pivot offset — these describe different coordinate systems and must not be merged.
- Input image must be **RGB565 uncompressed** (no RLE/QOI).

**label_ex**: rotatable/scalable text widget; uses the same 512-step angle and 256-step scale systems as `img_ex`.

**group**: lightweight child-container used to manage a set of child widgets with local coordinates, opacity propagation, and coordinated movement. It is also the structural base used by composite controls such as `slideshow`.

**slideshow**: paged composite widget built on top of `group`; each page uses local coordinates and supports swipe/page snapping behavior.

**toggle**: iOS-style animated switch. Animates over ~128 ms (8 steps × 16 ms by default). The track and thumb now both reuse the shared analytic capsule renderer.

**msgbox**: modal popup (`we_popup_obj_t`) that slides in from the top. Two layouts: one button (OK only) or two buttons (OK + Cancel). Show/hide via `we_popup_show()` / `we_popup_hide()`.

**chart**: scrolling waveform chart with circular buffer and optional grid overlay. Uses ARM-2D style cross-column AA: steep segments write a linear opacity ramp to the *previous* column (left edge) and current column (right edge); gentle segments fill `stroke` pixels solid. The current waveform-body + feathering idea references Arm-2D, but the implementation is rewritten for WeGui's ring-buffer, dirty-rectangle, PFB-clipping, and integer-coordinate pipeline. `stroke` controls line width (default 2); `WE_CHART_AA_MAX` caps the AA ramp height. **No Y-axis scaling**: data values are in pixel units (0 = widget center, positive = up, negative = down); caller must pre-scale raw data to pixel space (e.g. `val = raw_q15 * (h/2) / 32768`) before pushing. `y_min`/`y_max` and `we_chart_set_range` have been removed.

**progress**: horizontal progress bar using a direct `0~255` target value model with smooth animated display transitions and fine-grained dirty updates.

**Motion system** (`Core/we_motion.h`): eight easing functions (`we_ease_linear`, `_in/out_quad`, `_out_cubic`, `_in_out_sine`, `_out_bounce`, `_out_back`) accepting `t ∈ [0, 256]`.

### Dirty Rectangle Strategy (`WE_CFG_DIRTY_STRATEGY` in platform config)

- `0`: full-screen redraw
- `1`: one merged bounding box
- `2`: multi-rect (default; up to `WE_CFG_DIRTY_MAX_NUM` rects, smart O(N²) merge)

`WE_CFG_DEBUG_DIRTY_RECT` — currently enabled (`1`) in `we_user_config.h` (overlays dirty regions in red). Disable before production builds.

`we_lcd_t` tracks rendering stats: `stat_render_frames`, `stat_pfb_pushes`, `stat_pushed_pixels`.

### PFB / GRAM Sizing

The partial frame buffer covers only a few rows of the screen (configured via `USER_GRAM_NUM`). Formula: `USER_GRAM_NUM = SCREEN_WIDTH × rows` where `rows` is typically 6 for hardware (DMA double-buffered) and 2 for the simulator. Increasing rows trades RAM for fewer flush calls per frame.

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

The important architectural point is that demos are both examples and integration tests: they are the primary way this repository exercises widgets, timers, input, storage-backed assets, and rendering behavior end-to-end.

**Selecting active demo:**
- **STM32** (`STM32F103/main.c`): uncomment the desired `init` + `timer_create` pair
- **Simulator** (`Simulator/main_sim.c`): change `demo_id` (1–17) at the top of `main`

The `we_timer_page_message_demo` additionally creates its own internal timers for auto page-switching and message hide.

## Code Style

- Comments are in Chinese — use targeted edits only; do not do bulk text replacement (risk of mojibake)
- Prefer direct, readable code with minimal abstraction layers
- Prefer static variables in demos over complex state shells
- Keep demo code easy to copy into new projects

## Tooling

- `tool/bin2c/` — converts binary image files to C arrays; output goes to `tool/bin2c/output/merged_bin.c` (included by CMakeLists.txt for simulator builds)
- `tool/font2c/` — generates Font2C font files from TTF for external flash storage
- `tool/font2c_gui/` — GUI wrapper for font2c
- `tool/STM32F103_ex_flash_download/` — Keil flash algorithm for programming external SPI flash via debugger

## Simulator Tuning

`Simulator/sdl_port.h` defines `SIM_SCALE` (window pixel multiplier, default 1) and `SIM_MAX_FPS` (frame cap, default 30). Adjust these for HiDPI displays or performance testing.

## Key Files to Read First

When starting a new session involving this repo:

1. `we_user_config.h` — unified platform config (screen size, PFB rows, dirty strategy, timer counts)
2. `we_hw_port.h` — multi-platform routing header (selects config by preprocessor define)
3. `STM32F103/main.c` — hardware entry and demo selection
4. `Simulator/main_sim.c` — simulator entry
5. `Demo/simple_widget_demos.h` — all demo entry point declarations
6. `Core/we_gui_driver.h` — core API surface
7. `WEGUI_API_REFERENCE.md` — full API reference with usage examples
