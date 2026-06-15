# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WeGui-ARGB is a lightweight embedded GUI framework for MCU / SoC targets plus an SDL2 PC simulator. The platform-independent GUI kernel and demos live in `Core/` and `Demo/`; each hardware/simulator target provides only the port layer, startup code, and build project.

Primary targets currently present in this repository:
- `Simulator/` — SDL2 PC simulator built with CMake + MinGW/Ninja or MinGW Makefiles.
- `STM32F103/` — Keil MDK-ARM AC5 hardware target, with LCD, input, and W25Qxx external flash ports.
- `STM32F030/` — Keil MDK-ARM AC5 hardware target, with LCD and input ports.

Full API reference: `WEGUI_API_REFERENCE.md`.

## Build Commands

### Simulator (CMake + MinGW)

Use the repository wrapper scripts rather than calling CMake directly:

```powershell
# Clean configure + build
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/build_sim.ps1" -Clean

# Incremental build
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/build_sim.ps1"

# Run latest built simulator
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/run_latest_sim.ps1"
```

`Simulator/build_sim.ps1` auto-detects `ninja + gcc + g++` first and falls back to `mingw32-make + gcc + g++`. If the simulator build is stale or broken, delete `Simulator/build/` and rebuild with `-Clean`.

`Simulator/CMakeLists.txt` globs core/widget sources (`../Core/*.c` and `../Core/widgets/*/*.c`, one directory level deep) but lists `DEMO_SOURCES` **explicitly**. Adding a new widget compiles automatically; adding a new demo requires appending its `demo_xxx.c` to `DEMO_SOURCES`. The glob is one level deep, so the deprecated `Core/widgets/控件废案/dropdown/` copy (two levels deep) is not compiled — only `Core/widgets/dropdown/` is.

### STM32F103 Hardware (Keil MDK-ARM AC5)

```powershell
UV4.exe -r "STM32F103\MDK-ARM\Project.uvprojx" -t "WeGui_ARGB"
```

Build log: `STM32F103/MDK-ARM/Objects/Project.build_log.htm`

### STM32F030 Hardware (Keil MDK-ARM AC5)

```powershell
UV4.exe -r "STM32F030\MDK-ARM\Project.uvprojx" -t "STM32F030"
```

Build log: `STM32F030/MDK-ARM/Objects/Project.build_log.htm`

### VS Code Tasks

`.vscode/tasks.json` currently provides simulator tasks (`sim: build`, `sim: clean and build`, `sim: run latest`, `sim: build and run`) and STM32F103 Keil tasks (`stm32: build (AC5)`, `stm32: rebuild (AC5)`, `stm32: open MDK project`). The Keil tasks depend on local VS Code settings such as `wegui.keilUv4Path`, `wegui.stm32ProjectFile`, and `wegui.stm32TargetName`.

## Tests / Validation

There is **no standalone automated test suite or lint target** in this repository. Validation is done by building a target and running one demo as an integration smoke test.

Closest equivalent to a single test:
- **Simulator**: change `demo_id` in `Simulator/main_sim.c` (currently line 51), rebuild, run `wegui_sim`, and verify rendering/animation/input behavior.
- **STM32F103**: change `demo_id` in `STM32F103/main.c` (currently line 124), rebuild/flash, and verify on the LCD.
- **STM32F030**: change `demo_id` in `STM32F030/main.c` (currently line 69), rebuild/flash, and verify on the LCD.

Hardware flashing is done outside the build command (CMSIS-DAP / DAPLink / pyOCD are usable depending on the board). If flashed content appears stale, verify the relevant `.build_log.htm` timestamp before reflashing.

## Architecture

### Directory Layout

- `Core/` — platform-independent GUI kernel, drawing, widget implementations, dirty-rectangle engine, image/font support.
- `Demo/` — demo applications; each widget type has its own `demo_xxx.c`, with declarations in `simple_widget_demos.h`.
- `Simulator/` — SDL2 entry (`main_sim.c`), SDL LCD/input/storage port (`sdl_port.c/h`), simulator config (`we_sim_port_config.h`), and build/run scripts.
- `STM32F103/` — STM32F103 entry (`main.c`), Keil project, LCD SPI ports, button/input port, and W25Qxx external flash port.
- `STM32F030/` — STM32F030 entry (`main.c`), Keil project, LCD SPI ports, and button/input port.
- `tool/` — resource conversion and external-flash support tools (`bin2c`, `font2c`, `font2c_gui`, STM32F103 external flash download tooling).

### Platform Config Chain

`we_user_config.h` at the repository root is the unified user configuration entry point. Edit it first for screen size, color depth, PFB/GRAM rows, dirty strategy, timer counts, input/storage binding, and widget default tuning macros.

The core includes this config directly through `Core/we_gui_driver.h`. Platform routing then selects a target-specific LCD/port config through `we_hw_port.h` or an STM32-local port header:
- `WE_SIMULATOR` → `Simulator/we_sim_port_config.h`
- `WE_PLATFORM_STM32F030` → `STM32F030/Lcd_Port/stm32f030_hw_config.h`
- `WE_PLATFORM_CMS32C030` → CMS32C030 config (referenced by router, not present in this checkout)
- `WE_PLATFORM_CW32L012` → CW32L012 config (referenced by router, not present in this checkout)
- `WE_PLATFORM_AD15N` → AD15N config (referenced by router)
- default → STM32F103 config

Target hardware config headers select the LCD IC (`LCD_IC`) and physical LCD port (`LCD_PORT`), then define `lcd_set_addr`, `lcd_ic_init`, and `LCD_FLUSH_PORT`/flush callbacks that bind the GUI core to the concrete driver.

`Core/we_gui_config.h` validates required macros such as `LCD_DEEP`, `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `WE_CFG_DIRTY_STRATEGY`, and timer/input/storage limits with `#error` checks.

### Core Runtime Model

The runtime centers on one `we_lcd_t` instance (`Core/we_gui_driver.h`). That object owns:
- the partial frame buffer (PFB/GRAM) and LCD flush callbacks,
- the dirty-rectangle manager,
- the root linked list of GUI objects,
- GUI internal task slots, user timer slots, and the central animation list (`anim_head`),
- input state (`we_indev_data_t`) and optional storage callback,
- render statistics counters.

Widgets and demos mutate object state and mark regions dirty. `we_gui_task_handler()` consumes timers/input/dirty state and redraws through the currently bound LCD port.

### Initialization and Main Loop Pattern

Primary init bundles LCD, input, and storage binding:

```c
we_gui_init(we_lcd_t *p_lcd, colour_t bg, colour_t *gram_base, uint16_t gram_size,
            we_lcd_set_addr_cb_t set_addr_cb, we_lcd_flush_cb_t flush_cb,
            we_input_read_cb_t input_cb,      // NULL if unused
            we_storage_read_cb_t storage_cb); // NULL if unused
```

Lower-level LCD-only init is available through `we_lcd_init_with_port(...)`, with optional `we_input_init_with_port(...)` and `we_storage_init_with_port(...)` when the relevant config flags are enabled.

All current entry points follow the same flow:
1. initialize system clock/hardware/ports,
2. call `we_gui_init(...)` once,
3. initialize exactly one demo and create its periodic GUI timer,
4. loop over elapsed-time tick injection and `we_gui_task_handler(...)`.

Typical pattern:

```c
lcd_hw_init();
we_gui_init(&lcd, RGB888TODEV(10, 14, 20), gram, USER_GRAM_NUM,
            lcd_set_addr, LCD_FLUSH_PORT, input_cb, storage_cb);
we_xxx_simple_demo_init(&lcd);
we_gui_timer_create(&lcd, we_xxx_simple_demo_tick, 16U, 1U);

while (1) {
    we_gui_tick_inc(&lcd, elapsed_ms);
    we_gui_task_handler(&lcd);
}
```

The simulator additionally calls `sim_lcd_update()` after `we_gui_task_handler()`. Input is polled automatically inside `we_gui_task_handler()` through the registered input callback; call `we_gui_indev_handler()` directly only when managing input state manually.

### Timer API

```c
int8_t id = we_gui_timer_create(lcd, cb, period_ms, repeat); // repeat=1 periodic, 0 one-shot
we_gui_timer_stop(lcd, id);
we_gui_timer_start(lcd, id);
we_gui_timer_restart(lcd, id);   // reset accumulator + reactivate
we_gui_timer_delete(lcd, id);
```

Timer/task storage is fixed-size: `WE_CFG_GUI_TASK_MAX_NUM` for GUI internal tasks and `WE_CFG_GUI_TIMER_MAX_NUM` for user timers.

**Widget animations do NOT use task slots.** They run on the central animation engine: a `we_anim_t` node embedded in each widget struct, linked into `lcd->anim_head` via `we_anim_start()`/`we_anim_stop()` and stepped by `we_gui_task_handler()` each cycle. `we_anim_start` cannot fail and the count is unbounded; finished animations unlink themselves (idle cost = one NULL check). Rule: widget delete functions must call `we_anim_stop` before `we_obj_delete` (the node is owned by the widget). Users: toggle, progress, indicator, msgbox, slideshow, scroll_panel, dropdown (scrollbar fade).

### Widgets and Important Semantics

Widget implementations live in `Core/`; demos are in `Demo/`. Current main widgets include `label`, `btn`, `img`, `img_ex`, `arc`, `group`, `checkbox`, `label_ex`, `chart`, `toggle`, `progress`, `msgbox`, `img_flash`, `font_flash`, `slideshow`, `slider`, `scroll_panel`, `dropdown`, `stepper`, and `indicator`.

Important non-obvious semantics:
- `img_ex` and `label_ex` use a **512-step angle unit** (`0..511` = full circle; 90° = 128; 180° = 256). Use `WE_ANGLE(deg)` or `WE_DEG(deg)`.
- `img_ex`/`label_ex` scale uses a **256-step scale unit** (`256` = 1.0×, `128` = 0.5×, `512` = 2.0×).
- For `img_ex`, `cx/cy` are the screen transform center, while `pivot_ofs_x/y` are source-image local pivot offsets; do not merge those coordinate systems. Input images must be RGB565 uncompressed.
- `group` is the lightweight child-container and structural base for composites such as `slideshow`; children use local coordinates with opacity propagation and coordinated movement. Opacity propagation works via the lcd-level `opa_scale` multiplier: containers (group/slideshow/scroll_panel) multiply their opacity into it around the children pass, and every drawing primitive consumes it once at entry (`we_opa_apply`, zero cost when no fade is active). A fully transparent group also stops intercepting input.
- `slideshow` handles paged local-coordinate children and swipe/page snapping.
- `msgbox` is a modal `we_popup_obj_t`; show/hide through `we_popup_show()` / `we_popup_hide()`.
- `chart` uses a circular buffer and pixel-space data values. There is no Y-axis scaling API; callers must pre-scale raw data to pixels before pushing. `stroke` controls line width and `WE_CHART_AA_MAX` caps anti-aliased feather height.
- `progress` uses a direct `0..255` target value with smooth animated display transitions.
- `dropdown` is data-driven: the caller owns the `we_dropdown_option_t` array (the widget stores only a pointer, never copies text). Its expanded list draws through the LCD-level overlay popup so it is not clipped by `group`/`scroll_panel`/`slideshow` parents. Only one popup may be open screen-wide, enforced by the driver's single `popup_layer` slot (`we_popup_layer_open/close/...` in `we_gui_driver.h`).
- `stepper` stores its value as a **fixed-point `int32`**: real value = `value / 10^decimals`. Decimals are split out only at draw time to avoid Cortex-M0 soft-float cost. Continuous hold-to-repeat reuses the `STAY` event and does **not** consume a timer slot.
- `indicator` is a circular status lamp that animates an on/off color transition (optional glow) via the central animation engine (`we_anim_t`, no task slot) and `we_lerp`/`we_ease_*`. Default is read-only; enable `we_indicator_set_clickable()` for click-toggle. The glow stays inside the base box so it never leaks past dirty rectangles.
- `Core/we_motion.h` provides easing helpers accepting `t ∈ [0, 256]`.

### Dirty Rectangles and PFB/GRAM

`WE_CFG_DIRTY_STRATEGY` in `we_user_config.h` controls redraw strategy:
- `0`: full-screen redraw
- `1`: one merged bounding box
- `2`: multi-rect merge up to `WE_CFG_DIRTY_MAX_NUM`

The current shared config uses strategy `2` with `WE_CFG_DIRTY_MAX_NUM = 10`. `WE_CFG_DEBUG_DIRTY_RECT` overlays dirty regions in red when enabled; it is currently `0`.

The partial frame buffer covers only a few screen rows. `USER_GRAM_NUM = SCREEN_WIDTH × rows`; increasing rows trades RAM for fewer flushes. The current shared config uses `SCREEN_WIDTH = 240`, `SCREEN_HEIGHT = 240`, and `USER_GRAM_NUM = SCREEN_WIDTH * 8`.

### Input and Gestures

`we_indev_data_t indev_data` lives inside `we_lcd_t`; do not relocate it unless redesigning the input subsystem.

Swipe detection is built into `we_gui_indev_handler()`. On release, if movement from press exceeds `WE_CFG_SWIPE_THRESHOLD`, a directional swipe event (`WE_EVENT_SWIPE_LEFT/RIGHT/UP/DOWN`) is dispatched instead of a click. Container widgets can use swipe events for page snapping.

## Demo Style

Each demo follows this pattern:

```c
void we_xxx_simple_demo_init(we_lcd_t *lcd);
void we_xxx_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);
```

One demo should be a small, copyable example: static variables plus one init function plus one tick function. Demos are also the primary integration tests for widgets, timers, input, storage-backed assets, and rendering behavior.

Demo selection differs slightly by target (`demo_id` switch in each entry's `main`):
- `Simulator/main_sim.c`: there is no `case 9` (historical `key` slot); checkbox is 10, scroll_panel 19, dropdown 20, stepper 21, indicator 22.
- `STM32F103/main.c` and `STM32F030/main.c`: checkbox is `demo_id` 9, scroll_panel 18, dropdown 19, stepper 20, indicator 21.

When adding a demo, update the `demo_id` comment block + `switch` in all three entry files, declare its `init`/`tick` in `Demo/simple_widget_demos.h`, and add the `demo_xxx.c` to `DEMO_SOURCES` in `Simulator/CMakeLists.txt` (and to each Keil `.uvprojx`).

## Code Style

- Comments are in Chinese; make targeted edits and avoid bulk text replacement that could cause mojibake.
- Prefer direct, readable C with minimal abstraction layers.
- Prefer static variables in demos over complex state shells.
- Keep demo code easy to copy into user projects.

## Key Files to Read First

1. `we_user_config.h` — unified screen/PFB/dirty/timer/input/storage/widget config.
2. `we_hw_port.h` — platform routing by preprocessor define.
3. `Core/we_gui_driver.h` — core runtime object and public API surface.
4. `Core/we_gui_config.h` — required config macro validation.
5. `Simulator/main_sim.c` — simulator entry and demo selection.
6. `STM32F103/main.c` — F103 hardware entry and demo selection.
7. `STM32F030/main.c` — F030 hardware entry and demo selection.
8. `Demo/simple_widget_demos.h` — demo entry declarations.
9. `WEGUI_API_REFERENCE.md` — full API reference and usage examples.
