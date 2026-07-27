# 接口 Schema 说明

本页逐字段说明所有机器可读输出，面向 GUI、自动化脚本和程序集成。共五类：

1. 取模工具的 `--info` JSON
2. 统筹管理器 `img2bin_pack.exe` 的 `--info` JSON
3. 取模工具的批处理清单 `img2bin_<工具>-manifest.json`
4. 统筹管理器的运行清单 `img2bin_pack-manifest.json`
5. 错误 JSON

通道约定：

- `--info` 输出到 stdout，manifest 写入输出目录，均为 UTF-8 JSON
- 错误 JSON 输出到 **stderr**，每条一行；批处理中每张失败图片各输出一行（NDJSON）
- 版本字段随 `version.h` 演进；`schema_version` 当前为 `1.1.0`

二进制侧的机器接口（6 字节通用资源头、算法/像素格式 nibble 编码表）见
[协议与验证说明](README-protocol.md)的"通用资源头"一节。

## 一、取模工具 `--info`

顶层结构：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `schema_version` | string | info JSON 的 schema 版本 |
| `tool.id` | string | 工具唯一 id，如 `img2bin_raw` |
| `tool.kind` | string | 固定 `image_converter` |
| `tool.version` / `tool.version_semver` | string | 如 `V0.0.1` / `0.0.1` |
| `gui.display_name` / `gui.description` / `gui.gui_category` | object | 均为 `{zh_cn, en}` 双语文本 |
| `gui.priority` | number | GUI 排序权重，越小越靠前 |
| `algorithm.id` / `algorithm.algorithm_code` | string | 算法标识；`algorithm_code` 同时是 `input2<code>` 文件夹后缀与输出文件名中的算法段 |
| `algorithm.compression` | string | 压缩类别，如 `none` |
| `algorithm.supports_multi_format` | bool | 是否支持一次输出多种像素格式 |
| `defaults.format` | string | 默认像素格式 `rgb565` |
| `defaults.endianness` | string | `big` 或 `little` |
| `defaults.input_dir` / `defaults.output_dir` | string | `exe_dir/input`、`exe_dir/output` |
| `defaults.background_color` | string | `RRGGBB`，默认 `000000` |
| `defaults.index_interval` | string | 仅索引类工具存在，值 `image_width` |
| `capabilities.*` | bool/array/string | 能力开关，字段名自描述；`supports_index_interval` 是 pack 判断是否传 `--index-interval` 的依据 |
| `invocation.style` | string | 固定 `flag_cli` |
| `invocation.info_flag` / `invocation.help_flag` | string | `--info` / `--help` |
| `invocation.arguments[]` | array | 参数元数据，见下表 |
| `output.extension` | string | 固定 `bin` |
| `output.filename_pattern` | string | `{source_stem}_{format_name}_<算法>_{endianness_token}_{width}x{height}.bin` |
| `output.endianness_tokens` | object | `{"big":"be","little":"le"}` |
| `output.resource_header.size` | number | 固定 6 |
| `output.resource_header.resource_type` | number | 固定 0（图片） |
| `output.resource_header.algorithm_nibble` | number | 本工具的算法 nibble（写入格式码高 4 位） |
| `output.resource_header.layout` | string | `type:1,algo_format:1,width_be:2,height_be:2` |
| `exit_codes` | object | 见"退出码"一节 |
| `pixel_formats[]` | array | 支持的像素格式，见下表 |

`invocation.arguments[]` 每项：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `id` | string | 参数标识（如 `format`） |
| `flag` | string | 命令行旗标（如 `--format`） |
| `value_type` | string | 值类型：`path` / `pixel_format_name` / `csv_or_keyword` / `boolean_flag` / `hex_rgb` / `positive_integer` |
| `takes_value` | bool | 是否带值 |
| `required` | bool | 是否必填（当前全部为 false） |
| `conflicts_with` | array | 互斥参数 id 列表（如 `format` 与 `formats`） |
| `default` | any/null | 默认值 |
| `display_name` | object | `{zh_cn, en}` |
| `accepts` | array | `path` 类参数接受的对象种类（`file`/`directory`） |
| `special_values` | array | 特殊关键字（如 `formats` 的 `all`） |
| `element_type` | string/null | 列表值的元素类型 |
| `value_delimiter` | string/null | 列表值分隔符（如 `,`） |

`pixel_formats[]` 每项：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `name` | string | 格式名（小写，用于 `--format` 与输出文件名） |
| `display_name` | object | `{zh_cn, en}` |
| `bytes_per_pixel` | number | 每像素字节数 |
| `stores_alpha` | bool | 是否存储 Alpha |
| `uses_background_color` | bool | 是否参与背景色混合 |
| `endianness_affects_output` | bool | 大小端是否改变输出字节 |
| `header_nibble` | number | 该格式在通用资源头格式码低 4 位中的取值 |

### 退出码（取模工具）

| 值 | 键 | 含义 |
| --- | --- | --- |
| 0 | `success` | 全部成功 |
| 1 | `cli_error` | 命令行参数错误 |
| 2 | `input_error` | 输入不存在/无法读取 |
| 3 | `encode_error` | 编码失败 |
| 4 | `write_error` | 写输出失败 |
| 5 | `internal_error` | 内部错误 |
| 6 | `batch_partial_failure` | 批处理部分失败 |

## 二、pack `--info`

与工具 `--info` 同一信封结构，差异：

| 字段 | 说明 |
| --- | --- |
| `tool.id` | `img2bin_pack` |
| `tool.kind` | `batch_orchestrator` |
| （无 `algorithm` / `output` / `pixel_formats`） | pack 不做编码 |
| `capabilities.folder_convention` | `input2<algorithm_code>` |
| `capabilities.config_file` | `img2bin_pack.json` |
| `capabilities.discovers_tools_via` | `--info` |
| `capabilities.codegen_modes` | `["combined","split"]` |
| `capabilities.codegen_outputs` | `["c","h"]` |
| `capabilities.manifest_file` | `img2bin_pack-manifest.json` |
| `invocation.arguments[]` | 简化条目：仅 `name`/`flag`/`type` |
| `exit_codes` | 0 `success`、1 `cli_error`、2 `input_error`、5 `internal_error`、6 `batch_partial_failure` |

## 三、取模工具 manifest（`img2bin_<工具>-manifest.json`）

目录批处理时写入输出目录。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `tool.id` / `tool.version` | string | 生成者 |
| `run.output_directory` | string | 本次输出目录 |
| `run.endianness` | string | `big` / `little` |
| `run.requested_formats[]` | array | 本次请求的格式名列表 |
| `summary.source_images_total` | number | 输入图片总数 |
| `summary.source_images_succeeded` / `source_images_failed` | number | 成功/失败张数 |
| `summary.generated_bin_files_total` | number | 生成的 bin 总数（多格式时 > 图片数） |
| `items[]` | array | 每张输入图片一项 |

`items[]` 成功项：

```json
{
  "source_path": "…",
  "status": "success",
  "width": 128, "height": 64,
  "outputs": [ { "format": "rgb565", "path": "…", "bytes": 16384 } ]
}
```

`items[]` 失败项：

```json
{
  "source_path": "…",
  "status": "error",
  "error": {
    "code": "image_load_failed",
    "stage": "load",
    "exit_code": 2,
    "message": { "zh_cn": "…", "en": "…" },
    "detail": "…"
  }
}
```

## 四、pack manifest（`img2bin_pack-manifest.json`）

写入默认输出目录。

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `tool.id` / `tool.version` | string | `img2bin_pack` |
| `run.root` | string | 工作目录（绝对路径） |
| `run.output_directory` / `run.tools_directory` | string | 输出目录 / 工具目录 |
| `run.discovered_tools[]` | array | 每项 `{tool_id, algorithm_code, supports_index_interval}` |
| `summary.folders_total` | number | 参与的文件夹数 |
| `summary.folders_succeeded` / `folders_partial` / `folders_failed` / `folders_skipped` | number | 各状态计数 |
| `summary.collected_bin_files_total` | number | 从各工具 manifest 汇总的 bin 数 |
| `folders[]` | array | 每个文件夹一项，见下 |
| `codegen.enabled` | bool | 是否生成 `.c/.h` |
| `codegen.mode` | string | `combined` / `split` |
| `codegen.generated_files[]` | array | 生成文件的绝对路径 |

`folders[]` 每项：

| 字段 | 说明 |
| --- | --- |
| `folder` | 文件夹名 |
| `input_directory` / `output_directory` | 绝对路径 |
| `tool_id` / `algorithm_code` | 分派到的工具（未匹配时 `tool_id` 为空） |
| `status` | `success` / `partial` / `error` / `no_tool` / `skipped_empty` |
| `exit_code` | 子工具退出码；未执行为 -1 |
| `images_found` | 该文件夹中的图片数 |
| `outputs[]` | 该文件夹产生的 bin 绝对路径（取自子工具 manifest） |
| `detail` | 失败时的补充信息（含子工具输出尾部） |

## 五、错误 JSON

所有程序的致命错误都在 **stderr** 输出单行 JSON；批处理里每张失败图片各一行（NDJSON），可逐行解析：

```json
{"error":{"code":"input_path_invalid","exit_code":2,"message":{"zh_cn":"…","en":"…"},"file":"…","detail":"…","stage":"scan"}}
```

| 字段 | 说明 |
| --- | --- |
| `code` | 机器可读错误码（如 `cli_invalid`、`image_load_failed`、`input_path_invalid`、`no_tools_found`、`config_invalid`） |
| `exit_code` | 对应的进程退出码 |
| `message` | `{zh_cn, en}` 人类可读消息 |
| `file` | 涉及的文件（可选） |
| `detail` | 补充细节（可选） |
| `stage` | 出错阶段，如 `cli` / `scan` / `load` / `encode` / `write`（pack 错误无此字段） |

错误码集合允许扩展，集成方应把未知 `code` 当作一般错误处理，以 `exit_code` 决定流程。

## 六、输出文件名协议

`.bin` 文件名本身是机器接口，pack 的 codegen 完全依赖它：

```text
<原图名>_<像素格式>_<算法>_<be|le>_<宽>x<高>.bin
```

从右往左解析（原图名可含下划线）：尺寸段 `<宽>x<高>`、字节序段 `be|le`、算法段、像素格式段（必须是合法格式名），剩余为原图名。
