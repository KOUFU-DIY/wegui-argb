/**
 * @file  we_pinyin.c
 * @brief 拼音音节检索引擎（preview）：前缀二分 + 区间候选迭代 + 码点转 UTF-8
 *
 * 音节表按字母字典序排列，前缀匹配退化为两次二分（区间下界 / 上界），
 * 单次检索 O(log N) 无逐项扫描。候选池是 uint16 Unicode 码点平铺段，
 * 迭代器只做"段内步进 + 跨段进位"两个整数动作，无乘除无浮点。
 */

#include "we_pinyin.h"
#include <stddef.h> /* NULL（AC5 不会经其他头间接引入） */

/* 运行期二级字总开关（编译期未启用时恒 0，setter 为空操作） */
#if (WE_PINYIN_ENABLE_L2 == 1)
static uint8_t _py_l2_on = 1U;
#endif

/* --------------------------------------------------------------------------
 * 内部辅助
 * -------------------------------------------------------------------------- */

/**
 * @brief 判断音节表第 idx 项与输入串的前缀关系。
 * @param idx 传入：音节表索引。
 * @param input 传入：输入字母串（NUL 结尾）。
 * @return <0 音节整体小于输入；0 音节以输入为前缀；>0 音节大于输入。
 * @note "以输入为前缀"的音节在字典序上恰好构成连续区间：
 *       比较到输入串结束即判 0，否则按首个不同字节的大小判正负。
 */
static int8_t _py_prefix_cmp(uint16_t idx, const char *input)
{
    const char *py = we_pinyin_syllables[idx].py;
    uint8_t i;

    for (i = 0U; input[i] != '\0'; i++)
    {
        if (py[i] == '\0' || (uint8_t)py[i] < (uint8_t)input[i])
            return -1; /* 音节比输入短或字节更小：整体在输入之前 */
        if ((uint8_t)py[i] > (uint8_t)input[i])
            return 1;
    }
    return 0; /* 输入串走完且逐字节相等：音节以输入为前缀 */
}

/**
 * @brief 取音节段的可遍历候选数（按当前二级开关截断）。
 * @param syll_idx 传入：音节表索引。
 * @return 段内可遍历候选数。
 */
static uint8_t _py_seg_limit(uint16_t syll_idx)
{
#if (WE_PINYIN_ENABLE_L2 == 1)
    if (_py_l2_on)
        return we_pinyin_syllables[syll_idx].total_cnt;
#endif
    return we_pinyin_syllables[syll_idx].l1_cnt;
}

/* --------------------------------------------------------------------------
 * 公开 API
 * -------------------------------------------------------------------------- */

/**
 * @brief 二分检索与输入前缀匹配的音节区间。
 * @param input 传入：拼音字母串（NUL 结尾）。
 * @param first 传出：区间首音节索引。
 * @param count 传出：区间音节数（无匹配时写 0）。
 * @return 精确命中时返回音节索引，否则 -1。
 */
int16_t we_pinyin_match(const char *input, uint16_t *first, uint16_t *count)
{
    int32_t lo;
    int32_t hi;
    uint16_t lower;
    uint16_t upper;
    uint8_t len;

    if (count != NULL)
        *count = 0U;
    if (input == NULL || input[0] == '\0')
        return -1;

    for (len = 0U; input[len] != '\0'; len++)
    {
        if (len >= WE_PINYIN_MAX_LEN)
            return -1; /* 超过最长音节，必无匹配 */
    }

    /* 下界：第一个 >= 输入前缀的音节（prefix_cmp >= 0） */
    lo = 0;
    hi = (int32_t)WE_PINYIN_SYLLABLE_COUNT;
    while (lo < hi)
    {
        int32_t mid = lo + ((hi - lo) >> 1);
        if (_py_prefix_cmp((uint16_t)mid, input) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    lower = (uint16_t)lo;

    /* 上界：第一个 > 输入前缀的音节（prefix_cmp > 0） */
    hi = (int32_t)WE_PINYIN_SYLLABLE_COUNT;
    while (lo < hi)
    {
        int32_t mid = lo + ((hi - lo) >> 1);
        if (_py_prefix_cmp((uint16_t)mid, input) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    upper = (uint16_t)lo;

    if (upper == lower)
        return -1; /* 非法前缀：没有任何音节以输入开头 */

    if (first != NULL)
        *first = lower;
    if (count != NULL)
        *count = (uint16_t)(upper - lower);

    /* 精确命中判定：区间首项必是最短匹配，长度相等即完整音节 */
    if (we_pinyin_syllables[lower].py[len] == '\0')
        return (int16_t)lower;
    return -1;
}

/**
 * @brief 初始化候选迭代器（游标拉回区间开头）。
 * @param it 传入传出：迭代器。
 * @param first 传入：区间首音节索引。
 * @param count 传入：区间音节数。
 * @return 无。
 */
void we_pinyin_iter_init(we_pinyin_iter_t *it, uint16_t first, uint16_t count)
{
    if (it == NULL)
        return;

    if (first >= WE_PINYIN_SYLLABLE_COUNT)
        count = 0U; /* 起点越界视为空区间 */
    else if ((uint32_t)first + count > WE_PINYIN_SYLLABLE_COUNT)
        count = (uint16_t)(WE_PINYIN_SYLLABLE_COUNT - first);

    it->syll_first = first;
    it->syll_count = count;
    it->syll_cur = 0U;
    it->cand_cur = 0U;
}

/**
 * @brief 取出下一个候选码点并推进游标。
 * @param it 传入传出：迭代器。
 * @param out_cp 传出：候选 Unicode 码点。
 * @return 1 = 取到候选，0 = 区间遍历完毕。
 */
uint8_t we_pinyin_iter_next(we_pinyin_iter_t *it, uint16_t *out_cp)
{
    if (it == NULL || out_cp == NULL)
        return 0U;

    while (it->syll_cur < it->syll_count)
    {
        uint16_t syll_idx = (uint16_t)(it->syll_first + it->syll_cur);
        uint8_t limit = _py_seg_limit(syll_idx);

        if (it->cand_cur < limit)
        {
            *out_cp = we_pinyin_cands[we_pinyin_syllables[syll_idx].cand_ofs + it->cand_cur];
            it->cand_cur++;
            return 1U;
        }
        it->syll_cur++; /* 本段吐尽，跨到区间内下一音节 */
        it->cand_cur = 0U;
    }
    return 0U;
}

/**
 * @brief 运行期开/关二级字遍历（编译期未启用时为空操作）。
 * @param enable 传入：1 = 吐出二级段，0 = 只吐一级段。
 * @return 无。
 */
void we_pinyin_set_l2(uint8_t enable)
{
#if (WE_PINYIN_ENABLE_L2 == 1)
    _py_l2_on = (enable != 0U) ? 1U : 0U;
#else
    (void)enable;
#endif
}

/**
 * @brief 查询当前二级字遍历开关状态。
 * @return 1 = 开，0 = 关。
 */
uint8_t we_pinyin_get_l2(void)
{
#if (WE_PINYIN_ENABLE_L2 == 1)
    return _py_l2_on;
#else
    return 0U;
#endif
}

/**
 * @brief Unicode 码点转 UTF-8 字节串（NUL 结尾）。
 * @param cp 传入：码点（<= 0xFFFF）。
 * @param out 传出：UTF-8 字节 + NUL（至少 4 字节空间）。
 * @return 编码字节数（1~3），cp 为 0 时返回 0。
 */
uint8_t we_pinyin_cp_to_utf8(uint16_t cp, char out[4])
{
    if (out == NULL)
        return 0U;

    if (cp == 0U)
    {
        out[0] = '\0';
        return 0U;
    }
    if (cp < 0x80U)
    {
        out[0] = (char)cp;
        out[1] = '\0';
        return 1U;
    }
    if (cp < 0x800U)
    {
        out[0] = (char)(0xC0U | (cp >> 6));
        out[1] = (char)(0x80U | (cp & 0x3FU));
        out[2] = '\0';
        return 2U;
    }
    out[0] = (char)(0xE0U | (cp >> 12));
    out[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
    out[2] = (char)(0x80U | (cp & 0x3FU));
    out[3] = '\0';
    return 3U;
}
