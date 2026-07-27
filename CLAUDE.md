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

`Simulator/CMakeLists.txt` globs core/widget sources (`../Core/*.c` and `../Core/widgets/*/*.c`, excluding `*_bckup.c`) plus the preview zone (`../Core/widgets_preview/*/*.c` and `../Demo/preview/*.c`), but lists stable `DEMO_SOURCES` **explicitly** (demo `.c` files plus font/bin resource `.c` files). Adding a new widget or preview demo compiles automatically; adding a new stable demo requires appending its `demo_xxx.c` to `DEMO_SOURCES`.

### STM32F103 Hardware (Keil MDK-ARM AC5)

```powershell
UV4.exe -r "STM32F103\MDK-ARM\Project.uvprojx" -t "WeGui_ARGB"
```

Build log: `STM32F103/MDK-ARM/Objects/Project.build_log.htm`

### STM32F030 Hardware (Keil MDK-ARM AC5)

```powershell
UV4.exe -r "STM32F030\MDK-ARM\Project.uvprojx" -t "STM32F030"
```

Build log: `STM32F030/MDK-ARM/STM32F030/STM32F030.build_log.htm` (Keil writes F030 output into a folder named after the target, unlike F103's `Objects/`)

### VS Code Tasks

`.vscode/tasks.json` currently provides simulator tasks (`sim: stop running`, `sim: build`, `sim: clean and build`, `sim: run latest`, `sim: build and run`; the build tasks run `sim: stop running` first so the linker can overwrite a running `wegui_sim.exe`) and STM32F103 Keil tasks (`stm32: build (AC5)`, `stm32: rebuild (AC5)`, `stm32: open MDK project`). The Keil tasks depend on local VS Code settings such as `wegui.keilUv4Path`, `wegui.stm32ProjectFile`, and `wegui.stm32TargetName`.

## Tests / Validation

There is **no standalone automated test suite or lint target** in this repository. Validation is done by building a target and running one demo as an integration smoke test.

Closest equivalent to a single test:
- **Simulator**: change `#define DEMO_ID` near the top of `main` in `Simulator/main_sim.c` (`0` = showcase), rebuild, run `wegui_sim`, and verify rendering/animation/input behavior.
- **STM32F103**: change `#define DEMO_ID` near the top of `main` in `STM32F103/main.c`, rebuild/flash, and verify on the LCD.
- **STM32F030**: change `#define DEMO_ID` near the top of `main` in `STM32F030/main.c`, rebuild/flash, and verify on the LCD.

Hardware flashing is done outside the build command (CMSIS-DAP / DAPLink / pyOCD are usable depending on the board). If flashed content appears stale, verify the relevant `.build_log.htm` timestamp before reflashing.

## Architecture

### Directory Layout

- `Core/` — platform-independent GUI kernel, drawing, widget implementations, dirty-rectangle engine, image/font support.
- `Demo/` — demo applications; each widget type has its own `demo_xxx.c`, with declarations in `simple_widget_demos.h`.
- `Core/widgets_preview/` + `Demo/preview/` — **preview incubation zone** for experimental widgets (currently 26, e.g. `mask_group`, `ime_pinyin`). Compiled by all three targets (simulator via CMake globs; both Keil projects carry `we_widget_preview`/`demo_preview` file groups — the linker strips whatever the selected demo doesn't reference), DEMO_ID range 100+ (currently 101..126, resequenced 2026-07 with the 12 most-used widgets first; future graduations leave holes), ID table and demo entry declarations in `Demo/preview/preview_demos.h`, function naming `we_<name>_preview_demo_init/tick`. New preview widgets/demos are picked up automatically by the simulator globs but must be added to both `.uvprojx` file groups by hand. These widgets are unpolished and may be removed at any time; graduation moves them into `Core/widgets/` and the stable numbering range.
- `Simulator/` — SDL2 entry (`main_sim.c`), SDL LCD/input/storage port (`sdl_port.c/h`), simulator config (`we_sim_port_config.h`), and build/run scripts.
- `STM32F103/` — STM32F103 entry (`main.c`), Keil project, LCD SPI ports, button/input port, and W25Qxx external flash port.
- `STM32F030/` — STM32F030 entry (`main.c`), Keil project, LCD SPI ports, and button/input port.
- `tool/` — resource conversion and external-flash support tools (`bin2c`, `font2c`, `img2c`, `pinyin2c` — pinyin-table generator feeding the `ime_pinyin` preview widget — and STM32F103 external flash download tooling) plus `tool/gui/` (wegui-tools-manager, an Express + Vite/React unified web UI for the resource tools; run with `npm run dev` inside `tool/gui/`).
- `docs/demo_gifs/` — recorded GIFs of the 28 stable demos, embedded in `README.md`; `preview_gifs/` — GIFs of the preview demos (`demo_<ID>_<name>.gif`); `preview_shots/` — ad-hoc visual-verification screenshots plus their capture helper scripts (`_shot_window.ps1`, `_scan_red.ps1`, …).

### Platform Config Chain

`we_user_config.h` at the repository root is the unified user configuration entry point. Edit it first for screen size, color depth, PFB/GRAM rows, dirty strategy, timer counts, input/storage binding, and widget default tuning macros. It also holds the framework version string `WE_GUI_VERSION`; release commits bump it together with the version heading in `README.md`.

**Fonts are fully explicit — there is no default font.** Core never includes any resource header; every text widget takes a `font` pointer in its `obj_init` (mandatory: NULL makes init return without doing anything). The demo layer owns the resource reference: `Demo/simple_widget_demos.h` and `Demo/preview/preview_demos.h` include `demo_ascii_16.h` and define the legacy alias `we_font_consolas_18` as `((const unsigned char *)&demo_ascii_16)`, which all demos pass explicitly.

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
- user timer slots and the central animation list (`anim_head`),
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

Time-driven scheduling is a two-layer model: user timers (`timer_list`, fixed-size — `WE_CFG_GUI_TIMER_MAX_NUM` slots) for business-layer logic, plus the central animation engine for widget animations.

**Widget animations do NOT use timer slots.** They run on the central animation engine: a `we_anim_t` node embedded in each widget struct, linked into `lcd->anim_head` via `we_anim_start()`/`we_anim_stop()` and stepped by `we_gui_task_handler()` each cycle. `we_anim_start` cannot fail and the count is unbounded; finished animations unlink themselves (idle cost = one NULL check). Rule: widget delete functions must call `we_anim_stop` before `we_obj_delete` (the node is owned by the widget). Users: toggle, progress, indicator, msgbox, slideshow, scroll_panel, dropdown (scrollbar fade), line, box (only when `WE_BOX_USE_ANIM=1`; default off), gauge (value sweep), list (inertia/rebound + scrollbar fade), roller (snap/fling), marquee (scroll), toast (slide state machine).

### Widgets and Important Semantics

Widget implementations live in `Core/`; demos are in `Demo/`. Current main widgets include `label`, `btn`, `img`, `img_ex`, `arc`, `group`, `checkbox`, `label_ex`, `chart`, `toggle`, `progress`, `msgbox`, `img_flash`, `font_flash`, `slideshow`, `slider`, `scroll_panel`, `dropdown`, `stepper`, `indicator`, `line`, `box`, `gauge`, `list`, `roller`, `marquee`, and `toast`.

Important non-obvious semantics:
- `img_ex` and `label_ex` use a **512-step angle unit** (`0..511` = full circle; 90° = 128; 180° = 256). Use `WE_ANGLE(deg)` or `WE_DEG(deg)`.
- `img_ex`/`label_ex` scale uses a **256-step scale unit** (`256` = 1.0×, `128` = 0.5×, `512` = 2.0×).
- For `img_ex`, `cx/cy` are the screen transform center, while `pivot_ofs_x/y` are source-image local pivot offsets; do not merge those coordinate systems. Input images must be RGB565 uncompressed.
- `group` is the lightweight child-container and structural base for composites such as `slideshow`; children use local coordinates with opacity propagation and coordinated movement. Opacity propagation works via the lcd-level `opa_scale` multiplier: containers (group/slideshow/scroll_panel) multiply their opacity into it around the children pass, and every drawing primitive consumes it once at entry (`we_opa_apply`, zero cost when no fade is active). A fully transparent group also stops intercepting input. Moving a group uses fine-grained dirty marking: because the background is a uniform square fill (radius fixed at 0), the old/new-frame overlap region is pixel-identical after a translation (even when semi-transparent), so `_group_set_pos_cb` marks only the exposure L-strips of the old and new frames (`we_obj_invalidate_area_exclude` with the overlap as the hole) while children self-mark their old/new footprints through the per-child `we_obj_set_pos` in relayout; disjoint jumps and fully-transparent groups fall back to full old+new boxes, and `we_dirty_invalidate_exclude`'s built-in benefit threshold re-coarsens thin-overlap cases automatically. If the group background ever gains rounded corners, gradients, or textures, this optimization must revert to full-box marking.
- `slideshow` handles paged local-coordinate children and swipe/page snapping.
- `msgbox` is a modal `we_popup_obj_t`; show/hide through `we_popup_show()` / `we_popup_hide()`.
- `chart` uses a circular buffer and pixel-space data values. There is no Y-axis scaling API; callers must pre-scale raw data to pixels before pushing. `stroke` controls line width and `WE_CHART_AA_MAX` caps anti-aliased feather height.
- `progress` uses a direct `0..255` target value with smooth animated display transitions.
- `dropdown` is data-driven: the caller owns the `we_dropdown_option_t` array (the widget stores only a pointer, never copies text). Its expanded list draws through the LCD-level overlay popup so it is not clipped by `group`/`scroll_panel`/`slideshow` parents. Only one popup may be open screen-wide, enforced by the driver's single `popup_layer` slot (`we_popup_layer_open/close/...` in `we_gui_driver.h`). Expanded-list dragging allows ±24px rubber-band overscroll with a rebound animation on release (same idiom as `list`/`menu`, own `rb_anim` node stopped on close/delete). Fine-grained dirty marking: scrollbar fade/wake marks only the right-edge scrollbar strip, hover highlight changes mark only the affected row strips; scroll/rebound steps mark the popup content area (the floor, since every visible row shifts), and no dirty rect ever extends beyond the popup bounds.
- `stepper` stores its value as a **fixed-point `int32`**: real value = `value / 10^decimals`. Decimals are split out only at draw time to avoid Cortex-M0 soft-float cost. Continuous hold-to-repeat reuses the `STAY` event and does **not** consume a timer slot.
- `indicator` is a circular status lamp that animates an on/off color transition (optional glow) via the central animation engine (`we_anim_t`, no timer slot) and `we_lerp`/`we_ease_*`. Default is read-only; enable `we_indicator_set_clickable()` for click-toggle. The glow stays inside the base box so it never leaks past dirty rectangles.
- `line` is a width-configurable anti-aliased line segment (round/butt cap) that reuses the existing `we_draw_line` (Xiaolin Wu) primitive plus endpoint AA circles for round caps. Endpoints, position (`we_line_move`), color, and opacity each animate through the central animation engine via **three independent `we_anim_t` nodes** (geometry + color + opacity), so a move, a recolor, and a fade can run simultaneously. Compile-time `WE_LINE_USE_ANIM 0` strips the animation code and struct fields, degrading `we_line_anim_*` into instant-apply compatibility stubs. Default is decorative (passes input through); hit-testing is bounding-box granularity.
- `box` is a rectangular panel with **per-corner independent styling**: each of the four corners (indexed `WE_BOX_LT/RT/LB/RB`, same order as `WE_MASK_QUADRANT_*`) can be round, chamfered (45°), or square (`r = 0`), plus a border of configurable thickness/color and solid fill. Rendering splits into fast `we_fill_rect` blocks for the center/straight edges and corner compositing only inside four K×K corner squares (K = max(corner radii, border width, border width + inner radii)); the border ring is `outer-outline mask − inner-outline mask`. Bordered round corners resolve outer+inner coverage in a **single** 4×4 subsampling pass via `we_mask_quarter_ring_alpha` (core helper added for this); chamfer corners render as per-row spans with exact integer coverage (alpha only 0/128/255) — no per-pixel mask calls. All box setters skip invalidation when the value is unchanged. Chamfer borders inset the inner outline by `(2-√2)·bw ≈ 0.586·bw` (not `bw`) so the diagonal segment keeps the same visual thickness as straight edges. Fill-color/opacity animation is compile-time opt-in: `WE_BOX_USE_ANIM` defaults to **0**, making `we_box_anim_*` instant-apply stubs; define it to 1 before including the header to enable two independent `we_anim_t` nodes. Default is decorative (passes input through).
- `gauge` is a dial built entirely from existing AA primitives (`we_draw_line_round` ticks/pointer, analytic-fill center cap; no new render primitives). Value changes use **differential dirty marking**: only the old and new pointer footprints (two clamped bboxes, each unioned with the cap AA edge) are invalidated — the static tick ring is never redrawn, and a quantization-equal angle submits nothing. Tick endpoint geometry is cached relative to the dial center at init/`set_range`/`set_tick_count` (zero trig, zero mul/div in draw; moving the widget needs no rebuild), and value→angle mapping uses a **Q16 slope** pre-divided in `we_gauge_set_range` (auto-falls back to division for spans > 65536). Decorative (input passes through); delete via `we_gauge_obj_delete` (stops the anim node first).
- `list` is a data-driven list menu: the caller owns the `const char *const *` items array (pointer stored, never copied). Drag scrolling allows ±24px rubber-band overscroll with rebound; inertia is injected both from drag release and from no-STAY fast swipes (initial velocity estimated from total displacement over a 128ms slice); the scrollbar auto-fades after 600ms idle to a resident low alpha (same idiom as dropdown). Fine-grained dirty marking: row press/release marks one row strip, scrolling marks only the content clip rect, scrollbar fade marks only the scrollbar strip. **Two anim nodes** (inertia/rebound + scrollbar fade) — delete via `we_list_obj_delete`.
- `roller` is a wheel-style option picker (odd visible row count, center row highlighted, opacity falls off with distance from center). Slow release snaps to the nearest row; fast release enters **fling**: the landing point is projected from release velocity (geometric-series integer approximation), clamped and rounded to a row, and the velocity seeds the snap animation so the speed curve stays continuous at release. Tapping a visible off-center row animates directly to it; no-STAY swipes page by one row. Scrolling dirty-marks only the centered text column band (panel-corner side margins excluded); text measurement is fully cached (font-constant y-bbox + direct-mapped row-width cache), so the draw inner loop makes zero measurement calls. Delete via `we_roller_obj_delete`.
- `marquee` is a single-line marquee label: static left-aligned when the text fits, otherwise a seamless two-segment loop (text + 40px gap + text head) with a configurable seam pause. Scrolling runs on one central anim node (milli-pixel integer accumulator, auto-unlinks when the text fits or opacity is 0). Rendering uses a **windowed glyph loop** instead of `we_draw_string`: glyphs fully left of the window only advance the cursor (no bitmap fetch, no pixel scan) and the loop breaks past the right edge, so partial redraws touch only visible glyphs. Decorative; `event_cb` is NULL so input truly passes through. Delete via `we_marquee_obj_delete`.
- `toast` is a non-modal top banner: slide-in → stay → slide-out on one central anim node (Q8 progress state machine); it never intercepts input (`event_cb` NULL) and does **not** occupy the LCD-level `popup_layer` (plain object + `we_obj_bring_to_front`). Its position is managed manually via `base.y` so each animation step submits a **single union** of the old + new bboxes instead of two rects; calling `we_toast_show` during any phase re-enters smoothly from the current position/alpha. Over-wide text is tail-truncated with "..." via per-glyph advance accumulation (the prefix draws zero-copy by narrowing the PFB right edge). The caller owns the text pointer for the display duration. Delete via `we_toast_obj_delete`.
- `Core/we_motion.h` provides easing helpers accepting `t ∈ [0, 256]`.

### Dirty Rectangles and PFB/GRAM

`WE_CFG_DIRTY_STRATEGY` in `we_user_config.h` controls redraw strategy:
- `0`: full-screen redraw
- `1`: one merged bounding box
- `2`: multi-rect merge up to `WE_CFG_DIRTY_MAX_NUM`

The current shared config uses strategy `2` with `WE_CFG_DIRTY_MAX_NUM = 10`. Two debug switches sit beside it in `we_user_config.h`: `WE_CFG_DEBUG_DIRTY_RECT` overlays dirty regions in red, and `WE_CFG_DEBUG_PERF_STRESS` force-marks all top-level widgets dirty every frame to stress worst-case redraw throughput (implemented in `Core/we_gui_driver.c`). Make sure both are `0` before recording demo GIFs or taking performance numbers.

The partial frame buffer covers only a few screen rows. `USER_GRAM_NUM = SCREEN_WIDTH × rows`; increasing rows trades RAM for fewer flushes. The current shared config uses `SCREEN_WIDTH = 280`, `SCREEN_HEIGHT = 240`, and `USER_GRAM_NUM = SCREEN_WIDTH * 8`.

`WE_LCD_FLUSH_ALIGN_X` / `WE_LCD_FLUSH_ALIGN_Y` (power of two, default 1 = off) force flush-window pixel alignment for hardware that needs it (QSPI panels: x/y multiples of 2/4; SSD1306-class page OLEDs: `ALIGN_Y = 8`). Alignment is applied once at dirty-rect intake in `we_dirty_invalidate` (`Core/dirty_driver.c`) — rects are expanded to aligned bounds after screen clamping and before merging, so the expanded edges are fully re-rendered and every `set_addr` window is aligned by construction. Compile-time checks in `Core/we_gui_config.h` require `SCREEN_WIDTH`/`SCREEN_HEIGHT` and the PFB row count (`USER_GRAM_NUM / SCREEN_WIDTH`) to be multiples of the respective alignment.

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

Demo selection is a **compile-time `#define DEMO_ID`** + `#if/#elif` chain in each entry's `main` (no runtime `switch`); only the selected demo's `init`/`tick` is compiled in. Numbering is unified across all three targets — `1..30` and the preview range `101..126` are identical: `1` label, `2` btn, `3` img, `4` img_ex, `5` arc, `6` group, `7` slideshow, `8` concentric arc, `9` checkbox, `10` label_ex, `11` chart, `12` toggle, `13` progress, `14` msgbox, `15` flash img, `16` flash font, `17` slider, `18` scroll_panel, `19` dropdown, `20` stepper, `21` indicator, `22` line, `23` box, `24` gauge, `25` list, `26` roller, `27` marquee, `28` toast, `29` focus, `30` focus2. Only `0 = showcase` remains simulator-only (needs 800×480, guarded by a nested `#warning`; on STM32 an ID of 0 falls through to the fallback). The `#else` fallback is `label` on all three targets. Switch demos by editing the single `#define DEMO_ID` line near the top of `main`.

When adding a demo, update the `DEMO_ID` comment block + `#if/#elif` chain in all three entry files, declare its `init`/`tick` in `Demo/simple_widget_demos.h`, and add the `demo_xxx.c` to `DEMO_SOURCES` in `Simulator/CMakeLists.txt` (and to each Keil `.uvprojx`).

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
