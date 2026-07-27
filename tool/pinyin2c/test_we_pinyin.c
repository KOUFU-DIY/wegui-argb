/*
 * test_we_pinyin.c —— we_pinyin 引擎独立单测（脱离 GUI，gcc 直接编译）
 *
 * 用法：
 *   gcc -Wall -Wextra -I<widget_dir> test_we_pinyin.c -o t0 && ./t0        (默认 L2=0)
 *   gcc -DWE_PINYIN_ENABLE_L2=1 -I<widget_dir> test_we_pinyin.c -o t1 && ./t1
 *
 * 单翻译单元：直接 #include 引擎与表的源码。
 */
#include <stdio.h>
#include <string.h>

#include "we_pinyin.c"
#include "we_pinyin_table.c"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, name)                                                     \
    do                                                                        \
    {                                                                         \
        if (cond)                                                             \
        {                                                                     \
            g_pass++;                                                         \
            printf("  PASS  %s\n", name);                                     \
        }                                                                     \
        else                                                                  \
        {                                                                     \
            g_fail++;                                                         \
            printf("  FAIL  %s  (line %d)\n", name, __LINE__);                \
        }                                                                     \
    } while (0)

/* 全区间遍历计数（按当前 L2 开关） */
static unsigned long iter_all_count(void)
{
    we_pinyin_iter_t it;
    uint16_t cp;
    unsigned long n = 0;

    we_pinyin_iter_init(&it, 0U, (uint16_t)WE_PINYIN_SYLLABLE_COUNT);
    while (we_pinyin_iter_next(&it, &cp))
    {
        if (cp == 0U)
            return (unsigned long)-1; /* 池里不允许 0 码点 */
        n++;
    }
    return n;
}

int main(void)
{
    uint16_t first = 0U, count = 0U, cp = 0U;
    int16_t r;
    unsigned long l1_sum = 0, total_sum = 0;
    uint16_t i;
    char buf[4];

    printf("== we_pinyin unit test (WE_PINYIN_ENABLE_L2=%d) ==\n", WE_PINYIN_ENABLE_L2);

    /* ---- 1. 表结构不变量 ---- */
    {
        int sorted = 1, packed = 1, cnt_ok = 1;
        for (i = 0U; i < WE_PINYIN_SYLLABLE_COUNT; i++)
        {
            const we_pinyin_syllable_t *s = &we_pinyin_syllables[i];
            if (i + 1U < WE_PINYIN_SYLLABLE_COUNT &&
                strcmp(s->py, we_pinyin_syllables[i + 1U].py) >= 0)
                sorted = 0;
            if (i + 1U < WE_PINYIN_SYLLABLE_COUNT &&
                (uint32_t)s->cand_ofs + s->total_cnt != we_pinyin_syllables[i + 1U].cand_ofs)
                packed = 0;
            if (s->total_cnt == 0U || s->l1_cnt > s->total_cnt || strlen(s->py) > WE_PINYIN_MAX_LEN)
                cnt_ok = 0;
            l1_sum += s->l1_cnt;
            total_sum += s->total_cnt;
        }
        CHECK(sorted, "syllable table strictly ascending (dictionary order)");
        CHECK(packed, "candidate segments packed contiguously");
        {
            const we_pinyin_syllable_t *last = &we_pinyin_syllables[WE_PINYIN_SYLLABLE_COUNT - 1U];
            CHECK((uint32_t)last->cand_ofs + last->total_cnt == WE_PINYIN_CAND_COUNT,
                  "last segment ends exactly at WE_PINYIN_CAND_COUNT");
        }
        CHECK(cnt_ok, "per-syllable counts sane (total>0, l1<=total, len<=6)");
    }

    /* ---- 2. 精确命中 ---- */
    r = we_pinyin_match("zhong", &first, &count);
    CHECK(r >= 0, "match(\"zhong\") is an exact syllable hit");
    CHECK(r == (int16_t)first, "exact hit index equals range head");
    {
        we_pinyin_iter_t it;
        we_pinyin_iter_init(&it, (uint16_t)r, 1U);
        CHECK(we_pinyin_iter_next(&it, &cp) && cp == 0x4E2DU,
              "first candidate of \"zhong\" is U+4E2D (zhong1, top-freq)");
    }

    r = we_pinyin_match("de", &first, &count);
    CHECK(r >= 0, "match(\"de\") is an exact syllable hit");
    {
        we_pinyin_iter_t it;
        we_pinyin_iter_init(&it, (uint16_t)r, 1U);
        CHECK(we_pinyin_iter_next(&it, &cp) && cp == 0x7684U,
              "first candidate of \"de\" is U+7684 (de, rank-1 char)");
    }

    r = we_pinyin_match("ni", &first, &count);
    {
        we_pinyin_iter_t it;
        we_pinyin_iter_init(&it, (uint16_t)r, 1U);
        CHECK(r >= 0 && we_pinyin_iter_next(&it, &cp) && cp == 0x4F60U,
              "first candidate of \"ni\" is U+4F60");
    }
    r = we_pinyin_match("hao", &first, &count);
    {
        we_pinyin_iter_t it;
        we_pinyin_iter_init(&it, (uint16_t)r, 1U);
        CHECK(r >= 0 && we_pinyin_iter_next(&it, &cp) && cp == 0x597DU,
              "first candidate of \"hao\" is U+597D");
    }

    /* "a" 精确命中且其前缀区间应为 a/ai/an/ang/ao 五个音节 */
    r = we_pinyin_match("a", &first, &count);
    CHECK(r >= 0 && count == 5U &&
              strcmp(we_pinyin_syllables[first].py, "a") == 0 &&
              strcmp(we_pinyin_syllables[first + 4U].py, "ao") == 0,
          "match(\"a\") exact, prefix range = {a,ai,an,ang,ao}");

    /* ---- 3. 前缀区间（联想） ---- */
    r = we_pinyin_match("zh", &first, &count);
    CHECK(r == -1 && count > 0U, "match(\"zh\") no exact hit but non-empty range");
    {
        int all_zh = 1;
        for (i = 0U; i < count; i++)
        {
            if (strncmp(we_pinyin_syllables[first + i].py, "zh", 2U) != 0)
                all_zh = 0;
        }
        CHECK(all_zh, "every syllable in \"zh\" range starts with zh");
        CHECK(first == 0U || strncmp(we_pinyin_syllables[first - 1U].py, "zh", 2U) != 0,
              "syllable before range does not start with zh");
        CHECK(first + count == WE_PINYIN_SYLLABLE_COUNT ||
                  strncmp(we_pinyin_syllables[first + count].py, "zh", 2U) != 0,
              "syllable after range does not start with zh");
        CHECK(strcmp(we_pinyin_syllables[first].py, "zha") == 0,
              "\"zh\" range begins at \"zha\"");
        CHECK(strcmp(we_pinyin_syllables[first + count - 1U].py, "zhuo") == 0,
              "\"zh\" range ends at \"zhuo\"");
    }

    r = we_pinyin_match("zho", &first, &count);
    CHECK(r == -1 && count == 2U &&
              strcmp(we_pinyin_syllables[first].py, "zhong") == 0 &&
              strcmp(we_pinyin_syllables[first + 1U].py, "zhou") == 0,
          "match(\"zho\") range = {zhong, zhou}");

    /* ---- 4. 非法输入 ---- */
    r = we_pinyin_match("xyz", &first, &count);
    CHECK(r == -1 && count == 0U, "match(\"xyz\") = -1, empty range");
    r = we_pinyin_match("i", &first, &count);
    CHECK(r == -1 && count == 0U, "match(\"i\") = -1 (no i-initial syllable)");
    r = we_pinyin_match("", &first, &count);
    CHECK(r == -1 && count == 0U, "match(\"\") = -1");
    r = we_pinyin_match(NULL, &first, &count);
    CHECK(r == -1 && count == 0U, "match(NULL) = -1");
    r = we_pinyin_match("zhuangg", &first, &count);
    CHECK(r == -1 && count == 0U, "match(7-letter input) = -1 (over MAX_LEN)");

    /* ---- 5. 迭代器完整遍历 ---- */
#if (WE_PINYIN_ENABLE_L2 == 1)
    CHECK(we_pinyin_get_l2() == 1U, "L2 build: runtime switch defaults to ON");
    CHECK(iter_all_count() == total_sum, "L2 ON: full traversal == sum(total_cnt)");
    we_pinyin_set_l2(0U);
    CHECK(we_pinyin_get_l2() == 0U, "set_l2(0) turns switch off");
    CHECK(iter_all_count() == l1_sum, "L2 OFF: full traversal == sum(l1_cnt)");
    CHECK(total_sum > l1_sum, "table actually contains an L2 segment");
    we_pinyin_set_l2(1U);
    CHECK(iter_all_count() == total_sum, "set_l2(1) restores full traversal");
#else
    CHECK(we_pinyin_get_l2() == 0U, "default build: L2 switch reads 0");
    CHECK(iter_all_count() == l1_sum, "default build: traversal == sum(l1_cnt)");
    we_pinyin_set_l2(1U);
    CHECK(we_pinyin_get_l2() == 0U, "set_l2(1) is a no-op when L2 not compiled");
    CHECK(iter_all_count() == l1_sum, "traversal unchanged after no-op set_l2");
#endif

    /* 空区间迭代 */
    {
        we_pinyin_iter_t it;
        we_pinyin_iter_init(&it, 5U, 0U);
        CHECK(we_pinyin_iter_next(&it, &cp) == 0U, "empty-range iterator yields nothing");
        we_pinyin_iter_init(&it, (uint16_t)WE_PINYIN_SYLLABLE_COUNT, 3U);
        CHECK(we_pinyin_iter_next(&it, &cp) == 0U, "out-of-range iter_init clamps to empty");
    }

    /* ---- 6. UTF-8 编码 ---- */
    CHECK(we_pinyin_cp_to_utf8(0x4E2DU, buf) == 3U &&
              (uint8_t)buf[0] == 0xE4U && (uint8_t)buf[1] == 0xB8U &&
              (uint8_t)buf[2] == 0xADU && buf[3] == '\0',
          "cp_to_utf8(U+4E2D) == E4 B8 AD");
    CHECK(we_pinyin_cp_to_utf8(0x41U, buf) == 1U && buf[0] == 'A' && buf[1] == '\0',
          "cp_to_utf8(U+0041) == \"A\"");
    CHECK(we_pinyin_cp_to_utf8(0x00E9U, buf) == 2U &&
              (uint8_t)buf[0] == 0xC3U && (uint8_t)buf[1] == 0xA9U && buf[2] == '\0',
          "cp_to_utf8(U+00E9) == C3 A9");
    CHECK(we_pinyin_cp_to_utf8(0U, buf) == 0U && buf[0] == '\0',
          "cp_to_utf8(0) == empty");

    printf("== result: %d passed, %d failed ==\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
