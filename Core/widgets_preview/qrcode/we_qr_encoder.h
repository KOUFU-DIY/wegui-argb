#ifndef __WE_QR_ENCODER_H
#define __WE_QR_ENCODER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* --------------------------------------------------------------------------
 * we_qr_encoder —— 独立 QR 二维码编码器（preview 孵化区，qrcode 控件配套）
 *
 * 特性：
 *   1. 版本 1~4（21/25/29/33 模块），按内容长度自动选最小版本；
 *   2. ECC 等级固定 M；编码模式固定 byte mode（8bit 数据，ASCII 直通）；
 *   3. Reed-Solomon GF(256) 指数/对数表在首次调用时生成（本原多项式 0x11D）；
 *   4. v4-M 为 2 个 RS 块，内部完成标准交织；
 *   5. 掩码 0~7 全部实现，按简化惩罚分（仅规则 1：行/列连续同色）择优；
 *   6. 纯整数、零 malloc、无任何 GUI 依赖（可脱离工程单独编译做向量自测）。
 *
 * 限制（preview 阶段接受，毕业前评估）：
 *   1. 工作区为文件级 static（矩阵/码字缓冲共享），不可重入、非线程安全；
 *   2. 掩码惩罚只算规则 1（未算 2x2 块、仿 finder 图案、明暗占比三条规则），
 *      选出的掩码合法可解码，但不保证与完整评估器选择一致。
 * -------------------------------------------------------------------------- */

/* 支持的版本范围与推导常量 */
#define WE_QR_VERSION_MIN 1
#define WE_QR_VERSION_MAX 4
#define WE_QR_MODULES_MAX 33                            /* 版本4：17 + 4*4 */
#define WE_QR_ROW_BYTES   ((WE_QR_MODULES_MAX + 7) / 8) /* 位压缩后每行字节数 = 5 */
#define WE_QR_TEXT_MAX    62                            /* v4-M byte mode 数据容量（字节） */

/**
 * @brief 将文本编码为 QR 位矩阵（byte mode，ECC M，版本 1~4 自动选择）
 * @param data 传入：待编码字节流（ASCII/任意字节），len==0 时可为 NULL
 * @param len 传入：字节数（0~WE_QR_TEXT_MAX，超出返回失败）
 * @param out_bits 传出：位压缩模块矩阵，out_bits[行][列/8] 的第 (列&7) 位，1=暗模块
 * @param out_size 传出：矩阵边长（模块数：21/25/29/33）
 * @return 0 表示成功，-1 表示失败（参数非法或内容超容量）
 * @note 内部使用文件级 static 工作区，不可重入；失败时不修改 out_bits/out_size。
 */
int8_t we_qr_encode(const uint8_t *data, uint16_t len,
                    uint8_t out_bits[WE_QR_MODULES_MAX][WE_QR_ROW_BYTES],
                    uint8_t *out_size);

/**
 * @brief 读取位压缩矩阵中单个模块的明暗
 * @param bits 传入：we_qr_encode 输出的位压缩矩阵
 * @param row 传入：模块行（0 ~ size-1）
 * @param col 传入：模块列（0 ~ size-1）
 * @return 1 表示暗模块，0 表示亮模块
 */
static inline uint8_t we_qr_bit_get(const uint8_t bits[WE_QR_MODULES_MAX][WE_QR_ROW_BYTES],
                                    uint8_t row, uint8_t col)
{
    return (uint8_t)((bits[row][col >> 3] >> (col & 7U)) & 1U);
}

#ifdef __cplusplus
}
#endif

#endif /* __WE_QR_ENCODER_H */
