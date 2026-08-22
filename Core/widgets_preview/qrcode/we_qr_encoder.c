#include "we_qr_encoder.h"
#include <string.h>

/* --------------------------------------------------------------------------
 * we_qr_encoder —— QR 编码器实现（byte mode / ECC M / 版本 1~4）
 *
 * 流水线：
 *   1. 选版本：按内容字节数选能装下的最小版本（容量 = 数据码字 - 2）；
 *   2. 组位流：0100 模式头 + 8bit 长度 + 数据 + 终止符 + 0xEC/0x11 补齐；
 *   3. RS 纠错：GF(256) 查表乘法，生成多项式按 ECC 字节数现算，
 *      v4-M 拆 2 块分别求余后按标准交织（等长块，逐字节轮流取）；
 *   4. 摆版面：finder+分隔符 → timing → 对齐图案(v2+) → 格式位预留 → 蛇形填数据；
 *   5. 选掩码：0~7 全试，格式位同步写入后按简化惩罚分（仅规则 1）择优。
 *
 * 工作区全部为文件级 static（不可重入）：
 *   位打包矩阵 2 x 33x5 字节 + 码字缓冲约 230 字节（合计约 0.55KB RAM）；
 *   GF(256) 指数/对数双表为 const 常量表（512 字节驻 ROM）。
 * -------------------------------------------------------------------------- */

/* ---------------- 版本参数表（ECC 等级 M） ---------------- */
typedef struct
{
    uint8_t size;        /* 矩阵边长（模块数） */
    uint8_t total_cw;    /* 总码字数（数据+ECC） */
    uint8_t data_cw;     /* 数据码字数（全部块合计） */
    uint8_t ecc_per_blk; /* 每块 ECC 码字数 */
    uint8_t blocks;      /* RS 块数 */
    uint8_t align_pos;   /* 对齐图案中心行列坐标（0=无对齐图案） */
} qr_ver_info_t;

static const qr_ver_info_t k_ver[WE_QR_VERSION_MAX] = {
    { 21U,  26U, 16U, 10U, 1U,  0U }, /* v1-M：容量 14 字节 */
    { 25U,  44U, 28U, 16U, 1U, 18U }, /* v2-M：容量 26 字节 */
    { 29U,  70U, 44U, 26U, 1U, 22U }, /* v3-M：容量 42 字节 */
    { 33U, 100U, 64U, 18U, 2U, 26U }, /* v4-M：容量 62 字节（2 块交织） */
};

/* 格式信息 15bit 打表（ECC M + 掩码 0~7，BCH(15,5) 编码并异或 0x5412 后的结果，
 * 对应规范 Table C.1 中 M 行："101010000010010" 等，bit14 为串首位） */
static const uint16_t k_fmt_m[8] = {
    0x5412U, 0x5125U, 0x5E7CU, 0x5B4BU,
    0x45F9U, 0x40CEU, 0x4F97U, 0x4AA0U,
};

/* ---------------- GF(256) 指数/对数常量表（本原多项式 0x11D，驻 ROM） ----------------
 * 生成算法：x=1；i=0..254 依次 exp[i]=x、log[x]=i、x<<=1、溢出 0x100 即异或 0x11D；
 * exp[255]=1 为哨兵（正常查表路径不会读到），log[0]=0 恒不被查询。
 * 表内容与上述算法运行时填表逐字节一致。 */
static const uint8_t s_gf_exp[256] = {
    0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U, 0x1DU, 0x3AU, 0x74U, 0xE8U, 0xCDU, 0x87U, 0x13U, 0x26U,
    0x4CU, 0x98U, 0x2DU, 0x5AU, 0xB4U, 0x75U, 0xEAU, 0xC9U, 0x8FU, 0x03U, 0x06U, 0x0CU, 0x18U, 0x30U, 0x60U, 0xC0U,
    0x9DU, 0x27U, 0x4EU, 0x9CU, 0x25U, 0x4AU, 0x94U, 0x35U, 0x6AU, 0xD4U, 0xB5U, 0x77U, 0xEEU, 0xC1U, 0x9FU, 0x23U,
    0x46U, 0x8CU, 0x05U, 0x0AU, 0x14U, 0x28U, 0x50U, 0xA0U, 0x5DU, 0xBAU, 0x69U, 0xD2U, 0xB9U, 0x6FU, 0xDEU, 0xA1U,
    0x5FU, 0xBEU, 0x61U, 0xC2U, 0x99U, 0x2FU, 0x5EU, 0xBCU, 0x65U, 0xCAU, 0x89U, 0x0FU, 0x1EU, 0x3CU, 0x78U, 0xF0U,
    0xFDU, 0xE7U, 0xD3U, 0xBBU, 0x6BU, 0xD6U, 0xB1U, 0x7FU, 0xFEU, 0xE1U, 0xDFU, 0xA3U, 0x5BU, 0xB6U, 0x71U, 0xE2U,
    0xD9U, 0xAFU, 0x43U, 0x86U, 0x11U, 0x22U, 0x44U, 0x88U, 0x0DU, 0x1AU, 0x34U, 0x68U, 0xD0U, 0xBDU, 0x67U, 0xCEU,
    0x81U, 0x1FU, 0x3EU, 0x7CU, 0xF8U, 0xEDU, 0xC7U, 0x93U, 0x3BU, 0x76U, 0xECU, 0xC5U, 0x97U, 0x33U, 0x66U, 0xCCU,
    0x85U, 0x17U, 0x2EU, 0x5CU, 0xB8U, 0x6DU, 0xDAU, 0xA9U, 0x4FU, 0x9EU, 0x21U, 0x42U, 0x84U, 0x15U, 0x2AU, 0x54U,
    0xA8U, 0x4DU, 0x9AU, 0x29U, 0x52U, 0xA4U, 0x55U, 0xAAU, 0x49U, 0x92U, 0x39U, 0x72U, 0xE4U, 0xD5U, 0xB7U, 0x73U,
    0xE6U, 0xD1U, 0xBFU, 0x63U, 0xC6U, 0x91U, 0x3FU, 0x7EU, 0xFCU, 0xE5U, 0xD7U, 0xB3U, 0x7BU, 0xF6U, 0xF1U, 0xFFU,
    0xE3U, 0xDBU, 0xABU, 0x4BU, 0x96U, 0x31U, 0x62U, 0xC4U, 0x95U, 0x37U, 0x6EU, 0xDCU, 0xA5U, 0x57U, 0xAEU, 0x41U,
    0x82U, 0x19U, 0x32U, 0x64U, 0xC8U, 0x8DU, 0x07U, 0x0EU, 0x1CU, 0x38U, 0x70U, 0xE0U, 0xDDU, 0xA7U, 0x53U, 0xA6U,
    0x51U, 0xA2U, 0x59U, 0xB2U, 0x79U, 0xF2U, 0xF9U, 0xEFU, 0xC3U, 0x9BU, 0x2BU, 0x56U, 0xACU, 0x45U, 0x8AU, 0x09U,
    0x12U, 0x24U, 0x48U, 0x90U, 0x3DU, 0x7AU, 0xF4U, 0xF5U, 0xF7U, 0xF3U, 0xFBU, 0xEBU, 0xCBU, 0x8BU, 0x0BU, 0x16U,
    0x2CU, 0x58U, 0xB0U, 0x7DU, 0xFAU, 0xE9U, 0xCFU, 0x83U, 0x1BU, 0x36U, 0x6CU, 0xD8U, 0xADU, 0x47U, 0x8EU, 0x01U,
};
static const uint8_t s_gf_log[256] = {
    0x00U, 0x00U, 0x01U, 0x19U, 0x02U, 0x32U, 0x1AU, 0xC6U, 0x03U, 0xDFU, 0x33U, 0xEEU, 0x1BU, 0x68U, 0xC7U, 0x4BU,
    0x04U, 0x64U, 0xE0U, 0x0EU, 0x34U, 0x8DU, 0xEFU, 0x81U, 0x1CU, 0xC1U, 0x69U, 0xF8U, 0xC8U, 0x08U, 0x4CU, 0x71U,
    0x05U, 0x8AU, 0x65U, 0x2FU, 0xE1U, 0x24U, 0x0FU, 0x21U, 0x35U, 0x93U, 0x8EU, 0xDAU, 0xF0U, 0x12U, 0x82U, 0x45U,
    0x1DU, 0xB5U, 0xC2U, 0x7DU, 0x6AU, 0x27U, 0xF9U, 0xB9U, 0xC9U, 0x9AU, 0x09U, 0x78U, 0x4DU, 0xE4U, 0x72U, 0xA6U,
    0x06U, 0xBFU, 0x8BU, 0x62U, 0x66U, 0xDDU, 0x30U, 0xFDU, 0xE2U, 0x98U, 0x25U, 0xB3U, 0x10U, 0x91U, 0x22U, 0x88U,
    0x36U, 0xD0U, 0x94U, 0xCEU, 0x8FU, 0x96U, 0xDBU, 0xBDU, 0xF1U, 0xD2U, 0x13U, 0x5CU, 0x83U, 0x38U, 0x46U, 0x40U,
    0x1EU, 0x42U, 0xB6U, 0xA3U, 0xC3U, 0x48U, 0x7EU, 0x6EU, 0x6BU, 0x3AU, 0x28U, 0x54U, 0xFAU, 0x85U, 0xBAU, 0x3DU,
    0xCAU, 0x5EU, 0x9BU, 0x9FU, 0x0AU, 0x15U, 0x79U, 0x2BU, 0x4EU, 0xD4U, 0xE5U, 0xACU, 0x73U, 0xF3U, 0xA7U, 0x57U,
    0x07U, 0x70U, 0xC0U, 0xF7U, 0x8CU, 0x80U, 0x63U, 0x0DU, 0x67U, 0x4AU, 0xDEU, 0xEDU, 0x31U, 0xC5U, 0xFEU, 0x18U,
    0xE3U, 0xA5U, 0x99U, 0x77U, 0x26U, 0xB8U, 0xB4U, 0x7CU, 0x11U, 0x44U, 0x92U, 0xD9U, 0x23U, 0x20U, 0x89U, 0x2EU,
    0x37U, 0x3FU, 0xD1U, 0x5BU, 0x95U, 0xBCU, 0xCFU, 0xCDU, 0x90U, 0x87U, 0x97U, 0xB2U, 0xDCU, 0xFCU, 0xBEU, 0x61U,
    0xF2U, 0x56U, 0xD3U, 0xABU, 0x14U, 0x2AU, 0x5DU, 0x9EU, 0x84U, 0x3CU, 0x39U, 0x53U, 0x47U, 0x6DU, 0x41U, 0xA2U,
    0x1FU, 0x2DU, 0x43U, 0xD8U, 0xB7U, 0x7BU, 0xA4U, 0x76U, 0xC4U, 0x17U, 0x49U, 0xECU, 0x7FU, 0x0CU, 0x6FU, 0xF6U,
    0x6CU, 0xA1U, 0x3BU, 0x52U, 0x29U, 0x9DU, 0x55U, 0xAAU, 0xFBU, 0x60U, 0x86U, 0xB1U, 0xBBU, 0xCCU, 0x3EU, 0x5AU,
    0xCBU, 0x59U, 0x5FU, 0xB0U, 0x9CU, 0xA9U, 0xA0U, 0x51U, 0x0BU, 0xF5U, 0x16U, 0xEBU, 0x7AU, 0x75U, 0x2CU, 0xD7U,
    0x4FU, 0xAEU, 0xD5U, 0xE9U, 0xE6U, 0xE7U, 0xADU, 0xE8U, 0x74U, 0xD6U, 0xF4U, 0xEAU, 0xA8U, 0x50U, 0x58U, 0xAFU,
};

/* ---------------- 静态工作区（33x33 上限，矩阵按行位打包：每行 5 字节） ---------------- */
static uint8_t s_mod[WE_QR_MODULES_MAX][WE_QR_ROW_BYTES]; /* 模块颜色：位 1=暗 */
static uint8_t s_fun[WE_QR_MODULES_MAX][WE_QR_ROW_BYTES]; /* 功能模块标志：位 1=数据不可占用 */

/* 位打包矩阵访问：行内字节 = c >> 3，位 = 1 << (c & 7)（与 out_bits/we_qr_bit_get 同布局） */
static __inline uint8_t qr_m_get(uint8_t m[][WE_QR_ROW_BYTES], uint8_t r, uint8_t c)
{
    return (uint8_t)((m[r][c >> 3] >> (c & 7U)) & 1U);
}

static __inline void qr_m_put(uint8_t m[][WE_QR_ROW_BYTES], uint8_t r, uint8_t c, uint8_t v)
{
    if (v != 0U)
        m[r][c >> 3] |= (uint8_t)(1U << (c & 7U));
    else
        m[r][c >> 3] &= (uint8_t)~(1U << (c & 7U));
}

static __inline void qr_m_flip(uint8_t m[][WE_QR_ROW_BYTES], uint8_t r, uint8_t c)
{
    m[r][c >> 3] ^= (uint8_t)(1U << (c & 7U));
}

static uint8_t s_data_cw[64];  /* 数据码字（v4 上限 64） */
static uint8_t s_ecc[36];      /* ECC 码字（v4 上限 2 块 x 18） */
static uint8_t s_all_cw[100];  /* 交织后的最终码字序列（v4 上限 100） */
static uint8_t s_gen[26];      /* RS 生成多项式系数（不含最高位 1，v3 上限 26 阶） */
static uint32_t s_bit_pos;     /* 位流写游标 */

/**
 * @brief GF(256) 乘法（查表：exp[(log a + log b) mod 255]）
 * @param a 传入：乘数 A
 * @param b 传入：乘数 B
 * @return 乘积（GF(256) 域内）
 */
static uint8_t qr_gf_mul(uint8_t a, uint8_t b)
{
    uint16_t s;

    if (a == 0U || b == 0U)
        return 0U;
    s = (uint16_t)((uint16_t)s_gf_log[a] + (uint16_t)s_gf_log[b]);
    if (s >= 255U)
        s -= 255U;
    return s_gf_exp[s];
}

/**
 * @brief 计算 RS 生成多项式系数：连乘 (x - α^i)，i = 0..degree-1
 * @param degree 传入：ECC 码字数（多项式阶数，<= 26）
 * @return 无
 * @note 结果写入 s_gen[0..degree-1]（降幂排列、不含最高位系数 1）。
 */
static void qr_rs_gen_poly(uint8_t degree)
{
    uint8_t i;
    uint8_t j;
    uint8_t root = 1U;

    memset(s_gen, 0, sizeof(s_gen));
    s_gen[degree - 1U] = 1U; /* 初始多项式 = 1 */
    for (i = 0U; i < degree; i++)
    {
        /* 整体乘 (x - root)：先乘 root，再错位异或（GF 减法即异或） */
        for (j = 0U; j < degree; j++)
        {
            s_gen[j] = qr_gf_mul(s_gen[j], root);
            if ((uint8_t)(j + 1U) < degree)
                s_gen[j] ^= s_gen[j + 1U];
        }
        root = qr_gf_mul(root, 2U);
    }
}

/**
 * @brief 多项式长除求 RS 余式（即 ECC 码字）
 * @param data 传入：数据码字
 * @param data_len 传入：数据码字数
 * @param degree 传入：ECC 码字数（须先 qr_rs_gen_poly(degree)）
 * @param out_ecc 传出：degree 个 ECC 码字
 * @return 无
 */
static void qr_rs_remainder(const uint8_t *data, uint8_t data_len,
                            uint8_t degree, uint8_t *out_ecc)
{
    uint8_t i;
    uint8_t j;

    memset(out_ecc, 0, degree);
    for (i = 0U; i < data_len; i++)
    {
        uint8_t factor = (uint8_t)(data[i] ^ out_ecc[0]);
        memmove(out_ecc, out_ecc + 1, (size_t)(degree - 1U));
        out_ecc[degree - 1U] = 0U;
        for (j = 0U; j < degree; j++)
            out_ecc[j] ^= qr_gf_mul(s_gen[j], factor);
    }
}

/**
 * @brief 向数据位流追加 value 的低 nbits 位（MSB 先行）
 * @param value 传入：待写入值
 * @param nbits 传入：位数（<= 16）
 * @return 无
 */
static void qr_append_bits(uint16_t value, uint8_t nbits)
{
    int8_t i;

    for (i = (int8_t)(nbits - 1); i >= 0; i--)
    {
        if (((value >> i) & 1U) != 0U)
            s_data_cw[s_bit_pos >> 3] |= (uint8_t)(0x80U >> (s_bit_pos & 7U));
        s_bit_pos++;
    }
}

/**
 * @brief 写入一个功能模块（同时置功能标志，数据填充阶段跳过）
 * @param r 传入：模块行
 * @param c 传入：模块列
 * @param dark 传入：1=暗模块，0=亮模块
 * @return 无
 */
static void qr_set_fun(uint8_t r, uint8_t c, uint8_t dark)
{
    qr_m_put(s_mod, r, c, dark);
    qr_m_put(s_fun, r, c, 1U);
}

/**
 * @brief 绘制一个 finder 图案（7x7 回字）及其外圈 1 模块分隔符
 * @param size 传入：矩阵边长
 * @param r0 传入：finder 左上角行
 * @param c0 传入：finder 左上角列
 * @return 无
 * @note 按切比雪夫距离判色：d==2 为白环，d 为 0/1/3 为暗；-1/7 圈为分隔符（亮）。
 */
static void qr_draw_finder(uint8_t size, int16_t r0, int16_t c0)
{
    int16_t dr;
    int16_t dc;

    for (dr = -1; dr <= 7; dr++)
    {
        for (dc = -1; dc <= 7; dc++)
        {
            int16_t r = (int16_t)(r0 + dr);
            int16_t c = (int16_t)(c0 + dc);

            if (r < 0 || r >= (int16_t)size || c < 0 || c >= (int16_t)size)
                continue;
            if (dr >= 0 && dr <= 6 && dc >= 0 && dc <= 6)
            {
                int16_t ar = (dr > 3) ? (int16_t)(dr - 3) : (int16_t)(3 - dr);
                int16_t ac = (dc > 3) ? (int16_t)(dc - 3) : (int16_t)(3 - dc);
                int16_t d = (ar > ac) ? ar : ac;
                qr_set_fun((uint8_t)r, (uint8_t)c, (uint8_t)(d != 2));
            }
            else
            {
                qr_set_fun((uint8_t)r, (uint8_t)c, 0U); /* 分隔符固定亮 */
            }
        }
    }
}

/**
 * @brief 绘制对齐图案（5x5 回字，v2+ 各一个）
 * @param a 传入：对齐图案中心行列坐标
 * @return 无
 */
static void qr_draw_align(uint8_t a)
{
    int16_t dr;
    int16_t dc;

    for (dr = -2; dr <= 2; dr++)
    {
        for (dc = -2; dc <= 2; dc++)
        {
            int16_t ar = (dr < 0) ? (int16_t)(-dr) : dr;
            int16_t ac = (dc < 0) ? (int16_t)(-dc) : dc;
            int16_t d = (ar > ac) ? ar : ac;
            qr_set_fun((uint8_t)((int16_t)a + dr), (uint8_t)((int16_t)a + dc),
                       (uint8_t)(d != 1));
        }
    }
}

/**
 * @brief 写入两份 15bit 格式信息 + 固定暗模块（同时完成功能区预留）
 * @param size 传入：矩阵边长
 * @param fmt 传入：15bit 格式信息值（k_fmt_m[掩码号]）
 * @return 无
 * @note bit i 取 (fmt >> i) & 1；摆位与规范一致：
 *       第一份绕左上 finder，第二份拆在右上横条与左下竖条。
 */
static void qr_draw_format(uint8_t size, uint16_t fmt)
{
    uint8_t i;

    /* 第一份：绕左上 finder */
    for (i = 0U; i <= 5U; i++)
        qr_set_fun(i, 8U, (uint8_t)((fmt >> i) & 1U));
    qr_set_fun(7U, 8U, (uint8_t)((fmt >> 6) & 1U));
    qr_set_fun(8U, 8U, (uint8_t)((fmt >> 7) & 1U));
    qr_set_fun(8U, 7U, (uint8_t)((fmt >> 8) & 1U));
    for (i = 9U; i <= 14U; i++)
        qr_set_fun(8U, (uint8_t)(14U - i), (uint8_t)((fmt >> i) & 1U));

    /* 第二份：右上横条（bit0~7）+ 左下竖条（bit8~14） */
    for (i = 0U; i <= 7U; i++)
        qr_set_fun(8U, (uint8_t)(size - 1U - i), (uint8_t)((fmt >> i) & 1U));
    for (i = 8U; i <= 14U; i++)
        qr_set_fun((uint8_t)(size - 15U + i), 8U, (uint8_t)((fmt >> i) & 1U));

    /* 固定暗模块：(size-8, 8)，即规范中的 (4*版本+9, 8) */
    qr_set_fun((uint8_t)(size - 8U), 8U, 1U);
}

/**
 * @brief 构建全部功能图案并预留格式位区域
 * @param ver_idx 传入：版本索引（0 基，0=v1）
 * @return 无
 */
static void qr_build_function_patterns(uint8_t ver_idx)
{
    const qr_ver_info_t *vi = &k_ver[ver_idx];
    uint8_t size = vi->size;
    uint8_t i;

    memset(s_mod, 0, sizeof(s_mod));
    memset(s_fun, 0, sizeof(s_fun));

    /* 三个 finder + 分隔符 */
    qr_draw_finder(size, 0, 0);
    qr_draw_finder(size, 0, (int16_t)(size - 7U));
    qr_draw_finder(size, (int16_t)(size - 7U), 0);

    /* timing：第 6 行/列，明暗交替（偶坐标为暗） */
    for (i = 8U; i <= (uint8_t)(size - 9U); i++)
    {
        uint8_t dark = (uint8_t)((i & 1U) == 0U);
        qr_set_fun(6U, i, dark);
        qr_set_fun(i, 6U, dark);
    }

    /* 对齐图案（v2+ 一个，中心 align_pos） */
    if (vi->align_pos != 0U)
        qr_draw_align(vi->align_pos);

    /* 格式位区域预留（值稍后按所选掩码覆盖；此处只为标功能位） */
    qr_draw_format(size, 0U);
}

/**
 * @brief 将交织后的码字按蛇形（zigzag）路径填入数据模块
 * @param size 传入：矩阵边长
 * @param total_cw 传入：总码字数
 * @return 无
 * @note 从右下角开始按 2 列一组自右向左扫描，跳过第 6 列（timing）；
 *       码字位耗尽后剩余的 remainder 模块保持 0（亮），符合规范。
 */
static void qr_place_data(uint8_t size, uint8_t total_cw)
{
    uint32_t bit_idx = 0U;
    uint32_t bit_total = (uint32_t)total_cw * 8U;
    int16_t right;

    for (right = (int16_t)(size - 1U); right >= 1; right -= 2)
    {
        int16_t vert;

        if (right == 6)
            right = 5; /* 跳过 timing 列 */
        for (vert = 0; vert < (int16_t)size; vert++)
        {
            int16_t j;

            for (j = 0; j < 2; j++)
            {
                int16_t c = (int16_t)(right - j);
                uint8_t upward = (uint8_t)((((uint16_t)(right + 1)) & 2U) == 0U);
                int16_t r = upward ? (int16_t)(size - 1 - vert) : vert;

                if (qr_m_get(s_fun, (uint8_t)r, (uint8_t)c) == 0U && bit_idx < bit_total)
                {
                    uint8_t byte = s_all_cw[bit_idx >> 3];
                    qr_m_put(s_mod, (uint8_t)r, (uint8_t)c,
                             (uint8_t)((byte >> (7U - (bit_idx & 7U))) & 1U));
                    bit_idx++;
                }
            }
        }
    }
}

/**
 * @brief 判定掩码在 (r, c) 处是否翻转
 * @param mask 传入：掩码号（0~7）
 * @param r 传入：模块行
 * @param c 传入：模块列
 * @return 1 表示翻转，0 表示保持
 */
static uint8_t qr_mask_bit(uint8_t mask, uint8_t r, uint8_t c)
{
    uint16_t rc = (uint16_t)((uint16_t)r * c);

    switch (mask)
    {
    case 0U: return (uint8_t)(((r + c) & 1U) == 0U);
    case 1U: return (uint8_t)((r & 1U) == 0U);
    case 2U: return (uint8_t)((c % 3U) == 0U);
    case 3U: return (uint8_t)(((r + c) % 3U) == 0U);
    case 4U: return (uint8_t)((((r >> 1) + (c / 3U)) & 1U) == 0U);
    case 5U: return (uint8_t)(((rc & 1U) + (rc % 3U)) == 0U);
    case 6U: return (uint8_t)((((rc & 1U) + (rc % 3U)) & 1U) == 0U);
    default: return (uint8_t)(((((r + c) & 1U) + (rc % 3U)) & 1U) == 0U);
    }
}

/**
 * @brief 对全部数据模块按掩码翻转（XOR，可再调一次还原）
 * @param size 传入：矩阵边长
 * @param mask 传入：掩码号（0~7）
 * @return 无
 */
static void qr_apply_mask(uint8_t size, uint8_t mask)
{
    uint8_t r;
    uint8_t c;

    for (r = 0U; r < size; r++)
    {
        for (c = 0U; c < size; c++)
        {
            if (qr_m_get(s_fun, r, c) == 0U && qr_mask_bit(mask, r, c) != 0U)
                qr_m_flip(s_mod, r, c);
        }
    }
}

/**
 * @brief 简化惩罚分：仅规则 1（行/列内连续同色 >= 5 记 3+(长度-5) 分）
 * @param size 传入：矩阵边长
 * @return 惩罚分（越小越好）
 */
static uint32_t qr_penalty_rule1(uint8_t size)
{
    uint32_t pen = 0U;
    uint8_t r;
    uint8_t c;

    /* 行方向 */
    for (r = 0U; r < size; r++)
    {
        uint8_t run_color = qr_m_get(s_mod, r, 0U);
        uint16_t run = 1U;

        for (c = 1U; c < size; c++)
        {
            if (qr_m_get(s_mod, r, c) == run_color)
            {
                run++;
            }
            else
            {
                if (run >= 5U)
                    pen += 3U + (uint32_t)(run - 5U);
                run_color = qr_m_get(s_mod, r, c);
                run = 1U;
            }
        }
        if (run >= 5U)
            pen += 3U + (uint32_t)(run - 5U);
    }

    /* 列方向 */
    for (c = 0U; c < size; c++)
    {
        uint8_t run_color = qr_m_get(s_mod, 0U, c);
        uint16_t run = 1U;

        for (r = 1U; r < size; r++)
        {
            if (qr_m_get(s_mod, r, c) == run_color)
            {
                run++;
            }
            else
            {
                if (run >= 5U)
                    pen += 3U + (uint32_t)(run - 5U);
                run_color = qr_m_get(s_mod, r, c);
                run = 1U;
            }
        }
        if (run >= 5U)
            pen += 3U + (uint32_t)(run - 5U);
    }
    return pen;
}

/**
 * @brief 将文本编码为 QR 位矩阵（byte mode，ECC M，版本 1~4 自动选择）
 * @param data 传入：待编码字节流（len==0 时可为 NULL）
 * @param len 传入：字节数（0~WE_QR_TEXT_MAX）
 * @param out_bits 传出：位压缩模块矩阵（1=暗模块）
 * @param out_size 传出：矩阵边长（模块数）
 * @return 0 表示成功，-1 表示失败（参数非法或内容超容量）
 */
int8_t we_qr_encode(const uint8_t *data, uint16_t len,
                    uint8_t out_bits[WE_QR_MODULES_MAX][WE_QR_ROW_BYTES],
                    uint8_t *out_size)
{
    const qr_ver_info_t *vi;
    uint8_t ver_idx;
    uint8_t size;
    uint8_t data_per_blk;
    uint32_t total_data_bits;
    uint16_t i;
    uint8_t b;
    uint8_t pad;
    uint8_t best_mask;
    uint32_t best_pen;
    uint8_t m;
    uint16_t k;

    if (out_bits == NULL || out_size == NULL || (data == NULL && len != 0U))
        return -1;

    /* 1. 选版本：容量 = 数据码字 - 2（4bit 模式头 + 8bit 长度 + 4bit 终止符 = 2 字节） */
    for (ver_idx = 0U; ver_idx < (uint8_t)WE_QR_VERSION_MAX; ver_idx++)
    {
        if (len <= (uint16_t)(k_ver[ver_idx].data_cw - 2U))
            break;
    }
    if (ver_idx >= (uint8_t)WE_QR_VERSION_MAX)
        return -1; /* 超过 v4-M byte mode 容量（62 字节） */

    vi = &k_ver[ver_idx];
    size = vi->size;

    /* 2. 组数据位流：模式 0100 + 8bit 长度 + 数据 + 终止符 + 字节对齐 + 补齐字节 */
    memset(s_data_cw, 0, sizeof(s_data_cw));
    s_bit_pos = 0U;
    total_data_bits = (uint32_t)vi->data_cw * 8U;

    qr_append_bits(0x4U, 4U);          /* byte mode 模式指示符 */
    qr_append_bits((uint16_t)len, 8U); /* 版本 1~9 的 byte mode 长度域为 8bit */
    for (i = 0U; i < len; i++)
        qr_append_bits(data[i], 8U);

    {
        uint32_t rem = total_data_bits - s_bit_pos;
        s_bit_pos += (rem < 4U) ? rem : 4U; /* 终止符：至多 4 个 0（缓冲已清零） */
    }
    s_bit_pos = (s_bit_pos + 7U) & ~7UL; /* 补零到字节边界 */

    pad = 0xECU;
    while (s_bit_pos < total_data_bits)
    {
        s_data_cw[s_bit_pos >> 3] = pad;
        s_bit_pos += 8U;
        pad ^= (uint8_t)(0xECU ^ 0x11U); /* 0xEC / 0x11 交替 */
    }

    /* 3. 分块求 RS 纠错码并交织（v1~v3 单块，v4 两个等长块） */
    data_per_blk = (uint8_t)(vi->data_cw / vi->blocks);
    qr_rs_gen_poly(vi->ecc_per_blk);
    for (b = 0U; b < vi->blocks; b++)
    {
        qr_rs_remainder(&s_data_cw[(uint16_t)b * data_per_blk], data_per_blk,
                        vi->ecc_per_blk, &s_ecc[(uint16_t)b * vi->ecc_per_blk]);
    }

    k = 0U;
    for (i = 0U; i < data_per_blk; i++)
    {
        for (b = 0U; b < vi->blocks; b++)
            s_all_cw[k++] = s_data_cw[(uint16_t)b * data_per_blk + i];
    }
    for (i = 0U; i < vi->ecc_per_blk; i++)
    {
        for (b = 0U; b < vi->blocks; b++)
            s_all_cw[k++] = s_ecc[(uint16_t)b * vi->ecc_per_blk + i];
    }

    /* 4. 摆功能图案 + 蛇形填数据 */
    qr_build_function_patterns(ver_idx);
    qr_place_data(size, vi->total_cw);

    /* 5. 掩码 0~7 全试：XOR 施掩 → 写格式位 → 算简化惩罚 → XOR 还原
     *    （格式位属功能区不受掩码 XOR 影响，下一轮直接覆盖） */
    best_mask = 0U;
    best_pen = 0xFFFFFFFFUL;
    for (m = 0U; m < 8U; m++)
    {
        uint32_t pen;

        qr_apply_mask(size, m);
        qr_draw_format(size, k_fmt_m[m]);
        pen = qr_penalty_rule1(size);
        if (pen < best_pen)
        {
            best_pen = pen;
            best_mask = m;
        }
        qr_apply_mask(size, m); /* 还原为未掩码状态 */
    }
    qr_apply_mask(size, best_mask);
    qr_draw_format(size, k_fmt_m[best_mask]);

    /* 6. 压缩输出：out_bits[行][列/8] 的第 (列&7) 位 */
    memset(out_bits, 0, (size_t)WE_QR_MODULES_MAX * WE_QR_ROW_BYTES);
    for (i = 0U; i < size; i++)
    {
        uint8_t c;

        for (c = 0U; c < size; c++)
        {
            if (qr_m_get(s_mod, (uint8_t)i, c) != 0U)
                out_bits[i][c >> 3] |= (uint8_t)(1U << (c & 7U));
        }
    }
    *out_size = size;
    return 0;
}
