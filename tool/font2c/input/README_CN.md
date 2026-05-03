# input 示例说明

这个目录用于存放 `font2c` 的 JSON 配置。
这份文档只描述 JSON 的写法和使用方式。

exe 的一般使用方法见 `../README_CN.md`。
构建说明见 `../builder/README_CN.md`。

## JSON 文件编码

- 使用 `UTF-8` 无 BOM
- 换行使用 `LF`
- `charset.chars` 直接保存 Unicode 原字符
- 区间字符串使用 `U+` 加 `4` 到 `6` 位十六进制数字

## JSON 结构

一个 JSON 文件表示一次导出任务。

最小示例：

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

## 字段规则

- `version`：当前固定为 `1`
- `symbol`：必填，作为导出的 C 符号前缀，必须是合法标识符
- `font.file`：字体文件名或相对路径
- `font.size`：像素字号
- `font.face_index`：可选，默认 `0`，主要给 `.ttc` 使用
- `render.bpp`：只能是 `1`、`2`、`4`、`8`
- `deploy.mode`：只能是 `internal` 或 `external`

## 字符集规则

- `ranges` 可选
- `chars` 可选
- 但两者至少要有一个

区间写法：

```json
"ranges": [
  ["U+0020", "U+007E"],
  ["U+3000", "U+303F"]
]
```

直接字符写法：

```json
"chars": "菜单设置返回确定"
```

混合写法：

```json
"charset": {
  "ranges": [
    ["U+0020", "U+007E"],
    ["U+3000", "U+303F"]
  ],
  "chars": "菜单设置返回确定"
}
```

## 路径规则

- `font.file` 先相对当前 JSON 文件解析
- 如果找不到，再回退到项目 `fonts/` 目录
- 如果仍然找不到，再搜索系统已安装字体目录
- 输出文件基名固定来自 JSON 文件名
- 导出的 C 符号名固定来自 `symbol`

## 当前示例

- `arial_16_4bbp.json`
- `msyh_16_2bbp.json`
- `msyh_16_2bbp_ascii_menu_mix.json`
- `msyh_16_2bbp_ascii_punct_menu_mix.json`
- `msyh_24_2bbp_ascii_menu_mix.json`

## 说明

不要默认认为专有系统字体可以随项目公开分发。
