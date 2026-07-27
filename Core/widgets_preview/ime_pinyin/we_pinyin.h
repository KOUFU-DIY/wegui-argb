#ifndef __WE_PINYIN_H
#define __WE_PINYIN_H

#include <stdint.h>
#include "we_pinyin_table.h"

/* --------------------------------------------------------------------------
 * 拼音音节检索引擎（纯查表，零 GUI 依赖）—— preview 孵化区
 *
 * 数据由 tool/pinyin2c/gen_pinyin_table.py 生成（we_pinyin_table.c/.h）：
 *   1. we_pinyin_syllables[]：按字母字典序排列的音节表（~400 项）；
 *   2. we_pinyin_cands[]：候选池，uint16 Unicode 码点平铺（非 UTF-8、
 *      非 GB2312 内码），每音节一段，段内一级字在前（按常用度排序）、
 *      二级字接在后。
 *
 * 检索模型：
 *   we_pinyin_match 对输入串做二分，求出"以输入为前缀"的音节区间
 *   [first, first+count)；输入恰为完整音节时返回其索引（因前缀区间按
 *   字典序排列，精确命中音节必为区间首项）。候选经 we_pinyin_iter_t
 *   依区间顺序逐个吐出：先吐区间首音节整段，再吐后续联想音节段。
 *
 * 二级字开关：
 *   编译期 WE_PINYIN_ENABLE_L2（见 we_pinyin_table.h，默认 0）控制二级
 *   遍历代码是否编入；编入后还有运行期 we_pinyin_set_l2 总开关（默认开）。
 *   未编入时 set_l2 为空操作，迭代器恒只走一级段。
 *
 * 纯整数、零 malloc、全 const ROM 表；可脱离 GUI 独立编译做单测。
 * -------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* 候选迭代器：绑定一个音节区间，按区间顺序遍历各音节候选段 */
typedef struct
{
    uint16_t syll_first; /* 区间首音节索引 */
    uint16_t syll_count; /* 区间音节数 */
    uint16_t syll_cur;   /* 游标：当前音节（相对 first 的偏移） */
    uint8_t cand_cur;    /* 游标：当前音节段内的候选下标 */
} we_pinyin_iter_t;

/**
 * @brief 二分检索与输入前缀匹配的音节区间。
 * @param input 传入：拼音字母串（小写 a-z，ü 写作 v，NUL 结尾）。
 * @param first 传出：区间首音节索引（无匹配时不写入）。
 * @param count 传出：区间音节数（无匹配时写 0，可传 NULL）。
 * @return 输入恰为完整音节时返回其音节索引（>=0），否则 -1。
 * @note 精确命中的音节按字典序必为区间首项；count==0 表示非法前缀。
 *       input 为 NULL/空串/超长（> WE_PINYIN_MAX_LEN）一律按无匹配处理。
 */
int16_t we_pinyin_match(const char *input, uint16_t *first, uint16_t *count);

/**
 * @brief 初始化候选迭代器，绑定音节区间并把游标拉回区间开头。
 * @param it 传入传出：迭代器。
 * @param first 传入：区间首音节索引。
 * @param count 传入：区间音节数（0 = 空区间，next 恒返回 0）。
 * @return 无。
 */
void we_pinyin_iter_init(we_pinyin_iter_t *it, uint16_t first, uint16_t count);

/**
 * @brief 取出下一个候选码点并推进游标。
 * @param it 传入传出：迭代器。
 * @param out_cp 传出：候选 Unicode 码点。
 * @return 1 = 取到候选，0 = 区间遍历完毕。
 * @note 每音节段先吐一级字（常用度序），二级开关打开时接着吐二级字；
 *       段尽后自动跨到区间内下一音节。
 */
uint8_t we_pinyin_iter_next(we_pinyin_iter_t *it, uint16_t *out_cp);

/**
 * @brief 运行期开/关二级字遍历。
 * @param enable 传入：1 = 迭代器吐出二级段，0 = 只吐一级段。
 * @return 无。
 * @note WE_PINYIN_ENABLE_L2 == 0 时本函数为空操作（开关恒为 0）。
 *       开关是引擎级全局状态，影响所有迭代器。
 */
void we_pinyin_set_l2(uint8_t enable);

/**
 * @brief 查询当前二级字遍历开关状态。
 * @return 1 = 开，0 = 关（未编译二级支持时恒 0）。
 */
uint8_t we_pinyin_get_l2(void);

/**
 * @brief Unicode 码点转 UTF-8 字节串。
 * @param cp 传入：码点（<= 0xFFFF；CJK 基本区为 3 字节编码）。
 * @param out 传出：UTF-8 字节 + NUL 结尾（至少 4 字节空间）。
 * @return 编码字节数（1~3），cp 为 0 时返回 0 并写空串。
 */
uint8_t we_pinyin_cp_to_utf8(uint16_t cp, char out[4]);

#ifdef __cplusplus
}
#endif

#endif /* __WE_PINYIN_H */
