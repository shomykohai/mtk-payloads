/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <types.h>
#include <libc.h>
#include <crypto/tzcc.h>
#include <crypto/key_derive.h>
#include <debug.h>

#if defined(__aarch64__) && defined(CONFIG_CRYPTO_SSR)

#include <crypto/ssr/ssr.h>

#endif

static const u8 label_rpmb[]    = { 'R','P','M','B',' ','K','E','Y' };
static const u8 salt_rpmb[]     = { 'S','A','S','I' };

// FDE
static const u8 label_sqnc[]    = { 'S','Q','N','C','!','L','F','Z' };
static const u8 salt_sqnc[]     = { 'T','B','T','J' };

// TEE
static const u8 label_trod[]    = { 'T','R','O','D','"','M','G','[' };
static const u8 salt_trod[]     = { 'U','C','U','K' };

// AES IMG ENC
static const u8 label_fw[]      = { 'F','I','R','M','W','A','R','E' };
static const u8 salt_fw[]       = { 'E','N','C','C' };

// Motorola RPMB
static const u8 label_custom[]  = { 'C','C','U','S','T','O','M','M' };
static const u8 salt_custom[]   = { 'M','O','T','O' };

// ??
static const u8 label_base[]    = { 'B','A','S','E','_','K','E','Y' };
static const u8 salt_base[]     = { '9','5','2','7' };

static const u8 label_unk0[]    = { 'C','B','T','F','Z',0xAB,'e',0x60};
static const u8 salt_unk0[]     = { '8','6','3','8'};

static const u8 label_unk1[]    = { 'A','@','R','D','^','J','D','X'};
static const u8 salt_unk1[]     = { '8','4','1','6'};

key_attr_t key_attr_table[] = {
    {
        .src_key  = 1,
        .label    = (u8 *)label_rpmb,
        .label_sz = sizeof(label_rpmb),
        .salt     = (u8 *)salt_rpmb,
        .salt_sz  = sizeof(salt_rpmb),
        .policy   = 22, // S-CHIP
    },
    {
        .src_key  = 1,
        .label    = (u8 *)label_sqnc,
        .label_sz = sizeof(label_sqnc),
        .salt     = (u8 *)salt_sqnc,
        .salt_sz  = sizeof(salt_sqnc),
        .policy   = 22,
    },
    {
        .src_key  = 1,
        .label    = (u8 *)label_trod,
        .label_sz = sizeof(label_trod),
        .salt     = (u8 *)salt_trod,
        .salt_sz  = sizeof(salt_trod),
        .policy   = 22,
    },
    {
        .src_key  = 2,
        .label    = (u8 *)label_fw,
        .label_sz = sizeof(label_fw),
        .salt     = (u8 *)salt_fw,
        .salt_sz  = sizeof(salt_fw),
        .policy   = 23 // TODO: Find out what this policy is
    },
    {
        .src_key  = 0,
        .label    = NULL,
        .label_sz = 0,
        .salt     = NULL,
        .salt_sz  = 0,
        .policy   = 0
    },
    {
        // https://github.com/bkerler/mtkclient/blob/main/mtkclient/Library/Hardware/hwcrypto_dxcc.py#L1120-L1121
        .src_key  = 1,
        .label    = (u8 *)label_custom,
        .label_sz = sizeof(label_custom),
        .salt     = (u8 *)salt_custom,
        .salt_sz  = sizeof(salt_custom),
        .policy   = 22
    },
    {
        .src_key  = 1,
        .label    = (u8 *)label_base,
        .label_sz = sizeof(label_base),
        .salt     = (u8 *)salt_base,
        .salt_sz  = sizeof(salt_base),
        .policy   = 22
    },
    {
        .src_key  = 1,
        .label    = (u8 *)label_unk0,
        .label_sz = sizeof(label_unk0),
        .salt     = (u8 *)salt_unk0,
        .salt_sz  = sizeof(salt_unk0),
        .policy   = 22
    },
    {
        .src_key  = 1,
        .label    = (u8 *)label_unk1,
        .label_sz = sizeof(label_unk1),
        .salt     = (u8 *)salt_unk1,
        .salt_sz  = sizeof(salt_unk1),
        .policy   = 22
    },
};

KeyType key_type_from_str(const char *str) {
    if (str == NULL) return INPUT_KEY;

    if (strcmp(str, "RPMB") == 0) return RPMB_KEY;
    if (strcmp(str, "FDE") == 0) return FDE_KEY;
    if (strcmp(str, "TEE") == 0) return TEE_KEY;
    if (strcmp(str, "AES_IMG_ENC") == 0) return AES_IMG_ENC;
    if (strcmp(str, "AES_CUSTOM") == 0) return AES_CUSTOM;
    if (strcmp(str, "MOTOROLA") == 0) return CUSTOM_0;
    if (strcmp(str, "ROT") == 0) return CUSTOM_1;
    if (strcmp(str, "UNKNOWN_0") == 0) return UNKNOWN_0;
    if (strcmp(str, "UNKNOWN_1") == 0) return UNKNOWN_1;

    return INPUT_KEY;
}

u32 key_derive(KeyType key_type, u8* out, u32 len) {
    if (key_type >= sizeof(key_attr_table) / sizeof(key_attr_t)) {
        printf("%s: key_type=%d out of range (max=%zu)\n", __func__, key_type, sizeof(key_attr_table) / sizeof(key_attr_t));
        memset(out, 0, len);
        return 0;
    }

    key_attr_t key_attr = key_attr_table[key_type];

    u8 *label   = key_attr.label;
    u8 *context = key_attr.salt;

    if (label && context) {
        printf("%s: key_type=%d\n", __func__, key_type);

        return key_derive_input(label, key_attr.label_sz, context, key_attr.salt_sz, out, len);
    }

    memset(out, 0, len);
    return -2;
}

u32 key_derive_input(u8 *label, u32 label_sz, u8 *salt, u32 salt_sz, u8 *out, u32 len) {
    if (!label || !salt || !out || len == 0) {
        printf("%s: Invalid input parameters\n", __func__);
        return -1;
    }

    if (label_sz + salt_sz > len) {
        printf("%s: label_sz + salt_sz exceeds out_len\n", __func__);
        return -1;
    }

    printf("%s: label_sz=%u salt_sz=%u\n", __func__, label_sz, salt_sz);
    printf("%s: label='%.*s' context='%.*s'\n", __func__, label_sz, label, salt_sz, salt);

    int status = tzcc_key_derive(label, label_sz, salt, salt_sz, out, len);

#if defined(__aarch64__) && defined(CONFIG_CRYPTO_SSR)
    if (status != 0)
        status = ssr_key_derive(label, label_sz, salt, salt_sz, out, len);
#endif

    return status;
}
