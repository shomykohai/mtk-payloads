/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <storage/mmc/rpmb_mmc.h>
#include <storage/mmc/mmc.h>
#include <security/rpmb.h>
#include <crypto/hmac-sha256.h>
#include <crypto/sha256.h>
#include <libc.h>
#include <storage/mmc/mmc.h>
#include <debug.h>

#define RPMB_FRAME_SZ   512
#define RPMB_DATA_BEG   228

#define bswap16 __builtin_bswap16
#define bswap32 __builtin_bswap32

#include <mmio.h>

#define REG(dev, off)   ((uintptr_t)(dev)->base + (off))

static struct mmc_dev  g_mmc;
static bool g_mmc_valid;
static bool g_rpmb_initialized;
static struct rpmb_backend g_be;

__attribute__((aligned(64))) static struct rpmb_frame g_write_frames[32];

static int rpmb_mmc_init(uint32_t part, uint8_t *rpmb_key) {
    struct rpmb_frame f;
    int res;

    (void)part;

    if ((res = mmc_switch_part(&g_mmc, MMC_PART_RPMB)))
        return res;

    memset(&f, 0, sizeof(f));
    f.req_resp = bswap16(RPMB_GET_WRITE_COUNTER);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_GET_WRITE_COUNTER, 1))) goto out;
    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_GET_WRITE_COUNTER, 0))) goto out;

    printf("[RPMB] %s: write counter = %u\n", __func__, bswap32(f.wr_cnt));

    uint16_t result = bswap16(f.result);

    if (result == 0x0007) {
        printf("[RPMB] %s: key not programmed\n", __func__);
        res = -7;
        goto out;
    }
    if (result != 0) {
        printf("[RPMB] %s: unexpected result 0x%04x\n", __func__, result);
        res = -(int)result;
        goto out;
    }

    uint8_t mac[32];
    hmac_sha256(mac, f.data, RPMB_FRAME_SZ - RPMB_DATA_BEG, rpmb_key, RPMB_KEY_SZ);
    res = memcmp(mac, f.key_mac, 32) ? -1 : 0;
    if (res)
        printf("[RPMB] %s: key verify FAIL\n", __func__);

    g_rpmb_initialized = res == 0;

out:
    mmc_switch_part(&g_mmc, MMC_PART_USER);
    return res;
}

static int rpmb_mmc_read(uint32_t part, uint32_t address, uint8_t *data) {
    struct rpmb_frame f;
    int res;

    (void)part;

    if ((res = mmc_switch_part(&g_mmc, MMC_PART_RPMB)))
        return res;

    memset(&f, 0, sizeof(f));
    f.addr     = bswap16((uint16_t)(address & 0xFFFF));
    f.req_resp = bswap16(RPMB_READ_DATA);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_READ_DATA, 1))) goto out;
    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_READ_DATA, 0))) goto out;

    res = bswap16(f.result);
    if (!res)
        memcpy(data, f.data, RPMB_DATA_SZ);

out:
    mmc_switch_part(&g_mmc, MMC_PART_USER);
    return res;
}

static int rpmb_mmc_read_data_frames(struct mmc_dev *dev, uint8_t *out_data, uint16_t blks) {
    uint32_t cmd23_arg = (uint32_t)blks;

    writel_relaxed((uint32_t)blks, REG(dev, SDC_BLK_NUM));
    if (mmc_cmd(dev, MMC_SET_BLOCK_COUNT, cmd23_arg, MMC_RSP_R1, 0, 0, NULL))
        return -1;

    writel_relaxed((uint32_t)blks, REG(dev, SDC_BLK_NUM));
    if (mmc_cmd(dev, MMC_READ_MULTIPLE_BLOCK, 0, MMC_RSP_R1, 2, 0, NULL))
        return -1;

    uint32_t *dst = (uint32_t *)out_data;
    uint32_t words_left = (uint32_t)blks * 128;
    uint32_t frame_word = 0;
    uint32_t timeout = 1000000;
    volatile uint32_t *rx_reg = (volatile uint32_t *)REG(dev, MSDC_RXDATA);

    // This whole section below is so messy because we need to squeeze out
    // every bit of performance we can get.
    while (timeout--) {
        uint32_t ints = readl_relaxed(REG(dev, MSDC_INT));

        if (ints & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR)) {
            printf("[RPMB] %s: data error (ints=0x%08x)\n", __func__, ints);
            mmc_hw_reset(dev);
            return -1;
        }

        uint32_t cnt = (readl_relaxed(REG(dev, MSDC_FIFOCS))
                        & MSDC_FIFOCS_RXCNT) >> 2;

        while (cnt > 0 && words_left > 0) {
            uint32_t pos_in_frame = frame_word;
            uint32_t words_to_frame_end = 128 - pos_in_frame;
            uint32_t chunk = cnt < words_to_frame_end ? cnt : words_to_frame_end;
            if (chunk > words_left) chunk = words_left;

            uint32_t chunk_end = pos_in_frame + chunk;

            if (pos_in_frame < 57) {
                uint32_t skip = (chunk_end < 57 ? chunk_end : 57) - pos_in_frame;
                for (uint32_t s = skip; s >= 16; s -= 16) {
                    (void)*rx_reg; (void)*rx_reg; (void)*rx_reg; (void)*rx_reg;
                    (void)*rx_reg; (void)*rx_reg; (void)*rx_reg; (void)*rx_reg;
                    (void)*rx_reg; (void)*rx_reg; (void)*rx_reg; (void)*rx_reg;
                    (void)*rx_reg; (void)*rx_reg; (void)*rx_reg; (void)*rx_reg;
                }
                for (uint32_t s = skip & 15; s > 0; s--)
                    (void)*rx_reg;
                pos_in_frame += skip;
            }

            if (pos_in_frame >= 57 && pos_in_frame < 121 && pos_in_frame < chunk_end) {
                uint32_t keep = (chunk_end < 121 ? chunk_end : 121) - pos_in_frame;
                uint32_t c = keep;
                while (c >= 16) {
                    dst[0]  = *rx_reg; dst[1]  = *rx_reg;
                    dst[2]  = *rx_reg; dst[3]  = *rx_reg;
                    dst[4]  = *rx_reg; dst[5]  = *rx_reg;
                    dst[6]  = *rx_reg; dst[7]  = *rx_reg;
                    dst[8]  = *rx_reg; dst[9]  = *rx_reg;
                    dst[10] = *rx_reg; dst[11] = *rx_reg;
                    dst[12] = *rx_reg; dst[13] = *rx_reg;
                    dst[14] = *rx_reg; dst[15] = *rx_reg;
                    dst += 16; c -= 16;
                }
                while (c > 0) {
                    *dst++ = *rx_reg;
                    c--;
                }
                pos_in_frame += keep;
            }

            if (pos_in_frame >= 121 && pos_in_frame < chunk_end) {
                uint32_t skip = chunk_end - pos_in_frame;
                for (uint32_t s = skip; s > 0; s--)
                    (void)*rx_reg;
            }

            frame_word = (frame_word + chunk) & 127;
            words_left -= chunk;
            cnt -= chunk;
            timeout = 1000000;
        }

        if (!words_left)
            break;

        if ((ints & MSDC_INT_XFER_COMPL) &&
            (readl_relaxed(REG(dev, MSDC_FIFOCS)) & MSDC_FIFOCS_RXCNT) < 4)
            break;
    }

    if (timeout == 0) {
        printf("[RPMB] %s: timeout\n", __func__);
        mmc_hw_reset(dev);
        return -1;
    }

    uint32_t val;
    if (readl_poll_timeout(REG(dev, MSDC_INT), val,
                           (val & MSDC_INT_XFER_COMPL), 1000000)) {
        printf("[RPMB] %s: XFER_COMPL timeout\n", __func__);
        mmc_hw_reset(dev);
        return -1;
    }
    writel_relaxed(MSDC_INT_XFER_COMPL, REG(dev, MSDC_INT));

    if (words_left > 0) {
        printf("[RPMB] %s: early end (words_left=%u)\n", __func__, words_left);
        return -1;
    }

    return 0;
}

static int rpmb_mmc_read_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data) {
    int res = -1;
    struct rpmb_frame f;
    uint32_t chunk_size = 32768;

    (void)part;

    if ((res = mmc_switch_part(&g_mmc, MMC_PART_RPMB)))
        return res;

    for (uint32_t i = 0; i < blocks; i += chunk_size) {
        uint32_t current_blocks = blocks - i;
        if (current_blocks > chunk_size)
            current_blocks = chunk_size;

        memset(&f, 0, sizeof(f));
        f.addr     = bswap16((uint16_t)((address + i) & 0xFFFF));
        f.req_resp = bswap16(RPMB_READ_DATA);

        if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_READ_DATA, 1))) goto out;
        res = rpmb_mmc_read_data_frames(&g_mmc, data + (i * RPMB_DATA_SZ), current_blocks);
        if (res) goto out;
    }

out:
    mmc_switch_part(&g_mmc, MMC_PART_USER);
    return res;
}

static int rpmb_mmc_write(uint32_t part, uint32_t address, uint8_t *data, const uint8_t *rpmb_key) {
    struct rpmb_frame f;
    int res;

    (void)part;

    if ((res = mmc_switch_part(&g_mmc, MMC_PART_RPMB)))
        return res;

    memset(&f, 0, sizeof(f));
    f.req_resp = bswap16(RPMB_GET_WRITE_COUNTER);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_GET_WRITE_COUNTER, 1))) goto out;
    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_GET_WRITE_COUNTER, 0))) goto out;

    f.addr     = bswap16((uint16_t)(address & 0xFFFF));
    f.blk_cnt  = bswap16(1);
    f.result   = 0;
    f.req_resp = bswap16(RPMB_WRITE_DATA);
    memcpy(f.data, data, RPMB_DATA_SZ);

    hmac_sha256(f.key_mac, f.data, RPMB_FRAME_SZ - RPMB_DATA_BEG, rpmb_key, RPMB_KEY_SZ);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_WRITE_DATA, 1))) goto out;

    if ((res = mmc_wait_ready(&g_mmc))) goto out;

    memset(&f, 0, sizeof(f));
    f.req_resp = bswap16(RPMB_RESULT_READ);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_RESULT_READ, 1))) goto out;
    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_RESULT_READ, 0))) goto out;

    res = bswap16(f.result);

out:
    mmc_switch_part(&g_mmc, MMC_PART_USER);
    return res;
}

struct hmac_stream {
    sha256_t ss;
    uint8_t kx[64];
};

static void hmac_stream_init(struct hmac_stream *ctx, const uint8_t *key) {
    memset(ctx->kx, 0, 64);
    memcpy(ctx->kx, key, 32);
    for (int i = 0; i < 64; i++) ctx->kx[i] ^= 0x36;
    sha256_init(&ctx->ss);
    sha256_update(&ctx->ss, ctx->kx, 64);
}

static void hmac_stream_update(struct hmac_stream *ctx, const uint8_t *data, size_t len) {
    sha256_update(&ctx->ss, data, len);
}

static void hmac_stream_final(struct hmac_stream *ctx, uint8_t *out) {
    sha256_final(&ctx->ss, out);
    for (int i = 0; i < 64; i++) ctx->kx[i] ^= (0x36 ^ 0x5c);
    sha256_init(&ctx->ss);
    sha256_update(&ctx->ss, ctx->kx, 64);
    sha256_update(&ctx->ss, out, 32);
    sha256_final(&ctx->ss, out);
}

static int rpmb_mmc_write_blocks(uint32_t part, uint32_t address, uint32_t blocks, uint8_t *data, const uint8_t *rpmb_key) {
    struct rpmb_frame f;
    int res = -1;

    (void)part;

    if ((res = mmc_switch_part(&g_mmc, MMC_PART_RPMB)))
        return res;

    memset(&f, 0, sizeof(f));
    f.req_resp = bswap16(RPMB_GET_WRITE_COUNTER);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_GET_WRITE_COUNTER, 1))) goto out;
    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_GET_WRITE_COUNTER, 0))) goto out;

    uint32_t wr_cnt = bswap32(f.wr_cnt);

    uint32_t chunk_size = 32;

    for (uint32_t i = 0; i < blocks; i += chunk_size) {
        uint32_t curr_blks = blocks - i;
        if (curr_blks > chunk_size) curr_blks = chunk_size;

        struct hmac_stream hmac;
        hmac_stream_init(&hmac, rpmb_key);

        uint16_t req_resp_sw = bswap16(RPMB_WRITE_DATA);
        uint16_t blk_cnt_sw  = bswap16(curr_blks);
        uint16_t addr_sw     = bswap16((uint16_t)((address + i) & 0xFFFF));
        uint32_t wr_cnt_sw   = bswap32(wr_cnt);
        uint8_t *batch_data  = data + (i * RPMB_DATA_SZ);

        memset(g_write_frames, 0, curr_blks * sizeof(struct rpmb_frame));

        for (uint32_t j = 0; j < curr_blks; j++) {
            g_write_frames[j].req_resp = req_resp_sw;
            g_write_frames[j].blk_cnt  = blk_cnt_sw;
            g_write_frames[j].addr     = addr_sw;
            g_write_frames[j].wr_cnt   = wr_cnt_sw;
            memcpy(g_write_frames[j].data, batch_data + (j * RPMB_DATA_SZ), RPMB_DATA_SZ);

            hmac_stream_update(&hmac, (uint8_t *)&g_write_frames[j] + RPMB_DATA_BEG, RPMB_FRAME_SZ - RPMB_DATA_BEG);
        }
        hmac_stream_final(&hmac, g_write_frames[curr_blks - 1].key_mac);

        if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)g_write_frames, curr_blks, RPMB_WRITE_DATA, 1))) goto out;
        if ((res = mmc_wait_ready(&g_mmc))) goto out;

        memset(&f, 0, sizeof(f));
        f.req_resp = bswap16(RPMB_RESULT_READ);

        if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_RESULT_READ, 1))) goto out;
        if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_RESULT_READ, 0))) goto out;

        res = bswap16(f.result);
        if (res) {
            goto out;
        }

        wr_cnt++;
    }

out:
    mmc_switch_part(&g_mmc, MMC_PART_USER);
    return res;
}

static int rpmb_mmc_program_key(uint32_t part, const uint8_t *rpmb_key) {
    struct rpmb_frame f;
    int res;

    (void)part;

    if (!g_mmc_valid)
        return -1;

    if ((res = mmc_switch_part(&g_mmc, MMC_PART_RPMB)))
        return res;

    memset(&f, 0, sizeof(f));
    f.blk_cnt  = bswap16(1);
    f.req_resp = bswap16(RPMB_PROGRAM_KEY);
    memcpy(f.key_mac, rpmb_key, RPMB_KEY_SZ);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_PROGRAM_KEY, 1))) goto out;

    if ((res = mmc_wait_ready(&g_mmc))) goto out;

    memset(&f, 0, sizeof(f));
    f.req_resp = bswap16(RPMB_RESULT_READ);

    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_RESULT_READ, 1))) goto out;
    if ((res = mmc_rpmb_send(&g_mmc, (uint8_t *)&f, 1, RPMB_RESULT_READ, 0))) goto out;

    res = (int)bswap16(f.result);
    if (res)
        printf("[RPMB] %s: result=%d\n", __func__, res);

out:
    mmc_switch_part(&g_mmc, MMC_PART_USER);
    return res;
}

static bool rpmb_mmc_is_initialized(uint32_t part) {
    (void)part;
    return g_rpmb_initialized;
}

static void init_backend(void)
{
    g_be.init           = rpmb_mmc_init;
    g_be.is_initialized = rpmb_mmc_is_initialized;
    g_be.read_frame     = rpmb_mmc_read;
    g_be.read_blocks    = rpmb_mmc_read_blocks;
    g_be.write_frame    = rpmb_mmc_write;
    g_be.write_blocks   = rpmb_mmc_write_blocks;
    g_be.program_key    = rpmb_mmc_program_key;
    rpmb_set_backend(&g_be);
}

struct mmc_dev *rpmb_mmc_get_dev(void)
{
    return g_mmc_valid ? &g_mmc : NULL;
}

int rpmb_mmc_setup_dev(struct mmc_dev *dev)
{
    if (!dev)
        return -1;

    memcpy(&g_mmc, dev, sizeof(g_mmc));

    g_mmc_valid = true;
    init_backend();
    return 0;
}

int rpmb_mmc_setup(void *(*mmc_get_card)(int id)) {
    printf("%s: setting up RPMB backend\n", __func__);
    if (mmc_get_card) {
        void *card = mmc_get_card(0);
        if (card && mmc_dev_from_card(&g_mmc, card) == 0) {
            goto init;
        }
    }

    mmc_dev_setup(&g_mmc, 0x11230000, 1, NULL);
init:
    g_mmc_valid = true;
    init_backend();
    printf("%s: RPMB backend setup complete\n", __func__);
    return 0;
}
