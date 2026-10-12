# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WeGui-ARGB is a lightweight embedded GUI framework for MCU / SoC targets plus a lightweight PC simulator. The platform-independent GUI kernel and demos live in `Core/` and `Demo/`; each hardware/simulator target provides only the port layer, startup code, and build project.

Primary targets currently present in this repository:
- `SimLite/` — PC simulator on the vendored single-header `fenster` backend (Win32 GDI / macOS Cocoa / Linux X11, no SDL), compiled with TCC (vendored-able, ~2.5 MB, second-scale full builds) or system gcc/cc. Replaced the former SDL2+CMake `Simulator/` 2026-08; the golden-CRC autotest harness moved here with bit-identical hashes. **The developer tooling is local-only** (gitignored, like AD14N/GD32E230): `SimLite/debug/` (dev exe sources), `SimLite/autotest.ps1` and `SimLite/autotest/` (golden files + `.evt` scripts) exist in the working copy but are not published, so the public repo shows only the hardware-identical `main_lite.c` entry. `build_lite.ps1 -Dev` / `build_lite.sh --dev` fail with an explicit message when `debug/` is absent. Golden files therefore have no git history — OneDrive version history is the only rollback for a mistaken `-Update`.
- `STM32F103/` — Keil MDK-ARM AC5 hardware target, with LCD (6 TFT + 8 page-mode mono OLED + SSD1327 gray ICs over soft/hard/DMA SPI and soft-IIC, `LCD_TYPE` auto-derived per IC), input, and W25Qxx external flash ports.
- `STM32F030/` — Keil MDK-ARM AC5 hardware target, with LCD and input ports (TFT SPI only — `DEEP_GRAY8` is blocked by a compile-time `#error` in `stm32f030_hw_config.h` until the OLED/GRAY ports are ported over from F103, preventing a builds-fine-but-black-screen trap).
- `AD14N/` — JieLi AD142A4 (sh54 core) hardware target, CodeBlocks + pi32 LTO toolchain, with SPI1+DMA LCD port (480×320 ST7796S), single-button key port, and a trimmed in-repo SDK subset.
- `GD32E230/` — GD32E230C8 (Cortex-M23, 64 KB flash / 8 KB RAM) hardware target for the "老王电子" 240×240 ST7789 board, Keil MDK-ARM **AC6** (Cortex-M23 needs ARMCLANG; the two STM32 projects are AC5), with SPI0+DMA TFT port and SPI1+DMA GD25Q128 external-flash port. Local-only like AD14N (the whole folder is gitignored).

Full API reference: `WEGUI_API_REFERENCE.md` (ch.16 is a focus/key-nav quick reference; the authoritative description is the "Focus and Key Navigation" section below plus `Core/we_focus.h` / `Core/we_types.h` comments).

## Build Commands

### SimLite (TCC / gcc, no CMake)

```powershell
# Release build (demo chosen by the DEMO_ID macro in SimLite/main_lite.c)
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/build_lite.ps1"

# Override DEMO_ID from the command line without editing source (-DDEMO_ID=N)
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/build_lite.ps1" -Demo 25 -Run

# Developer build wegui_lite_dev (runtime demo select / --shot / --autotest / --gif / --list)
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/build_lite.ps1" -Dev

# macOS / Linux (clang+Cocoa / cc+libx11-dev)
sh SimLite/build_lite.sh [--dev]
```

`build_lite.ps1` prefers TCC unpacked at `SimLite/tcc/tcc.exe` (gitignored; second-scale full builds) and falls back to gcc (`-O2 -g`, debuggable via `.vscode/launch.json`). Sources are globbed from `Core/*.c`, `Core/widgets/*/*.c`, `Core/widgets_preview/*/*.c`, `Demo/preview/*.c` plus the stable-demo manifest `Demo/demo_manifest.txt` (one `.c` per line, `sim-only` tag marks simulator-only entries like showcase; read by `build_lite.ps1`/`.sh` and both AD14N scripts — a new stable demo is one manifest line, no script edits) and the generated resource `.c` files under `tool/`; new widgets/preview demos compile automatically. The release exe compiles exactly one demo (hardware-identical usage); the `-Dev` exe carries the runtime registry (`SimLite/debug/sim_demo_registry.c`) for no-rebuild demo switching.

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

### AD14N Hardware (JieLi AD142A4, CodeBlocks / pi32)

Requires the JieLi pi32 toolchain at `C:\JL\pi32` (installed with JieLi's CodeBlocks distribution). Two equivalent build paths:

```powershell
# Headless build (compile + LTO link + app.bin); add -Download to flash via USB
powershell -NoProfile -ExecutionPolicy Bypass -File "AD14N/build_ad14n.ps1"

# CodeBlocks CLI (also runs the post-build download.bat, which packs wegui/update.ufw;
# without a board attached the USB-download step just fails harmlessly)
& "C:\Program Files\CodeBlocks\codeblocks.exe" /na /nd /ns --rebuild --target=Release AD14N\AD14N_wegui.cbp
```

`AD14N/AD14N_wegui.cbp` is generated by `AD14N/gen_cbp.ps1`, which globs Core/widgets/preview/Demo sources — rerun it after adding/removing any `.c` file (the AD14N target needs no manual project-file edits, unlike the Keil `.uvprojx`). `build_ad14n.ps1` globs the same set at build time. Artifacts land in `AD14N/sdk/app/post_build/sh54/` (`app.bin` ≈ 30–37 KB per demo; `wegui/update.ufw` is the flashable firmware package). Memory model: 32 KB RAM total — 2 KB stack (cpu_lib `startup.o`), ~10.8 KB `.bss` incl. the 7.5 KB PFB (480×8 rows), remainder heap; hot flush/SPI code runs from RAM via the `.wegui.text.cache.L2` section.

### GD32E230 Hardware (Keil MDK-ARM AC6, local-only)

```powershell
UV4.exe -r "GD32E230\MDK-ARM\Project.uvprojx" -t "GD32E230" -o "GD32E230\MDK-ARM\build_log.txt"
```

Compiler define `WE_PLATFORM_GD32E230` routes `we_hw_port.h` to `GD32E230/Lcd_Port/gd32e230_hw_config.h`; the shared `we_user_config.h` is already this board's native 240×240 (demos keep their 280×240 layout anchored top-left, so the right 40 columns are cut). The project vendors the GD32E23x V2.5.0 standard peripheral library plus the CMSIS core headers under `GD32E230/Libraries/` (no RTE component), and its `system_gd32e23x.c` selects the 72 MHz IRC8M/2 PLL branch because the board has no crystal. Only rcu/gpio/spi/dma/misc peripheral sources are in the project. `GD32E230/MDK-ARM/Project.uvprojx` is generated from source globs (same groups as the STM32 projects) — regenerate or edit by hand when adding a `.c`. Board wiring: LCD on SPI0 (SCK PB3, MOSI PB5, DC PB4, CS PA15, RST PA12, backlight PA11, IM PF7 high = SPI mode), SPI0_TX = DMA channel 2 (`DMA_Channel1_2_IRQHandler`); GD25Q128 on SPI1 (SCK PB13, MISO PB14, MOSI PB15, CS PB12, WP PB10, HOLD PB11), SPI1_RX/TX = DMA channels 3/4, byte-access FIFO mode. No touch or keys on the board (input port is the empty stub). External-flash assets are burned with `tool/4.ex_flash_download/GD32E230/` (see “Resource Pipeline”); until the GD25Q128 has been programmed, demos 15/16 render empty placeholders.

`.vscode/tasks.json` currently provides SimLite tasks (`lite: stop running`, `lite: build` — the default build task on Ctrl+Shift+B, `lite: run`, `lite: build and run`, `lite: build (gcc, debug)` — gdb-debuggable release build of `wegui_lite.exe` used by `launch.json`; build tasks run `lite: stop running` first so the linker can overwrite a running exe; the dev build and autotest are local-only and run from the terminal, not as tasks) and STM32F103 Keil tasks (`stm32: build (AC5)`, `stm32: rebuild (AC5)`, `stm32: open MDK project`). The Keil tasks depend on local VS Code settings such as `wegui.keilUv4Path`, `wegui.stm32ProjectFile`, and `wegui.stm32TargetName`.

## Tests / Validation

### Golden-CRC regression (SimLite, headless)

```powershell
# Compare every demo (1..33, 101..127 minus graduated holes 117/120) against the fingerprint-matched golden file
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/autotest.ps1"

# Only specific demos
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/autotest.ps1" -Ids 18,7,25

# Re-baseline after an INTENDED visual change (review FAILs first; merges — only re-run ids are rewritten)
powershell -NoProfile -ExecutionPolicy Bypass -File "SimLite/autotest.ps1" -Update
```

The script builds the developer exe **once** (`build_lite.ps1 -Dev`, tcc-first; `-SkipBuild` to reuse), then runs `wegui_lite_dev <id> --autotest 180 --out <file>` headless per id (no window, fixed 16ms ticks, FNV-1a hash chained over every frame's ARGB8888 framebuffer) — no per-id reconfigure/recompile, a full 57-demo + 10-script sweep takes ~80 s. The scaffold lives in `SimLite/debug/lite_autotest.c/h` and is only compiled into the `-Dev` exe — the release build contains no regression code (no compile gate needed; the `debug/` folder is simply not in the release source list). Hash discipline: the framebuffer layout and 565→ARGB8888 bit-replication in `fenster_port.c` are bit-identical to the retired SDL simulator, so golden values survived the migration unchanged (verified 2026-08: full sweep ALL PASS against the pre-migration golden file). **Config fingerprint**: golden hashes depend on screen size / color depth / MONO style (MONO counts because the simulator's mono-preview binarization is applied inside the hashed buffer); the DMA double-buffer axis stays in the fingerprint for file naming, but since rendering is slice-independent it no longer changes the hashes, so each config gets its own golden file: `golden.txt` is the canonical `280x240-rgb565-mono0-dma1` (the layout the demos are drawn for), other configs use `golden_<fingerprint>.txt` — currently `golden_240x240-rgb565-mono0-dma1.txt` (the shared `we_user_config.h` as of 2026-09; recorded 2026-09-29) and `golden_128x64-gray8-mono1-dma0.txt` (the 128×64 small-screen demo branches and MONO widget styles). The dev exe reports its fingerprint via `wegui_lite_dev --cfg`; the driver auto-selects the matching golden file and validates the `cfg=` header line in it against the exe before running — a mismatch refuses to run, so a wrong-config `-Update` can no longer wash the baseline table. **Interaction-trajectory goldens**: if `SimLite/autotest/scripts/<id>.evt` exists (frame-stamped `down/move/up` — STAY auto-injected between down and up — plus `kinject/kpress/krelease` semantic keys), the demo is run a second time with `--script` and compared as a separate golden line, so drags, inertia, focus navigation and popup open/select/close are locked frame-exact (10 scripts currently: 7/18/19/25/29/30/102/103/105/107; a script may override its frame count via a first-line `# frames=N`). Diagnostics: env `WE_AUTOTEST_DUMP=1` writes per-frame CRCs, `WE_AUTOTEST_PPM=f1,f2,...` captures those frames as PPM images; `wegui_lite_dev <id> --shot N out.ppm` renders N frames headless and dumps a PPM for eyeballing. A FAIL means behavior changed — inspect before deciding whether it's a regression or an intended change to re-baseline. Input *feel* still needs a human. Runs are deterministic (unseeded `rand()`, no wall clock). Note: `sim_demo_registry.c` historically mapped id 113 (ime_pinyin) to a NULL tick, so the dev exe never ran its auto-typing script and both goldens held a static frame — fixed 2026-08-22 (registry now wires the real tick; both golden files re-recorded, id 113 entries now cover the animated pinyin→candidate→commit sequence). All three golden files were re-recorded 2026-09-30 after rendering was made slice-independent (canonical: 4/10/16/118/125; 240x240: those plus 113; gray8: 4/10/16) — every changed entry was traced to the specific fix before re-recording.

### Manual smoke test

Validation beyond rendering is done by building a target and running one demo as an integration smoke test:
- **SimLite**: change `#define DEMO_ID` near the top of `main` in `SimLite/main_lite.c` (`0` = showcase), rebuild (`build_lite.ps1`, ~2 s with tcc), run `wegui_lite`, and verify rendering/animation/input behavior; or skip the edit entirely with `wegui_lite_dev <id>`.
- **STM32F103**: change `#define DEMO_ID` near the top of `main` in `STM32F103/main.c`, rebuild/flash, and verify on the LCD.
- **STM32F030**: change `#define DEMO_ID` near the top of `main` in `STM32F030/main.c`, rebuild/flash, and verify on the LCD.
- **AD14N**: change `#define DEMO_ID` near the top of `app` in `AD14N/main.c`, run `AD14N/build_ad14n.ps1 -Download` (JieLi USB download), and verify on the LCD. No touch — the single button on PA0 maps to short-press `WE_KEY_NEXT` / long-press `WE_KEY_OK`, so focus-navigable demos are fully operable; demos 15/16 (external flash) show empty placeholders and 113 (ime_pinyin, ~700 KB font) exceeds the 512 KB flash.

Hardware flashing is done outside the build command (CMSIS-DAP / DAPLink / pyOCD are usable depending on the board; the AD14N flashes through JieLi's `isd_download.exe` via `download.bat`/`-Download` instead). If flashed content appears stale, verify the relevant `.build_log.htm` timestamp before reflashing.

## Architecture

### Directory Layout

- `Core/` — platform-independent GUI kernel, split by responsibility behind the umbrella header `wegui.h` (widgets, demos and ports include only `wegui.h`): `we_types.h` (every shared type — `we_obj_t`, `we_class_t`, `we_lcd_t`, `we_port_t`, events), `we_core.c/.h` (`we_gui_init` / `we_gui_run`), `we_obj.c/.h` (object tree, attach/detach/delete + reclamation, invalidation, top layer/modal), `we_tick.c/.h` (user timers + central animation engine), `we_input.c/.h` (touch polling, recursive hit-test, gestures), `we_focus.c/.h` (key queue, focus navigation, cursor ring), `we_refresh.c` (dirty-rect consumption, PFB slicing, kernel recursive draw), `we_draw.c/.h` (PFB clip window, blending, fills, rounded-rect fill/ring `we_draw_round_rect_analytic_fill` / `we_draw_round_rect_ring`, masks, AA lines, sin/cos, `we_draw_alpha_mask`, rotate/scale geometry `we_xform_*`), `we_image.c/.h` (image decoders + format dispatch), `we_text.c/.h` (`we_utf8_next`, glyph lookup incl. `we_font_code_to_index` shared with the external-flash font, text measure incl. `we_get_text_ink_width`, draw), `we_dirty.c/.h` (dirty-rect manager). `we_internal.h` is the kernel-private cross-module header — widgets must not include it. Widgets live under `Core/widgets/<name>/`. `Core/components/` holds pure components that never touch `we_lcd_t` (include them as `"components/xxx.h"`): the embeddable scroll-physics state machine `we_scroll.c/.h` (drag/overscroll/inertia/rebound; used by list/menu/table/logview/dropdown — scroll_panel and roller keep bespoke physics by design) and the easing library `we_motion.c/.h`. New Core `.c` files must be added to both Keil `.uvprojx` by hand (the simulator and the AD14N build glob `Core/*.c` + `Core/components/*.c` automatically; rerun `AD14N/gen_cbp.ps1` to refresh the CodeBlocks project).
- `Demo/` — demo applications; each widget type has its own `demo_xxx.c`, with declarations in `simple_widget_demos.h`. `demo_common.h` provides small shared helpers (FPS display, right-aligned X); demos are written directly against fixed layouts — 280×240 primary, plus a per-demo 128×64 small-screen branch selected by `WE_DEMO_SMALL_SCREEN` (`SCREEN_WIDTH <= 128`, defined in `simple_widget_demos.h`).
- `Demo/we_lcd_port_template.c/h`, `we_input_port_template.c/h`, `we_storage_port_template.c/h`, `we_user_config.h_template(...)` — the porting kit for bringing up a new platform.
- `Core/widgets_preview/` + `Demo/preview/` — **preview incubation zone** for experimental widgets (currently 25, e.g. `mask_group`, `ime_pinyin`, `glowring`). Compiled by all four targets (simulator and AD14N via source globs; both Keil projects carry `we_widget_preview`/`demo_preview` file groups — the linker strips whatever the selected demo doesn't reference), DEMO_ID range 100+ (currently 101..127 with graduated holes 117→33 segdisp and 120→32 imgbtn, resequenced 2026-07 with the 12 most-used widgets first; graduations leave holes), ID table and demo entry declarations in `Demo/preview/preview_demos.h`, function naming `we_<name>_preview_demo_init/tick`. New preview widgets/demos are picked up automatically by the simulator/AD14N globs but must be added to both `.uvprojx` file groups by hand. All 25 preview demos carry the same `WE_DEMO_SMALL_SCREEN` 128×64 branch as the stable set (`preview_demos.h` defines the same macro; small-screen size defaults for keyboard echo bar / ime_pinyin bar heights / radio indicator live in the respective widget headers gated on `SCREEN_WIDTH <= 128`). These widgets are unpolished and may be removed at any time; graduation moves them into `Core/widgets/` and the stable numbering range.
- `SimLite/` — PC simulator: entry `main_lite.c` (pure porting template, hardware-identical `#define DEMO_ID` + `#if/#elif` chain, no debug facilities), vendored `fenster.h` (single-header Win32 GDI / Cocoa / X11 windowing), port `fenster_port.c` + `lite_port.h` (mouse→touch, keyboard→semantic keys with auto-repeat, WASD swipe injection, external-flash image via `merged_bin.bin` next to the exe, ARGB8888 framebuffer + `lite_fb()`/`lite_dump_ppm()` debug accessors), simulator config `we_sim_port_config.h` (same filename as the retired SDL simulator's — include-path order picks this one), build scripts `build_lite.ps1`/`.sh` (tcc-first / gcc, `-Dev` for the developer exe), the regression driver `autotest.ps1` + `autotest/` (golden.txt + scripts/*.evt), and `debug/` — developer-only sources compiled solely into `wegui_lite_dev`: `debug_main.c` (runtime demo select via argv, `--shot`, `--autotest`, `--gif`), `sim_demo_registry.c/h` (id→init/tick table incl. 0=showcase), `lite_autotest.c/h` (headless CRC regression scaffold, ported bit-compatible from the retired SDL `sim_autotest.c`; also exports the `lite_at_script_*` trajectory-replay helpers), `lite_gif.c/h` (headless GIF89a recorder — `--gif out.gif [--from s] [--dur s] [--fps n] [--script s.evt]` renders the demo on the same deterministic fixed-16ms pipeline and encodes the [from, from+dur) window as an infinitely-looping GIF; zero-dependency lossless-first encoder: frames with ≤256 unique colors get an exact per-frame local color table (pixel-perfect, LZW code width adapts to palette size), busier frames fall back to a 6×6×6 cube + 40-gray global palette with Bayer 4×4 ordered dithering; standard 12-bit GIF-LZW — byte-identical output for identical commands, works with the same .evt scripts as autotest, run summary reports the lossless/total frame ratio).
- `STM32F103/` — STM32F103 entry (`main.c`), Keil project, LCD SPI ports (soft/hard/DMA 4-line), button/input port, and W25Qxx external flash port.
- `STM32F030/` — STM32F030 entry (`main.c`), Keil project, LCD SPI ports, and button/input port.
- `AD14N/` — JieLi AD142A4 entry (`main.c` — SDK boot flow `c_main → system_init → app`, then the standard WeGui main loop), `sdk_glue.c` (2 ms tick ISR → `g_we_tick_ms`/`jiffies`, `os_time_dly`, plus a self-contained `snprintf`/`vsnprintf` — JieLi's libc has none), `app_config.h/.c` + `app_modules.h` (SDK-style board config; debug UART on PA9 @1 Mbps), `Lcd_Port/` (`ad14n_hw_config.h` LCD_IC selection, `ad14n_lcd_spi1_port.c` SPI1-register+DMA bus primitives with in-place RGB565 byte swap, six screen-IC drivers), `Key_Port/` (PA0 single button → semantic keys), `sdk/` (trimmed JieLi SDK subset: headers, `cpu_lib.a`, bsp sources, `post_build/sh54/` link/pack toolchain incl. the `wegui/` variant of `app_ld.c`/`download_bat.c`/`isd_config.ini`), `AD14N_wegui.cbp` + `gen_cbp.ps1` + `build_ad14n.ps1`.
- `tool/` — **numbered example-driven resource pipeline** (see “Resource Pipeline” below): `0.tool/windows/` (the underlying exes: font2c, img2bin_raw, img2bin_indexqoi, img2bin_indexqoimask, bin2c, res_manager), `1.font2c/` (font conversion), `2.img2c/` (image conversion), `3.bin2c/` (external-flash merge), `4.ex_flash_download/` (external-flash burn projects, one subfolder per chip: `STM32F103/` for W25Qxx, `GD32E230/` for GD25Q128 — the latter local-only, gitignored together with `GD32E230/`), `pinyin2c/` (pinyin-table generator feeding the `ime_pinyin` preview widget). Each stage folder has a companion numbered `.txt` note written by the user (`1.字体取模.txt`, …) — treat those as the user-facing docs and edit them minimally. Each stage’s example output IS the demo asset set; `0.1.2.3.update_all.bat` chains stages 1→2→3.

### Platform Config Chain

`we_user_config.h` at the repository root is the unified user configuration entry point. Edit it first for screen size, color depth, PFB/GRAM rows, dirty strategy, timer counts, input/key/storage binding, and widget default tuning macros. It also holds the framework version string `WE_GUI_VERSION`; release commits bump it together with the version heading in `README.md`.

**Fonts are fully explicit — there is no default font.** Core never includes any resource header; every text widget takes a `font` pointer in its `obj_init` (mandatory: NULL makes init return without doing anything). The demo layer owns the resource reference: `Demo/simple_widget_demos.h` and `Demo/preview/preview_demos.h` include the generated fonts (`tool/1.font2c/output/`) and define the legacy alias `we_font_consolas_18`, which all demos pass explicitly — it resolves to `simli_16_2bpp` on the primary layout and to `simsun_12_2bpp` when `WE_DEMO_SMALL_SCREEN` is set (the unreferenced font array is stripped by the linker).

The core includes this config directly through `Core/we_types.h` and **never includes a platform header** — so no port-side macro can make the kernel and a port translation unit disagree. Port drivers and `main.c` pick up their target config through `we_hw_port.h` (or an STM32-local port header):
- `WE_SIMULATOR` → `SimLite/we_sim_port_config.h`
- `WE_PLATFORM_STM32F030` → `STM32F030/Lcd_Port/stm32f030_hw_config.h`
- `WE_PLATFORM_CMS32C030` → CMS32C030 config (referenced by router, not present in this checkout)
- `WE_PLATFORM_CW32L012` → CW32L012 config (referenced by router, not present in this checkout)
- `WE_PLATFORM_AD15N` → AD15N config (referenced by router, not present in this checkout)
- `WE_PLATFORM_AD14N` → `AD14N/Lcd_Port/ad14n_hw_config.h`
- `WE_PLATFORM_GD32E230` → `GD32E230/Lcd_Port/gd32e230_hw_config.h`
- default → STM32F103 config

`SCREEN_WIDTH`/`SCREEN_HEIGHT` in `we_user_config.h` have **no per-platform branch**: the shared config is 240×240 RGB565 with a 2-row PFB for every target (the 老王 GD32E230 board's native size and its 8 KB RAM budget; the STM32 port configs carry the panel framing — F103 `SCREEN_X_OFFSET 20` for its 240×280 ST7789V3 panel, F030 `0` for its 240×240 panel). Demos are still written against the 280×240 layout (or the 128×64 small-screen branch when `SCREEN_WIDTH <= 128`), so on a 240-wide screen the right 40 columns are cut, and on larger panels like the AD14N's 480×320 they render anchored to the top-left.

Target hardware config headers select the LCD IC (`LCD_IC`) and physical LCD port (`LCD_PORT`); the port supplies `lcd_set_windows`, `lcd_ic_init`, and the flush callbacks under the unified naming `lcd_rgb565_port` / `lcd_rgb888_port` / `lcd_oled_port` / `lcd_gray_port`. `main.c` binds them to the core through a `static const we_port_t` descriptor (`set_addr`, `flush`, optional `touch_read` / `storage_read`, and `flush_async`) passed to `we_gui_init`. `flush` always receives a **pixel count** (RGB888 ports send ×3 bytes internally). `flush_async = 1` declares a flush that returns before the transfer ends (DMA ports with `GRAM_DMA_BUFF_EN=1`; boards derive it from a board-local `LCD_FLUSH_ASYNC`): the kernel then splits the PFB into two halves at init (asserting if a half cannot hold a whole, Y-aligned row set), and the port must wait for the previous transfer at the start of the next `flush`/`set_addr`. Synchronous ports keep the whole PFB.

`Core/we_gui_config.h` supplies defaults for every optional macro and validates required ones (`LCD_DEEP`, `SCREEN_WIDTH`, `SCREEN_HEIGHT`, `WE_CFG_DIRTY_STRATEGY`, timer/input/storage/focus limits) with `#error` checks. When a macro's default matters, read it here rather than trusting the commented-out example values in `we_user_config.h`.

### Core Runtime Model

The runtime centers on one `we_lcd_t` instance (`Core/we_types.h`). That object owns:
- the partial frame buffer (PFB/GRAM), the current draw window `clip`, and the bound port descriptor (`port`),
- the dirty-rectangle manager,
- the root linked list of GUI objects,
- the user timer list (`timer_head`) and the central animation list (`anim_head`),
- input state (`we_indev_data_t`), focus state + semantic key queue,
- the top-layer object list and modal-object slot (`top_list_head` / `modal_obj`),
- the render-statistics frame counter (`stat_render_frames`, FPS data source).

Widgets and demos mutate object state and mark regions dirty. `we_gui_run()` consumes input/keys/timers/animations/dirty state and redraws through the bound port.

### Initialization and Main Loop Pattern

Init binds the port descriptor and the PFB:

```c
void we_gui_init(we_lcd_t *p_lcd, const we_port_t *port, colour_t bg,
                 colour_t *gram, uint16_t gram_pixels);
uint32_t we_gui_run(we_lcd_t *p_lcd, uint32_t now_ms); /* returns ms until the next needed call */
```

`we_gui_run` returns 0 when it should be called again as soon as possible (animation running, dirty area left, keys pending) and `WE_GUI_RUN_IDLE` when nothing is pending (no animation, timer or touch polling) — a power-aware port may sleep for the returned time; the shipped ports simply loop.
All current entry points follow the same flow:
1. initialize system clock/hardware/ports,
2. call `we_gui_init(...)` once,
3. initialize exactly one demo and create its periodic GUI timer,
4. loop calling `we_gui_run(&lcd, now_ms)` with a millisecond time base that starts at 0.

Typical pattern:

```c
static const we_port_t port = {
    .set_addr = lcd_set_windows,
    .flush = lcd_rgb565_port,
    .touch_read = NULL,               /* level report: pressed + x/y */
    .storage_read = w25qxx_read_data, /* NULL if no external flash */
    .flush_async = LCD_FLUSH_ASYNC,
};

lcd_hw_init();
we_gui_init(&lcd, &port, RGB888TODEV(10, 14, 20), gram, USER_GRAM_NUM);
static we_gui_timer_t demo_timer;
we_xxx_simple_demo_init(&lcd);
we_gui_timer_create(&lcd, &demo_timer, we_xxx_simple_demo_tick, 16U, 1U);

uint32_t t0 = g_sys_ms;               /* free-running 32-bit ms counter (SysTick) */
while (1)
    (void)we_gui_run(&lcd, g_sys_ms - t0);
```

The simulator additionally calls `lite_present()` after `we_gui_run()`. Touch is polled inside `we_gui_run()` through `port->touch_read`: the port reports only the level (pressed + coordinates; the kernel pre-fills the last coordinates, so a chip that reports nothing on release needs no special case) and the kernel derives PRESSED/STAY/RELEASED; `WE_CFG_INDEV_PERIOD_MS` throttles sampling, `WE_CFG_REFR_PERIOD_MS` caps the frame rate. Call `we_gui_indev_handler()` directly only when injecting touch edges yourself (script replay). Semantic keys are *not* polled — ports push them (see focus section).

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

**Widget animations do NOT use user timers.** They run on the central animation engine: a `we_anim_t` node embedded in each widget struct, linked into `lcd->anim_head` via `we_anim_start()`/`we_anim_stop()` and stepped by `we_gui_run()` each frame. `we_anim_start` cannot fail, is idempotent (re-linking an already-linked node only refreshes the callback), and the count is unbounded; finished animations unlink themselves (idle cost = one NULL check). Invariant: a `step_cb` may unlink only its own node, and must not delete *other* objects (`we_obj_delete` clears the removed node's `next`, truncating the current frame's traversal). Widgets with animation nodes: toggle, progress, indicator, msgbox, slideshow, scroll_panel, dropdown, line (3 independent nodes), box (only when `WE_BOX_USE_ANIM=1`), gauge, list (2 nodes), roller, marquee, toast.

### Timing, ISR-Safety, and Callback Contract

Rules the kernel relies on that no single function comment owns:

- **The ISR-safe surface is exactly three functions**: `we_gui_key_inject` / `we_gui_key_press` / `we_gui_key_release` (SPSC key ring — the injector writes only `tail`). Every other API — object create/delete/attach, timer and animation start/stop, invalidation, `we_gui_run` — is main-loop only; calling them from an interrupt is a data race even when it happens to work.
- **`we_gui_run(lcd, now_ms)` measures time itself**: elapsed = `now_ms` minus the previous call's `now_ms` (wrap-safe `uint32_t`), clamped to 65535 ms per call. The first call's elapsed is `now_ms` itself, so pass a time base that starts at 0 (`sys_ms - t0`, `t0` captured just before the loop). Boards use a free-running 32-bit SysTick counter (`g_sys_ms`), whose 32-bit read is atomic on Cortex-M — no read-then-clear race. Order inside one call: touch poll (every `WE_CFG_INDEV_PERIOD_MS`) → semantic keys → user timers → animations + render (every `WE_CFG_REFR_PERIOD_MS`; both default 0 = every call).
- **Periodic timers fire at most once per handler cycle**: the callback's `elapsed_ms` is the true time since its last fire (≥ period, capped at 65535); the kernel never drops time. A long stall (flash erase, blocking peripheral wait) arrives as one large elapsed value — the callback's own accumulator logic decides whether to fold or replay the backlog.
- **Kernel recursion depth equals container nesting depth**: draw, hit-test, focus descent, and delete each recurse once per nesting level (demos stay ≤3–4 levels). M0 stack budgeting should assume the deepest container chain the application builds, plus the widget draw path below it.
- **What each callback may do**:
  - *timer cb*: anything — it runs in `we_gui_run` after input dispatch and before animation stepping and rendering; creating/deleting objects is safe here.
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

**Kernel-owned drawing.** `_we_draw_obj` (`Core/we_refresh.c`) draws every object the same way: reject by bbox against the current window, narrow the PFB window to the object's bbox (so a `draw_cb` physically cannot paint outside its bbox — the old latent ghosting class is closed), call `draw_cb`, and for `CHILD_OWNER` classes ask the class hook `content_cb(obj, &area)` for the child area (it may only shrink the bbox; `scroll_panel` returns its inner rect) and the child opacity (0 = subtree skipped for both drawing and hit-testing; the kernel multiplies it into `opa_scale`), recurse into the children, then call `draw_post_cb` in the bbox window (`scroll_panel` scrollbar, `mask_group` mask pass). Container `draw_cb`s draw only their own background. Both hooks are optional (`NULL` = bbox area, opacity 255, no post pass). Per-object narrowing measured at ~0–5% CPU.

**`we_obj_delete()` is the single delete entry point**; the ~25 `we_xxx_obj_delete` functions are thin wrappers. Fixed sequence: `class_p->delete_cb` (widget teardown, `lcd`/`class_p` still valid) → **post-order recursion into `children_head` for `CHILD_OWNER` classes** → invalidate for ghost erase → unlink → **kernel-side reclamation of every reference to the object** → clear fields.

The reclamation step is a mechanism, not a convention — it covers `pressed_obj`, `focus_obj` (walking the ancestor chain), the modal slot if the object is the current modal, and **every `we_anim_t` node whose `owner` is this object** (all `we_anim_start` call sites pass the widget instance pointer, whose address equals the base object's, so multi-node widgets like `line` are cleared in one sweep). Consequence: deleting a container cannot leave zombie animation nodes or orphaned children behind, and a widget author who forgets `we_anim_stop` loses nothing but an explicit teardown. The one thing reclamation cannot see is a **companion object** a widget links separately (dropdown's embedded `overlay` on the top layer): the widget must tear it down in its own `delete_cb`, because container deletion calls `we_obj_delete` directly and never goes through the `we_xxx_obj_delete` wrapper. `delete_cb` must never call `we_obj_delete` on itself (infinite recursion).

**Cross-widget raw bindings self-heal via `class_p`.** `keyboard.target` / `ime_pinyin.target` (→ textarea) and `textarea.editor` (→ popup editor) are raw pointers between independently-owned objects. Each holder resolves them through a small `_xx_target()` / `_ta_editor()` accessor that treats `class_p == NULL` as "the target was deleted", clears the binding, and returns NULL — the same tell `we_gui_indev_handler` uses for a stale `pressed_obj`. Any future cross-object pointer should follow that idiom.

### Widgets

Stable widgets live in `Core/widgets/<name>/we_widget_<name>.c/.h`; preview widgets in `Core/widgets_preview/<name>/`. Current stable set: `label`, `btn`, `img`, `img_ex`, `arc`, `group`, `checkbox`, `label_ex`, `chart`, `toggle`, `progress`, `msgbox`, `img_flash`, `font_flash`, `slideshow`, `slider`, `scroll_panel`, `dropdown`, `stepper`, `indicator`, `line`, `box`, `gauge`, `list`, `roller`, `marquee`, `toast`, `imgbtn`, `segdisp`.

**Every widget directory carries a `widget.md`** (功能 / 适用场景 / 关键 API / 可调宏 / 事件与行为 / 注意事项 / 已完成的毕业优化 / 对应 demo). Read `Core/widgets/<name>/widget.md` before the source when working on one widget — it documents the tuning macros, dirty-marking strategy, and delete contract. All 29 stable and 25 preview widgets have one.

Cross-cutting rules that no single `widget.md` owns:
- `img_ex` and `label_ex` use a **512-step angle unit** (`0..511` = full circle; 90° = 128; 180° = 256) — use `WE_ANGLE(deg)` / `WE_DEG(deg)` — and a **256-step scale unit** (`256` = 1.0×, `128` = 0.5×, `512` = 2.0×).
- For `img_ex`, `cx/cy` are the screen transform center while `pivot_ofs_x/y` are source-image local pivot offsets; do not merge those coordinate systems. `img_ex` accepts **only uncompressed RGB565** source images (the plain `img` widget handles the other formats: ARGB8565 raw, both indexed-QOI variants, the A1/A2/A4/A8 alpha bitmaps, and the indexed-QOI-MASK compressed A8 mask — see Resource Pipeline). **Format dispatch lives in the render layer**, not in the widgets: `we_img_render_auto` / `we_img_format_supported` (`Core/we_image.c`) are shared by `img` and `imgbtn`, so adding a pixel format touches one place. Alpha bitmaps carry no color: `img` blends them with a per-object foreground color (`we_img_obj_set_color`, default white); their rows are byte-aligned MSB-first. The compressed alpha variant `IMG_A8_INDEXQOIMASK` (0x6B) decodes row-by-row through a per-row byte-offset index, so PFB slices only decode visible rows with zero extra RAM (`we_img_render_indexqoi_mask`, trim via `WE_CFG_ENABLE_INDEXQOI_MASK`).
- `img_ex` and `label_ex` share the inverse-mapping geometry in `Core/we_draw.c`: `we_xform_steps` (rounded Q12 steps, pure 32-bit — exactly 4096 at 0°/1.0×), `we_xform_origin` (pixel-centre sampling: 0°/1.0× reproduces the source pixel-for-pixel, 180° is an exact point reflection, and each sample depends only on its own screen coordinate, so PFB slicing and dirty-rect shape never change the result), and `we_xform_bbox` (corners in half-pixel units rounded outward, so no sampled pixel ever falls outside the bbox). Change the sampling convention there, never per widget.
- **Data-driven widgets never copy their data.** `dropdown` (`we_dropdown_option_t[]`), `list`/`roller` (`const char *const *`), `marquee`/`toast` (text pointer) store the caller's pointer; the caller must keep it alive for the widget's lifetime.
- **Popups are ordinary top-layer objects with a modal flag** — the old `popup_layer` slot is gone. `we_obj_attach_to_top` puts an object on the LCD top layer (drawn after the normal list and the focus cursor, never clipped by `group`/`scroll_panel`/`slideshow` parents, hit-tested with priority); `we_modal_open/close/get` declares one of them modal: hit-testing is then restricted to the top layer with misses routed to the modal object ("its hit area is the whole screen" — outside-tap-closes stays widget-side), and semantic keys go straight to its `event_cb` (press edges raw, release edges `key | WE_KEY_RELEASE_FLAG`). Opening a new modal sends `WE_EVENT_MODAL_CLOSE` to the old one (modals are mutually exclusive; non-modal top-layer objects like `toast` coexist freely — a toast can float above the keyboard). Modal users: `dropdown` (embedded `overlay` companion object), `msgbox` (the widget object itself; hit area = its panel, which also bounds draw culling), preview `keyboard`/`ime_pinyin` (the widget object itself); `toast` is a non-modal top-layer object; the preview `menu` draws its pages inside its own rect. `we_obj_bring_to_front` on a top-layer object reorders it within the top layer (the owner-list lookup checks the top list). Touch focus-follow skips top-layer hits (tapping a popup/toast never steals the focus ring).
- **The event space is segmented** (`we_event_t`; one `event_cb` receives every segment): `0x00–0x0F` touch/gesture/kernel queries, `0x10–0x17` semantic keys, `0x20+` focus notifications, `0x40–0x7F` reserved for widget/app custom events (allocate from `WE_EVENT_USER_BASE`), and `0x80` is the release-edge modifier bit — modal objects actually receive `0x90–0x97` for key release edges, so custom codes must stay below `0x80`.
- Opacity propagation uses the lcd-level `opa_scale` multiplier: containers (`group`/`slideshow`/`scroll_panel`) multiply their opacity into it around the children pass, and every drawing primitive consumes it once at entry (`we_opa_apply`, zero cost when no fade is active). A fully transparent container also stops intercepting input.
- **MONO style** (`WE_CFG_MONO_STYLE`, default 0, set 1 for 1bpp page-OLED targets): participating widgets switch default palettes to pure black/white and change draw strategy — solid fills become white-outline+black-fill, pressed/selected become invert+cutout text, semi-transparent decorations (grid lines, scrollbars, trails, press previews) become fully opaque or are dropped — so port-side threshold binarization stays readable. Widget-layer branches only; the render pipeline is untouched. Coverage: 11 stable widgets (btn/toggle/slider/msgbox/progress/dropdown/stepper/list/roller/box/toast) + 20 preview widgets; `colorwheel`/`animimg`/`canvas` have no own palette, `mask_group` derives its defaults from `lcd->bg_color`, and `glowring` is an alpha gradient that binarizes to a hard-edged ring, so those five are intentionally N/A.
- Moving a `group` uses fine-grained dirty marking that **depends on the background being a uniform square fill** (radius fixed at 0): the old/new-frame overlap is pixel-identical after a translation, so `_group_set_pos_cb` marks only the exposure L-strips. **If the group background ever gains rounded corners, gradients, or textures, this must revert to full old+new box marking.**
- `stepper` stores its value as a **fixed-point `int32`**: real value = `value / 10^decimals`, split out only at draw time to avoid Cortex-M0 soft-float cost. Its hold-to-repeat reuses the `STAY` event and consumes no timer slot.
- `progress` takes a direct `0..255` target value; `chart` works in **pixel space** with a circular buffer and has no Y-axis scaling API (pre-scale before pushing).
- Hit-testing skips objects whose class has `event_cb == NULL` — that is the only true pass-through (an `event_cb` that returns 0 is still hit and blocks whatever lies behind it). Pure display widgets use a NULL `event_cb`: `label`, `label_ex`, `img`, `img_ex`, `img_flash`, `font_flash`, `arc`, `chart`, `progress`, `gauge`, `segdisp`, `marquee`, `toast`. `line`, `box` and `indicator` swap between two const classes: decorative (NULL `event_cb`) until a user callback is set (`we_line_set_event_cb` / `we_box_set_event_cb` / `we_indicator_set_event_cb`) or `we_indicator_set_clickable(obj, 1)`.
- `box` is a panel with **per-corner independent styling** (`WE_BOX_LT/RT/LB/RB`, same order as `WE_MASK_QUADRANT_*`): each corner round / chamfered (45°) / square, plus border and fill. Bordered round corners resolve outer+inner coverage in one 4×4 subsampling pass via `we_mask_quarter_ring_alpha`; chamfer borders inset the inner outline by `(2−√2)·bw ≈ 0.586·bw` (not `bw`) so the diagonal keeps the same visual thickness. `WE_BOX_USE_ANIM` defaults to **0**, making `we_box_anim_*` instant-apply stubs.
- `line` is a width-configurable AA segment (round/butt cap) over `we_draw_line` (Xiaolin Wu) plus endpoint AA circles. Geometry, color, and opacity animate on **three independent `we_anim_t` nodes**, so a move, a recolor, and a fade can run at once. `WE_LINE_USE_ANIM 0` strips the animation code and degrades `we_line_anim_*` to instant-apply stubs. Hit-testing is bounding-box granularity.
- `Core/components/we_motion.h` provides easing helpers accepting `t ∈ [0, 256]`, plus `we_motion_advance` (steps a 0..256 progress by elapsed/duration, at least 1 per call) shared by the eased animations of `box`, `gauge`, `indicator` and `line`.

### Dirty Rectangles and PFB/GRAM

`WE_CFG_DIRTY_STRATEGY` in `we_user_config.h` controls redraw strategy:
- `0`: full-screen redraw
- `1`: one merged bounding box
- `2`: multi-rect merge up to `WE_CFG_DIRTY_MAX_NUM`

The current shared config uses strategy `2` with `WE_CFG_DIRTY_MAX_NUM = 10`. Two debug switches sit beside it in `we_user_config.h`: `WE_CFG_DEBUG_DIRTY_RECT` overlays dirty regions in red, and `WE_CFG_DEBUG_PERF_STRESS` force-marks all top-level widgets dirty every frame to stress worst-case redraw throughput (implemented in `Core/we_refresh.c`). Make sure both are `0` before recording demo GIFs or taking performance numbers.

The partial frame buffer covers only a few screen rows. `USER_GRAM_NUM = SCREEN_WIDTH × rows`; increasing rows trades RAM for fewer flushes. The current shared config uses `SCREEN_WIDTH = 240`, `SCREEN_HEIGHT = 240`, and `USER_GRAM_NUM = SCREEN_WIDTH * 2` (960 B, sized for the 8 KB-RAM GD32E230; roomier MCUs can raise the row count — GRAY8 targets get 8 rows forced by the derived-parameter block at the end of the file).

The current draw window is `lcd->clip` (`we_rect_t`, inclusive, screen-absolute) with `pfb_gram` pointing at its top-left pixel and `pfb_width` as the row stride; `we_pfb_clip_save/_to/_restore` narrow it (always go through them — the pointer offset is part of the contract). **Rendering is independent of the slice height**: the same frame hashes identically with 1/2/3/8-row and full-screen PFBs in all three configs. Keep it that way — fixed-point DDA samplers (`img_ex`, `label_ex`, `mask_group` gradient) derive each pixel's value from a fixed anchor (transform center / object origin) plus integer multiples of the truncated step instead of re-deriving an exact start value per slice, and text line-skipping accounts for glyph ink that hangs below the line box (`we_draw_string` widens the test by the font's `max_box_height`; `font_flash` includes `ink_below` in its bbox, since the external CJK font's glyphs reach 2 rows past its 16 px line height).

`WE_LCD_FLUSH_ALIGN_X` / `WE_LCD_FLUSH_ALIGN_Y` (power of two, default 1 = off) force flush-window pixel alignment for hardware that needs it (QSPI panels: x/y multiples of 2/4; SSD1306-class page OLEDs: `ALIGN_Y = 8`). Alignment is applied once at dirty-rect intake in `we_dirty_invalidate` (`Core/we_dirty.c`) — rects are expanded to aligned bounds after screen clamping and before merging, so the expanded edges are fully re-rendered and every `set_addr` window is aligned by construction. Compile-time checks in `Core/we_gui_config.h` require `SCREEN_WIDTH`/`SCREEN_HEIGHT` and the PFB row count (`USER_GRAM_NUM / SCREEN_WIDTH`) to be multiples of the respective alignment.

### Input and Gestures

`we_indev_data_t indev_data` lives inside `we_lcd_t`; do not relocate it unless redesigning the input subsystem.

**Dispatch model** (`we_gui_indev_handler`): an LCD-level popup gets first refusal, then the kernel runs a **recursive hit-test** (`_we_hit_test`; the top layer is tested after the normal list and wins ties, mirroring draw order) that intersects each object's rect with its ancestors' visible rects — so a child scrolled out of its `scroll_panel` viewport is not hittable — and returns the deepest, topmost object. On PRESSED the kernel **bubbles up the parent chain** from that object, and the first `event_cb` returning non-zero becomes `lcd->pressed_obj`; all later events in the sequence go straight there. Because the click is only dispatched when `pressed_obj` still equals the object under the finger at release, the "did the finger drift off the widget" re-check is kernel-side.

**Gesture hand-off.** A child that consumes STAY would starve its scrollable ancestor of drag input, so once movement passes `WE_CFG_DRAG_THRESHOLD` (default 8) the kernel offers `WE_EVENT_DRAG_BEGIN` up the ancestor chain, once per touch sequence. The first container to answer non-zero takes over via `we_indev_grab()`: the previous `pressed_obj` gets one `WE_EVENT_RELEASED` to unwind its pressed visual and, since it is no longer `pressed_obj`, never sees a CLICKED. This is what lets interactive widgets live inside scrollable containers.

**Container contract.** The kernel descends into every `CHILD_OWNER` container unconditionally; containers never forward events to children by hand, and never draw them either (see "Kernel-owned drawing" below). Children are hittable only inside the container's `content_cb` area. A container implements only `WE_EVENT_HIT_TEST` (return 0 to skip this container *and its whole subtree* — used for a fully transparent `group`/`mask_group`, or a point outside `scroll_panel`'s inner rect), `WE_EVENT_DRAG_BEGIN` if it scrolls/pages, and its own gesture handling; everything else returns 0 so unconsumed presses bubble to outer containers.

**Focus-cursor ghost prevention is kernel-side**: `we_obj_set_pos` (move) and `we_obj_set_size` (resize, top-left anchored) invalidate the focus ring before and after changing `lcd->focus_obj`'s geometry (the ring hangs GAP+THICKNESS outside the widget bbox, which the widget's own old/new marking cannot cover). Code that moves or resizes a focused widget — container relayout, scroll-follow, `imgbtn` swapping to a different-sized image — needs no manual ring marking; widgets must never hand-roll the ring geometry.

Swipe detection is also in `we_gui_indev_handler()`: on release, movement from press exceeding `WE_CFG_SWIPE_THRESHOLD` (default 30) dispatches `WE_EVENT_SWIPE_LEFT/RIGHT/UP/DOWN` instead of a click. `lcd->gesture_had_stay` records whether the sequence ever hit STAY, which scrollable widgets use to tell a drag from a fast flick; `lcd->gesture_drag_done` marks the drag hand-off offer as already made.

**Input-side trimming macros** (both default 1 in `Core/we_gui_config.h`): `WE_CFG_ENABLE_NESTED_INPUT 0` removes hit-test descent into containers and the drag hand-off ask — for flat UIs that put no interactive children inside scrollable containers. `WE_CFG_ENABLE_TOP_LAYER 0` removes the top-layer list and modal machinery entirely (`we_lcd_t` loses both fields, `we_obj_attach_to_top` degrades to appending the object at the normal-list tail — never-linked popup objects such as the dropdown overlay and the keyboard/ime popups get linked too — and the `we_modal_*` trio become no-op stubs) — msgbox/toast/dropdown/keyboard still compile and display, minus the always-on-top and modal-swallow guarantees (msgbox falls back to a full-screen hit area: blocks touch, not keys).

### Focus and Key Navigation

Added in V0.2.2 and **not covered by `WEGUI_API_REFERENCE.md`** — this section plus the comments in `Core/we_focus.h` / `Core/we_types.h` are the reference. Gated by `WE_CFG_ENABLE_KEY_INPUT` (`1` in the shared user config; the core default in `we_gui_config.h` is `0`, so a bare port compiles the whole subsystem out at zero cost). Touch and keys coexist — enabling keys changes nothing about touch handling. **The cursor ring is input-source gated** (`WE_FOCUS_F_CURSOR_VIS`): any key activity or a programmatic `we_focus_set`/`we_focus_edit_enter` shows it; a touch press hides it — touch still moves focus underneath (the finger is the cursor), so a later key press resumes navigation from the last-tapped widget with the ring back on. Demos `29` (`demo_focus.c`) and `30` (`demo_focus2.c`) exercise it; the simulator maps arrows / Tab / Shift+Tab / Enter / Space / Esc / Backspace in `SimLite/fenster_port.c` (direction/Tab auto-repeat is port-side, OK is dual-edge).

**Ports inject semantic keys; they never poll.** The port debounces physical buttons (independent keys, 5-way joystick, EC11 encoder…) and translates them into `WE_KEY_UP/DOWN/LEFT/RIGHT/PREV/NEXT/OK/BACK`:
- Dual-edge ports call `we_gui_key_press(lcd, key)` / `we_gui_key_release(lcd, key)`. Only OK consumes the release edge (widgets stay visually pressed while OK is held); other keys may skip release reporting. Direction-key auto-repeat is the port's job — re-inject the press edge.
- Simple ports call `we_gui_key_inject(lcd, key)` for a complete tap. Both edges are consumed in the same cycle, so the OK pressed visual does not persist — use dual-edge reporting for held-press feel.
- Injection is ISR-safe: keys land in a **volatile SPSC ring buffer** (`key_queue`, `WE_CFG_KEY_QUEUE_LEN` — power of two ≥ 4 (a tap injects two edges), core default 8, usable capacity = len−1). The injector writes only `tail` and the OK level byte (`key_ok_down`: press sets it, release clears it), the consumer only `head`. Release edges are encoded as `key | WE_KEY_RELEASE_FLAG`. A full queue drops the incoming code — one lost input, no state. The OK release edge alone must never be lost (a stuck `OK_HELD` would keep the widget pressed and dedup would swallow all later OK presses), so after draining the queue each cycle the kernel delivers the release effect whenever a widget is still held while `key_ok_down` reads 0 — the queue entry is the ordered messenger, the level byte is the truth, and a dropped OK release can never leave the widget stuck.

**Kernel → widget notifications travel on the same callback and must never be injected by a port**: `WE_KEY_EVT_FOCUS` (focus query — returning 0 refuses focus for this instance, e.g. a disabled control), `WE_KEY_EVT_DEFOCUS`, `WE_KEY_EVT_FLASH_END` (press cancelled — bounce back without clicking), `WE_KEY_EVT_OK_RELEASE` (bounce back *and* fire the action), `WE_KEY_EVT_CHILD_FOCUS` (sent up the ancestor chain so e.g. `scroll_panel` scrolls the focused child into view).

**Focusability is structural**: a class with `WE_CLASS_FLAG_FOCUSABLE` in `class_flags` is focusable and receives the semantic-key (0x10) and focus-notification (0x20) event segments through its ordinary `event_cb`. All class descriptors use designated initializers, so classes that do not set the flag are non-focusable. `WE_CLASS_FLAG_FOCUS_ENTER` marks containers focus can descend into. An `event_cb` returning non-zero for a key means "consumed"; anything unconsumed falls through to the focus manager's default navigation (direction keys move by bounding-box-center proximity, PREV/NEXT walk a linear ring, OK enters a container or triggers a widget, BACK exits a container and clears focus at top level).

Public API: `we_focus_set` / `we_focus_get` / `we_focus_candidate` (structural test used by containers to refuse focus when their subtree is empty), plus `we_focus_edit_enter/exit/active` for the **edit state** that value widgets (slider, stepper, roller, list) enter from their OK branch so direction keys adjust the value instead of moving focus. Kernel state lives in `lcd->focus_obj` / `focus_flags` (`WE_FOCUS_F_EDIT/OK_HELD/OK_ARMED/CURSOR_VIS`) — read-only for ports and applications. `we_obj_delete` walks the ancestor chain to avoid dangling focus.

**Modal key channel**: while a modal object is registered (`we_modal_open`), semantic keys bypass the focus manager and go straight to that object's `event_cb` — press edges as the raw key value, release edges as `key | WE_KEY_RELEASE_FLAG` (`0x90..0x97` on the wire). Unconsumed keys do not fall through (modal swallows all keys). `dropdown`, `msgbox` and the preview `keyboard` / `ime_pinyin` rely on this; there is no separate hook to install since the unified `event_cb` receives everything. Popups that take OK dual-edge must ignore a release edge with no matching press seen by the popup itself — a popup opened by a background widget's OK release receives that same release edge right after opening.

**Size trimming** (all default on): `WE_CFG_FOCUS_EDIT 0` removes the edit state entirely (only click-like semantics remain); `WE_CFG_FOCUS_NESTED 0` flattens the focus ring to top-level widgets; and each widget has its own `WE_<NAME>_USE_KEY` macro (defined in that widget's header) to drop its key callback and focusability individually. Stable widgets with key support: `btn`, `checkbox`, `toggle`, `indicator`, `slider`, `stepper`, `roller`, `list`, `scroll_panel`, `dropdown`, `imgbtn`, plus `msgbox` (modal key channel only — not focusable). Preview: `calendar`, `logview`, `menu`, `radio`, `table`, `textarea`. Cursor appearance is tunable via `WE_CFG_FOCUS_CURSOR_THICKNESS/GAP/R/G/B`.

### Resource Pipeline (`tool/`) and Image Format

`tool/` is a numbered, example-driven pipeline: each stage is a self-contained tool whose example inputs/outputs double as the demo asset set, so a user can reproduce the whole flow by double-clicking the stage `.bat`s (or `tool/0.1.2.3.update_all.bat` for the 1→2→3 chain). Stage notes live in the user-written numbered `.txt` files.

- `1.font2c/` — fonts. `1.manage_input.bat` is an interactive wizard that generates `input/*.json` configs; `2.build.bat` runs font2c over all of them into `output/`. Current demo fonts: `simli_16_2bpp` (internal ASCII — the `we_font_consolas_18` alias target used by every demo), `msyh_16_4bpp_ime` (internal, ASCII + GB2312 level-1 charset for the `ime_pinyin` preview), `gbsn00lp_2_16_4bpp` (external ASCII+CJK: index `.c/.h` compiled into the MCU, glyph-data `.bin` merged into external flash; demos 0/16). Font files resolve against `fonts/` then the system font directories (`SIMLI.TTF`/`msyh.ttc` come from Windows, not the repo).
- `2.img2c/` — images. `img2c_rgb565.bat` (matches this repo’s RGB565 targets; an rgb888 variant exists for other targets). `input/` buckets encode pixel format × compression × destination: `*_2c` buckets are merged into the internal-array `output/c/res_img.c/.h`, `*_2bin` buckets become per-image bins in `output/bin/`. Demo images: `demo_rgb565_raw_be_64x80` (uncompressed RGB565 — the only format `img_ex` accepts; also used by imgbtn/showcase), `demo_rgb565_indexqoi_be_128x64`, `demo_argb8565_indexqoi_be_80x80`, `demo_argb8565_raw_be_80x80`. The alpha buckets hold six 48×48 icons split across `A8_indexqoimask_2c/2bin` (indexed-QOI-MASK compressed, the recommended alpha format, encoded by `img2bin_indexqoimask` at 6-bit quantization) and `A8_raw_2c/2bin` (kept for future rotated-image support); the A1/A2/A4 raw buckets were removed 2026-08 (core decoders remain). Consumed by demo 31 (`demo_img_alpha.c`, raw-vs-compressed comparison) and demo 32 (imgbtn tint/swap icons).
- `3.bin2c/` — external-flash merge. `build_bin.bat` concatenates `1.font2c/output/*.bin` + `2.img2c/output/bin/*.bin` into `output/merged_bin.bin/.c/.h` (`.c` holds only the `bin_addr_table[]`; `.h` has the ID enum plus SIZE/ADDR macros) and additionally an **embed-data** variant under `output/embed/` that is compiled ONLY by the burn project (not committed — a ~17 MB generated C source; run `build_bin.bat` once before burning).
- `4.ex_flash_download/STM32F103/` — Keil burn project (AC5): compiles `3.bin2c/output/embed/merged_bin.c`, links it through `flash_download.sct` (pins `g_merged_bin_data` to address 0 with `:gdef:` + `+FIRST`) and writes it to the W25Qxx through the debugger using the custom `WE_STM32F103_W25Q128.FLM` algorithm. The algorithm project `Flash_FLM/NewDevice.uvprojx` is self-contained — its own register definitions (names and values match `stm32f10x.h`), no StdPeriph library — so it builds from this folder alone; `FLASH_SPI_SOFT` in `stm32f103_flash_spi_port.h` switches between hardware SPI2 (PB13/14/15, CS PB12) and GPIO bit-bang for other pins. It runs the chip from the HSI PLL at 64 MHz (SPI 16 MHz, no crystal needed), programs 2048-byte pages inside the default `0x20000000 / 0x1000` algorithm RAM, and takes its stack size from `FlashStk.s`. `算法文件(拷贝到keil安装目录)/V00B/` holds the current build plus the Flash Download settings in `帮助.txt`; `V00A/` is the earlier StdPeriph build (needs `0x4000` of algorithm RAM). **Changing an external asset means re-burning** — a firmware rebuild alone will not update external flash.
- `4.ex_flash_download/GD32E230/` — the same burn flow for the GD32E230 board (Keil AC6, Cortex-M23); local-only like the board's target folder (the whole folder is gitignored). `flash_download.uvprojx` links `merged_bin.c` through `flash_download.sct`, which pins `g_merged_bin_data` to address 0 with `+FIRST` (armlink orders AC6 read-only sections by name and would otherwise place `bin_addr_table` first, shifting every asset). The algorithm `Flash_FLM/WE_GD32E230_GD25Q128.FLM` drives the GD25Q128 on SPI1 at 18 MHz (the datasheet's SPI limit) from its own register definitions (no firmware library linked; names match the vendor headers), brings the clock to 72 MHz itself when the chip is in its reset state, and programs 2048-byte pages so the default `0x20000000 / 0x1000` algorithm RAM fits the 8 KB chip. `STACK_SIZE` is defined from C with a file-scope `__asm` because uVision's AC6 armasm call lacks `--apcs=/ropi/rwpi`. The FLM has to be copied into `<Keil>\ARM\Flash` once; `flash_download.uvoptx` (debugger + algorithm selection) falls under the `*.uvoptx` ignore rule, so `算法文件(拷贝到keil安装目录)/V00A/帮助.txt` lists the Flash Download settings to enter by hand.
- Consumers: all three targets add `tool/1.font2c/output`, `tool/2.img2c/output/c`, `tool/3.bin2c/output` to their include paths and compile the three font `.c`, `res_img.c`, and the table-only `merged_bin.c`. Demos include `res_img.h` (internal arrays) or `merged_bin.h` (external-flash ID/address table).
- **The simulator embeds no external-flash data**: its storage port opens `merged_bin.bin` next to the exe (`build_lite.ps1`/`.sh` copy it post-build) and serves reads via `fseek`/`fread`; a missing file reads as `0xFF` (erased flash). Changing external assets = rerun `3.bin2c` + copy/restart, no rebuild — mirroring the hardware “re-burn” flow.
- Batch files are saved as **GBK + CRLF** (cmd’s native encoding; UTF-8 or LF-only bats mis-parse Chinese comments into broken commands). The numbered stage-note `.txt` files are **UTF-8** — do not read/write them as GBK (PowerShell 5.1 console round-trips can make a UTF-8 file *look* GBK-decodable; verify with a strict decode first). `.ps1` scripts with Chinese comments need a **UTF-8 BOM** (PowerShell 5.1 parses BOM-less files as ANSI — mojibake comment bytes can contain quote characters that break parsing); note that plain-text file edits may strip the BOM, so re-check/re-add it after editing any `.ps1`.

Image resource format v2 (`Core/image_res.h`) is the contract between the tools and the core: a 6-byte header `[res_type][format][width_hi][width_lo][height_hi][height_lo]` (dimensions always big-endian) followed by pixel data, parsed with the `IMG_DAT_*` macros. The format byte splits into a compression nibble `[7:4]` (`0x0` none, `0x1/0x2` RLE, `0x3` plain QOI, `0x4` indexed QOI — **V3 stream only**: 14-byte index header (byte0=0x0F) + u16/u32 per-row jump tables (prefix split by row number) + row dedup (identical rows share one offset) + static palette (≤64 full-pixel entries, op `0x00..0x3F`); row-self-contained (row head op is palette/0xFF only, RUN never crosses rows), V2 (0x0E) and V1 (0x0D) streams are rejected by the decoders, `0x5` QOIF — tool-reserved, no core decoder, `0x6` indexed QOI_MASK — A8-only: per-row byte-offset index (u16/u32 split tables, row dedup) + static dictionary + INDEX/DIFF/DELTA/RUN/ALPHA tag stream with optional 8/7/6/5-bit quantization, spec in `tool/0.tool/windows/img2bin_indexqoimask/README`) and a pixel-format nibble `[3:0]` (`0x0` RGB565 … `0x8` ARGB8565, `0xB`/`0xC`/`0xD`/`0xE` = A8/A4/A2/A1 alpha bitmaps, `0xF` OLED bitmap; `0x9/0xA` tool-reserved). `WE_CFG_ENABLE_INDEXED_QOI` / `WE_CFG_ENABLE_INDEXQOI_MASK` in `we_user_config.h` trim the indexed-QOI / indexed-QOI-MASK decoders and their dispatch paths out of the image widgets.

## Demo Style

Each demo follows this pattern:

```c
void we_xxx_simple_demo_init(we_lcd_t *lcd);
void we_xxx_simple_demo_tick(we_lcd_t *lcd, uint16_t ms_tick);
```

One demo should be a small, copyable example: static variables plus one init function plus one tick function, written against the fixed 280×240 layout (each demo also carries a 128×64 small-screen branch under `WE_DEMO_SMALL_SCREEN`). Demos are also the primary integration tests for widgets, timers, input, storage-backed assets, and rendering behavior. A demo whose feature is compile-gated (e.g. the focus demos under `WE_CFG_ENABLE_KEY_INPUT`) provides empty `#else` stubs so every target still links.

Demo selection is a **compile-time `#define DEMO_ID`** + `#if/#elif` chain in each entry's `main` (no runtime `switch`); only the selected demo's `init`/`tick` is compiled in. Numbering is unified across all four targets (SimLite / STM32F103 / STM32F030 / AD14N) — `1..33` and the preview range `101..127` are identical: `1` label, `2` btn, `3` img, `4` img_ex, `5` arc, `6` group, `7` slideshow, `8` concentric arc, `9` checkbox, `10` label_ex, `11` chart, `12` toggle, `13` progress, `14` msgbox, `15` flash img, `16` flash font, `17` slider, `18` scroll_panel, `19` dropdown, `20` stepper, `21` indicator, `22` line, `23` box, `24` gauge, `25` list, `26` roller, `27` marquee, `28` toast, `29` focus, `30` focus2, `31` img_alpha, `32` imgbtn, `33` segdisp. Only `0 = showcase` remains simulator-only (needs 800×480, guarded by a nested `#warning`; on hardware targets an ID of 0 falls through to the fallback). The `#else` fallback is `label` on all targets. Switch demos by editing the single `#define DEMO_ID` line near the top of `main` (on AD14N it sits inside `app` in `AD14N/main.c`; on SimLite `build_lite.ps1 -Demo N` can override it without editing).

When adding a demo, update the `DEMO_ID` comment block + `#if/#elif` chain in all four entry files, declare its `init`/`tick` in `Demo/simple_widget_demos.h`, add one line to `Demo/demo_manifest.txt` (all four build scripts read it; rerun `AD14N/gen_cbp.ps1` to refresh the CodeBlocks project), register it in `SimLite/debug/sim_demo_registry.c` (dev exe / autotest), and add it to each Keil `.uvprojx` by hand.

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
4. `Core/wegui.h` → `Core/we_types.h` (runtime object, class descriptor, port descriptor, event/key enums) and the per-module headers (`we_core.h`, `we_obj.h`, `we_tick.h`, `we_input.h`, `we_focus.h`, `we_draw.h`, `we_image.h`, `we_text.h`) — the public API surface.
5. `Core/widgets/<name>/widget.md` — per-widget documentation; the first stop for any single-widget task.
6. `SimLite/main_lite.c` — simulator entry and demo selection.
7. `STM32F103/main.c` / `STM32F030/main.c` — hardware entries and demo selection.
8. `Demo/simple_widget_demos.h` — demo entry declarations.
9. `tool/` numbered `.txt` notes — per-stage tool usage (fonts / images / merge / burn).
10. `WEGUI_API_REFERENCE.md` — full API reference and usage examples (predates the focus subsystem).
