/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#pragma once

#include <stdint.h>

typedef uint32_t SaSiStatus;

typedef enum {
    USER_KEY         = 0,
    ROOT_KEY         = 1,
    PROVISIONING_KEY = 2,
    SESSION_KEY      = 3,
    RESERVED_KEY     = 4,
    PLATFORM_KEY     = 5,
    CUSTOMER_KEY     = 6,
    END_OF_KEYS      = 0x7FFFFFFF
} HwCryptoKey_t;

/* Hardware descriptor */
typedef struct {
    uint32_t din_addr;   /* word[0]: DIN address (low 32 bits) */
    uint32_t din_ctrl;   /* word[1]: DIN control / size */
    uint32_t dout_addr;  /* word[2]: DOUT address (low 32 bits) */
    uint32_t dout_ctrl;  /* word[3]: DOUT control / size */
    uint32_t op_cfg;     /* word[4]: operation / crypto config */
    uint32_t addr_high;  /* word[5]: high address bits (DIN[15:0] | DOUT[31:16]) */
} HwDesc;

#define SBROM_REG_IRR                   0xA00
#define SBROM_REG_ICR                   0xA08
#define SBROM_REG_LCS                   0xAA0
#define SBROM_REG_STATUS                0xBA0
#define SBROM_REG_DESC_WORD0            0xE80
#define SBROM_REG_DESC_WORD1            0xE84
#define SBROM_REG_DESC_WORD2            0xE88
#define SBROM_REG_DESC_WORD3            0xE8C
#define SBROM_REG_DESC_WORD4            0xE90
#define SBROM_REG_DESC_WORD5            0xE94
#define SBROM_REG_DESC_QUEUE            0xE9C

#define INFRACFG_CG_SET0    0x10001088
#define INFRACFG_CG_CLR0    0x1000108C
#define INFRACFG_CG_SET1    0x10001080
#define INFRACFG_CG_CLR1    0x10001084

#define TZCC_CLK_ENABLE_MASK    0x18000000U
#define TZCC_CLK_DISABLE_MASK   0x08000000U
#define TZCC_CLK_5G_ENABLE      0x600U
#define TZCC_CLK_5G_DISABLE     0x200U

#define KEY_LENGTH_DEFAULT  16
#define KEY_LENGTH_ROOT     32

#define AES_CMAC_RESULT_SIZE 16

#define DESC_QUEUE_FREE_SLOTS_MASK  0x3FF

#define SBROM_OK            0x00000000
#define SBROM_ERROR         0xF6000001
#define SBROM_TIMEOUT       0xF6000002
#define INVALID_LENGTH      0xF2000003

SaSiStatus SBROM_KeyDerivation(uintptr_t hwBaseAddress,
                               HwCryptoKey_t aesKeyType,
                               const uint8_t *seed,
                               uint32_t seed_size,
                               const uint8_t *salt,
                               uint32_t saltSize,
                               uint8_t *derivedKeyAddr,
                               uint32_t derivedKeySize);

void SBROM_ClockEnable(void);
void SBROM_ClockDisable(void);
