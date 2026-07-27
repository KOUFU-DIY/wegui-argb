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
 *   矩阵 2 x 33x33 字节 + 码字缓冲约 200 字节，仅模拟器编译无 RAM 压力。
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

/* ---------------- GF(256) 表（首次调用时生成，本原多项式 0x11D） ---------------- */
static uint8_t s_gf_exp[256];
static uint8_t s_gf_log[256];
static uint8_t s_gf_ready = 0U;

/* ---------------- 静态工作区（33x33 上限，byte-per-module 便于读写） ---------------- */
static uint8_t s_mod[WE_QR_MODULES_MAX][WE_QR_MODULES_MAX]; /* 模块颜色：1=暗 */
static uint8_t s_fun[WE_QR_MODULES_MAX][WE_QR_MODULES_MAX]; /* 功能模块标志：1=数据不可占用 */

static uint8_t s_data_cw[64];  /* 数据码字（v4 上限 64） */
static uint8_t s_ecc[36];      /* ECC 码字（v4 上限 2 块 x 18） */
static uint8_t s_all_cw[100];  /* 交织后的最终码字序列（v4 上限 100） */
static uint8_t s_gen[26];      /* RS 生成多项式系数（不含最高位 1，v3 上限 26 阶） */
static uint32_t s_bit_pos;     /* 位流写游标 */

/**
 * @brief 生成 GF(256) 指数/对数表（本原多项式 0x11D，仅首次调用执行）
 * @return 无
 */
static void qr_gf_init(void)
{
    uint16_t x = 1U;
    uint16_t i;

    for (i = 0U; i < 255U; i++)
    {
        s_gf_exp[i] = (uint8_t)x;
        s_gf_log[x] = (uint8_t)i;
        x <<= 1;
        if ((x & 0x100U) != 0U)
            x ^= 0x11DU;
    }
    s_gf_exp[255] = 1U; /* 哨兵：正常路径不会读到 */
    s_gf_ready = 1U;
}

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
    s_mod[r][c] = dark;
    s_fun[r][c] = 1U;
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

                if (s_fun[r][c] == 0U && bit_idx < bit_total)
                {
                    uint8_t byte = s_all_cw[bit_idx >> 3];
                    s_mod[r][c] = (uint8_t)((byte >> (7U - (bit_idx & 7U))) & 1U);
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
            if (s_fun[r][c] == 0U && qr_mask_bit(mask, r, c) != 0U)
                s_mod[r][c] ^= 1U;
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
        uint8_t run_color = s_mod[r][0];
        uint16_t run = 1U;

        for (c = 1U; c < size; c++)
        {
            if (s_mod[r][c] == run_color)
            {
                run++;
            }
            else
            {
                if (run >= 5U)
                    pen += 3U + (uint32_t)(run - 5U);
                run_color = s_mod[r][c];
                run = 1U;
            }
        }
        if (run >= 5U)
            pen += 3U + (uint32_t)(run - 5U);
    }

    /* 列方向 */
    for (c = 0U; c < size; c++)
    {
        uint8_t run_color = s_mod[0][c];
        uint16_t run = 1U;

        for (r = 1U; r < size; r++)
        {
            if (s_mod[r][c] == run_color)
            {
                run++;
            }
            else
            {
                if (run >= 5U)
                    pen += 3U + (uint32_t)(run - 5U);
                run_color = s_mod[r][c];
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

    if (s_gf_ready == 0U)
        qr_gf_init();

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
            if (s_mod[i][c] != 0U)
                out_bits[i][c >> 3] |= (uint8_t)(1U << (c & 7U));
        }
    }
    *out_size = size;
    return 0;
}
