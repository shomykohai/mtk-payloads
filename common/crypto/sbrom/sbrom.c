/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <crypto/sbrom/sbrom.h>
#include <stdint.h>
#include <stddef.h>
#include <libc.h>
#include <mmio.h>
#include <debug.h>

#define SBROM_MEM_POLL_TIMEOUT 10000000U

static inline void sb_write(volatile uint32_t *base, uint32_t offset, uint32_t value) {
    *(volatile uint32_t *)((uint8_t *)base + offset) = value;
}

static int sb_push_desc(volatile uint32_t *base, HwDesc *desc) {
    printf("[SBROM] push desc: write-only submit op=0x%08x\n", desc->op_cfg);
    sb_write(base, SBROM_REG_DESC_WORD0, desc->din_addr);
    sb_write(base, SBROM_REG_DESC_WORD1, desc->din_ctrl);
    sb_write(base, SBROM_REG_DESC_WORD2, desc->dout_addr);
    sb_write(base, SBROM_REG_DESC_WORD3, desc->dout_ctrl);
    sb_write(base, SBROM_REG_DESC_WORD4, desc->op_cfg);
    sb_write(base, SBROM_REG_DESC_WORD5, desc->addr_high);
    mem_barrier();
    printf("[SBROM] push desc: submitted\n");
    return SBROM_OK;
}

static void sb_clear_irq(volatile uint32_t *base) {
    printf("[SBROM] clear irq\n");
    sb_write(base, SBROM_REG_ICR, 4);
}

static int sb_wait_completion(volatile uint32_t *base) {
    HwDesc desc;
    volatile uint64_t completion_flag = 0;
    uint64_t flag_addr = (uintptr_t)&completion_flag;
    uint32_t timeout = SBROM_MEM_POLL_TIMEOUT;

    printf("[SBROM] completion wait start\n");
    sb_clear_irq(base);

    desc.din_addr  = 0;
    desc.din_ctrl  = 0x08000011;
    desc.dout_addr = (uint32_t)flag_addr;
    desc.dout_ctrl = 0x08000012;
    desc.op_cfg    = 0x00000100;
    desc.addr_high = ((flag_addr >> 32) & 0xFFFFFFFF) << 16;

    int res = sb_push_desc(base, &desc);
    if (res)
        return res;

    printf("[SBROM] completion: polling memory flag @0x%08x\n", (uint32_t)flag_addr);
    while (timeout--) {
        mem_barrier();
        if (completion_flag != 0) {
            printf("[SBROM] completion flag=0x%08x%08x\n",
                (uint32_t)(completion_flag >> 32), (uint32_t)completion_flag);
            sb_clear_irq(base);
            printf("[SBROM] completion ok\n");
            return SBROM_OK;
        }
    }

    printf("[SBROM] completion memory flag timeout\n");
    return SBROM_TIMEOUT;
}

static int sb_aes_cmac_driver(volatile uint32_t *base, HwCryptoKey_t aesKeyType, uint64_t key, uint64_t buf, int bufferlen, uint64_t out) {
    HwDesc desc;
    int keylen = KEY_LENGTH_DEFAULT;

    printf("[SBROM] cmac start: base=%p key_type=%u len=%d\n", base, aesKeyType, bufferlen);
    if (aesKeyType == ROOT_KEY) {
        /*
         * Some DAs/SOCs hang on TZCC LCS reads even though the descriptor
         * engine itself is usable. Use the root key size directly for RPMB
         * derivation instead of probing LCS first.
         */
        keylen = KEY_LENGTH_ROOT;
        printf("[SBROM] cmac: skipping LCS read, using root key length %d\n", keylen);
    }

    sb_clear_irq(base);

    uint32_t kval = (keylen << 19) - 0x800000;

    // Setup AES-CMAC
    desc.din_addr  = 0;
    desc.din_ctrl  = 0x08000041;
    desc.dout_addr = 0;
    desc.dout_ctrl = 0;
    desc.op_cfg    = kval | 0x01001C20;
    desc.addr_high = 0;
    int res = sb_push_desc(base, &desc);
    if (res)
        return res;

    memset(&desc, 0, sizeof(desc));

    if (aesKeyType == USER_KEY) {
        desc.din_addr  = (uint32_t)key;
        desc.din_ctrl  = 0x42;
        desc.addr_high = (uint16_t)(key >> 32);
    }

    desc.op_cfg = kval
               | ((aesKeyType & 3) << 15)
               | (((aesKeyType >> 2) & 3) << 20)
               | 0x04001C20;
    res = sb_push_desc(base, &desc);
    if (res)
        return res;

    // Send the data :D
    desc.din_addr  = (uint32_t)buf;
    desc.din_ctrl  = (4 * (bufferlen & 0xFFFFFF)) | 2;
    desc.dout_addr = 0;
    desc.dout_ctrl = 0;
    desc.op_cfg    = 1;
    desc.addr_high = (uint16_t)(buf >> 32);
    res = sb_push_desc(base, &desc);
    if (res)
        return res;

    // Read the output i think
    if (aesKeyType != PROVISIONING_KEY) {
        desc.din_addr  = 0;
        desc.din_ctrl  = 0;
        desc.dout_addr = (uint32_t)out;
        desc.dout_ctrl = 0x42;
        desc.op_cfg    = 0x08001C26;
        desc.addr_high = ((uint16_t)(out >> 32)) << 16;
        res = sb_push_desc(base, &desc);
        if (res)
            return res;
    }

    printf("[SBROM] cmac: descriptors queued, waiting completion\n");
    return sb_wait_completion(base);
}


static int sb_aes_cmac(volatile uint32_t *base, HwCryptoKey_t aesKeyType, uint8_t *buffer, int bufferlen, uint8_t *outbuf) {
    // MAP page

    return sb_aes_cmac_driver(base, aesKeyType, 0, (uintptr_t)buffer, bufferlen, (uintptr_t)outbuf);

    // UNMAP page
}

// Old devices use 0x18000000, newer ones seem to use 0x600, regardless, I did both and it worked anyway?
void SBROM_ClockEnable(void) {
    printf("[SBROM] clock enable: CLR0 0x%08x <= 0x%08x\n", INFRACFG_CG_CLR0, TZCC_CLK_ENABLE_MASK);
    *(volatile uint32_t *)INFRACFG_CG_CLR0 = TZCC_CLK_ENABLE_MASK;   /* 0x18000000 */
    printf("[SBROM] clock enable: CLR1 0x%08x <= 0x%08x\n", INFRACFG_CG_CLR1, TZCC_CLK_5G_ENABLE);
    *(volatile uint32_t *)INFRACFG_CG_CLR1 = TZCC_CLK_5G_ENABLE;     /* 0x600 */
    printf("[SBROM] clock enable: done\n");
}

void SBROM_ClockDisable(void) {
    printf("[SBROM] clock disable: SET0 0x%08x <= 0x%08x\n", INFRACFG_CG_SET0, TZCC_CLK_DISABLE_MASK);
    *(volatile uint32_t *)INFRACFG_CG_SET0 = TZCC_CLK_DISABLE_MASK;
    printf("[SBROM] clock disable: SET1 0x%08x <= 0x%08x\n", INFRACFG_CG_SET1, TZCC_CLK_5G_DISABLE);
    *(volatile uint32_t *)INFRACFG_CG_SET1 = TZCC_CLK_5G_DISABLE;
    printf("[SBROM] clock disable: done\n");
}

/* KDF: NIST SP 800-108 Counter Mode using AES-CMAC
 *
 * [counter (1 byte)] [seed] [0x00 separator] [salt] [output_length_in_bits]
 *
 */
SaSiStatus SBROM_KeyDerivation(uintptr_t hwBaseAddress, HwCryptoKey_t aesKeyType,
                                const uint8_t *seed, uint32_t seed_size,
                                const uint8_t *salt, uint32_t saltSize,
                                uint8_t *derivedKeyAddr, uint32_t derivedKeySize)
{
    volatile uint32_t *base = (volatile uint32_t *)hwBaseAddress;
    uint8_t buffer[68] __attribute__((aligned(4)));
    uint8_t tmp[16] __attribute__((aligned(4)));

    uint32_t pos = 0;
    uint32_t blocks;
    SaSiStatus status;

    if (!seed && (seed_size || seed_size > 0x20))
        return INVALID_LENGTH;

    if (!salt && (saltSize || saltSize > 0x20))
        return INVALID_LENGTH;

    printf("[SBROM] KDF start: base=0x%08x seed=%u salt=%u out=%u\n",
        (uint32_t)hwBaseAddress, seed_size, saltSize, derivedKeySize);

    memset(buffer, 0, sizeof(buffer));

    buffer[pos++] = 1;

    if (seed) {
        memcpy(&buffer[pos], seed, seed_size);
        pos += seed_size;
    }

    buffer[pos++] = 0;

    if (salt) {
        memcpy(&buffer[pos], salt, saltSize);
        pos += saltSize;
    }

    buffer[pos] = (8 * derivedKeySize) & 0xFF;

    blocks = (derivedKeySize + 15) >> 4;
    printf("[SBROM] KDF blocks=%u msg_len=%u\n", blocks, seed_size + saltSize + 3);

    for (uint32_t i = 0; i < blocks; i++) {
        buffer[0] = i + 1;

        mem_barrier();

        printf("[SBROM] KDF block %u/%u\n", i + 1, blocks);
        status = sb_aes_cmac(base, aesKeyType, buffer, seed_size + saltSize + 3, tmp);

        if (status) {
            printf("[SBROM] KDF block %u failed: 0x%08x\n", i + 1, status);
            return status;
        }

        memcpy(derivedKeyAddr + (16 * i), tmp, 16);
    }

    printf("[SBROM] KDF complete\n");
    return SBROM_OK;
}
