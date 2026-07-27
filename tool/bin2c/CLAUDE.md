# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

`bin2c` is a small C99 command-line tool that merges all `.bin` files from one or more input directories (`--input` is repeatable; entries are blocked in the order directories were given, sorted case-insensitively by file name within each block) into three outputs: a concatenated `.bin`, a `.c` file containing the data as an `unsigned char` array, and a `.h` file with a resource ID enum, per-file `_SIZE`/`_ADDR` macros, an address table, and a `MERGED_BIN_SIZE` total. It targets embedded/MCU resource packing workflows.

The entire program is one source file: `builder/tool_src/bin2c.c`. There are no external dependencies, no tests, and no linter.

## Build

Windows (requires MinGW + CMake on PATH; uses "MinGW Makefiles" generator):

```cmd
builder\build_win.cmd
```

macOS/Unix:

```sh
sh builder/build_mac.sh
```

Both scripts run CMake from `builder/`, build into `builder/build/<platform>/`, and copy the resulting executable to the repo root (`bin2c.exe` / `bin2c`). The root executable and `builder/build/` are gitignored; releases are produced by `.github/workflows/release.yml` on `v*` tags (MSYS2/MinGW for Windows, plain CMake for macOS).

To test manually, drop `.bin` files into `input_bin/` and run the built executable from the repo root; results appear in `output/`. Both directories are auto-created and their contents are gitignored.

## Architecture of bin2c.c

Pipeline in `main()`: parse args (`--input` may repeat; dirs collected into `input_dirs`) → `ensure_directory()` for output and each input dir → `collect_bin_files()` per dir (names only, tagged with `dir`/`dir_index`) → `qsort` with `compare_entries` (dir_index first, then ASCII case-insensitive file name) → assign symbols in sorted order → compute per-file sizes and running offsets → `write_header()` → `write_source()` → `write_binary()`. Input files are re-opened and streamed for each output rather than buffered in memory, so files are read up to three times.

Key points to preserve when editing:

- **Platform split**: directory scanning has two implementations selected by `#ifdef _WIN32` — `FindFirstFileA` (Windows, pattern `*.bin` plus an `ends_with_bin()` check to defeat 8.3 short-name matches) vs `opendir`/`readdir` + `ends_with_bin()` (POSIX). Any change to file discovery must be made in both branches.
- **Ownership in `BinEntry`**: `file_name` and `symbol` are heap-owned and freed by `free_entries()`; `dir` is a borrowed pointer into `input_dirs` (argv strings or a literal) and must never be freed.
- **Symbol generation**: symbols are assigned in `main()` *after* sorting (not during collection) so that duplicate base names get their `_2`, `_3`… suffixes in final merge order — first occurrence keeps the clean name. `symbol_exists()` must tolerate NULL symbols because entries are unassigned during that loop. `make_symbol_base()` uppercases the file name minus extension, collapses non-alphanumerics to `_`, prefixes `_` if it starts with a digit, and falls back to `BIN` if empty. Header macros (`XXX_ID`, `XXX_SIZE`, `XXX_ADDR`) and the `.c` array contents must stay in the same sorted order — the address table is index-matched to the enum.
- **Error handling convention**: functions return 0/1 and write a message into a caller-provided `errbuf`; `main()` uses `goto cleanup` with a single `ok` flag. Follow this pattern rather than exiting mid-function.
- **CLI flags** are parsed in a simple loop in `main()`; several flags are aliases (`--output-dir`/`--output-path`, `--output-base`/`--output-name`). When adding or changing flags, update `print_usage()` and **both** `README.md` and `README_CN.md` (they document the same content; README.md is also largely in Chinese).
