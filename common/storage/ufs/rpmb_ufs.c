/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <stdint.h>
#include <stdbool.h>
#include <security/rpmb.h>
#include <storage/ufs/rpmb_ufs.h>
#include <crypto/hmac-sha256.h>
#include <crypto/sha256.h>
#include <libc.h>
#include <debug.h>

#define RPMB_FRAME_SZ   512
#define RPMB_DATA_BEG   228

#define bswap16 __builtin_bswap16
#define bswap32 __builtin_bswap32

#define UFS_CMD_OPCODE          0xC4
#define UFS_CMD_DIR_OUT         1
#define UFS_CMD_DIR_IN          2
#define UFS_CMD_SECURITY_OUT    0x00ECB500
#define UFS_CMD_SECURITY_IN     0x00ECA200
#define UFS_CMD_CDB_LEN         0x0C

#define RPMB_GET_WRITE_COUNTER  2
#define RPMB_WRITE_DATA         3
#define RPMB_READ_DATA          4
#define RPMB_RESULT_READ        5

struct ufs_rpmb_cmd {
    u32 opcode;
    u32 tag;
    u32 direction;
    u32 cdb_word;
    u32 one_a;
    u8  unk14;
    u8  flags15;
    u16 unk16;
    u32 unk18;
    u8  unk1c;
    u8  pad1d;
    u16 cdb_len;
    u32 data_len;
    void *data;
} __attribute__((packed));

static ufs_get_lu_fn g_ufs_get_lu;
static ufs_get_tag_fn g_ufs_get_tag;
static ufs_queuecommand_fn g_ufs_queuecommand;
static ufs_put_tag_fn g_ufs_put_tag;
static bool g_rpmb_initialized[MAX_RPMB_PARTS];
static struct rpmb_backend g_be;

__attribute__((aligned(64))) static struct rpmb_frame g_frame;
__attribute__((aligned(64))) static struct rpmb_frame g_write_frames[32];

static int rpmb_ufs_write_blocks(
    uint32_t part,
    uint32_t address,
    uint32_t blocks,
    uint8_t *data,
    const uint8_t *rpmb_key
);

static int ufs_rpmb_command(void *ufs, u32 tag, void *data, u32 data_len, bool write)
{
    struct ufs_rpmb_cmd cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opcode = UFS_CMD_OPCODE;
    cmd.tag = tag;
    cmd.direction = write ? UFS_CMD_DIR_OUT : UFS_CMD_DIR_IN;
    cmd.cdb_word = write ? UFS_CMD_SECURITY_OUT : UFS_CMD_SECURITY_IN;
    cmd.one_a = 1;
    cmd.flags15 = 2;
    cmd.cdb_len = UFS_CMD_CDB_LEN;
    cmd.data_len = data_len;
    cmd.data = data;

    return g_ufs_queuecommand(ufs, &cmd);
}

static int ufs_rpmb_send_frames(uint32_t part, struct rpmb_frame *frames, uint32_t frame_count)
{
    void *ufs;
    u32 tag = 0;
    int got_tag;
    int res;

    if (part >= MAX_RPMB_PARTS) {
        printf("[RPMB-UFS] invalid RPMB partition %u\n", part);
        return -1;
    }

    if (!g_ufs_get_lu || !g_ufs_get_tag || !g_ufs_queuecommand || !g_ufs_put_tag) {
        printf("[RPMB-UFS] UFS helper pointers are not ready\n");
        return -1;
    }

    printf("[RPMB-UFS] send start: part=%u frames=%u req=0x%04x\n",
        part, frame_count, bswap16(frames[0].req_resp));

    ufs = g_ufs_get_lu(part);
    printf("[RPMB-UFS] get_lu(%u) -> %p\n", part, ufs);
    if (!ufs) {
        printf("[RPMB-UFS] failed to get UFS LU for RPMB partition %u\n", part);
        return -1;
    }

    got_tag = g_ufs_get_tag(ufs, &tag);
    printf("[RPMB-UFS] get_tag -> %d tag=%u\n", got_tag, tag);
    if (!got_tag) {
        printf("[RPMB-UFS] failed to allocate UFS tag\n");
        return -1;
    }

    printf("[RPMB-UFS] SECURITY PROTOCOL OUT len=0x%x\n", RPMB_FRAME_SZ * frame_count);
    res = ufs_rpmb_command(ufs, tag, frames, RPMB_FRAME_SZ * frame_count, true);
    printf("[RPMB-UFS] SECURITY PROTOCOL OUT res=%d\n", res);

    printf("[RPMB-UFS] put_tag %u\n", tag);
    g_ufs_put_tag(ufs, tag);
    printf("[RPMB-UFS] send done res=%d\n", res);
    return res;
}

static int ufs_rpmb_xfer(
    uint32_t part,
    struct rpmb_frame *request,
    uint32_t request_count,
    struct rpmb_frame *response,
    uint32_t response_count
)
{
    void *ufs;
    u32 tag = 0;
    int got_tag;
    int res;

    if (part >= MAX_RPMB_PARTS) {
        printf("[RPMB-UFS] invalid RPMB partition %u\n", part);
        return -1;
    }

    if (!g_ufs_get_lu || !g_ufs_get_tag || !g_ufs_queuecommand || !g_ufs_put_tag) {
        printf("[RPMB-UFS] UFS helper pointers are not ready\n");
        return -1;
    }

    printf("[RPMB-UFS] xfer start: part=%u req_frames=%u resp_frames=%u req=0x%04x\n",
        part, request_count, response_count, bswap16(request[0].req_resp));

    ufs = g_ufs_get_lu(part);
    printf("[RPMB-UFS] get_lu(%u) -> %p\n", part, ufs);
    if (!ufs) {
        printf("[RPMB-UFS] failed to get UFS LU for RPMB partition %u\n", part);
        return -1;
    }

    got_tag = g_ufs_get_tag(ufs, &tag);
    printf("[RPMB-UFS] get_tag -> %d tag=%u\n", got_tag, tag);
    if (!got_tag) {
        printf("[RPMB-UFS] failed to allocate UFS tag\n");
        return -1;
    }

    printf("[RPMB-UFS] SECURITY PROTOCOL OUT len=0x%x\n", RPMB_FRAME_SZ * request_count);
    res = ufs_rpmb_command(ufs, tag, request, RPMB_FRAME_SZ * request_count, true);
    printf("[RPMB-UFS] SECURITY PROTOCOL OUT res=%d\n", res);
    if (res)
        goto out;

    memset(response, 0, RPMB_FRAME_SZ * response_count);
    printf("[RPMB-UFS] SECURITY PROTOCOL IN len=0x%x\n", RPMB_FRAME_SZ * response_count);
    res = ufs_rpmb_command(ufs, tag, response, RPMB_FRAME_SZ * response_count, false);
    printf("[RPMB-UFS] SECURITY PROTOCOL IN res=%d\n", res);

out:
    printf("[RPMB-UFS] put_tag %u\n", tag);
    g_ufs_put_tag(ufs, tag);
    printf("[RPMB-UFS] xfer done res=%d\n", res);
    return res;
}

struct hmac_stream {
    sha256_t ss;
    uint8_t kx[64];
};

static void hmac_stream_init(struct hmac_stream *ctx, const uint8_t *key)
{
    memset(ctx->kx, 0, sizeof(ctx->kx));
    memcpy(ctx->kx, key, RPMB_KEY_SZ);
    for (int i = 0; i < 64; i++)
        ctx->kx[i] ^= 0x36;
    sha256_init(&ctx->ss);
    sha256_update(&ctx->ss, ctx->kx, 64);
}

static void hmac_stream_update(struct hmac_stream *ctx, const uint8_t *data, size_t len)
{
    sha256_update(&ctx->ss, data, len);
}

static void hmac_stream_final(struct hmac_stream *ctx, uint8_t *out)
{
    sha256_final(&ctx->ss, out);
    for (int i = 0; i < 64; i++)
        ctx->kx[i] ^= (0x36 ^ 0x5c);
    sha256_init(&ctx->ss);
    sha256_update(&ctx->ss, ctx->kx, 64);
    sha256_update(&ctx->ss, out, 32);
    sha256_final(&ctx->ss, out);
}

static int rpmb_ufs_get_write_counter(
    uint32_t part,
    const uint8_t *rpmb_key,
    uint32_t *write_counter,
    bool verify_mac
)
{
    int res;

    memset(&g_frame, 0, sizeof(g_frame));
    g_frame.req_resp = bswap16(RPMB_GET_WRITE_COUNTER);

    res = ufs_rpmb_xfer(part, &g_frame, 1, &g_frame, 1);
    if (res)
        return res;

    res = bswap16(g_frame.result);
    if (res) {
        printf("[RPMB-UFS] get write counter result=0x%04x\n", res);
        return -res;
    }

    if (verify_mac) {
        uint8_t mac[32];
        hmac_sha256(mac, g_frame.data, RPMB_FRAME_SZ - RPMB_DATA_BEG, rpmb_key, RPMB_KEY_SZ);
        if (memcmp(mac, g_frame.key_mac, sizeof(mac)) != 0) {
            printf("[RPMB-UFS] write counter key verify FAIL\n");
            return -1;
        }
    }

    *write_counter = bswap32(g_frame.wr_cnt);
    printf("[RPMB-UFS] write counter=%u\n", *write_counter);
    return 0;
}

static int rpmb_ufs_init(uint32_t part, uint8_t *rpmb_key)
{
    uint32_t write_counter;

    if (part >= MAX_RPMB_PARTS)
        return -1;

    g_rpmb_initialized[part] = false;

    int res = rpmb_ufs_get_write_counter(part, rpmb_key, &write_counter, true);
    if (res)
        return res;

    g_rpmb_initialized[part] = true;
    return 0;
}

static bool rpmb_ufs_is_initialized(uint32_t part)
{
    if (part >= MAX_RPMB_PARTS)
        return false;

    return g_rpmb_initialized[part];
}

static int rpmb_ufs_read(uint32_t part, uint32_t address, uint8_t *data)
{
    int res;

    if (part >= MAX_RPMB_PARTS) {
        printf("[RPMB-UFS] invalid read partition %u\n", part);
        return -1;
    }

    printf("[RPMB-UFS] read one block: part=%u address=%u\n", part, address);

    memset(&g_frame, 0, sizeof(g_frame));
    g_frame.addr = bswap16((uint16_t)(address & 0xFFFF));
    g_frame.blk_cnt = bswap16(1);
    g_frame.req_resp = bswap16(RPMB_READ_DATA);

    res = ufs_rpmb_xfer(part, &g_frame, 1, &g_frame, 1);
    if (res)
        return res;

    res = bswap16(g_frame.result);
    if (res) {
        printf("[RPMB-UFS] read result=0x%04x\n", res);
        return -res;
    }

    memcpy(data, g_frame.data, RPMB_DATA_SZ);
    return 0;
}

static int rpmb_ufs_read_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data)
{
    printf("[RPMB-UFS] read blocks: part=%u address=%u blocks=%u\n", part, address, blocks);

    for (uint32_t i = 0; i < blocks; i++) {
        int res = rpmb_ufs_read(part, address + i, data + (i * RPMB_DATA_SZ));
        if (res)
            return res;
    }

    return 0;
}

static int rpmb_ufs_write(uint32_t part, uint32_t address, uint8_t *data, const uint8_t *rpmb_key)
{
    return rpmb_ufs_write_blocks(part, address, 1, data, rpmb_key);
}

static int rpmb_ufs_write_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data, const uint8_t *rpmb_key)
{
    int res;
    uint32_t wr_cnt;
    uint32_t chunk_size = 32;

    if (part >= MAX_RPMB_PARTS) {
        printf("[RPMB-UFS] invalid write partition %u\n", part);
        return -1;
    }

    if (!rpmb_key) {
        printf("[RPMB-UFS] write requires an RPMB key\n");
        return -1;
    }

    printf("[RPMB-UFS] write blocks: part=%u address=%u blocks=%u\n", part, address, blocks);

    res = rpmb_ufs_get_write_counter(part, rpmb_key, &wr_cnt, true);
    if (res)
        return res;

    for (uint32_t i = 0; i < blocks; i += chunk_size) {
        uint32_t curr_blks = blocks - i;
        if (curr_blks > chunk_size)
            curr_blks = chunk_size;

        struct hmac_stream hmac;
        hmac_stream_init(&hmac, rpmb_key);

        uint16_t req_resp_sw = bswap16(RPMB_WRITE_DATA);
        uint16_t blk_cnt_sw = bswap16((uint16_t)curr_blks);
        uint16_t addr_sw = bswap16((uint16_t)((address + i) & 0xFFFF));
        uint32_t wr_cnt_sw = bswap32(wr_cnt);
        uint8_t *batch_data = data + (i * RPMB_DATA_SZ);

        memset(g_write_frames, 0, curr_blks * sizeof(struct rpmb_frame));

        for (uint32_t j = 0; j < curr_blks; j++) {
            g_write_frames[j].req_resp = req_resp_sw;
            g_write_frames[j].blk_cnt = blk_cnt_sw;
            g_write_frames[j].addr = addr_sw;
            g_write_frames[j].wr_cnt = wr_cnt_sw;
            memcpy(g_write_frames[j].data, batch_data + (j * RPMB_DATA_SZ), RPMB_DATA_SZ);

            hmac_stream_update(
                &hmac,
                (uint8_t *)&g_write_frames[j] + RPMB_DATA_BEG,
                RPMB_FRAME_SZ - RPMB_DATA_BEG
            );
        }
        hmac_stream_final(&hmac, g_write_frames[curr_blks - 1].key_mac);

        printf("[RPMB-UFS] sending write chunk: sector=%u blocks=%u wr_cnt=%u\n",
            address + i, curr_blks, wr_cnt);
        res = ufs_rpmb_send_frames(part, g_write_frames, curr_blks);
        if (res)
            return res;

        memset(&g_frame, 0, sizeof(g_frame));
        g_frame.req_resp = bswap16(RPMB_RESULT_READ);

        printf("[RPMB-UFS] reading write result\n");
        res = ufs_rpmb_xfer(part, &g_frame, 1, &g_frame, 1);
        if (res)
            return res;

        res = bswap16(g_frame.result);
        if (res) {
            printf("[RPMB-UFS] write result=0x%04x\n", res);
            return -res;
        }

        printf("[RPMB-UFS] write chunk OK\n");
        wr_cnt++;
    }

    return 0;
}

static int rpmb_ufs_program_key(uint32_t part, const uint8_t *rpmb_key)
{
    (void)part;
    (void)rpmb_key;
    printf("[RPMB-UFS] program_key is not implemented in this experimental backend\n");
    return -1;
}

int rpmb_ufs_setup(
    ufs_get_lu_fn get_lu,
    ufs_get_tag_fn get_tag,
    ufs_queuecommand_fn queuecommand,
    ufs_put_tag_fn put_tag
)
{
    if (!get_lu || !get_tag || !queuecommand || !put_tag) {
        printf("[RPMB-UFS] missing UFS helper pointers\n");
        return -1;
    }

    g_ufs_get_lu = get_lu;
    g_ufs_get_tag = get_tag;
    g_ufs_queuecommand = queuecommand;
    g_ufs_put_tag = put_tag;

    for (u32 i = 0; i < MAX_RPMB_PARTS; i++)
        g_rpmb_initialized[i] = true;

    g_be.init = rpmb_ufs_init;
    g_be.is_initialized = rpmb_ufs_is_initialized;
    g_be.read_frame = rpmb_ufs_read;
    g_be.read_blocks = rpmb_ufs_read_blocks;
    g_be.write_frame = rpmb_ufs_write;
    g_be.write_blocks = rpmb_ufs_write_blocks;
    g_be.program_key = rpmb_ufs_program_key;
    rpmb_set_backend(&g_be);

    printf("[RPMB-UFS] backend setup complete\n");
    return 0;
}
