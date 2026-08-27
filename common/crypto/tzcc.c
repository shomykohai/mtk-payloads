/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <stdint.h>
#include <libc.h>
#include <crypto/tzcc.h>
#include <crypto/key_derive.h>
#include <debug.h>

static uintptr_t tzcc_base = 0;

void set_tzcc_base(uintptr_t base) {
    tzcc_base = base;
}

uintptr_t get_tzcc_base(void) {
    return tzcc_base;
}

int tzcc_key_derive(const uint8_t *label, uint32_t label_len,
                    const uint8_t *ctx, uint32_t ctx_len,
                    uint8_t *out, uint32_t out_len)
{
    if (tzcc_base == 0)
        return -1;

    printf("[TZCC] key derive start: base=0x%08x label_len=%u ctx_len=%u out_len=%u\n",
        (uint32_t)tzcc_base, label_len, ctx_len, out_len);

    printf("[TZCC] enabling SBROM clock\n");
    SBROM_ClockEnable();
    printf("[TZCC] SBROM clock enabled\n");

    printf("[TZCC] calling SBROM KDF\n");
    int status = SBROM_KeyDerivation(tzcc_base, ROOT_KEY,
                                     label, label_len,
                                     ctx, ctx_len,
                                     out, out_len);
    printf("[TZCC] SBROM KDF returned 0x%08x\n", status);

    SBROM_ClockDisable();
    printf("[TZCC] key derive status=0x%08x\n", status);
    return status;
}
