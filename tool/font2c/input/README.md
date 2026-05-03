# input samples

This directory stores JSON configs for `font2c`.
This document only describes JSON usage.

General executable usage is in `../README.md`.
Build instructions are in `../builder/README.md`.

## JSON File Encoding

- use `UTF-8` without BOM
- use `LF` line endings
- `charset.chars` stores literal Unicode characters
- range strings use `U+` plus `4` to `6` hexadecimal digits

## JSON Shape

One JSON file describes one export task.

Minimal example:

```json
{
  "version": 1,
  "symbol": "your_font_symbol",
  "font": {
    "file": "your_font.ttf",
    "size": 16
  },
  "render": {
    "bpp": 2
  },
  "charset": {
    "ranges": [
      ["U+0020", "U+007E"]
    ],
    "chars": "菜单设置"
  },
  "deploy": {
    "mode": "internal"
  }
}
```

## Field Rules

- `version`: currently fixed to `1`
- `symbol`: required C symbol prefix, must be a valid identifier
- `font.file`: font filename or relative path
- `font.size`: pixel size
- `font.face_index`: optional, default `0`, mainly for `.ttc`
- `render.bpp`: must be `1`, `2`, `4`, or `8`
- `deploy.mode`: must be `internal` or `external`

## Charset Rules

- `ranges` is optional
- `chars` is optional
- at least one of them must exist

Range format:

```json
"ranges": [
  ["U+0020", "U+007E"],
  ["U+3000", "U+303F"]
]
```

Direct chars format:

```json
"chars": "菜单设置返回确定"
```

Mixed format:

```json
"charset": {
  "ranges": [
    ["U+0020", "U+007E"],
    ["U+3000", "U+303F"]
  ],
  "chars": "菜单设置返回确定"
}
```

## Path Rules

- `font.file` is resolved relative to the JSON file first
- if not found, it falls back to the project `fonts/` directory
- if still not found, the tool searches installed system font directories
- output basename always comes from the JSON filename
- C symbol names always come from `symbol`

## Included Samples

- `arial_16_4bbp.json`
- `msyh_16_2bbp.json`
- `msyh_16_2bbp_ascii_menu_mix.json`
- `msyh_16_2bbp_ascii_punct_menu_mix.json`
- `msyh_24_2bbp_ascii_menu_mix.json`

## Note

Do not assume proprietary system fonts are safe to redistribute.
