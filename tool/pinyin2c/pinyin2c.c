/**
 * @file  pinyin2c.c
 * @brief 拼音音节表生成器（单文件 C11，零运行依赖）
 *
 * 注音数据库 pinyin_db.inc 由 gen_pinyin_table.py --dump-db 预先烘焙
 * （pypinyin 只在烘焙时用一次），本程序日常生成完全脱离 Python。
 *
 * 用法（与 Python 版命令行对齐）：
 *   pinyin2c --level l1              GB2312 一级字预设
 *   pinyin2c --level l1l2            GB2312 一级+二级预设
 *   pinyin2c --charset <utf8文件>    自定义字集（数据库 ∩ 字集，空音节剔除）
 *   可选：--out <目录>（默认 output/）、--install（覆盖拷贝到控件目录，
 *   目标路径按可执行文件所在目录 ../../Core/widgets_preview/ime_pinyin 解析）
 *
 * 输出与 Python 版逐字节一致（--level 两预设已回归 diff 验证）；
 * --charset 模式段内顺序取数据库频序（Python 版按字集出现序，二者语义
 * 等价：全部视为一级段），不在 GB2312 数据库内的字会被剔除并计数报告。
 *
 * 构建（MinGW 静态链接，除系统 DLL 外零依赖）：
 *   gcc -O2 -s -static -o pinyin2c.exe pinyin2c.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pinyin_db.inc"

/* 唯一的非 ISO-C 调用：创建输出目录（Win/POSIX 各一行，无其它平台 API） */
#ifdef _WIN32
#include <direct.h>
#define PY2C_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define PY2C_MKDIR(p) mkdir((p), 0755)
#endif

#define CJK_FIRST 0x4E00u
#define CJK_LAST 0x9FFFu
#define PATH_MAX_LEN 1024
#define STEM_MAX_LEN 256

/* ---------------- 生成结果模型 ---------------- */

typedef struct
{
    const char *py;    /* 指向数据库音节字符串 */
    unsigned ofs;      /* 段起始（g_pool 下标） */
    unsigned l1_cnt;   /* 一级段字数 */
    unsigned total;    /* 总字数 */
} entry_t;

static unsigned short g_pool[PY_DB_CAND_COUNT]; /* 本次输出的候选池 */
static unsigned g_pool_len;
static entry_t g_entries[PY_DB_SYLL_COUNT];
static int g_n_entries;

/* --charset 模式状态 */
static unsigned char g_cs_set[CJK_LAST - CJK_FIRST + 1];   /* 字集成员位图 */
static unsigned short g_cs_chars[CJK_LAST - CJK_FIRST + 1]; /* 原序去重字集 */
static int g_cs_n;

/* ---------------- 基础工具 ---------------- */

/**
 * @brief 打开文件写（二进制模式保证 LF 行尾），失败即报错退出。
 */
static FILE *xfopen_w(const char *path)
{
    FILE *f = fopen(path, "wb");

    if (f == NULL)
    {
        fprintf(stderr, "error: cannot write %s\n", path);
        exit(1);
    }
    return f;
}

/**
 * @brief 码点转 UTF-8（1~3 字节，返回长度）。
 */
static int cp_to_utf8(unsigned cp, char out[4])
{
    if (cp < 0x80u)
    {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u)
    {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    out[0] = (char)(0xE0u | (cp >> 12));
    out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[2] = (char)(0x80u | (cp & 0x3Fu));
    return 3;
}

/**
 * @brief 把码点序列拼成 UTF-8 字符串（写入调用方缓冲，NUL 结尾）。
 */
static void cps_to_utf8(const unsigned short *cps, unsigned n, char *buf, size_t cap)
{
    size_t pos = 0;
    unsigned i;

    for (i = 0; i < n; i++)
    {
        char tmp[4];
        int len = cp_to_utf8(cps[i], tmp);

        if (pos + (size_t)len + 1 > cap)
            break;
        memcpy(buf + pos, tmp, (size_t)len);
        pos += (size_t)len;
    }
    buf[pos] = '\0';
}

/* ---------------- 预设构建 ---------------- */

/**
 * @brief 按 --level 预设从数据库构建输出表。
 * @param with_l2 1 = l1l2（保留二级段），0 = l1（只留一级段，空音节剔除）。
 */
static void build_level(int with_l2)
{
    int i;

    g_pool_len = 0;
    g_n_entries = 0;
    for (i = 0; i < PY_DB_SYLL_COUNT; i++)
    {
        const py_db_syll_t *s = &py_db_sylls[i];
        unsigned keep = with_l2 ? (unsigned)(s->l1_cnt + s->l2_cnt) : s->l1_cnt;
        entry_t *e;
        unsigned k;

        if (keep == 0)
            continue;
        e = &g_entries[g_n_entries++];
        e->py = s->py;
        e->ofs = g_pool_len;
        e->l1_cnt = s->l1_cnt;
        e->total = keep;
        for (k = 0; k < keep; k++)
            g_pool[g_pool_len++] = py_db_cands[s->ofs + k];
    }
}

/**
 * @brief 按自定义字集构建输出表：数据库候选 ∩ 字集，空音节剔除。
 * @return 字集中不在数据库内（无法注音）的字数。
 * @note 全部字视为一级段（l1_cnt == total），与 Python 版语义一致；
 *       段内顺序取数据库频序。
 */
static int build_charset(void)
{
    static unsigned char db_has[CJK_LAST - CJK_FIRST + 1];
    int i;
    int not_in_db = 0;

    for (i = 0; i < PY_DB_CAND_COUNT; i++)
        db_has[py_db_cands[i] - CJK_FIRST] = 1;

    g_pool_len = 0;
    g_n_entries = 0;
    for (i = 0; i < PY_DB_SYLL_COUNT; i++)
    {
        const py_db_syll_t *s = &py_db_sylls[i];
        unsigned all = (unsigned)(s->l1_cnt + s->l2_cnt);
        unsigned kept = 0;
        unsigned k;

        for (k = 0; k < all; k++)
        {
            unsigned short cp = py_db_cands[s->ofs + k];

            if (g_cs_set[cp - CJK_FIRST])
            {
                if (kept == 0)
                {
                    entry_t *e = &g_entries[g_n_entries];

                    e->py = s->py;
                    e->ofs = g_pool_len;
                }
                g_pool[g_pool_len++] = cp;
                kept++;
            }
        }
        if (kept > 0)
        {
            g_entries[g_n_entries].l1_cnt = kept; /* 自定义字集全部视为一级段 */
            g_entries[g_n_entries].total = kept;
            g_n_entries++;
        }
    }

    for (i = 0; i < g_cs_n; i++)
    {
        if (!db_has[g_cs_chars[i] - CJK_FIRST])
            not_in_db++;
    }
    return not_in_db;
}

/* ---------------- 字集文件解析 ---------------- */

/**
 * @brief 读取 UTF-8 字集文件，去重收集 CJK（U+4E00..U+9FFF）码点。
 * @note 非 CJK 字符（含 BOM、ASCII、4 字节 emoji）一律跳过，与 Python 版
 *       过滤规则一致；非法 UTF-8 序列直接报错退出。
 */
static void load_charset_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long size;
    long i;

    if (f == NULL)
    {
        fprintf(stderr, "error: cannot read %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0)
    {
        fprintf(stderr, "error: cannot stat %s\n", path);
        exit(1);
    }
    buf = (unsigned char *)malloc((size_t)size + 1);
    if (buf == NULL || (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size))
    {
        fprintf(stderr, "error: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);

    g_cs_n = 0;
    i = 0;
    while (i < size)
    {
        unsigned char c0 = buf[i];
        unsigned cp;
        int len;

        if (c0 < 0x80u)
        {
            cp = c0;
            len = 1;
        }
        else if ((c0 & 0xE0u) == 0xC0u)
        {
            len = 2;
            if (i + 1 >= size || (buf[i + 1] & 0xC0u) != 0x80u)
                goto bad_utf8;
            cp = ((unsigned)(c0 & 0x1Fu) << 6) | (buf[i + 1] & 0x3Fu);
        }
        else if ((c0 & 0xF0u) == 0xE0u)
        {
            len = 3;
            if (i + 2 >= size || (buf[i + 1] & 0xC0u) != 0x80u || (buf[i + 2] & 0xC0u) != 0x80u)
                goto bad_utf8;
            cp = ((unsigned)(c0 & 0x0Fu) << 12) |
                 ((unsigned)(buf[i + 1] & 0x3Fu) << 6) | (buf[i + 2] & 0x3Fu);
        }
        else if ((c0 & 0xF8u) == 0xF0u)
        {
            len = 4;
            if (i + 3 >= size || (buf[i + 1] & 0xC0u) != 0x80u ||
                (buf[i + 2] & 0xC0u) != 0x80u || (buf[i + 3] & 0xC0u) != 0x80u)
                goto bad_utf8;
            cp = 0; /* 基本区外，必非 CJK 基本区字符，跳过 */
        }
        else
        {
            goto bad_utf8;
        }

        if (cp >= CJK_FIRST && cp <= CJK_LAST && !g_cs_set[cp - CJK_FIRST])
        {
            g_cs_set[cp - CJK_FIRST] = 1;
            g_cs_chars[g_cs_n++] = (unsigned short)cp;
        }
        i += len;
    }
    free(buf);

    if (g_cs_n == 0)
    {
        fprintf(stderr, "error: 字集文件中没有 CJK 字符\n");
        exit(1);
    }
    return;

bad_utf8:
    fprintf(stderr, "error: invalid UTF-8 in %s (offset %ld)\n", path, i);
    exit(1);
}

/* ---------------- 产物发射（与 Python 版逐字节一致） ---------------- */

/**
 * @brief 发射公共注释头（.c 与 .h 共用）。
 */
static void emit_hdr(FILE *f, const char *preset)
{
    unsigned long l1_total = 0;
    unsigned long l2_total = 0;
    unsigned long rom;
    int i;

    for (i = 0; i < g_n_entries; i++)
    {
        l1_total += g_entries[i].l1_cnt;
        l2_total += g_entries[i].total - g_entries[i].l1_cnt;
    }
    rom = (unsigned long)g_n_entries * 12u + (unsigned long)g_pool_len * 2u;

    fprintf(f, "/* auto-generated by tool/pinyin2c -- DO NOT EDIT\n");
    fprintf(f, " * preset      : %s\n", preset);
    fprintf(f, " * syllables   : %d\n", g_n_entries);
    fprintf(f, " * L1 cands    : %lu\n", l1_total);
    fprintf(f, " * L2 cands    : %lu\n", l2_total);
    fprintf(f, " * ROM approx  : %lu bytes (%d * 12 + %u * 2)\n", rom, g_n_entries, g_pool_len);
    fprintf(f, " * 数据来源：GB2312 字集 + pypinyin 注音（白名单过滤），详见 tool/pinyin2c/README.md\n");
    fprintf(f, " */\n");
}

/**
 * @brief 发射 we_pinyin_table*.h。
 */
static void emit_h(const char *path, const char *preset)
{
    FILE *f = xfopen_w(path);

    emit_hdr(f, preset);
    fprintf(f, "#ifndef __WE_PINYIN_TABLE_H\n");
    fprintf(f, "#define __WE_PINYIN_TABLE_H\n");
    fprintf(f, "\n");
    fprintf(f, "#include <stdint.h>\n");
    fprintf(f, "\n");
    fprintf(f, "/* 二级字段编译期开关：0 = 表内含二级字但引擎只遍历一级段（默认），\n");
    fprintf(f, " * 1 = 编译进二级段遍历支持（运行期再经 we_pinyin_set_l2 开关）。\n");
    fprintf(f, " * 只想彻底省 ROM 请改用 l1 预设表覆盖本表（见 tool/pinyin2c/README.md）。 */\n");
    fprintf(f, "#ifndef WE_PINYIN_ENABLE_L2\n");
    fprintf(f, "#define WE_PINYIN_ENABLE_L2 0\n");
    fprintf(f, "#endif\n");
    fprintf(f, "\n");
    fprintf(f, "#define WE_PINYIN_SYLLABLE_COUNT %dU\n", g_n_entries);
    fprintf(f, "#define WE_PINYIN_CAND_COUNT     %uU\n", g_pool_len);
    fprintf(f, "#define WE_PINYIN_MAX_LEN        6\n");
    fprintf(f, "\n");
    fprintf(f, "/* 排序音节表条目：py 字典序升序；候选段 = we_pinyin_cands[cand_ofs ..)，\n");
    fprintf(f, " * 段内一级字在前（按常用度排序），二级字接在后。 */\n");
    fprintf(f, "typedef struct\n");
    fprintf(f, "{\n");
    fprintf(f, "    char py[7];      /* 音节字母（NUL 结尾） */\n");
    fprintf(f, "    uint16_t cand_ofs;  /* 候选段在 we_pinyin_cands 中的起始下标 */\n");
    fprintf(f, "    uint8_t l1_cnt;     /* 段内一级字数 */\n");
    fprintf(f, "    uint8_t total_cnt;  /* 段内总字数（一级 + 二级） */\n");
    fprintf(f, "} we_pinyin_syllable_t;\n");
    fprintf(f, "\n");
    fprintf(f, "extern const we_pinyin_syllable_t we_pinyin_syllables[WE_PINYIN_SYLLABLE_COUNT];\n");
    fprintf(f, "extern const uint16_t we_pinyin_cands[WE_PINYIN_CAND_COUNT];\n");
    fprintf(f, "\n");
    fprintf(f, "#endif /* __WE_PINYIN_TABLE_H */\n");
    fclose(f);
}

/**
 * @brief 发射 we_pinyin_table*.c（音节表 + 带注释的候选池）。
 */
static void emit_c(const char *path, const char *preset)
{
    FILE *f = xfopen_w(path);
    int i;

    emit_hdr(f, preset);
    fprintf(f, "\n");
    fprintf(f, "#include \"we_pinyin_table.h\"\n");
    fprintf(f, "\n");
    fprintf(f, "const we_pinyin_syllable_t we_pinyin_syllables[WE_PINYIN_SYLLABLE_COUNT] = {\n");
    for (i = 0; i < g_n_entries; i++)
    {
        const entry_t *e = &g_entries[i];

        fprintf(f, "    { \"%s\", %uU, %uU, %uU },\n", e->py, e->ofs, e->l1_cnt, e->total);
    }
    fprintf(f, "};\n");
    fprintf(f, "\n");
    fprintf(f, "const uint16_t we_pinyin_cands[WE_PINYIN_CAND_COUNT] = {\n");
    for (i = 0; i < g_n_entries; i++)
    {
        const entry_t *e = &g_entries[i];
        char chars_txt[1024];
        unsigned k;

        cps_to_utf8(&g_pool[e->ofs], e->total, chars_txt, sizeof(chars_txt));
        fprintf(f, "    /* %s [%u..%u] L1:%u %s */\n",
                e->py, e->ofs, e->ofs + e->total - 1u, e->l1_cnt, chars_txt);
        for (k = 0; k < e->total; k += 12u)
        {
            unsigned end = (k + 12u < e->total) ? k + 12u : e->total;
            unsigned j;

            for (j = k; j < end; j++)
                fprintf(f, "%s0x%04XU,", (j == k) ? "    " : " ", g_pool[e->ofs + j]);
            fprintf(f, "\n");
        }
    }
    fprintf(f, "};\n");
    fclose(f);
}

/**
 * @brief 发射字符全集连续串（charset_*.txt，UTF-8 无换行）。
 */
static void emit_charset_txt(const char *path, const unsigned short *cps, unsigned n)
{
    FILE *f = xfopen_w(path);
    unsigned i;

    for (i = 0; i < n; i++)
    {
        char tmp[4];
        int len = cp_to_utf8(cps[i], tmp);

        fwrite(tmp, 1, (size_t)len, f);
    }
    fclose(f);
}

/**
 * @brief 发射 font2c 取模配置 JSON（--charset 模式；格式对齐
 *        Python json.dumps(ensure_ascii=False, indent=2)）。
 */
static void emit_font2c_json(const char *path, const char *stem)
{
    FILE *f = xfopen_w(path);
    int i;

    fprintf(f, "{\n");
    fprintf(f, "  \"version\": 1,\n");
    fprintf(f, "  \"symbol\": \"msyh_16_4bbp_%s\",\n", stem);
    fprintf(f, "  \"font\": {\n");
    fprintf(f, "    \"file\": \"msyh.ttc\",\n");
    fprintf(f, "    \"size\": 16\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"render\": {\n");
    fprintf(f, "    \"bpp\": 4\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"charset\": {\n");
    fprintf(f, "    \"ranges\": [\n");
    fprintf(f, "      [\n");
    fprintf(f, "        \"U+0020\",\n");
    fprintf(f, "        \"U+007E\"\n");
    fprintf(f, "      ]\n");
    fprintf(f, "    ],\n");
    fprintf(f, "    \"chars\": \"");
    for (i = 0; i < g_cs_n; i++)
    {
        char tmp[4];
        int len = cp_to_utf8(g_cs_chars[i], tmp);

        fwrite(tmp, 1, (size_t)len, f); /* CJK 基本区字符无需 JSON 转义 */
    }
    fprintf(f, "\"\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"deploy\": {\n");
    fprintf(f, "    \"mode\": \"internal\"\n");
    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    fclose(f);
}

/* ---------------- 路径工具 ---------------- */

/**
 * @brief 取路径的文件名主干（去目录、去最后一个扩展名），等价 Path.stem。
 */
static void path_stem(const char *path, char *out, size_t cap)
{
    const char *base = path;
    const char *p;
    const char *dot;
    size_t len;

    for (p = path; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    dot = strrchr(base, '.');
    len = (dot != NULL && dot != base) ? (size_t)(dot - base) : strlen(base);
    if (len >= cap)
        len = cap - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

/**
 * @brief 取可执行文件所在目录（--install 解析控件目录用）。
 */
static void exe_dir(const char *argv0, char *out, size_t cap)
{
    const char *last = NULL;
    const char *p;
    size_t len;

    for (p = argv0; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
            last = p;
    }
    if (last == NULL)
    {
        snprintf(out, cap, ".");
        return;
    }
    len = (size_t)(last - argv0);
    if (len >= cap)
        len = cap - 1;
    memcpy(out, argv0, len);
    out[len] = '\0';
}

/* ---------------- 主流程 ---------------- */

static void usage(void)
{
    fprintf(stderr,
            "usage: pinyin2c (--level l1|l1l2 | --charset <utf8 file>) [--out <dir>] [--install]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *level = NULL;
    const char *charset_path = NULL;
    const char *out_dir = "output";
    int install = 0;
    const char *preset;
    int not_in_db = 0;
    unsigned long l1_total = 0;
    unsigned long l2_total = 0;
    char path_c[PATH_MAX_LEN];
    char path_h[PATH_MAX_LEN];
    char stem[STEM_MAX_LEN];
    int i;

    for (i = 1; i < argc; i++)
    {
        const char *arg = argv[i];
        const char *val = NULL;
        char key[32];
        const char *eq = strchr(arg, '=');

        if (eq != NULL && (size_t)(eq - arg) < sizeof(key))
        {
            memcpy(key, arg, (size_t)(eq - arg));
            key[eq - arg] = '\0';
            val = eq + 1;
            arg = key;
        }

        if (strcmp(arg, "--level") == 0)
            level = (val != NULL) ? val : ((++i < argc) ? argv[i] : NULL);
        else if (strcmp(arg, "--charset") == 0)
            charset_path = (val != NULL) ? val : ((++i < argc) ? argv[i] : NULL);
        else if (strcmp(arg, "--out") == 0)
            out_dir = (val != NULL) ? val : ((++i < argc) ? argv[i] : NULL);
        else if (strcmp(arg, "--install") == 0)
            install = 1;
        else
            usage();
    }
    if ((level == NULL) == (charset_path == NULL) || out_dir == NULL)
        usage(); /* --level 与 --charset 二选一 */
    if (level != NULL && strcmp(level, "l1") != 0 && strcmp(level, "l1l2") != 0)
        usage();

    (void)PY2C_MKDIR(out_dir); /* 已存在时静默失败，后续 fopen 兜底报错 */

    if (level != NULL)
    {
        preset = level;
        build_level(strcmp(level, "l1l2") == 0);
    }
    else
    {
        preset = "custom";
        load_charset_file(charset_path);
        not_in_db = build_charset();
        if (g_n_entries == 0)
        {
            fprintf(stderr, "error: 字集与注音数据库（GB2312 全集）无交集\n");
            exit(1);
        }
    }

    snprintf(path_c, sizeof(path_c), "%s/we_pinyin_table_%s.c", out_dir, preset);
    snprintf(path_h, sizeof(path_h), "%s/we_pinyin_table_%s.h", out_dir, preset);
    emit_c(path_c, preset);
    emit_h(path_h, preset);

    /* 字符全集 / font2c 配置导出（与 Python 版产物集合一致） */
    if (level != NULL)
    {
        char path_txt[PATH_MAX_LEN];

        snprintf(path_txt, sizeof(path_txt), "%s/charset_l1.txt", out_dir);
        emit_charset_txt(path_txt, py_db_charset_l1, PY_DB_CHARSET_L1_COUNT);
        if (strcmp(level, "l1l2") == 0)
        {
            static unsigned short all_cs[PY_DB_CHARSET_L1_COUNT + PY_DB_CHARSET_L2_COUNT];

            memcpy(all_cs, py_db_charset_l1, sizeof(py_db_charset_l1));
            memcpy(all_cs + PY_DB_CHARSET_L1_COUNT, py_db_charset_l2, sizeof(py_db_charset_l2));
            snprintf(path_txt, sizeof(path_txt), "%s/charset_l1l2.txt", out_dir);
            emit_charset_txt(path_txt, all_cs, PY_DB_CHARSET_L1_COUNT + PY_DB_CHARSET_L2_COUNT);
        }
    }
    else
    {
        char path_txt[PATH_MAX_LEN];
        char path_json[PATH_MAX_LEN];

        path_stem(charset_path, stem, sizeof(stem));
        snprintf(path_txt, sizeof(path_txt), "%s/charset_%s.txt", out_dir, stem);
        emit_charset_txt(path_txt, g_cs_chars, (unsigned)g_cs_n);
        snprintf(path_json, sizeof(path_json), "%s/%s_font2c_chars.json", out_dir, stem);
        emit_font2c_json(path_json, stem);
    }

    for (i = 0; i < g_n_entries; i++)
    {
        l1_total += g_entries[i].l1_cnt;
        l2_total += g_entries[i].total - g_entries[i].l1_cnt;
    }
    printf("preset      : %s\n", preset);
    printf("syllables   : %d\n", g_n_entries);
    printf("L1 cands    : %lu\n", l1_total);
    printf("L2 cands    : %lu\n", l2_total);
    printf("ROM approx  : %lu bytes\n",
           (unsigned long)g_n_entries * 12u + (unsigned long)g_pool_len * 2u);
    if (charset_path != NULL)
        printf("not in db   : %d\n", not_in_db);
    printf("output      : we_pinyin_table_%s.c / we_pinyin_table_%s.h\n", preset, preset);

    if (install)
    {
        char dir[PATH_MAX_LEN / 2];
        char dst_c[PATH_MAX_LEN];
        char dst_h[PATH_MAX_LEN];

        exe_dir(argv[0], dir, sizeof(dir));
        snprintf(dst_c, sizeof(dst_c), "%s/../../Core/widgets_preview/ime_pinyin/we_pinyin_table.c", dir);
        snprintf(dst_h, sizeof(dst_h), "%s/../../Core/widgets_preview/ime_pinyin/we_pinyin_table.h", dir);
        emit_c(dst_c, preset);
        emit_h(dst_h, preset);
        printf("installed   : %s\n", dst_c);
    }
    return 0;
}
