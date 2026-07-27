#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_pinyin_table.py —— WeGui ime_pinyin 控件的拼音音节表生成器

产出（写入 output/，UTF-8）：
  we_pinyin_table_<preset>.c/.h   排序音节表 + 候选池（uint16 Unicode 码点）
  charset_l1.txt / charset_l1l2.txt  字符全集（font2c 取模字符集用）
  <preset>_font2c_chars.json       --charset 模式：可直接改造成 font2c input 的 JSON

三种预设（--level / --charset 三选一）：
  --level l1        GB2312 一级字（3755）        -> we_pinyin_table_l1.c/.h
  --level l1l2      GB2312 一级+二级（3755+3008）-> we_pinyin_table_l1l2.c/.h
  --charset <file>  自定义字集（UTF-8 文本，取其中 CJK 字符）-> we_pinyin_table_custom.c/.h

--install 把本次生成的表复制为
  Core/widgets_preview/ime_pinyin/we_pinyin_table.c/.h（引擎实际编译的那份）。

数据来源：
  1. 字集：Python 内置 gb2312 编解码器（区位码 16~55 区=一级，56~87 区=二级）；
  2. 注音：pypinyin（heteronym=True，Style.NORMAL，ü 输出为 v），
     经内嵌《标准普通话音节表》白名单过滤，杜绝 "ng"/"m" 等非键入音节；
  3. 段内排序：内嵌高频字次序表（头部 100 字为标准现代汉语语料频序，
     其后为近似常用序），排不到的字保持 GB2312 原序（本身即按拼音排列）。

正确性优先：某字全部读音都被白名单过滤时整字剔除并在报告中列出，
绝不猜测注音。
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

try:
    from pypinyin import pinyin, Style
except ImportError:
    print("error: 需要 pypinyin（pip install pypinyin）", file=sys.stderr)
    sys.exit(1)

SCRIPT_DIR = Path(__file__).resolve().parent
OUTPUT_DIR = SCRIPT_DIR / "output"
WIDGET_DIR = SCRIPT_DIR.parent.parent / "Core" / "widgets_preview" / "ime_pinyin"

MAX_PY_LEN = 6  # 最长音节 zhuang/chuang/shuang

# ---------------------------------------------------------------------------
# 标准普通话音节白名单（键入形式：ü 写作 v）。
# 仅作过滤，不参与生成：pypinyin 给出的读音必须命中白名单才收录。
# 覆盖含 zhei/shei/dei/kei/lo/yo/dia/lia/chua/rua/den/nou/nun 等口语音节。
# ---------------------------------------------------------------------------
VALID_SYLLABLES = set("""
a ai an ang ao e ei en eng er o ou
yi ya yao ye you yan yin yang ying yong yu yue yuan yun yo
wu wa wo wai wei wan wen wang weng
ba bo bai bei bao ban ben bang beng bi bie biao bian bin bing bu
pa po pai pei pao pou pan pen pang peng pi pie piao pian pin ping pu
ma mo me mai mei mao mou man men mang meng mi mie miao miu mian min ming miu mu
fa fo fei fou fan fen fang feng fu
da de dai dei dao dou dan den dang deng di dia die diao diu dian ding dong du duan dun dui duo
ta te tai tao tou tan tang teng ti tie tiao tian ting tong tu tuan tun tui tuo
na nai nei nao nou nan nen nang neng ni nie niao niu nian nin niang ning nong nu nuan nun nv nve nuo ne
la le lai lei lao lou lan lang leng li lia lie liao liu lian lin liang ling long lu luan lun luo lv lve lo
ga ge gai gei gao gou gan gen gang geng gong gu gua guai guan guang gui gun guo
ka ke kai kei kao kou kan ken kang keng kong ku kua kuai kuan kuang kui kun kuo
ha he hai hei hao hou han hen hang heng hong hu hua huai huan huang hui hun huo
ji jia jie jiao jiu jian jin jiang jing jiong ju juan jue jun
qi qia qie qiao qiu qian qin qiang qing qiong qu quan que qun
xi xia xie xiao xiu xian xin xiang xing xiong xu xuan xue xun
zha zhe zhi zhai zhei zhao zhou zhan zhen zhang zheng zhong zhu zhua zhuai zhuan zhuang zhui zhun zhuo
cha che chi chai chao chou chan chen chang cheng chong chu chua chuai chuan chuang chui chun chuo
sha she shi shai shei shao shou shan shen shang sheng shu shua shuai shuan shuang shui shun shuo
re ri rao rou ran ren rang reng rong ru rua ruan rui run ruo
za ze zi zai zei zao zou zan zen zang zeng zong zu zuan zun zui zuo
ca ce ci cai cao cou can cen cang ceng cong cu cuan cun cui cuo
sa se si sai sao sou san sen sang seng song su suan sun sui suo
""".split())

# ---------------------------------------------------------------------------
# 注音策略：默认取 pypinyin 单一首选读音（heteronym 数据含大量冷僻文读，
# 会把"董"塞进 zhong、"家"塞进 jie，噪声太大），另用下面这份精选常用
# 多音字白名单显式给出完整读音表（首项为首选）。冷僻读音宁缺毋滥。
#   嗯：词典读 n/ng（无法用字母键入），各家输入法均按 en 收录。
# ---------------------------------------------------------------------------
COMMON_POLYPHONES = {
    "了": ["le", "liao"],   "地": ["di", "de"],     "得": ["de", "dei"],
    "着": ["zhe", "zhao", "zhuo"],                  "还": ["hai", "huan"],
    "行": ["xing", "hang"], "长": ["chang", "zhang"], "重": ["zhong", "chong"],
    "乐": ["le", "yue"],    "觉": ["jue", "jiao"],  "便": ["bian", "pian"],
    "传": ["chuan", "zhuan"], "调": ["diao", "tiao"], "校": ["xiao", "jiao"],
    "什": ["shen", "shi"],  "都": ["dou", "du"],    "曾": ["ceng", "zeng"],
    "藏": ["cang", "zang"], "弹": ["dan", "tan"],   "单": ["dan", "shan"],
    "朝": ["chao", "zhao"], "参": ["can", "shen"],  "差": ["cha", "chai"],
    "血": ["xue", "xie"],   "薄": ["bao", "bo"],    "剥": ["bo", "bao"],
    "露": ["lu", "lou"],    "落": ["luo", "la"],    "熟": ["shu", "shou"],
    "色": ["se", "shai"],   "宿": ["su", "xiu"],    "削": ["xiao", "xue"],
    "壳": ["ke", "qiao"],   "塞": ["sai", "se"],    "省": ["sheng", "xing"],
    "盛": ["sheng", "cheng"], "提": ["ti", "di"],   "恶": ["e", "wu"],
    "奇": ["qi", "ji"],     "降": ["jiang", "xiang"], "角": ["jiao", "jue"],
    "卡": ["ka", "qia"],    "泊": ["bo", "po"],     "圈": ["quan", "juan"],
    "匙": ["chi", "shi"],   "折": ["zhe", "she"],   "和": ["he", "huo"],
    "没": ["mei", "mo"],    "扎": ["zha", "za"],    "轧": ["ya", "zha"],
    "粘": ["zhan", "nian"], "幢": ["zhuang", "chuang"], "吓": ["xia", "he"],
    "纤": ["xian", "qian"], "择": ["ze", "zhai"],   "迫": ["po", "pai"],
    "弄": ["nong", "long"], "咳": ["ke", "hai"],    "吁": ["yu", "xu"],
    "秘": ["mi", "bi"],     "蚌": ["bang", "beng"], "澄": ["cheng", "deng"],
    "囤": ["tun", "dun"],   "扒": ["ba", "pa"],     "爪": ["zhua", "zhao"],
    "率": ["shuai", "lv"],  "嗯": ["en"],
}

# ---------------------------------------------------------------------------
# 罕用读音降权：高频字的次要读音不享受频序特权（仍收录，只按未排名字
# 参与该音节的段内排序），避免"着"霸占 zhao、"都"霸占 du 之类的错觉。
# ---------------------------------------------------------------------------
DEMOTE_READINGS = {
    ("着", "zhao"), ("着", "zhuo"), ("都", "du"),
    ("没", "mo"), ("和", "huo"),
}

# ---------------------------------------------------------------------------
# 高频字次序表：决定同音节段内候选顺序（越靠前越先出）。
# 第 1~100 字为标准现代汉语语料字频序（多方转载一致的头部）；
# 其后为近似常用序（次序仅影响候选排列，不影响正确性）。
# 生成时若发现重复字会直接报错终止。
# ---------------------------------------------------------------------------
FREQ_HEAD = (
    # ---- 1~100：标准语料频序 ----
    "的一是不了人我在有他这为之大来以个中上们到说国和地也子时道出而"
    "要于就下得可你年生自会那后能对着事其里所去行过家十用发天如然作"
    "方成者多日都三小军二无同么经法当起与好看学进种将还分此心前面又"
    "定见只主没公从"
    # ---- 101+：近似常用序（仅影响候选排列先后） ----
    "使点业本把性做高被己工想开它合情向头文体美相现实加量长部果民明"
    "全力光电化内水山金老因或由西东南北京华名器走常先门口少才声数目"
    "平更白变条打车风气五四九八七六百千万第位外空色路身员战争报类强"
    "给别太星音世界图书术结接解请清任求处叫件住远录难亲快语言论问题"
    "建议记程式转活动物套指调温湿压设备状态"
    "失败错误确认取消返回退保存删除修改查询始停暂继续完"
    "红绿蓝黄黑灰紫粉春夏秋冬早晚昨今"
    "吃喝睡站坐跑跳飞游唱歌舞画写读听讲话字词句章页篇课班校师教育科"
    "房屋庭院楼层窗床桌椅柜箱包衣裤鞋帽巾枕妈爸儿女男"
    "爱她您谁哪吗吧呢呀啦很让找帮助等候跟但已再零"
    "王李张刘陈杨赵周吴徐孙马朱胡郭何林罗郑梁谢宋唐许韩冯邓曹彭曾肖"
    "田董袁潘蒋蔡余杜叶苏魏吕丁沈姚卢姜崔钟谭陆汪范石廖贾韦邹熊孟秦"
    "阎薛侯雷龙段郝孔邵史毛顾赖武康贺严尹钱施牛洪龚"
)

# ---------------------------------------------------------------------------
# 基础工具
# ---------------------------------------------------------------------------


def build_freq_rank():
    """把 FREQ_HEAD 展开为 字 -> 名次 的字典，出现重复立即报错。"""
    rank = {}
    for ch in FREQ_HEAD:
        if ch in rank:
            raise SystemExit(f"FREQ_HEAD 内有重复字：{ch!r}（请修表）")
        rank[ch] = len(rank)
    return rank


def gb2312_chars(row_first, row_last):
    """按区位码扫描 GB2312 汉字（row 16~55 一级 / 56~87 二级），返回有序列表。"""
    chars = []
    for row in range(row_first, row_last + 1):
        for pos in range(1, 95):
            raw = bytes((0xA0 + row, 0xA0 + pos))
            try:
                ch = raw.decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(ch) == 1 and ord(ch) <= 0xFFFF:
                chars.append(ch)
    return chars


def readings_of(ch):
    """取一个字的键入读音（去声调、v 形式、白名单过滤）。

    默认单一首选读音；常用多音字经 COMMON_POLYPHONES 白名单显式展开。
    """
    if ch in COMMON_POLYPHONES:
        raw = list(COMMON_POLYPHONES[ch])
    else:
        res = pinyin(ch, style=Style.NORMAL, heteronym=False, errors="ignore")
        raw = [res[0][0]] if res and res[0] else []
    out = []
    for syl in raw:
        syl = syl.strip().lower()
        if syl in VALID_SYLLABLES and syl not in out:
            out.append(syl)
        elif ch in COMMON_POLYPHONES and syl not in VALID_SYLLABLES:
            raise SystemExit(f"COMMON_POLYPHONES 中 {ch!r} 的读音 {syl!r} 不在音节白名单内")
    return out


def collect(chars_l1, chars_l2):
    """建表：syllable -> ([一级字码点], [二级字码点])，段内先按频序后按原序。

    返回 (table, dropped)；dropped 为全部读音被过滤而剔除的字。
    """
    freq_rank = build_freq_rank()
    unranked_base = len(freq_rank)
    table = {}
    dropped = []
    order = {}  # 字 -> 扫描序（GB2312 原序，频序表排不到时使用）

    for level, chars in ((1, chars_l1), (2, chars_l2)):
        for ch in chars:
            if ch not in order:
                order[ch] = len(order)
            syls = readings_of(ch)
            if not syls:
                dropped.append(ch)
                continue
            for syl in syls:
                slot = table.setdefault(syl, ([], []))
                seg = slot[0] if level == 1 else slot[1]
                if ch not in seg:
                    seg.append(ch)

    def sort_key(syl):
        def key(ch):
            r = freq_rank.get(ch)
            if r is None or (ch, syl) in DEMOTE_READINGS:
                return (1, unranked_base + order[ch])
            return (0, r)
        return key

    for syl, (seg1, seg2) in table.items():
        seg1.sort(key=sort_key(syl))
        seg2.sort(key=sort_key(syl))
    return table, dropped


# ---------------------------------------------------------------------------
# C 代码发射
# ---------------------------------------------------------------------------


def emit_c(table, preset, out_c, out_h):
    """把音节表写成 we_pinyin_table_<preset>.c/.h（内部统一 include 规范名）。"""
    syllables = sorted(table.keys())
    cand_pool = []      # (codepoint, char) 平铺池
    entries = []        # (py, ofs, l1_cnt, total_cnt)
    for syl in syllables:
        seg1, seg2 = table[syl]
        if len(syl) > MAX_PY_LEN:
            raise SystemExit(f"音节超长：{syl}")
        total = len(seg1) + len(seg2)
        if total == 0:
            continue
        if len(seg1) > 255 or total > 255:
            raise SystemExit(f"音节 {syl} 候选数超过 uint8：{total}")
        ofs = len(cand_pool)
        if ofs + total > 0xFFFF:
            raise SystemExit("候选池超过 uint16 寻址范围")
        for ch in seg1 + seg2:
            cand_pool.append((ord(ch), ch))
        entries.append((syl, ofs, len(seg1), total))

    l1_total = sum(e[2] for e in entries)
    l2_total = sum(e[3] - e[2] for e in entries)
    rom_bytes = len(entries) * 12 + len(cand_pool) * 2

    hdr = f"""/* auto-generated by tool/pinyin2c -- DO NOT EDIT
 * preset      : {preset}
 * syllables   : {len(entries)}
 * L1 cands    : {l1_total}
 * L2 cands    : {l2_total}
 * ROM approx  : {rom_bytes} bytes ({len(entries)} * 12 + {len(cand_pool)} * 2)
 * 数据来源：GB2312 字集 + pypinyin 注音（白名单过滤），详见 tool/pinyin2c/README.md
 */
"""

    h_text = hdr + f"""#ifndef __WE_PINYIN_TABLE_H
#define __WE_PINYIN_TABLE_H

#include <stdint.h>

/* 二级字段编译期开关：0 = 表内含二级字但引擎只遍历一级段（默认），
 * 1 = 编译进二级段遍历支持（运行期再经 we_pinyin_set_l2 开关）。
 * 只想彻底省 ROM 请改用 l1 预设表覆盖本表（见 tool/pinyin2c/README.md）。 */
#ifndef WE_PINYIN_ENABLE_L2
#define WE_PINYIN_ENABLE_L2 0
#endif

#define WE_PINYIN_SYLLABLE_COUNT {len(entries)}U
#define WE_PINYIN_CAND_COUNT     {len(cand_pool)}U
#define WE_PINYIN_MAX_LEN        {MAX_PY_LEN}

/* 排序音节表条目：py 字典序升序；候选段 = we_pinyin_cands[cand_ofs ..)，
 * 段内一级字在前（按常用度排序），二级字接在后。 */
typedef struct
{{
    char py[{MAX_PY_LEN + 1}];      /* 音节字母（NUL 结尾） */
    uint16_t cand_ofs;  /* 候选段在 we_pinyin_cands 中的起始下标 */
    uint8_t l1_cnt;     /* 段内一级字数 */
    uint8_t total_cnt;  /* 段内总字数（一级 + 二级） */
}} we_pinyin_syllable_t;

extern const we_pinyin_syllable_t we_pinyin_syllables[WE_PINYIN_SYLLABLE_COUNT];
extern const uint16_t we_pinyin_cands[WE_PINYIN_CAND_COUNT];

#endif /* __WE_PINYIN_TABLE_H */
"""

    lines = [hdr, '#include "we_pinyin_table.h"', ""]
    lines.append("const we_pinyin_syllable_t we_pinyin_syllables[WE_PINYIN_SYLLABLE_COUNT] = {")
    for syl, ofs, l1c, total in entries:
        lines.append(f'    {{ "{syl}", {ofs}U, {l1c}U, {total}U }},')
    lines.append("};")
    lines.append("")
    lines.append("const uint16_t we_pinyin_cands[WE_PINYIN_CAND_COUNT] = {")
    pos = 0
    for syl, ofs, l1c, total in entries:
        seg = cand_pool[ofs:ofs + total]
        chars_txt = "".join(ch for _, ch in seg)
        lines.append(f"    /* {syl} [{ofs}..{ofs + total - 1}] L1:{l1c} {chars_txt} */")
        for i in range(0, total, 12):
            chunk = seg[i:i + 12]
            lines.append("    " + " ".join(f"0x{cp:04X}U," for cp, _ in chunk))
        pos += total
    lines.append("};")
    lines.append("")

    out_h.write_text(h_text, encoding="utf-8", newline="\n")
    out_c.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return len(entries), l1_total, l2_total, rom_bytes


def emit_db(table, chars_l1, chars_l2, out_path):
    """把 l1l2 全集注音数据烘焙成 C 可 #include 的数据库（pinyin_db.inc）。

    供 pinyin2c.c（零依赖 C 版生成器）使用；日常生成不再需要 Python，
    仅刷新注音数据时重跑本模式。
    """
    syllables = sorted(table.keys())
    entries = []   # (syl, l1_cnt, l2_cnt, ofs)
    pool = []      # 候选码点平铺（每音节先一级段后二级段）
    for syl in syllables:
        seg1, seg2 = table[syl]
        if len(seg1) + len(seg2) == 0:
            continue
        entries.append((syl, len(seg1), len(seg2), len(pool)))
        pool.extend(ord(c) for c in seg1 + seg2)

    l1_total = sum(e[1] for e in entries)
    l2_total = sum(e[2] for e in entries)

    def rows(values, per_line=12):
        out = []
        for i in range(0, len(values), per_line):
            out.append("    " + " ".join(f"0x{v:04X}," for v in values[i:i + per_line]))
        return out

    lines = [f"""/* auto-generated by gen_pinyin_table.py --dump-db -- DO NOT EDIT
 * 完整注音数据库：GB2312 一级+二级全集（段内已按常用度排序），
 * 供 pinyin2c.c 直接 #include。日常生成用 pinyin2c.exe 即可；
 * 仅刷新注音数据（FREQ_HEAD / COMMON_POLYPHONES / pypinyin 升级）时
 * 才需重跑：python gen_pinyin_table.py --dump-db
 * syllables : {len(entries)}
 * L1 cands  : {l1_total}
 * L2 cands  : {l2_total}
 * charset   : L1 {len(chars_l1)} + L2 {len(chars_l2)}（GB2312 区位扫描序）
 */

typedef struct
{{
    const char *py;        /* 音节字母（字典序） */
    unsigned short l1_cnt; /* 一级段字数 */
    unsigned short l2_cnt; /* 二级段字数 */
    unsigned int ofs;      /* 候选段起始下标（段内先一级后二级） */
}} py_db_syll_t;

#define PY_DB_SYLL_COUNT {len(entries)}
#define PY_DB_CAND_COUNT {len(pool)}
#define PY_DB_CHARSET_L1_COUNT {len(chars_l1)}
#define PY_DB_CHARSET_L2_COUNT {len(chars_l2)}

static const py_db_syll_t py_db_sylls[PY_DB_SYLL_COUNT] = {{"""]
    for syl, l1c, l2c, ofs in entries:
        lines.append(f'    {{ "{syl}", {l1c}, {l2c}, {ofs} }},')
    lines.append("};")
    lines.append("")
    lines.append("static const unsigned short py_db_cands[PY_DB_CAND_COUNT] = {")
    for syl, l1c, l2c, ofs in entries:
        seg = pool[ofs:ofs + l1c + l2c]
        chars_txt = "".join(chr(v) for v in seg)
        lines.append(f"    /* {syl} L1:{l1c} L2:{l2c} {chars_txt} */")
        lines.extend(rows(seg))
    lines.append("};")
    lines.append("")
    lines.append("/* GB2312 区位扫描序字符全集（charset_*.txt 导出用） */")
    lines.append("static const unsigned short py_db_charset_l1[PY_DB_CHARSET_L1_COUNT] = {")
    lines.extend(rows([ord(c) for c in chars_l1]))
    lines.append("};")
    lines.append("")
    lines.append("static const unsigned short py_db_charset_l2[PY_DB_CHARSET_L2_COUNT] = {")
    lines.extend(rows([ord(c) for c in chars_l2]))
    lines.append("};")
    lines.append("")

    out_path.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return len(entries), l1_total, l2_total


def emit_font2c_json(chars, name, out_path):
    """输出可直接放入 tool/font2c/input/ 的取模配置（ASCII 区间 + 自定义字符）。"""
    cfg = {
        "version": 1,
        "symbol": name,
        "font": {"file": "msyh.ttc", "size": 16},
        "render": {"bpp": 4},
        "charset": {
            "ranges": [["U+0020", "U+007E"]],
            "chars": "".join(chars),
        },
        "deploy": {"mode": "internal"},
    }
    out_path.write_text(json.dumps(cfg, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8", newline="\n")


# ---------------------------------------------------------------------------
# 主流程
# ---------------------------------------------------------------------------


def main():
    ap = argparse.ArgumentParser(description="WeGui ime_pinyin 音节表生成器")
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--level", choices=["l1", "l1l2"], help="GB2312 一级 / 一级+二级 预设")
    g.add_argument("--charset", metavar="FILE", help="自定义字集文件（UTF-8 文本）")
    g.add_argument("--dump-db", action="store_true",
                   help="烘焙完整注音数据库 pinyin_db.inc（供 pinyin2c.c 使用）")
    ap.add_argument("--install", action="store_true",
                    help="把生成结果复制为 Core/widgets_preview/ime_pinyin/we_pinyin_table.c/.h")
    args = ap.parse_args()

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    if args.dump_db:
        full_l1 = gb2312_chars(16, 55)
        full_l2 = gb2312_chars(56, 87)
        table, dropped = collect(full_l1, full_l2)
        out = SCRIPT_DIR / "pinyin_db.inc"
        n_syl, n_l1, n_l2 = emit_db(table, full_l1, full_l2, out)
        print(f"db          : {out.name}")
        print(f"syllables   : {n_syl}")
        print(f"L1 cands    : {n_l1}")
        print(f"L2 cands    : {n_l2}")
        print(f"dropped     : {len(dropped)} {''.join(dropped) if dropped else ''}")
        return

    chars_l1 = gb2312_chars(16, 55)
    if args.level:
        preset = args.level
        chars_l2 = gb2312_chars(56, 87) if preset == "l1l2" else []
        src_l1, src_l2 = chars_l1, chars_l2
    else:
        preset = "custom"
        raw = Path(args.charset).read_text(encoding="utf-8")
        seen = []
        for ch in raw:
            if 0x4E00 <= ord(ch) <= 0x9FFF and ch not in seen:
                seen.append(ch)
        if not seen:
            raise SystemExit("字集文件中没有 CJK 字符")
        src_l1, src_l2 = seen, []  # 自定义字集全部视为一级段

    table, dropped = collect(src_l1, src_l2)

    out_c = OUTPUT_DIR / f"we_pinyin_table_{preset}.c"
    out_h = OUTPUT_DIR / f"we_pinyin_table_{preset}.h"
    n_syl, n_l1, n_l2, rom = emit_c(table, preset, out_c, out_h)

    # 字符全集导出（font2c 取模字符集）
    if args.level:
        (OUTPUT_DIR / "charset_l1.txt").write_text("".join(chars_l1),
                                                   encoding="utf-8", newline="\n")
        if preset == "l1l2":
            (OUTPUT_DIR / "charset_l1l2.txt").write_text("".join(chars_l1 + chars_l2),
                                                         encoding="utf-8", newline="\n")
    else:
        stem = Path(args.charset).stem
        (OUTPUT_DIR / f"charset_{stem}.txt").write_text("".join(src_l1),
                                                        encoding="utf-8", newline="\n")
        emit_font2c_json(src_l1, f"msyh_16_4bbp_{stem}",
                         OUTPUT_DIR / f"{stem}_font2c_chars.json")

    print(f"preset      : {preset}")
    print(f"syllables   : {n_syl}")
    print(f"L1 cands    : {n_l1}")
    print(f"L2 cands    : {n_l2}")
    print(f"ROM approx  : {rom} bytes")
    print(f"dropped     : {len(dropped)} {''.join(dropped) if dropped else ''}")
    print(f"output      : {out_c.name} / {out_h.name}")

    if args.install:
        WIDGET_DIR.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(out_c, WIDGET_DIR / "we_pinyin_table.c")
        shutil.copyfile(out_h, WIDGET_DIR / "we_pinyin_table.h")
        print(f"installed   : {WIDGET_DIR / 'we_pinyin_table.c'}")


if __name__ == "__main__":
    main()
