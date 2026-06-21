# 解码编写说明

本页用于说明如何根据当前工具生成的 `.bin` 编写解码器。  
内容以当前实现为准。

## 先看结论

如果你要写解码器，建议按这个顺序理解：

1. 先看 [像素格式说明](README-formats.md)
2. 再看本页的压缩流规则
3. 如果要做局部跳转解码，再看 `indexQOI`

## 一、所有算法共同规则

## 1. 像素遍历顺序

所有工具都按以下顺序编码：

```text
从左到右
从上到下
```

也就是标准行优先顺序。

## 2. 输出是纯 payload

除了 `indexQOI` 以外，其余算法都不包含宽高、格式码或结束标记：

- `RAW`
- `RLE`
- `IMPRLE`
- `QOI`
- `QOIF`

因此解码器必须从外部拿到：

- 宽度
- 高度
- 像素格式
- 字节序

## 3. 字节序

当前工具支持：

- 大端：`be`
- 小端：`le`

解码时不要只看算法名，还要结合文件名或外部信息判断是否为 `little-endian`。

## 二、RAW 解码

`RAW` 最简单。

## 数据结构

```text
像素0原始字节 + 像素1原始字节 + 像素2原始字节 + ...
```

## 解码步骤

1. 根据像素格式确定每像素字节数
2. 逐像素读取固定长度字节组
3. 按 [像素格式说明](README-formats.md) 解包

## 三、原始 RLE 解码

## 数据结构

```text
{数量, 数据组, 数量, 数据组, ..., 0x00}
```

其中：

- `数量` 为 1 字节
- `数据组` 的长度由像素格式决定
- 末尾 `0x00` 表示结束

## 规则

- `数量` 取值范围：`1..255`
- `0x00` 只能表示结束，不能表示一段数据
- 每一段表示“把紧随其后的像素组重复 N 次”

## 伪代码

```c
while (1) {
    uint8_t count = read_u8();
    if (count == 0x00) {
        break;
    }

    read pixel_group[group_size];
    repeat count times:
        emit pixel_group;
}
```

## 四、改进 RLE 解码

## 数据结构

```text
{控制字节, 数据, 控制字节, 数据, ..., 0x00}
```

控制字节规则：

- `bit7 = 0`：后接若干组不连续数据，直接取用
- `bit7 = 1`：后接 1 组数据，重复若干次
- `bit6..0`：长度

## 规则

- 原样段长度范围：`1..127`
- 重复段长度范围：`1..127`
- `0x00` 表示结束

## 伪代码

```c
while (1) {
    uint8_t tag = read_u8();
    if (tag == 0x00) {
        break;
    }

    uint8_t count = tag & 0x7F;
    if ((tag & 0x80) == 0) {
        repeat count times:
            read pixel_group[group_size];
            emit pixel_group;
    } else {
        read pixel_group[group_size];
        repeat count times:
            emit pixel_group;
    }
}
```

## 五、QOI 家族总览

当前有三种 QOI 系列：

- `QOI`：原始 QOI，带 64 项字典索引
- `QOIF`：原始 QOI（无字典）
- `indexQOI`：索引 QOI，文件头 + 索引表 + `QOIF` payload

## 和标准 QOI 的相同点

- 有 `INDEX / DIFF / LUMA / RUN / RGB / RGBA` 这套思想
- `RUN` 最大长度仍为 62

## 和标准 QOI 的不同点

- 没有标准 QOI 文件头
- 没有标准 QOI 结束码
- `OP_RGB / OP_RGBA` 后跟的原始字节长度不是固定 3/4，而是跟当前像素格式相关
- 内部比较与差分运算，作用在“量化后的目标格式通道值”上，不是原始 8 位 RGBA

## 当前实现的初始状态

解码开始前，建议按当前实现初始化：

- `prev.r = 0`
- `prev.g = 0`
- `prev.b = 0`
- `prev.a = 当前格式的最大 Alpha`

也就是：

- 非 Alpha 格式：`prev.a = 255`
- `ARGB8888` / `ARGB8565`：`prev.a = 255`
- `ARGB6666`：`prev.a = 63`
- `ARGB4444`：`prev.a = 15`
- `ARGB2222`：`prev.a = 3`
- `RAGB5155`：`prev.a = 1`

## 六、QOI opcode 规则

## 1. OP_INDEX

```text
0x00..0x3F
```

只在 `img2bin_qoi.exe` 中出现。  
`QOIF` 和 `indexQOI` 都不会输出它。

索引表大小固定 64 项。

哈希公式：

```text
hash = (r * 3 + g * 5 + b * 7 + a * 11) & 63
```

说明：

- 对非 Alpha 格式，参与哈希的 `a` 固定视为 `255`
- 这里的 `r/g/b/a` 都是“量化后的目标格式通道值”

建议解码器把字典初始化为“无效项”。

## 2. OP_DIFF

```text
0x40..0x7F
```

位含义：

```text
bits5..4 = dr + 2
bits3..2 = dg + 2
bits1..0 = db + 2
```

取值范围：

- `dr`：`-2..1`
- `dg`：`-2..1`
- `db`：`-2..1`

要求：

- Alpha 必须和上一个像素相同

## 3. OP_LUMA

第一字节：

```text
0x80..0xBF
bits5..0 = dg + 32
```

第二字节：

```text
bits7..4 = (dr - dg) + 8
bits3..0 = (db - dg) + 8
```

取值范围：

- `dg`：`-32..31`
- `dr - dg`：`-8..7`
- `db - dg`：`-8..7`

要求：

- Alpha 必须和上一个像素相同

## 4. OP_RUN

```text
0xC0..0xFD
count = (opcode & 0x3F) + 1
```

范围：

- `1..62`

表示把上一个像素重复输出 `count` 次。

## 5. OP_RGB

```text
0xFE
```

注意：在本项目里它不是固定后接 3 字节，而是“后接该格式的颜色原始字节”。

### 后接字节数

| 格式 | `OP_RGB` 后接字节 |
| --- | --- |
| `ARGB8888` | 3 字节颜色，Alpha 保持上一个像素 |
| `ARGB8565` | 2 字节 `RGB565`，Alpha 保持上一个像素 |
| `RGB888` | 3 字节 |
| `RGB565` | 2 字节 |
| `RGB332` | 1 字节 |

## 6. OP_RGBA

```text
0xFF
```

注意：在本项目里它也不是固定 4 字节，而是“后接该格式的完整原始像素字节”。

### 后接字节数

| 格式 | `OP_RGBA` 后接字节 |
| --- | --- |
| `ARGB8888` | 4 |
| `ARGB6666` | 3 |
| `ARGB4444` | 2 |
| `ARGB2222` | 1 |
| `ARGB8565` | 3 |
| `RAGB5155` | 2 |

## 七、QOI 解码注意点

## 1. current/prev 状态应保存量化通道，不是 8 位 RGBA

例如对于 `RGB565`，建议内部维护：

```text
R5, G6, B5, A=255
```

对于 `ARGB4444`，建议维护：

```text
A4, R4, G4, B4
```

只有这样，`INDEX / DIFF / LUMA` 才会与当前工具输出一致。

## 2. 遇到 OP_RGB / OP_RGBA 时，要先把原始字节解包成量化通道

不能只把字节原样复制到状态里。  
因为后面还要参与：

- `DIFF`
- `LUMA`
- `INDEX`

## 3. 输出像素时，再按当前格式重新打包

推荐流程：

1. 解出当前像素的量化通道状态
2. 如需写回目标格式字节流，则按 [像素格式说明](README-formats.md) 的规则重新组包

## 八、QOIF 解码

`QOIF` 就是“去掉字典索引后的 QOI”。

因此：

- 不会出现 `OP_INDEX`
- 不需要维护 64 项字典
- 其余规则与当前项目中的 `QOI` 相同

如果你只需要实现一种更简单的解码器，通常建议优先实现 `QOIF`。

## 九、indexQOI 解码

`indexQOI = 索引头 + 索引表 + QOIF payload`

## 1. 索引头格式

固定 13 字节：

```text
byte0   = 头长度，当前固定 0x0D
byte1-2 = 宽度，16bit，大端
byte3-4 = 高度，16bit，大端
byte5-6 = 索引间隔，16bit，大端
byte7-8 = u16 索引区字节长度，16bit，大端
byte9-10 = u24 索引区字节长度，16bit，大端
byte11-12 = u32 索引区字节长度，16bit，大端
```

## 2. payload 起始位置

```text
payload_pos = 13 + u16_bytes + u24_bytes + u32_bytes
```

## 3. 索引值的含义

每个索引值都是：

```text
相对 payload 起点的字节偏移
```

不是相对文件起点。

因此真正的 payload 指针是：

```text
target = payload_start + offset
```

## 4. 索引点是什么

当前实现中：

- 默认索引间隔 = 图片宽度
- 也可由 `--index-interval` 指定
- 每到一个索引位置，编码器都会取消压缩，直接写一个原始像素块

因此你可以：

1. 选中某个索引
2. 直接跳到对应 payload 偏移
3. 从那个像素位置继续往后解码

## 5. 如何定位第 N 个像素

设：

- `interval = 索引间隔`
- `pixel_index = 想读取的像素位置`

则：

```text
index_slot = pixel_index / interval
base_pixel = index_slot * interval
offset = 第 index_slot 个索引值
cursor = payload_start + offset
```

然后从 `cursor` 开始解码，初始像素位置就是 `base_pixel`。

## 6. indexQOI 和普通 QOIF 的关系

`indexQOI` 的 payload 部分，本质上就是一条特殊的 `QOIF`：

- 不使用字典索引
- 在索引位置强制输出原始像素块

因此如果你已经写好了 `QOIF` 解码器，只需要：

1. 先解析 `indexQOI` 头和索引表
2. 再把 payload 当作 `QOIF` 解

## 十、当前实现中的关键兼容约定

这些点最容易在不同实现之间产生分歧，建议你直接按这里实现：

- `QOI / QOIF` 没有标准 QOI 头
- `QOI / QOIF / indexQOI` 没有尾部结束码
- `indexQOI` 偏移值相对的是 payload 起点，不是文件起点
- `indexQOI` 中：
  - 非 Alpha 格式原始块使用 `0xFE`
  - 带 Alpha 格式原始块使用 `0xFF`
- `RGB888` 在当前实现里按工具定义的字节顺序处理

## 十一、推荐的实现顺序

如果你是第一次写解码器，建议这样做：

1. 先实现 `RAW`
2. 再实现 `RLE`
3. 再实现 `IMPRLE`
4. 再实现 `QOIF`
5. 再实现 `QOI`
6. 最后实现 `indexQOI`

## 十二、写解码器时最有用的资料

- [像素格式说明](README-formats.md)
- [协议与验证说明](README-protocol.md)
- 目标工具执行 `--info` 返回的 JSON
- 由当前工具自己生成的 `.bin`、`manifest.json` 和命令记录
