/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#ifndef RPMB_H
#define RPMB_H

#include <stdint.h>
#include <stdbool.h>

#define MAX_RPMB_PARTS 4
#define RPMB_DATA_SZ  256
#define RPMB_KEY_SZ   32
#define RPMB_FRAME_SZ 512

struct rpmb_frame {
    uint8_t  stuff[196];
    uint8_t  key_mac[32];
    uint8_t  data[256];
    uint8_t  nonce[16];
    uint32_t wr_cnt;
    uint16_t addr;
    uint16_t blk_cnt;
    uint16_t result;
    uint16_t req_resp;
};

struct rpmb_backend {
    int  (*init)( uint32_t part, uint8_t *rpmb_key);
    bool (*is_initialized)(uint32_t part);
    int  (*read_frame)(uint32_t part, uint32_t address, uint8_t *data);
    int  (*read_blocks)(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data);
    int  (*write_frame)(uint32_t part, uint32_t address, uint8_t *data, const uint8_t *rpmb_key);
    int  (*write_blocks)(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data, const uint8_t *rpmb_key);
    int  (*program_key)(uint32_t part, const uint8_t *rpmb_key);
    uint32_t (*get_sector_count)(uint32_t part);
    bool (*is_region_enabled)(uint32_t part);
};

void rpmb_set_backend(const struct rpmb_backend *be);
void rpmb_set_key(uint32_t part, const uint8_t *key);
uint8_t *rpmb_get_key(uint32_t part);
bool rpmb_is_initialized(uint32_t part);

int rpmb_init(uint32_t part);
int rpmb_read(uint32_t part, uint32_t address, uint8_t *data);
int rpmb_read_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data);
int rpmb_write(uint32_t part, uint32_t address, uint8_t *data);
int rpmb_write_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data);
int rpmb_program_key(uint32_t part);
uint32_t rpmb_get_sector_count(uint32_t part);
bool rpmb_is_region_enabled(uint32_t part);

struct rpmb_stream_ctx {
    uint32_t rpmb_part;
    uint32_t start_sector;
};

// Streamed RPMB RW operations, for speed points
int rpmb_read_stream_cb(uint64_t offset, uint8_t *data, uint32_t length, void *ctx);
int rpmb_write_stream_cb(uint64_t offset, uint8_t *data, uint32_t length, void *ctx);

#endif
