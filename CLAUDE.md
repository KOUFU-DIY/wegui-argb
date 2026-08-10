# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WeGui-ARGB is a lightweight embedded GUI framework for MCU / SoC targets plus an SDL2 PC simulator. The platform-independent GUI kernel and demos live in `Core/` and `Demo/`; each hardware/simulator target provides only the port layer, startup code, and build project.

Primary targets currently present in this repository:
- `Simulator/` — SDL2 PC simulator built with CMake + MinGW/Ninja or MinGW Makefiles.
- `STM32F103/` — Keil MDK-ARM AC5 hardware target, with LCD, input, and W25Qxx external flash ports.
- `STM32F030/` — Keil MDK-ARM AC5 hardware target, with LCD and input ports.

Full API reference: `WEGUI_API_REFERENCE.md` (ch.16 is a focus/key-nav quick reference; the authoritative description is the "Focus and Key Navigation" section below plus `Core/we_gui_driver.h` comments).

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

`Simulator/CMakeLists.txt` globs core/widget sources (`../Core/*.c` and `../Core/widgets/*/*.c`, excluding `*_bckup.c`) plus the preview zone (`../Core/widgets_preview/*/*.c` and `../Demo/preview/*.c`), but lists stable `DEMO_SOURCES` **explicitly** (demo `.c` files plus the generated font/image resource `.c` files under `../tool/1.font2c/output/`, `../tool/2.img2c/output/c/` and `../tool/3.bin2c/output/`). Adding a new widget or preview demo compiles automatically; adding a new stable demo requires appending its `demo_xxx.c` to `DEMO_SOURCES`.

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

### Golden-CRC regression (simulator, headless)

```powershell
# Compare every demo (1..31, 101..126) against Simulator/autotest/golden.txt
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/autotest.ps1"

# Only specific demos
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/autotest.ps1" -Ids 18,7,25

# Re-baseline after an INTENDED visual change (review FAILs first)
powershell -NoProfile -ExecutionPolicy Bypass -File "Simulator/autotest.ps1" -Update
```

Per demo the script reconfigures `Simulator/build_autotest/` with `-DWE_DEMO_ID=<id>` and `-DWE_SIM_AUTOTEST=1` (the scaffold in `sim_autotest.c/h` is compile-gated, default 0 — the everyday `Simulator/build` exe contains no regression code and ignores `--autotest`; `DEMO_ID` in `main_sim.c` is `#ifndef`-guarded), rebuilds (only `main_sim.c` recompiles), and runs `wegui_sim --autotest 180` headless (SDL dummy video driver, fixed 16ms ticks, FNV-1a hash chained over every frame's screen buffer, result written to a file because the exe links `-mwindows`). **Interaction-trajectory goldens**: if `Simulator/autotest/scripts/<id>.evt` exists (frame-stamped `down/move/up` — STAY auto-injected between down and up — plus `kinject/kpress/krelease` semantic keys), the demo is run a second time with `--script` and compared as a separate golden line, so drags, inertia, focus navigation and popup open/select/close are locked frame-exact (10 scripts currently: 7/18/19/25/29/30/102/103/105/107; a script may override its frame count via a first-line `# frames=N`). Diagnostics: env `WE_AUTOTEST_DUMP=1` writes per-frame CRCs, `WE_AUTOTEST_PPM=f1,f2,...` captures those frames as PPM images. A FAIL means behavior changed — inspect before deciding whether it's a regression or an intended change to re-baseline (`-Update` rewrites all lines). Input *feel* still needs a human. Runs are deterministic (unseeded `rand()`, no wall clock).

### Manual smoke test

Validation beyond rendering is done by building a target and running one demo as an integration smoke test:
- **Simulator**: change `#define DEMO_ID` near the top of `main` in `Simulator/main_sim.c` (`0` = showcase), rebuild, run `wegui_sim`, and verify rendering/animation/input behavior.
- **STM32F103**: change `#define DEMO_ID` near the top of `main` in `STM32F103/main.c`, rebuild/flash, and verify on the LCD.
- **STM32F030**: change `#define DEMO_ID` near the top of `main` in `STM32F030/main.c`, rebuild/flash, and verify on the LCD.

Hardware flashing is done outside the build command (CMSIS-DAP / DAPLink / pyOCD are usable depending on the board). If flashed content appears stale, verify the relevant `.build_log.htm` timestamp before reflashing.

## Architecture

### Directory Layout

- `Core/` — platform-independent GUI kernel (`we_gui_driver.c` — objects/input/focus/timers/animation/PFB engine), rendering primitives split into `we_render.c` (image decoders, masks, AA lines, sin/cos, fills — pure move, declarations stay in `we_render.h`/`we_gui_driver.h`), the embeddable scroll-physics component `we_scroll.c/.h` (drag/overscroll/inertia/rebound state machine; used by list/menu/table/logview/dropdown — scroll_panel and roller keep bespoke physics by design), dirty engine `dirty_driver.c`, image/font support; widgets under `Core/widgets/<name>/`. New Core `.c` files must be added to both Keil `.uvprojx` by hand (simulator globs pick them up automatically).
- `Demo/` — demo applications; each widget type has its own `demo_xxx.c`, with declarations in `simple_widget_demos.h`. `demo_common.h` also exposes `we_demo_scale_*` helpers, but those are **legacy**: current demos are written directly against a fixed 280×240 layout.
- `Demo/we_lcd_port_template.c/h`, `we_input_port_template.c/h`, `we_storage_port_template.c/h`, `we_user_config.h_template(...)` — the porting kit for bringing up a new platform.
- `Core/widgets_preview/` + `Demo/preview/` — **preview incubation zone** for experimental widgets (currently 26, e.g. `mask_group`, `ime_pinyin`). Compiled by all three targets (simulator via CMake globs; both Keil projects carry `we_widget_preview`/`demo_preview` file groups — the linker strips whatever the selected demo doesn't reference), DEMO_ID range 100+ (currently 101..126, resequenced 2026-07 with the 12 most-used widgets first; future graduations leave holes), ID table and demo entry declarations in `Demo/preview/preview_demos.h`, function naming `we_<name>_preview_demo_init/tick`. New preview widgets/demos are picked up automatically by the simulator globs but must be added to both `.uvprojx` file groups by hand. These widgets are unpolished and may be removed at any time; graduation moves them into `Core/widgets/` and the stable numbering range.
- `Simulator/` — SDL2 entry (`main_sim.c`), SDL LCD/input/key port (`sdl_port.c/h`), simulator config (`we_sim_port_config.h`), the headless regression scaffold (`sim_autotest.c/h`), and build/run scripts. `main_sim.c` is meant to read as a porting template — it holds only the init/demo-select/main-loop skeleton; the autotest machinery (arg parsing, script parsing, frame loop, hashing, diagnostics) lives entirely in `sim_autotest.c` behind two calls, and the whole scaffold is compile-gated by `WE_SIM_AUTOTEST` (default 0 in `sim_autotest.h` — the everyday build contains no regression code and ignores `--autotest`; `autotest.ps1` passes `-DWE_SIM_AUTOTEST=1` automatically).
- `STM32F103/` — STM32F103 entry (`main.c`), Keil project, LCD SPI ports (soft/hard/DMA 4-line), button/input port, and W25Qxx external flash port.
- `STM32F030/` — STM32F030 entry (`main.c`), Keil project, LCD SPI ports, and button/input port.
- `tool/` — **numbered example-driven resource pipeline** (see “Resource Pipeline” below): `0.tool/windows/` (the underlying exes: font2c, img2bin_raw, img2bin_indexqoi, bin2c), `1.font2c/` (font conversion), `2.img2c/` (image conversion), `3.bin2c/` (external-flash merge), `4.STM32F103_ex_flash_download/` (W25Qxx burn project), `pinyin2c/` (pinyin-table generator feeding the `ime_pinyin` preview widget). Each stage folder has a companion numbered `.txt` note written by the user (`1.字体取模.txt`, …) — treat those as the user-facing docs and edit them minimally. Each stage’s example output IS the demo asset set; `0.1.2.3.update_all.bat` chains stages 1→2→3.

### Platform Config Chain

`we_user_config.h` at the repository root is the unified user configuration entry point. Edit it first for screen size, color depth, PFB/GRAM rows, dirty strategy, timer counts, input/key/storage binding, and widget default tuning macros. It also holds the framework version string `WE_GUI_VERSION` (currently `V0.2.2`); release commits bump it together with the version heading in `README.md`.

**Fonts are fully explicit — there is no default font.** Core never includes any resource header; every text widget takes a `font` pointer in its `obj_init` (mandatory: NULL makes init return without doing anything). The demo layer owns the resource reference: `Demo/simple_widget_demos.h` and `Demo/preview/preview_demos.h` include `simli_16_2bpp.h` (generated into `tool/1.font2c/output/`) and define the legacy alias `we_font_consolas_18` as `((const unsigned char *)&simli_16_2bpp)`, which all demos pass explicitly.

The core includes this config directly through `Core/we_gui_driver.h`. Platform routing then selects a target-specific LCD/port config through `we_hw_port.h` or an STM32-local port header:
- `WE_SIMULATOR` → `Simulator/we_sim_port_config.h`
- `WE_PLATFORM_STM32F030` → `STM32F030/Lcd_Port/stm32f030_hw_config.h`
- `WE_PLATFORM_CMS32C030` → CMS32C030 config (referenced by router, not present in this checkout)
- `WE_PLATFORM_CW32L012` → CW32L012 config (referenced by router, not present in this checkout)
- `WE_PLATFORM_AD15N` → AD15N config (referenced by router)
- default → STM32F103 config

Target hardware config headers select the LCD IC (`LCD_IC`) and physical LCD port (`LCD_PORT`), then define `lcd_set_addr`, `lcd_ic_init`, and `LCD_FLUSH_PORT`/flush callbacks that bind the GUI core to the concrete driver.

`Core/we_gui_config.h` supplies defaults for every optional macro and validates required ones (`LCD_DEEP`, `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `WE_CFG_DIRTY_STRATEGY`, timer/input/storage/focus limits) with `#error` checks. When a macro's default matters, read it here rather than trusting the commented-out example values in `we_user_config.h`.

### Core Runtime Model

The runtime centers on one `we_lcd_t` instance (`Core/we_gui_driver.h`). That object owns:
- the partial frame buffer (PFB/GRAM) and LCD flush callbacks,
- the dirty-rectangle manager,
- the root linked list of GUI objects,
- user timer slots and the central animation list (`anim_head`),
- input state (`we_indev_data_t`), focus state + semantic key queue, and optional storage callback,
- the top-layer object list and modal-object slot (`top_list_head` / `modal_obj`),
- the render-statistics frame counter (`stat_render_frames`, FPS data source).

Widgets and demos mutate object state and mark regions dirty. `we_gui_task_handler()` consumes timers/input/keys/dirty state and redraws through the currently bound LCD port.

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

The simulator additionally calls `sim_lcd_update()` after `we_gui_task_handler()`. Touch input is polled automatically inside `we_gui_task_handler()` through the registered input callback; call `we_gui_indev_handler()` directly only when managing input state manually. Semantic keys are *not* polled — ports push them (see focus section).

### Timer API and the Central Animation Engine

```c
static we_gui_timer_t t;                            // node is caller-owned (static or outliving use)
we_gui_timer_create(lcd, &t, cb, period_ms, repeat); // repeat=1 periodic, 0 one-shot; cannot fail
we_gui_timer_stop(lcd, &t);
we_gui_timer_start(lcd, &t);
we_gui_timer_restart(lcd, &t);   // reset accumulator + reactivate
we_gui_timer_delete(lcd, &t);    // unlink; node reusable
```

Both scheduling layers are **intrusive linked lists** now: user timers (`lcd->timer_head`, caller-owned `we_gui_timer_t` nodes — no slot table, no `WE_CFG_GUI_TIMER_MAX_NUM`, create is idempotent like `we_anim_start`) for business logic, plus the central animation engine for widget animations.

**Widget animations do NOT use user timers.** They run on the central animation engine: a `we_anim_t` node embedded in each widget struct, linked into `lcd->anim_head` via `we_anim_start()`/`we_anim_stop()` and stepped by `we_gui_task_handler()` each cycle. `we_anim_start` cannot fail, is idempotent (re-linking an already-linked node only refreshes the callback), and the count is unbounded; finished animations unlink themselves (idle cost = one NULL check). Invariant: a `step_cb` may unlink only its own node, and must not delete *other* objects (`we_obj_delete` clears the removed node's `next`, truncating the current frame's traversal). Widgets with animation nodes: toggle, progress, indicator, msgbox, slideshow, scroll_panel, dropdown, line (3 independent nodes), box (only when `WE_BOX_USE_ANIM=1`), gauge, list (2 nodes), roller, marquee, toast.

### Timing, ISR-Safety, and Callback Contract

Rules the kernel relies on that no single function comment owns:

- **The ISR-safe surface is exactly three functions**: `we_gui_key_inject` / `we_gui_key_press` / `we_gui_key_release` (SPSC key ring — the injector writes only `tail`). Every other API — object create/delete/attach, timer and animation start/stop, invalidation, `we_gui_tick_inc` — is main-loop only; calling them from an interrupt is a data race even when it happens to work.
- **`we_gui_tick_inc` accumulates into a `uint16_t`**: feed elapsed milliseconds from the main loop; a single accumulated gap must stay below 65536 ms or time is silently lost.
- **Periodic-timer catch-up is capped**: after a long stall (flash erase, blocking peripheral wait), each periodic timer fires at most `WE_CFG_TIMER_CATCHUP_MAX` (default 4) callbacks in the recovery frame; the rest of the backlog is dropped and the timer re-times from now.
- **Kernel recursion depth equals container nesting depth**: draw, hit-test, focus descent, and delete each recurse once per nesting level (demos stay ≤3–4 levels). M0 stack budgeting should assume the deepest container chain the application builds, plus the widget draw path below it.
- **What each callback may do**:
  - *timer cb*: anything — it runs at the top of `we_gui_task_handler` before input and rendering; creating/deleting objects is safe here.
  - *anim `step_cb`*: may unlink only its own node; must not delete any object — not others (`we_obj_delete` clears the removed node's `next`, truncating the frame's anim traversal) and not its own widget (same truncation via the kernel's anim-node reclamation). Self-destruction is deferred to a timer.
  - *`event_cb`*: must not delete its own object or an ancestor of it mid-event (the kernel dereferences the object after the callback returns — press bubbling, release re-check). Deleting objects in unrelated branches is safe: the kernel reclaims pressed/focus/modal/anim references.
  - *`delete_cb`*: tear down own resources only (stop own timers, close own modal); never call `we_obj_delete` — not on itself (infinite recursion) and not on siblings (the parent's child-walk holds a stored `next`).
  - *`draw_cb`*: render only. Never mark dirty and never mutate the tree from a draw callback — it runs once per PFB slice, several times per frame.
  - *`WE_EVENT_MODAL_CLOSE` handler*: put the popup away and nothing else; in particular do not open another modal from inside it (`we_modal_open` is mid-flight on the stack).

### Object Tree, Containers, and Lifetime

**Two independent container properties, two flag bits in `we_class_t.class_flags`** (the field is deliberately *outside* the `WE_CFG_ENABLE_KEY_INPUT` guard — the structural bit is needed by the delete path in pure-touch builds too):

- `WE_CLASS_FLAG_CHILD_OWNER` — **structural**: the object's prefix is `we_child_owner_t` (`base` + `children_head`), so the kernel may take `children_head` and recurse into it. Set by `group`, `scroll_panel`, `slideshow`, `mask_group`.
- `WE_CLASS_FLAG_FOCUS_ENTER` — **behavioral**: focus may descend into this container via OK and pop back out via BACK. Set only by `group` and `scroll_panel`.

Keeping these separate is load-bearing: `slideshow` and `mask_group` *are* structural child owners (deletion and reparenting must walk their `children_head`) but must not accept focus yet — slideshow has no per-page focus scope, so a focus ring that descends into it would stop on off-page children and draw the cursor off-screen. Adding `FOCUS_ENTER` to slideshow is the natural follow-up once page scoping exists.

**Never hand-roll list surgery.** The kernel owns the object list; use `we_obj_attach_to_lcd`, `we_obj_append_to_list`, `we_obj_detach` (unlink + clear `next`/`parent`, keeping `lcd`/`class_p` so the object stays usable), `we_obj_set_parent` (detach + relink; a non-`CHILD_OWNER` parent falls back to the top level rather than corrupting memory), and `we_obj_bring_to_front`.

**`we_obj_delete()` is the single delete entry point**; the ~25 `we_xxx_obj_delete` functions are thin wrappers. Fixed sequence: `class_p->delete_cb` (widget teardown, `lcd`/`class_p` still valid) → **post-order recursion into `children_head` for `CHILD_OWNER` classes** → invalidate for ghost erase → unlink → **kernel-side reclamation of every reference to the object** → clear fields.

The reclamation step is a mechanism, not a convention — it covers `pressed_obj`, `focus_obj` (walking the ancestor chain), the modal slot if the object is the current modal, and **every `we_anim_t` node whose `owner` is this object** (all `we_anim_start` call sites pass the widget instance pointer, whose address equals the base object's, so multi-node widgets like `line` are cleared in one sweep). Consequence: deleting a container cannot leave zombie animation nodes or orphaned children behind, and a widget author who forgets `we_anim_stop` loses nothing but an explicit teardown. `delete_cb` must never call `we_obj_delete` on itself (infinite recursion).

**Cross-widget raw bindings self-heal via `class_p`.** `keyboard.target` / `ime_pinyin.target` (→ textarea) and `textarea.editor` (→ popup editor) are raw pointers between independently-owned objects. Each holder resolves them through a small `_xx_target()` / `_ta_editor()` accessor that treats `class_p == NULL` as "the target was deleted", clears the binding, and returns NULL — the same tell `we_gui_indev_handler` uses for a stale `pressed_obj`. Any future cross-object pointer should follow that idiom.

### Widgets

Stable widgets live in `Core/widgets/<name>/we_widget_<name>.c/.h`; preview widgets in `Core/widgets_preview/<name>/`. Current stable set: `label`, `btn`, `img`, `img_ex`, `arc`, `group`, `checkbox`, `label_ex`, `chart`, `toggle`, `progress`, `msgbox`, `img_flash`, `font_flash`, `slideshow`, `slider`, `scroll_panel`, `dropdown`, `stepper`, `indicator`, `line`, `box`, `gauge`, `list`, `roller`, `marquee`, `toast`.

**Every widget directory carries a `widget.md`** (功能 / 适用场景 / 关键 API / 可调宏 / 事件与行为 / 注意事项 / 已完成的毕业优化 / 对应 demo). Read `Core/widgets/<name>/widget.md` before the source when working on one widget — it documents the tuning macros, dirty-marking strategy, and delete contract. All 26 preview widgets have one; the only stable widgets missing it are `box` and `line`.

Cross-cutting rules that no single `widget.md` owns:
- `img_ex` and `label_ex` use a **512-step angle unit** (`0..511` = full circle; 90° = 128; 180° = 256) — use `WE_ANGLE(deg)` / `WE_DEG(deg)` — and a **256-step scale unit** (`256` = 1.0×, `128` = 0.5×, `512` = 2.0×).
- For `img_ex`, `cx/cy` are the screen transform center while `pivot_ofs_x/y` are source-image local pivot offsets; do not merge those coordinate systems. `img_ex` accepts **only uncompressed RGB565** source images (the plain `img` widget handles the other formats: ARGB8565 raw, both indexed-QOI variants, and the A1/A2/A4/A8 alpha bitmaps — see Resource Pipeline). Alpha bitmaps carry no color: `img` blends them with a per-object foreground color (`we_img_obj_set_color`, default white); their rows are byte-aligned MSB-first, a different layout from the linear bit stream `we_draw_alpha_mask`/fonts use.
- **Data-driven widgets never copy their data.** `dropdown` (`we_dropdown_option_t[]`), `list`/`roller` (`const char *const *`), `marquee`/`toast` (text pointer) store the caller's pointer; the caller must keep it alive for the widget's lifetime.
- **Popups are ordinary top-layer objects with a modal flag** — the old `popup_layer` slot is gone. `we_obj_attach_to_top` puts an object on the LCD top layer (drawn after the normal list and the focus cursor, never clipped by `group`/`scroll_panel`/`slideshow` parents, hit-tested with priority); `we_modal_open/close/get` declares one of them modal: hit-testing is then restricted to the top layer with misses routed to the modal object ("its hit area is the whole screen" — outside-tap-closes stays widget-side), and semantic keys go straight to its `event_cb` (press edges raw, release edges `key | WE_KEY_RELEASE_FLAG`). Opening a new modal sends `WE_EVENT_MODAL_CLOSE` to the old one (modals are mutually exclusive; non-modal top-layer objects like `toast` coexist freely — a toast can float above the keyboard). Users: `dropdown` (embedded `overlay` companion object), preview `keyboard`/`ime_pinyin` (the widget object itself); `msgbox`/`toast` are non-modal top-layer objects; the preview `menu` draws its pages inside its own rect. Touch focus-follow skips top-layer hits (tapping a popup/toast never steals the focus ring).
- **The event space is segmented** (`we_event_t`; one `event_cb` receives every segment): `0x00–0x0F` touch/gesture/kernel queries, `0x10–0x17` semantic keys, `0x20+` focus notifications, `0x40–0x7F` reserved for widget/app custom events (allocate from `WE_EVENT_USER_BASE`), and `0x80` is the release-edge modifier bit — modal objects actually receive `0x90–0x97` for key release edges, so custom codes must stay below `0x80`.
- Opacity propagation uses the lcd-level `opa_scale` multiplier: containers (`group`/`slideshow`/`scroll_panel`) multiply their opacity into it around the children pass, and every drawing primitive consumes it once at entry (`we_opa_apply`, zero cost when no fade is active). A fully transparent container also stops intercepting input.
- Moving a `group` uses fine-grained dirty marking that **depends on the background being a uniform square fill** (radius fixed at 0): the old/new-frame overlap is pixel-identical after a translation, so `_group_set_pos_cb` marks only the exposure L-strips. **If the group background ever gains rounded corners, gradients, or textures, this must revert to full old+new box marking.**
- `stepper` stores its value as a **fixed-point `int32`**: real value = `value / 10^decimals`, split out only at draw time to avoid Cortex-M0 soft-float cost. Its hold-to-repeat reuses the `STAY` event and consumes no timer slot.
- `progress` takes a direct `0..255` target value; `chart` works in **pixel space** with a circular buffer and has no Y-axis scaling API (pre-scale before pushing).
- Decorative widgets pass input straight through (`event_cb` NULL or returning 0): `line`, `box`, `gauge`, `marquee`, `toast`. `indicator` is read-only until `we_indicator_set_clickable()`.
- `box` (no `widget.md` yet) is a panel with **per-corner independent styling** (`WE_BOX_LT/RT/LB/RB`, same order as `WE_MASK_QUADRANT_*`): each corner round / chamfered (45°) / square, plus border and fill. Bordered round corners resolve outer+inner coverage in one 4×4 subsampling pass via `we_mask_quarter_ring_alpha`; chamfer borders inset the inner outline by `(2−√2)·bw ≈ 0.586·bw` (not `bw`) so the diagonal keeps the same visual thickness. `WE_BOX_USE_ANIM` defaults to **0**, making `we_box_anim_*` instant-apply stubs.
- `line` (no `widget.md` yet) is a width-configurable AA segment (round/butt cap) over `we_draw_line` (Xiaolin Wu) plus endpoint AA circles. Geometry, color, and opacity animate on **three independent `we_anim_t` nodes**, so a move, a recolor, and a fade can run at once. `WE_LINE_USE_ANIM 0` strips the animation code and degrades `we_line_anim_*` to instant-apply stubs. Hit-testing is bounding-box granularity.
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

**Dispatch model** (`we_gui_indev_handler`): an LCD-level popup gets first refusal, then the kernel runs a **recursive hit-test** (`_we_hit_test`; the top layer is tested after the normal list and wins ties, mirroring draw order) that intersects each object's rect with its ancestors' visible rects — so a child scrolled out of its `scroll_panel` viewport is not hittable — and returns the deepest, topmost object. On PRESSED the kernel **bubbles up the parent chain** from that object, and the first `event_cb` returning non-zero becomes `lcd->pressed_obj`; all later events in the sequence go straight there. Because the click is only dispatched when `pressed_obj` still equals the object under the finger at release, the "did the finger drift off the widget" re-check is kernel-side.

**Gesture hand-off.** A child that consumes STAY would starve its scrollable ancestor of drag input, so once movement passes `WE_CFG_DRAG_THRESHOLD` (default 8) the kernel offers `WE_EVENT_DRAG_BEGIN` up the ancestor chain, once per touch sequence. The first container to answer non-zero takes over via `we_indev_grab()`: the previous `pressed_obj` gets one `WE_EVENT_RELEASED` to unwind its pressed visual and, since it is no longer `pressed_obj`, never sees a CLICKED. This is what lets interactive widgets live inside scrollable containers.

**Container contract.** The kernel descends into every `CHILD_OWNER` container unconditionally; containers never forward events to children by hand. A container implements only `WE_EVENT_HIT_TEST` (return 0 to skip this container *and its whole subtree* — used for a fully transparent `group`/`mask_group`, or a point outside `scroll_panel`'s inner rect), `WE_EVENT_DRAG_BEGIN` if it scrolls/pages, and its own gesture handling; everything else returns 0 so unconsumed presses bubble to outer containers.

**Focus-cursor ghost prevention is kernel-side**: `we_obj_set_pos` invalidates the focus ring before and after moving `lcd->focus_obj` (the ring hangs GAP+THICKNESS outside the widget bbox, which the widget's own old/new marking cannot cover). Code that moves a focused widget — container relayout, scroll-follow — needs no manual ring marking.

Swipe detection is also in `we_gui_indev_handler()`: on release, movement from press exceeding `WE_CFG_SWIPE_THRESHOLD` (default 30) dispatches `WE_EVENT_SWIPE_LEFT/RIGHT/UP/DOWN` instead of a click. `lcd->gesture_had_stay` records whether the sequence ever hit STAY, which scrollable widgets use to tell a drag from a fast flick; `lcd->gesture_drag_done` marks the drag hand-off offer as already made.

**Input-side trimming macros** (both default 1 in `Core/we_gui_config.h`): `WE_CFG_ENABLE_NESTED_INPUT 0` removes hit-test descent into containers and the drag hand-off ask — for flat UIs that put no interactive children inside scrollable containers. `WE_CFG_ENABLE_TOP_LAYER 0` removes the top-layer list and modal machinery entirely (`we_lcd_t` loses both fields, `we_obj_attach_to_top` degrades to `we_obj_bring_to_front`, the `we_modal_*` trio become no-op stubs) — msgbox/toast/dropdown still compile and display, minus the always-on-top and modal-swallow guarantees.

### Focus and Key Navigation

Added in V0.2.2 and **not covered by `WEGUI_API_REFERENCE.md`** — this section plus the comments in `Core/we_gui_driver.h` are the reference. Gated by `WE_CFG_ENABLE_KEY_INPUT` (`1` in the shared user config; the core default in `we_gui_config.h` is `0`, so a bare port compiles the whole subsystem out at zero cost). Touch and keys coexist — enabling keys changes nothing about touch handling. **The cursor ring is input-source gated** (`WE_FOCUS_F_CURSOR_VIS`): any key activity or a programmatic `we_focus_set`/`we_focus_edit_enter` shows it; a touch press hides it — touch still moves focus underneath (the finger is the cursor), so a later key press resumes navigation from the last-tapped widget with the ring back on. Demos `29` (`demo_focus.c`) and `30` (`demo_focus2.c`) exercise it; the simulator maps arrows / Tab / Shift+Tab / Enter / Space / Esc / Backspace in `Simulator/sdl_port.c`.

**Ports inject semantic keys; they never poll.** The port debounces physical buttons (independent keys, 5-way joystick, EC11 encoder…) and translates them into `WE_KEY_UP/DOWN/LEFT/RIGHT/PREV/NEXT/OK/BACK`:
- Dual-edge ports call `we_gui_key_press(lcd, key)` / `we_gui_key_release(lcd, key)`. Only OK consumes the release edge (widgets stay visually pressed while OK is held); other keys may skip release reporting. Direction-key auto-repeat is the port's job — re-inject the press edge.
- Simple ports call `we_gui_key_inject(lcd, key)` for a complete tap; OK then goes through the `WE_CFG_FOCUS_FLASH_MS` (default 90ms) minimum-press window so the press is visible.
- Injection is ISR-safe: keys land in a **volatile SPSC ring buffer** (`key_queue`, `WE_CFG_KEY_QUEUE_LEN` — power of two ≥ 4 (a tap injects two edges), core default 8, usable capacity = len−1). The injector writes only `tail`, the consumer only `head`. Release edges are encoded as `key | WE_KEY_RELEASE_FLAG`. A full queue drops the incoming code — dropping the OK release edge would leave the focused widget stuck pressed and all later OK presses swallowed by held-key dedup, so that one case is tracked in an injector-owned drop counter (`key_ok_drop_seq`) and re-completed by the consumer at the top of each cycle (honoring the minimum-press window); other keys just lose one input, no state.

**Kernel → widget notifications travel on the same callback and must never be injected by a port**: `WE_KEY_EVT_FOCUS` (focus query — returning 0 refuses focus for this instance, e.g. a disabled control), `WE_KEY_EVT_DEFOCUS`, `WE_KEY_EVT_FLASH_END` (press cancelled — bounce back without clicking), `WE_KEY_EVT_OK_RELEASE` (bounce back *and* fire the action), `WE_KEY_EVT_CHILD_FOCUS` (sent up the ancestor chain so e.g. `scroll_panel` scrolls the focused child into view).

**Focusability is structural**: `we_class_t` gained `key_cb` and `class_flags`, and a **non-NULL `key_cb` means focusable**. All class descriptors use designated initializers, so widgets that predate the feature are automatically NULL/non-focusable. `WE_CLASS_FLAG_CHILD_OWNER` marks composite containers (prefix `we_child_owner_t`) that focus can descend into. A `key_cb` returning non-zero means "consumed"; anything unconsumed falls through to the focus manager's default navigation (direction keys move by bounding-box-center proximity, PREV/NEXT walk a linear ring, OK enters a container or triggers a widget, BACK exits a container and clears focus at top level).

Public API: `we_focus_set` / `we_focus_get` / `we_focus_candidate` (structural test used by containers to refuse focus when their subtree is empty), plus `we_focus_edit_enter/exit/active` for the **edit state** that value widgets (slider, stepper, roller, list) enter from their OK branch so direction keys adjust the value instead of moving focus. Kernel state lives in `lcd->focus_obj` / `focus_flags` (`WE_FOCUS_F_EDIT/OK_HELD/OK_ARMED/REL_PEND`) / `key_flash_left_ms` — read-only for ports and applications. `we_obj_delete` walks the ancestor chain to avoid dangling focus.

**Modal key channel**: while a modal object is registered (`we_modal_open`), semantic keys bypass the focus manager and go straight to that object's `event_cb` — press edges as the raw key value, release edges as `key | WE_KEY_RELEASE_FLAG` (`0x90..0x97` on the wire). Unconsumed keys do not fall through (modal swallows all keys). `dropdown` and the preview `keyboard` / `ime_pinyin` rely on this; there is no separate hook to install since the unified `event_cb` receives everything.

**Size trimming** (all default on): `WE_CFG_FOCUS_EDIT 0` removes the edit state entirely (only click-like semantics remain); `WE_CFG_FOCUS_NESTED 0` flattens the focus ring to top-level widgets; and each widget has its own `WE_<NAME>_USE_KEY` macro (defined in that widget's header) to drop its key callback and focusability individually. Stable widgets with key support: `btn`, `checkbox`, `toggle`, `indicator`, `slider`, `stepper`, `roller`, `list`, `scroll_panel`, `dropdown`. Preview: `calendar`, `imgbtn`, `logview`, `menu`, `radio`, `table`, `textarea`. Cursor appearance and feel are tunable via `WE_CFG_FOCUS_CURSOR_THICKNESS/GAP/R/G/B` and `WE_CFG_FOCUS_FLASH_MS`.

### Resource Pipeline (`tool/`) and Image Format

`tool/` is a numbered, example-driven pipeline: each stage is a self-contained tool whose example inputs/outputs double as the demo asset set, so a user can reproduce the whole flow by double-clicking the stage `.bat`s (or `tool/0.1.2.3.update_all.bat` for the 1→2→3 chain). Stage notes live in the user-written numbered `.txt` files.

- `1.font2c/` — fonts. `1.manage_input.bat` is an interactive wizard that generates `input/*.json` configs; `2.build.bat` runs font2c over all of them into `output/`. Current demo fonts: `simli_16_2bpp` (internal ASCII — the `we_font_consolas_18` alias target used by every demo), `msyh_16_4bpp_ime` (internal, ASCII + GB2312 level-1 charset for the `ime_pinyin` preview), `gbsn00lp_2_16_4bpp` (external ASCII+CJK: index `.c/.h` compiled into the MCU, glyph-data `.bin` merged into external flash; demos 0/16). Font files resolve against `fonts/` then the system font directories (`SIMLI.TTF`/`msyh.ttc` come from Windows, not the repo).
- `2.img2c/` — images. `img2c_rgb565.bat` (matches this repo’s RGB565 targets; an rgb888 variant exists for other targets). `input/` buckets encode pixel format × compression × destination: `*_2c` buckets are merged into the internal-array `output/c/res_img.c/.h`, `*_2bin` buckets become per-image bins in `output/bin/`. Demo images: `demo_rgb565_raw_be_64x80` (uncompressed RGB565 — the only format `img_ex` accepts; also used by imgbtn/showcase), `demo_rgb565_indexqoi_be_128x64`, `demo_argb8565_indexqoi_be_80x80`, `demo_argb8565_raw_be_80x80`. The A1/A2/A4/A8 alpha-bitmap buckets (six 48×48 icons per depth) are decoded by the `img` widget and consumed by demo 31 (`demo_img_alpha.c`).
- `3.bin2c/` — external-flash merge. `build_bin.bat` concatenates `1.font2c/output/*.bin` + `2.img2c/output/bin/*.bin` into `output/merged_bin.bin/.c/.h` (`.c` holds only the `bin_addr_table[]`; `.h` has the ID enum plus SIZE/ADDR macros) and additionally an **embed-data** variant under `output/embed/` that is compiled ONLY by the burn project (not committed — a ~17 MB generated C source; run `build_bin.bat` once before burning).
- `4.STM32F103_ex_flash_download/` — Keil burn project: compiles `3.bin2c/output/embed/merged_bin.c` and writes it to the W25Qxx through the debugger using the custom `Flash_FLM/WE_STM32F103_W25Q128.FLM` algorithm. **Changing an external asset means re-burning** — a firmware rebuild alone will not update external flash.
- Consumers: all three targets add `tool/1.font2c/output`, `tool/2.img2c/output/c`, `tool/3.bin2c/output` to their include paths and compile the three font `.c`, `res_img.c`, and the table-only `merged_bin.c`. Demos include `res_img.h` (internal arrays) or `merged_bin.h` (external-flash ID/address table).
- **The simulator embeds no external-flash data**: its storage port opens `merged_bin.bin` next to the exe (CMake copies it post-build) and serves reads via `fseek`/`fread`; a missing file reads as `0xFF` (erased flash). Changing external assets = rerun `3.bin2c` + restart the sim, no rebuild — mirroring the hardware “re-burn” flow.
- Batch files are saved as **GBK + CRLF** (cmd’s native encoding; UTF-8 or LF-only bats mis-parse Chinese comments into broken commands).

Image resource format v2 (`Core/image_res.h`) is the contract between the tools and the core: a 6-byte header `[res_type][format][width_hi][width_lo][height_hi][height_lo]` (dimensions always big-endian) followed by pixel data, parsed with the `IMG_DAT_*` macros. The format byte splits into a compression nibble `[7:4]` (`0x0` none, `0x1/0x2` RLE, `0x3` plain QOI, `0x4` indexed QOI, `0x5` QOIF — tool-reserved, no core decoder) and a pixel-format nibble `[3:0]` (`0x0` RGB565 … `0x8` ARGB8565, `0xB`/`0xC`/`0xD`/`0xE` = A8/A4/A2/A1 alpha bitmaps, `0xF` OLED bitmap; `0x9/0xA` tool-reserved). `WE_CFG_ENABLE_INDEXED_QOI` in `we_user_config.h` trims the indexed-QOI decoder and its dispatch path out of the image widget.

## Demo Style

Each demo follows this pattern:

```c
void we_xxx_simple_demo_init(we_lcd_t *lcd);
void we_xxx_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);
```

One demo should be a small, copyable example: static variables plus one init function plus one tick function, written against the fixed 280×240 layout. Demos are also the primary integration tests for widgets, timers, input, storage-backed assets, and rendering behavior. A demo whose feature is compile-gated (e.g. the focus demos under `WE_CFG_ENABLE_KEY_INPUT`) provides empty `#else` stubs so every target still links.

Demo selection is a **compile-time `#define DEMO_ID`** + `#if/#elif` chain in each entry's `main` (no runtime `switch`); only the selected demo's `init`/`tick` is compiled in. Numbering is unified across all three targets — `1..31` and the preview range `101..126` are identical: `1` label, `2` btn, `3` img, `4` img_ex, `5` arc, `6` group, `7` slideshow, `8` concentric arc, `9` checkbox, `10` label_ex, `11` chart, `12` toggle, `13` progress, `14` msgbox, `15` flash img, `16` flash font, `17` slider, `18` scroll_panel, `19` dropdown, `20` stepper, `21` indicator, `22` line, `23` box, `24` gauge, `25` list, `26` roller, `27` marquee, `28` toast, `29` focus, `30` focus2, `31` img_alpha. Only `0 = showcase` remains simulator-only (needs 800×480, guarded by a nested `#warning`; on STM32 an ID of 0 falls through to the fallback). The `#else` fallback is `label` on all three targets. Switch demos by editing the single `#define DEMO_ID` line near the top of `main`.

When adding a demo, update the `DEMO_ID` comment block + `#if/#elif` chain in all three entry files, declare its `init`/`tick` in `Demo/simple_widget_demos.h`, and add the `demo_xxx.c` to `DEMO_SOURCES` in `Simulator/CMakeLists.txt` (and to each Keil `.uvprojx`).

## Code Style

- Comments are in Chinese; make targeted edits and avoid bulk text replacement that could cause mojibake.
- Prefer direct, readable C with minimal abstraction layers.
- Prefer static variables in demos over complex state shells.
- Keep demo code easy to copy into user projects.
- Cortex-M0 is a first-class target: no malloc, no floating point, and no `%`/`/` on hot paths (use power-of-two masks or pre-divided Q16 slopes, as `gauge` and the key queue do).
- `WE_ASSERT` (default no-op) guards the cold lifecycle/init API entries in the kernel; define it in `we_user_config.h` during bring-up for loud misuse diagnosis. Hot paths and the three ISR-safe key injectors deliberately stay assert-free, and the silent NULL guards behind every assert keep release builds tolerant.
- When adding a widget, write its `Core/widgets/<name>/widget.md` alongside the code.

## Key Files to Read First

1. `we_user_config.h` — unified screen/PFB/dirty/input/key/storage/widget config.
2. `Core/we_gui_config.h` — required-macro validation **and** the authoritative defaults for every optional macro.
3. `we_hw_port.h` — platform routing by preprocessor define.
4. `Core/we_gui_driver.h` — core runtime object, event/key enums, and public API surface.
5. `Core/widgets/<name>/widget.md` — per-widget documentation; the first stop for any single-widget task.
6. `Simulator/main_sim.c` — simulator entry and demo selection.
7. `STM32F103/main.c` / `STM32F030/main.c` — hardware entries and demo selection.
8. `Demo/simple_widget_demos.h` — demo entry declarations.
9. `tool/` numbered `.txt` notes — per-stage tool usage (fonts / images / merge / burn).
10. `WEGUI_API_REFERENCE.md` — full API reference and usage examples (predates the focus subsystem).
