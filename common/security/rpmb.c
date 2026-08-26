/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <security/rpmb.h>
#include <stddef.h>

extern void *memcpy(void *dst, const void *src, unsigned long n);

static const struct rpmb_backend *g_be;
static uint8_t g_key[MAX_RPMB_PARTS][RPMB_KEY_SZ];

void rpmb_set_backend(const struct rpmb_backend *be) {
    g_be = be;
}

uint8_t *rpmb_get_key(uint32_t part) {
    if (part >= MAX_RPMB_PARTS)
        return NULL;

    return g_key[part];
}

void rpmb_set_key(uint32_t part, const uint8_t *key) {
    if (part >= MAX_RPMB_PARTS || !key)
        return;

    memcpy(g_key[part], key, RPMB_KEY_SZ);
}

bool rpmb_is_initialized(uint32_t part) {
    if (!g_be || !g_be->init) return false;

    if (part >= MAX_RPMB_PARTS)
        return false;

    return g_be->is_initialized ? g_be->is_initialized(part) : false;
}

int rpmb_init(uint32_t part) {
    if (!g_be || !g_be->init) return -1;

    if (part >= MAX_RPMB_PARTS)
        return -1;

    return g_be->init(part, g_key[part]);
}

int rpmb_read(uint32_t part, uint32_t address, uint8_t *data) {
    if (!g_be || !g_be->read_frame) return -1;
    if (part >= MAX_RPMB_PARTS) return -1;
    return g_be->read_frame(part, address, data);
}

int rpmb_read_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data) {
    if (!g_be || !g_be->read_blocks) return -1;
    if (part >= MAX_RPMB_PARTS) return -1;
    return g_be->read_blocks(part, address, blocks, data);
}

int rpmb_write(uint32_t part, uint32_t address, uint8_t *data) {
    if (!g_be || !g_be->write_frame) return -1;

    if (part >= MAX_RPMB_PARTS)
        return -1;

    return g_be->write_frame(part, address, data, g_key[part]);
}

int rpmb_write_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data) {
    if (!g_be || !g_be->write_blocks) return -1;

    if (part >= MAX_RPMB_PARTS)
        return -1;

    return g_be->write_blocks(part, address, blocks, data, g_key[part]);
}

int rpmb_program_key(uint32_t part) {
    if (!g_be || !g_be->program_key) return -1;

    if (part >= MAX_RPMB_PARTS)
        return -1;

    return g_be->program_key(part, g_key[part]);
}

uint32_t rpmb_get_sector_count(uint32_t part) {
    if (!g_be || !g_be->get_sector_count || part >= MAX_RPMB_PARTS)
        return 0;

    return g_be->get_sector_count(part);
}

int rpmb_read_stream_cb(uint64_t offset, uint8_t *data, uint32_t length, void *ctx) {
    struct rpmb_stream_ctx *rctx = (struct rpmb_stream_ctx *)ctx;
    uint32_t sector_offset = (uint32_t)(offset / RPMB_DATA_SZ);
    uint32_t blocks = length / RPMB_DATA_SZ;

    return rpmb_read_blocks(rctx->rpmb_part, rctx->start_sector + sector_offset, blocks, data);
}

int rpmb_write_stream_cb(uint64_t offset, uint8_t *data, uint32_t length, void *ctx) {
    struct rpmb_stream_ctx *rctx = (struct rpmb_stream_ctx *)ctx;
    uint32_t sector_offset = (uint32_t)(offset / RPMB_DATA_SZ);
    uint32_t blocks = length / RPMB_DATA_SZ;

    return rpmb_write_blocks(rctx->rpmb_part, rctx->start_sector + sector_offset, blocks, data);
}
