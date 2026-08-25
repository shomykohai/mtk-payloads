/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <types.h>
#include <mmio.h>
#include <libc.h>
#include <debug.h>
#include <crypto/tzcc.h>
#include <crypto/key_derive.h>
#include <security/rpmb.h>
#include <storage/mmc/rpmb_mmc.h>
#include <security/sej.h>
#include <hal.h>
#include <da.h>
#include <protocol.h>

volatile da_ctx_t g_da_ctx;

int cmd_ack(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    u32 ack=STATUS_OK;

    printf("Status OK!\n");
    return channel->write((u8 *)&ack,4);
}

int cmd_setup_da_ctx(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    da_ctx_t da_ctx = {0};
    u32 size = sizeof(da_ctx);
    int status = 0;

    status = channel->read((u8*)&da_ctx, &size);
    if (status != 0) {
        printf("Failed to read DA context!\n");
        printf("Some functionality may not work correctly.\n");
        return status;
    }

    g_da_ctx = da_ctx;

    if (g_da_ctx.usb_log) {
        printf("Enabling USB logging\n");
        da_log_register(channel);
    }

    sej_init(g_da_ctx.sej_base);
    set_tzcc_base(g_da_ctx.tzcc_base);

    printf("SEJ base: 0x%08" PRIx32 "\n", g_da_ctx.sej_base);
    printf("TZCC base: 0x%08" PRIx32 "\n", g_da_ctx.tzcc_base);
    printf("Read packet size: 0x%08" PRIx32 "\n", g_da_ctx.read_packet_size);
    printf("Write packet size: 0x%08" PRIx32 "\n", g_da_ctx.write_packet_size);

    if (g_da_ctx.storage == STORAGE_EMMC) {
        printf("Storage type: eMMC\n");
        rpmb_mmc_setup(mmc_get_card);
    } else {
        printf("Unsupported storage type in DA context: %u\n", g_da_ctx.storage);
        g_da_ctx.storage = STORAGE_UNKNOWN;
    }

    printf("DA context setup complete!\n");

    return 0;
}

int cmd_readmem(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    address_range_t range = {0};
    u32 range_size = sizeof(range);

    status = channel->read((u8*)&range, &range_size);
    if (status != 0) {
        printf("Failed to read memory range!\n");
        return status;
    }

    if (range.length == 0) {
        printf("Memory read length is invalid (%d bytes)!\n", range.length);
        return STATUS_INVALID_PARAMETERS;
    }

    channel->write((u8*)&status, 4);

    printf("Reading memory: address=0x%08" PRIx32 " length=0x%" PRIx32 "\n", range.start, range.length);

    u8* src = (u8*)(uptr)range.start;
    status = upload_data(channel, src, range.length, "Memory Read");

    return status;
}


int cmd_writemem(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    address_range_t range = {0};
    u32 range_size = sizeof(range);

    status = channel->read((u8*)&range, &range_size);
    if (status != 0) {
        printf("Failed to read memory range!\n");
        return status;
    }

    channel->write((u8*)&status, 4);

    printf("Writing memory: address=0x%08" PRIx32 " length=0x%" PRIx32 "\n", range.start, range.length);

    u8* dest = (u8*)(uptr)range.start;
    status = download_data(channel, &dest, range.length, "Memory Write");

    return status;
}

int cmd_readregister(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    u32 reg_addr = 0;
    u32 size = 4;
    u32 value = 0;

    status = channel->read((u8*)&reg_addr, &size);
    if (status != 0) {
        printf("Failed to read register address!\n");
        return status;
    }

    channel->write((u8*)&status, 4);

    reg_addr &= ~0x3;

    printf("Reading register: address=0x%08" PRIx32 "\n", reg_addr);

    value = readl((void*)(uptr)reg_addr);

    return channel->write((u8*)&value, 4);
}

int cmd_writeregister(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    u32 reg_addr = 0;
    u32 reg_value = 0;
    u32 size = 4;

    status = channel->read((u8*)&reg_addr, &size);
    if (status != 0) {
        printf("Failed to read register address!\n");
        return status;
    }

    status = channel->read((u8*)&reg_value, &size);
    if (status != 0) {
        printf("Failed to read register value!\n");
        return status;
    }

    reg_addr &= ~0x3;

    printf("Writing register: address=0x%08" PRIx32 " value=0x%08" PRIx32 "\n", reg_addr, reg_value);

    writel(reg_value, (void*)(uptr)reg_addr);

    return channel->write((u8*)&status, 4);
}

int cmd_key_derive(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    u32 length = sizeof(u32);
    u8 key[32] __attribute__((aligned(16))) = {0};
    u32 key_length = 0x20;
    int key_type = 0;
    u8 label[32] = {0};
    u8 salt[32] = {0};
    u32 label_len = 0;
    u32 salt_len = 0;

    status = channel->read((u8*)&key_type, &length);
    if (status != 0) {
        printf("Failed to read key type!\n");
        return status;
    }

    status = channel->read((u8*)&key_length, &length);
    if (status != 0) {
        printf("Failed to read key length!\n");
        return status;
    }

    if (key_type == INPUT_KEY) {
        status = channel->read((u8*)&label_len, &length);
        if (status != 0) {
            printf("Failed to read label length!\n");
            return status;
        }

        status = channel->read((u8*)&salt_len, &length);
        if (status != 0) {
            printf("Failed to read salt length!\n");
            return status;
        }

        if (label_len > sizeof(label) || salt_len > sizeof(salt)) {
            printf("%s: label_len (%" PRIu32 ") or salt_len (%" PRIu32 ") exceeds max 32\n",
                   __func__, label_len, salt_len);
            return STATUS_INVALID_KEY_SOURCE;
        }

        status = channel->read(label, &label_len);
        if (status != 0) {
            printf("Failed to read label!\n");
            return status;
        }

        status = channel->read(salt, &salt_len);
        if (status != 0) {
            printf("Failed to read salt!\n");
            return status;
        }
    }

    if (key_length > sizeof(key) || (key_length != 0x10 && key_length != 0x20 && key_length != 0x18)) {
        printf("%s: Invalid key output length 0x%" PRIx32 "\n", __func__, key_length);
        return STATUS_INVALID_KEY_LENGTH;
    }

    status = STATUS_OK;
    channel->write((u8*)&status, 4);

    if (key_type == INPUT_KEY) {
        printf("Deriving key from input material\n");
        status = key_derive_input(label, label_len, salt, salt_len, key, key_length);
    } else {
        printf("Deriving key of type %d\n", key_type);
        status = (int)key_derive(key_type, key, key_length);
    }

    if (status != 0) {
        printf("Key derivation failed with status %d\n", status);
        memset(key, 0, sizeof(key));
    }

    channel->write(key, key_length);

    return status;
}

int cmd_sej_aes(com_channel_struct *channel) {
    // 4 MB because the V5 DA heap is small
    #define AES_MAX_LEN 0x400000
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;

    sej_param_t params;

    u8* data_buf = NULL;

    u32 param_len = sizeof(sej_param_t);
    status = channel->read((u8*)&params, &param_len);
    if (status != 0) {
        printf("Failed to read SEJ parameters!\n");
        return status;
    }

    if (params.length > AES_MAX_LEN) {
        return STATUS_SEJ_EXCEED_MAX_LEN;
    }

    if (params.length == 0) {
        printf("SEJ AES length is invalid (%d bytes)!\n", params.length);
        return STATUS_INVALID_PARAMETERS;
    }

    data_buf = (u8*)malloc(params.length + 4);
    if (data_buf == NULL) {
        printf("Failed to allocate 0x%" PRIx32 " bytes for SEJ AES data\n", params.length);
        return STATUS_MALLOC_FAILED;
    }
    memset(data_buf, 0, params.length + 4);

    channel->write((u8*)&status, 4);

    printf("SEJ AES: encrypt=%d xor=%d key_id=%d legacy=%d anti_clone=%d mode=%d length=0x%" PRIx32 "\n",
            params.encrypt, params.xor_en, params.key_id, params.legacy, params.anti_clone, params.mode, params.length);

    status = download_data(channel, &data_buf, params.length, "SEJ AES Data");
    if (status != 0)
        goto out;

    if (params.encrypt)
        sp_sej_enc(data_buf, data_buf, params);
    else
        sp_sej_dec(data_buf, data_buf, params);

    status = upload_data(channel, data_buf, params.length, "SEJ AES Result");

out:
    free(data_buf);
    return status;
}

int cmd_rpmb_init(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    u32 size = 0x20;
    u32 status_len = 4;
    u32 rpmb_part = 0;
    u32 ack = STATUS_OK;
    u8 rpmbkey[0x20];

    status = channel->read((u8*)&rpmb_part, &status_len);
    if (status != 0) {
        printf("Failed to read RPMB partition!\n");
        return status;
    }

    status = channel->read((u8*)rpmbkey, &size);
    if (status != 0) {
        printf("Failed to read RPMB key!\n");
        return status;
    }

    if (g_da_ctx.storage == STORAGE_UNKNOWN) {
        printf("Storage type unknown, cannot initialize RPMB!\n");
        status = STATUS_RPMB_STORAGE_NOT_SUPPORTED;
        goto out;
    }

    if (rpmb_is_initialized(rpmb_part)) {
        printf("RPMB partition %u already initialized! Skipping\n", rpmb_part);
        goto out;
    }

    printf("Setting RPMB key for partition %u\n", rpmb_part);
    rpmb_set_key(rpmb_part, rpmbkey);

    printf("Initializing RPMB partition %u\n", rpmb_part);
    if (rpmb_init(rpmb_part) < 0)
        status = STATUS_RPMB_KEY_INVALID;

out:
    channel->write((u8*)&ack, 4);
    return status;
}

int cmd_rpmb_read(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    u32 status_sz = 4;
    u32 rpmb_part = 0;
    storage_range_t sector = {0};
    u32 sector_sz = sizeof(sector);

    status = channel->read((u8*)&rpmb_part, &status_sz);
    if (status != 0) {
        printf("Failed to read RPMB partition!\n");
        return status;
    }

    status = channel->read((u8*)&sector, &sector_sz);
    if (status != 0) {
        printf("Failed to read RPMB sector range!\n");
        return status;
    }

    u32 data_len = sector.sector_count * RPMB_DATA_SZ;

    if (g_da_ctx.storage == STORAGE_UNKNOWN) {
        printf("Storage type unknown, cannot read RPMB!\n");
        return STATUS_RPMB_STORAGE_NOT_SUPPORTED;
    }

    if (rpmb_is_initialized(rpmb_part) == false) {
        printf("RPMB partition %u not initialized!\n", rpmb_part);
        return STATUS_RPMB_NOT_INIT;
    }

    if (data_len == 0) {
        printf("RPMB read length is invalid (%d bytes)!\n", data_len);
        return STATUS_INVALID_PARAMETERS;
    }

    printf("RPMB: Reading 0x%" PRIx32 " bytes from partition %u, starting at sector %u\n", data_len, rpmb_part, sector.start_sector);

    struct rpmb_stream_ctx rctx = { .rpmb_part = rpmb_part, .start_sector = sector.start_sector };
    status = upload_data_stream(channel, data_len, 0, rpmb_read_stream_cb, &rctx, "RPMB Read");

    if (status == 0) {
        printf("Finished reading RPMB\n");
    } else if (status != STATUS_MALLOC_FAILED && status != STATUS_UPLOAD_ACK_NOT_OK) {
        printf("RPMB read failed with error %d\n", status);
        status = STATUS_RPMB_READ_FAILED;
    }

    return status;
}

int cmd_rpmb_write(com_channel_struct *channel) {
    printf("\n\n*** Enter %s cmd ***\n\n", __func__);

    int status = 0;
    u32 status_sz = 4;
    u32 rpmb_part = 0;
    storage_range_t sector = {0};
    u32 sector_sz = sizeof(sector);

    status = channel->read((u8*)&rpmb_part, &status_sz);
    if (status != 0) {
        printf("Failed to read RPMB partition!\n");
        return status;
    }

    status = channel->read((u8*)&sector, &sector_sz);
    if (status != 0) {
        printf("Failed to read RPMB sector range!\n");
        return status;
    }

    u32 data_len = sector.sector_count * RPMB_DATA_SZ;

    if (g_da_ctx.storage == STORAGE_UNKNOWN) {
        printf("Storage type unknown, cannot write RPMB!\n");
        return STATUS_RPMB_STORAGE_NOT_SUPPORTED;
    }

    if (rpmb_is_initialized(rpmb_part) == false) {
        printf("RPMB partition %u not initialized!\n", rpmb_part);
        return STATUS_RPMB_NOT_INIT;
    }

    channel->write((u8*)&status, status_sz);

    printf("RPMB: Writing 0x%" PRIx32 " bytes to partition %u, starting at sector %u\n", data_len, rpmb_part, sector.start_sector);

    struct rpmb_stream_ctx rctx = { .rpmb_part = rpmb_part, .start_sector = sector.start_sector };
    status = download_data_stream(channel, data_len, 32 * 1024, rpmb_write_stream_cb, &rctx, "RPMB Write");

    if (status == 0) {
        printf("Finished writing RPMB\n");
    } else if (status != STATUS_MALLOC_FAILED && status != STATUS_DOWNLOAD_ACK_NOT_OK) {
        printf("RPMB write failed with error %d\n", status);
        status = STATUS_RPMB_WRITE_FAILED;
    }

    return status;
}
