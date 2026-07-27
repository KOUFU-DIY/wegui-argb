# pinyin2c

为 `Core/widgets_preview/ime_pinyin/` 的拼音引擎生成音节索引表（纯 const ROM 数据）。

**主路径是 `pinyin2c.exe`**（单文件 C11 + 预烘焙数据库，零运行依赖，静态链接
仅引用 KERNEL32/msvcrt 系统 DLL）。Python 脚本仅在刷新注音数据库时使用（见文末）。

## 用法（三种预设，三选一）

```powershell
# GB2312 一级字（3755 字）
.\pinyin2c.exe --level l1

# GB2312 一级 + 二级字（3755 + 3008 字）
.\pinyin2c.exe --level l1l2

# 自定义字集（UTF-8 文本文件，取其中 CJK 字符；候选 = 注音数据库 ∩ 字集）
.\pinyin2c.exe --charset my_chars.txt
```

可选参数：

- `--out <目录>`：输出目录（默认 `output/`，不存在会自动创建）；
- `--install`：额外把生成结果写为
  `Core/widgets_preview/ime_pinyin/we_pinyin_table.c/.h`（引擎真正编译的那份）。
  目标路径按 exe 所在目录解析（`../../Core/...`），exe 请保持在 `tool/pinyin2c/` 下。

产物（写入 `--out` 目录，UTF-8 + LF）：

| 文件 | 说明 |
| --- | --- |
| `we_pinyin_table_<preset>.c/.h` | 音节表 + 候选池（C 符号名各预设相同，勿同时参与编译） |
| `charset_l1.txt` / `charset_l1l2.txt` | 字符全集连续串（font2c 取模字符集用），`--level` 模式导出 |
| `charset_<stem>.txt` | `--charset` 模式的字符全集（原序去重） |
| `<stem>_font2c_chars.json` | `--charset` 模式：可直接放进 `tool/font2c/input/` 的取模配置（ASCII 区间 + 自定义字符，16px 4bpp msyh，internal 模式） |

从源码重编 exe（MinGW gcc 一行命令）：

```powershell
gcc -O2 -s -static -o pinyin2c.exe pinyin2c.c
```

## 当前生效表与换级别

控件目录只保留一份 `we_pinyin_table.c/.h`，当前为 **l1l2 版**（内含二级段，
`WE_PINYIN_ENABLE_L2` 默认 0：数据在 ROM 但引擎只遍历一级段，运行期
`we_pinyin_set_l2` 为空操作；定义为 1 后才编译二级遍历支持）。

换级别 = 覆盖拷贝：把 `output/we_pinyin_table_l1.c/.h` 覆盖到控件目录，或直接
`.\pinyin2c.exe --level l1 --install`。l1 版物理上不含二级段，比 l1l2 版省约
6 KB ROM。`output/` 里的其余预设成品仅作留档，不参与编译（C 符号名相同，
同时编译会重定义冲突）。

## 一份字集同时喂两个工具（--charset 工作流）

```powershell
# 1. 准备字集文件（UTF-8，内容随意，工具只取其中的 CJK 字符并去重）
Set-Content my_ui_chars.txt -Value "开始设置返回欢迎使用" -Encoding UTF8

# 2. 一次生成：拼音表 + font2c 取模配置
.\pinyin2c.exe --charset my_ui_chars.txt

# 3. 拼音表进控件目录（--install 或手动覆盖拷贝）
.\pinyin2c.exe --charset my_ui_chars.txt --install

# 4. 取模配置进 font2c，生成同字集的中文字库
Copy-Item output/my_ui_chars_font2c_chars.json ../font2c/input/
cd ../font2c ; ./font2c.exe build input/my_ui_chars_font2c_chars.json -o output
```

这样候选池里的每个字都保证在字库里有字形（控件在填充候选页时还会再经
`we_font_get_glyph_info` 过滤一次，双保险）。

`--charset` 模式细节：空音节自动剔除；不在注音数据库（GB2312 全集）内的字
无法注音，会从表中剔除并在运行报告 `not in db` 行计数（字库 JSON/charset txt
仍包含它们——取模覆盖但打不出来）；段内顺序取数据库频序（Python 版按字集
出现序，二者仅排列不同，语义一致：全部视为一级段）。

## 数据来源与正确性

1. **字集**：Python 内置 `gb2312` 编解码器按区位码扫描（16~55 区 = 一级
   3755 字，56~87 区 = 二级 3008 字），不含任何手抄表。
2. **注音**：pypinyin。默认取单一首选读音；常用多音字（了/地/得/着/还/
   行/长/重/乐/觉…共 60+ 字）经脚本内 `COMMON_POLYPHONES` 白名单显式展开
   全部常用读音。刻意**不用** pypinyin 的 heteronym 全量数据——其中大量
   词典冷僻文读（"董 zhǒng""家 jiē"之类）会污染候选栏。宁缺读音，不给
   错觉。
3. **音节合法性**：所有读音须命中脚本内嵌的标准普通话音节白名单
   （410+ 音节，ü 写作 v，含 zhei/dei/lo 等口语音节），滤掉 "n/ng/m"
   等无法键入的叹词读音（嗯 已显式改按 en 收录）。整字全部读音被滤掉时
   剔除该字并在运行报告中列出（当前 0 字被剔除）。
4. **段内排序**：内嵌高频字次序表 `FREQ_HEAD`（头 100 字为标准现代汉语
   语料字频序，其后为近似常用序，仅影响候选先后不影响正确性）；排不到的
   字保持 GB2312 原序（本身按拼音排列）。`DEMOTE_READINGS` 把高频字的
   次要读音降为未排名参与排序（如"都"不霸占 du 首位）。

## 表结构（与 we_pinyin.h 引擎约定一致）

```c
typedef struct {
    char py[7];         /* 音节字母，NUL 结尾，表按 py 字典序升序 */
    uint16_t cand_ofs;  /* 候选段在 we_pinyin_cands[] 中的起始下标 */
    uint8_t l1_cnt;     /* 段内一级字数（段首连续存放，按常用度排序） */
    uint8_t total_cnt;  /* 段内总字数（一级 + 二级） */
} we_pinyin_syllable_t;

extern const we_pinyin_syllable_t we_pinyin_syllables[];  /* ~400 项 */
extern const uint16_t we_pinyin_cands[];  /* Unicode 码点平铺池（非 UTF-8、非 GB2312 内码） */
```

候选池编码定死为 **uint16 Unicode 码点**，commit 上屏时才由引擎
`we_pinyin_cp_to_utf8` 转 UTF-8。

## 引擎单测

`test_we_pinyin.c` 是 `Core/widgets_preview/ime_pinyin/we_pinyin.c` 的独立单测
（单翻译单元直接 include 引擎与表源码，零 GUI 依赖）：

```powershell
$W = "..\..\Core\widgets_preview\ime_pinyin"
gcc -Wall -Wextra -Werror -std=c11 -I$W test_we_pinyin.c -o t0.exe ; .\t0.exe                          # L2 关（默认）
gcc -Wall -Wextra -Werror -std=c11 -DWE_PINYIN_ENABLE_L2=1 -I$W test_we_pinyin.c -o t1.exe ; .\t1.exe  # L2 开
```

断言覆盖：表结构不变量（字典序/段连续/计数合法）、精确命中（zhong→中、
de→的、ni→你、hao→好）、前缀区间（zh 覆盖 zha..zhuo、zho={zhong,zhou}）、
非法输入、全表遍历数与 l1/total 计数一致、L2 编译期与运行期开关差异、
码点转 UTF-8（中=E4 B8 AD）。当前 34/36 条（两种编译模式）全绿——含用
pinyin2c.exe 产物替换表后的复跑。

## 注音数据库刷新（才需要 Python）

`gen_pinyin_table.py` 现在只承担一个职责：把 pypinyin 注音 + 频序 + 多音字
白名单**烘焙**成 `pinyin_db.inc`（C 可 include 的 const 数据库，~154 KB，
400 音节 / 6830 候选 / 6763 字符全集）。日常生成完全不需要 Python。

只有改注音数据（`FREQ_HEAD` 频序表、`COMMON_POLYPHONES` 多音字白名单、
`DEMOTE_READINGS` 降权表，或升级 pypinyin）时才重跑：

```powershell
pip install pypinyin                        # 一次性（烘焙依赖）
python gen_pinyin_table.py --dump-db        # 重烘 pinyin_db.inc
gcc -O2 -s -static -o pinyin2c.exe pinyin2c.c   # 重编 exe
```

Python 脚本仍保留完整的 `--level/--charset/--install` 生成模式作为交叉验证
基准：`--level l1 / l1l2` 两预设产物与 exe 输出已做过逐字节 diff 一致
（唯一有意的格式统一：注释头生成者标识改为 `tool/pinyin2c`，不再区分
脚本/exe）。

## 已知局限

- 单字输入法：无词组、无用户词频学习；
- `FREQ_HEAD` 第 100 名以后的次序为近似常用序（不是语料精确频序）；
- 多音字仅覆盖白名单内的常用字，白名单外的多音字只有首选读音；
- `--charset` 模式所有字视为一级段（l1_cnt == total_cnt）；exe 版能注音的
  字以 GB2312 全集为上限（数据库外的 CJK 字请回退 Python 版或扩库重烘焙）。
