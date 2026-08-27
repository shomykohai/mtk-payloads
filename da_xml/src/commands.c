/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include "xml.h"
#include <types.h>
#include <crypto/ssr/registers.h>
#include <debug.h>
#include <security/sej.h>
#include <mmio.h>
#include <drivers/uart.h>
#include <crypto/tzcc.h>
#include <crypto/key_derive.h>
#include <security/rpmb.h>
#include <storage/mmc/rpmb_mmc.h>
#include <storage/ufs/rpmb_ufs.h>
#include <da.h>
#include <protocol_functions.h>
#include <commands.h>
#include <nanoprintf.h>

#ifdef __aarch64__

#include <crypto/ssr/ssr.h>

#endif

volatile da_ctx_t g_da_ctx;

typedef struct {
    const char *name;
    int val;
} enum_map_t;

static int parse_enum(const char *str, const enum_map_t *map, size_t count, int default_val) {
    if (!str) return default_val;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(str, map[i].name) == 0) return map[i].val;
    }
    return default_val;
}

static const enum_map_t key_id_map[] = {
    {"SW_KEY", AES_SW_KEY}, {"SwKey", AES_SW_KEY},
    {"HW_KEY", AES_HW_KEY}, {"HwKey", AES_HW_KEY},
    {"HW_WRAPPED_KEY", AES_HW_WRAP_KEY}, {"HwWrappedKey", AES_HW_WRAP_KEY},
    {"RID_KEY", AES_RID_KEY}, {"RidKey", AES_RID_KEY},
    {"CUSTOM_KEY", AES_CUSTOM_KEY}, {"CustomKey", AES_CUSTOM_KEY},
};

static const enum_map_t key_sz_map[] = {
    {"KEY_128", AES_KEY_128}, {"Key128", AES_KEY_128},
    {"KEY_192", AES_KEY_192}, {"Key192", AES_KEY_192},
    {"KEY_256", AES_KEY_256}, {"Key256", AES_KEY_256},
};


int cmd_ack(struct com_channel_struct *channel, const char *xml) {
    (void)xml;
    int status = STATUS_OK;
    const char *target_file = "ack.xml";
    char ack_xml[512];

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    int len = npf_snprintf(ack_xml, sizeof(ack_xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<ack>"
        "<status>OK</status>"
        "<register_major_command>0x%08x</register_major_command>"
        "<malloc>0x%08x</malloc>"
        "<free>0x%08x</free>"
        "<get_node_text>0x%08x</get_node_text>"
        "<mxmlLoadString>0x%08x</mxmlLoadString>"
        "<mmc_get_card>0x%08x</mmc_get_card>"
        "</ack>",
        (unsigned int)(uintptr_t)register_major_command,
        (unsigned int)(uintptr_t)malloc,
        (unsigned int)(uintptr_t)free,
        (unsigned int)(uintptr_t)get_node_text,
        (unsigned int)(uintptr_t)mxmlLoadString,
        (unsigned int)(uintptr_t)mmc_get_card
    );

    status = upload(channel, target_file, ack_xml, (u32)len, "ACK");

    return status;
}

int cmd_da_ctx(struct com_channel_struct *channel, const char* xml) {
    int status = STATUS_OK;
    xml_parser_t tree;

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    XML_LOAD(tree, xml, "da/arg/tzcc_base", "da/arg/ssr_base",
                        "da/arg/sej_base", "da/arg/usb_log",
                        "da/arg/storage", "da/arg/da2_base",
                        "da/arg/da2_size", NULL);

    uintptr_t sej_base  = XML_ATOULL(tree, "da/arg/sej_base");
    uintptr_t tzcc_base = XML_ATOULL(tree, "da/arg/tzcc_base");
    u32 da2_addr  = XML_ATOULL(tree, "da/arg/da2_base");
    u32 da2_size   = XML_ATOULL(tree, "da/arg/da2_size");
    const char* storage = XML_TEXT(tree, "da/arg/storage");
    bool usb_log = XML_IS_YES(tree, "da/arg/usb_log");

#ifdef __aarch64__
    uintptr_t ssr_base = XML_ATOULL(tree, "da/arg/ssr_base");
    set_ssr_base(ssr_base);
#endif

    sej_init(sej_base);
    set_tzcc_base(tzcc_base);

    if (usb_log) {
        printf("Enabling USB logging\n");
        da_log_register(channel);
    }

    printf("SEJ base: 0x%08" PRIx32 "\n", sej_base);
    printf("TZCC base: 0x%08" PRIx32 "\n", tzcc_base);

    storage_type storage_type_enum;
    if (strncmp(storage, "EMMC", 4) == 0) {
        printf("Storage type: eMMC\n");
        storage_type_enum = STORAGE_EMMC;
        rpmb_mmc_setup(mmc_get_card);
    } else if (strncmp(storage, "UFS", 3) == 0) {
        printf("Storage type: UFS\n");
        if (rpmb_ufs_setup(
                ufs_get_lu,
                ufs_get_tag,
                ufs_queuecommand,
                ufs_put_tag,
                ufs_read_desc
            ) != 0) {
            printf("UFS RPMB helpers unavailable; continuing without RPMB support\n");
            storage_type_enum = STORAGE_UNKNOWN;
        } else {
            storage_type_enum = STORAGE_UFS;
        }
    } else {
        printf("Unsupported storage type in DA context: %s\n", storage);
        storage_type_enum = STORAGE_UNKNOWN;
    }

    g_da_ctx = (da_ctx_t){
        .tzcc_base = tzcc_base,
        .sej_base = sej_base,
        .da2_addr = da2_addr,
        .da2_size = da2_size,
        .storage = storage_type_enum,
        .usb_log = usb_log,
    };

    printf("DA context setup complete!\n");

    return status;
}

int cmd_readmem(struct com_channel_struct *channel, const char* xml) {
    int status = STATUS_OK;
    const char *target_file = "readmem.bin";
    xml_parser_t tree;
    uintptr_t address;
    u32 length;

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    XML_LOAD(tree, xml, "da/arg/address", "da/arg/length", NULL);

    address = XML_ATOULL(tree, "da/arg/address");
    length = XML_ATOULL(tree, "da/arg/length");

    printf("ReadMem: address=0x%08lx length=0x%08x\n", address, length);

    status = upload(channel, target_file, (const char*)address, length, "memory read");

    return status;
}

int cmd_writemem(struct com_channel_struct *channel, const char* xml) {
    int status = STATUS_OK;
    const char *source_file = "writemem.bin";
    xml_parser_t tree;
    char *address;
    u32 length;

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    XML_LOAD(tree, xml, "da/arg/address", "da/arg/length", NULL);

    address = (char *)(uintptr_t)XML_ATOULL(tree, "da/arg/address");
    length = XML_ATOULL(tree, "da/arg/length") + 4; // +4 or download fails on '*pdata_len <= total_length'

    printf("WriteMem: address=0x%08lx length=0x%08x\n", (uintptr_t)address, length);

    status = download(channel, source_file, &address, &length, "memory write");

    return status;
}

int cmd_readregister(struct com_channel_struct *channel, const char *xml) {
    int status = STATUS_OK;
    const char *target_file = "readreg.bin";
    xml_parser_t tree;
    uintptr_t address;
    char read_reg_xml[128];

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    XML_LOAD(tree, xml, "da/arg/address", NULL);

    address = XML_ATOULL(tree, "da/arg/address");

    printf("ReadRegister: address=0x%08" PRIxPTR "\n", address);

    u32 value = readl(address);

    u32 len = npf_snprintf(read_reg_xml, sizeof(read_reg_xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<register>"
        "<address>0x%08" PRIxPTR "</address>"
        "<value>0x%08" PRIx32 "</value>"
        "</register>",
        address, value
    );

    status = upload(channel, target_file, read_reg_xml, len, "register read");

    return status;
}

int cmd_writeregister(struct com_channel_struct *channel, const char *xml) {
    (void)channel;
    int status = STATUS_OK;
    xml_parser_t tree;
    uintptr_t address;
    u32 value;

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    XML_LOAD(tree, xml, "da/arg/address", "da/arg/value", NULL);

    address = XML_ATOULL(tree, "da/arg/address");
    value = XML_ATOULL(tree, "da/arg/value");

    printf("WriteRegister: address=0x%08lx value=0x%08x\n", address, value);

    writel(value, address);

    return status;
}

int cmd_key_derive(struct com_channel_struct *channel, const char *xml) {
    int status = STATUS_OK;
    u32 key_length = 0x20;
    u8 key[32] __attribute__((aligned(16))) = {0};
    char key_hex[65] = {0};

    u8 label[32] = {0};
    u8 salt[32] = {0};
    u32 label_len = 0;
    u32 salt_len = 0;

    xml_parser_t tree;
    const char *key_type_str = NULL;
    const char *key_len_str = NULL;
    const char *label_str = NULL;
    const char *salt_str = NULL;

    char key_derive_xml[256];
    const char *key_derive_fmt =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<key>"
        "<type>%s</type>"
        "<status>%d</status>"
        "<result>%s</result>"
        "</key>";

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    XML_LOAD(tree, xml, "da/arg", NULL);

    key_type_str = XML_TEXT(tree, "da/arg/key_type");
    key_len_str  = XML_TEXT(tree, "da/arg/key_length");

    if (key_len_str != NULL) {
        key_length = (u32)atoul(key_len_str);
    }

    if (key_length > sizeof(key) || (key_length != 0x10 && key_length != 0x20 && key_length != 0x18)) {
        printf("%s: Invalid key output length 0x%" PRIx32 "\n", __func__, key_length);
        set_error_msg("Invalid key derive output length");
        status = STATUS_ERR;
        goto out;
    }

    if (key_type_str != NULL && strcmp(key_type_str, "INPUT") == 0) {
        label_str = XML_TEXT(tree, "da/arg/label");
        salt_str  = XML_TEXT(tree, "da/arg/salt");

        if (label_str != NULL) {
            label_len = hex_to_bytes(label_str, label, sizeof(label));
        }
        if (salt_str != NULL) {
            salt_len = hex_to_bytes(salt_str, salt, sizeof(salt));
        }

        if (label_len > sizeof(label) || salt_len > sizeof(salt)) {
            printf("%s: label_len (%" PRIu32 ") or salt_len (%" PRIu32 ") exceeds max 32\n",
                   __func__, label_len, salt_len);
            set_error_msg("Invalid label or salt length");
            status = STATUS_ERR;
            goto out;
        }

        status = key_derive_input(label, label_len, salt, salt_len, key, key_length);

    } else {
        KeyType key_type = key_type_from_str(key_type_str);

        printf("Deriving key of type %s (%d)\n", key_type_str ? key_type_str : "NULL", key_type);
        if (da_key_derive != NULL) {
            printf("Key Derive: using native DA helper at %p\n", da_key_derive);
            u8 *native_key = (u8 *)malloc(key_length);
            if (native_key == NULL) {
                printf("Key Derive: failed to allocate native output buffer\n");
                set_error_msg("Failed to allocate native key output buffer");
                status = STATUS_ERR;
                goto out;
            }

            memset(native_key, 0, key_length);
            status = da_key_derive((u32)key_type, native_key, key_length);
            if (status == STATUS_OK)
                memcpy(key, native_key, key_length);

            memset(native_key, 0, key_length);
            free(native_key);
        } else {
            printf("Key Derive: native DA helper unavailable, using extension fallback\n");
            status = (int)key_derive(key_type, key, key_length);
        }
    }

    if (status != STATUS_OK) {
        printf("Key Derive failed with status=0x%08x\n", status);
        set_error_msg("Key derivation failed");
        status = STATUS_ERR;
        goto out;
    }

    if (status == STATUS_OK) {
        bytes_to_hex(key, key_length, key_hex);
    }

    u32 len = npf_snprintf(
        key_derive_xml, sizeof(key_derive_xml),
        key_derive_fmt,
        key_type_str ? key_type_str : "UNKNOWN",
        status,
        key_hex
    );

    status = upload(channel, "derived_key.xml", key_derive_xml, len, "derived key result");

out:
    return status;
}

int cmd_sej_aes(struct com_channel_struct *channel, const char* xml) {
    // V6 can handle it
    #define AES_MAX_LEN 0x800000

    int status = STATUS_OK;
    const char *source_file = "sej_aes.bin";
    u32 data_length = 0;
    void* data_buf = NULL;
    xml_parser_t tree;
    bool anti_clone;
    bool encrypt;
    sej_param_t params = {0};

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    XML_LOAD(tree, xml, "da/arg/encrypt", "da/arg/ac", NULL);

    encrypt    = XML_IS_YES(tree, "da/arg/encrypt");
    anti_clone = XML_IS_YES(tree, "da/arg/ac");

    params.mode = XML_IS_YES(tree, "da/arg/cbc") ? AES_CBC_MODE : AES_ECB_MODE;
    params.key_id= parse_enum(XML_TEXT(tree, "da/arg/key_id"), key_id_map, sizeof(key_id_map)/sizeof(key_id_map[0]), AES_SW_KEY);
    params.key_sz     = parse_enum(XML_TEXT(tree, "da/arg/key_size"), key_sz_map, sizeof(key_sz_map)/sizeof(key_sz_map[0]), AES_KEY_256);

    printf("SEJ AES: encrypt=%d anti_clone=%d\n", encrypt, anti_clone);

    status = download(channel, source_file, (char**)&data_buf, &data_length, "SEJ AES data");

    params.length = data_length;
    params.anti_clone = anti_clone;
    params.encrypt = encrypt;

    printf("SEJ AES: download status=%d data_buf=%p data_length=0x%08x\n",
        status, data_buf, data_length);

    if (status != STATUS_OK) {
        printf("SEJ AES: download failed\n");
        goto free;
    }

    if (data_length > AES_MAX_LEN) {
        printf("SEJ AES: rejecting data_length=0x%08x > AES_MAX_LEN=0x%08x\n",
            data_length, AES_MAX_LEN);

        set_error_msg("SEJ AES data length exceeds maximum allowed");
        status = STATUS_ERR;
        goto end;
    }

    if (encrypt)
        sp_sej_enc(data_buf, data_buf, params);
    else
        sp_sej_dec(data_buf, data_buf, params);

    status = upload(channel, source_file, (const char*)data_buf, data_length, "SEJ AES result");

    printf("SEJ AES: upload status=%d\n", status);

free:
    if (data_buf)
        free(data_buf);
end:
    printf("SEJ AES: done, status=%d\n", status);
    return status;
}

int cmd_rpmb_init(struct com_channel_struct *channel, const char *xml) {
    (void)channel;

    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    int status = STATUS_OK;
    xml_parser_t tree;

    u32 rpmb_part;
    u8 rpmbkey[32];
    const char *key_hex;

    XML_LOAD(tree, xml, "da/arg/partition", "da/arg/key", NULL);

    rpmb_part = XML_ATOULL(tree, "da/arg/partition");
    key_hex   = XML_TEXT(tree, "da/arg/key");

    printf("RPMB Init: partition=%u\n", rpmb_part);

    if (g_da_ctx.storage == STORAGE_UNKNOWN) {
        printf("Storage type unknown, cannot initialize RPMB!\n");
        set_error_msg("Storage type unknown, cannot initialize RPMB");
        status = STATUS_ERR;
        goto end;
    }

    if (rpmb_part >= MAX_RPMB_PARTS) {
        printf("Invalid RPMB partition %u (maximum %u)\n",
            rpmb_part, MAX_RPMB_PARTS - 1);
        status = STATUS_ERR;
        goto end;
    }

    if (strlen(key_hex) != 64) {
        printf("RPMB key must be 64 hex chars (32 bytes)\n");
        set_error_msg("RPMB key must be 64 hex chars (32 bytes)");
        status = STATUS_ERR;
        goto end;
    }

    if (hex_to_bytes(key_hex, rpmbkey, sizeof(rpmbkey)) < 0) {
        printf("Invalid RPMB key format\n");
        set_error_msg("Invalid RPMB key format");
        status = STATUS_ERR;
        goto end;
    }

    printf("Setting RPMB key for partition %u\n", rpmb_part);
    rpmb_set_key(rpmb_part, rpmbkey);

    /*
     * Always ask the backend to authenticate the supplied key.  The old UFS
     * backend marked itself initialized as soon as it was ready for reads.
     * Treating that readiness flag as proof that a key was installed skipped
     * rpmb_set_key()/rpmb_init() entirely and made authenticated writes use
     * the zero-filled global key.
     *
     * rpmb_init() only reads the write counter and verifies its MAC; it does
     * not program the one-time RPMB key, so re-validating is safe.
     */
    printf("Authenticating RPMB key for partition %u\n", rpmb_part);
    if (rpmb_init(rpmb_part) < 0) {
        printf("RPMB initialization failed\n");
        set_error_msg("RPMB initialization failed");
        status = STATUS_ERR;
    } else {
        printf("RPMB key authentication succeeded\n");
    }

end:
    return status;
}

int cmd_rpmb_info(struct com_channel_struct *channel, const char *xml) {
    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    int status = STATUS_OK;
    xml_parser_t tree;
    u32 rpmb_part;
    char info_xml[256];

    XML_LOAD(tree, xml, "da/arg/partition", NULL);
    rpmb_part = (u32)XML_ATOULL(tree, "da/arg/partition");

    if (rpmb_part >= MAX_RPMB_PARTS) {
        printf("Invalid RPMB partition %u (maximum %u)\n",
            rpmb_part, MAX_RPMB_PARTS - 1);
        return STATUS_ERR;
    }

    u32 sectors = rpmb_get_sector_count(rpmb_part);
    bool enabled = rpmb_is_region_enabled(rpmb_part);
    u32 bytes = sectors * RPMB_DATA_SZ;
    printf("RPMB Info: partition=%u enabled=%u sectors=%u bytes=0x%x\n",
        rpmb_part, enabled, sectors, bytes);

    u32 len = npf_snprintf(info_xml, sizeof(info_xml),
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<rpmb_info>"
        "<partition>%u</partition>"
        "<enabled>%s</enabled>"
        "<sector_count>%u</sector_count>"
        "<byte_size>%u</byte_size>"
        "</rpmb_info>",
        (unsigned int)rpmb_part,
        enabled ? "yes" : "no",
        (unsigned int)sectors,
        (unsigned int)bytes);

    status = upload(channel, "rpmb_info.xml", info_xml, len, "RPMB region information");
    return status;
}

int cmd_rpmb_read(struct com_channel_struct *channel, const char *xml) {
    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    int status = STATUS_OK;
    xml_parser_t tree;
    u32 rpmb_part;
    u32 start_sector;
    u32 sector_count;

    XML_LOAD(tree, xml, "da/arg/partition", "da/arg/start_sector", "da/arg/sectors_count", NULL);

    rpmb_part = (u32)XML_ATOULL(tree, "da/arg/partition");
    start_sector = (u32)XML_ATOULL(tree, "da/arg/start_sector");
    sector_count = (u32)XML_ATOULL(tree, "da/arg/sectors_count");

    if (g_da_ctx.storage == STORAGE_UNKNOWN) {
        printf("Storage type unknown, cannot read RPMB!\n");
        set_error_msg("Storage type unknown, cannot read RPMB");
        status = STATUS_ERR;
        goto end;
    }

    if (rpmb_part >= MAX_RPMB_PARTS) {
        printf("Invalid RPMB partition %u (maximum %u)\n",
            rpmb_part, MAX_RPMB_PARTS - 1);
        status = STATUS_ERR;
        goto end;
    }

    /* UFS authenticated reads are optional; eMMC still needs backend init. */
    if (g_da_ctx.storage != STORAGE_UFS && rpmb_is_initialized(rpmb_part) == false) {
        printf("RPMB partition %u not initialized!\n", rpmb_part);
        set_error_msg("RPMB partition not initialized");
        status = STATUS_ERR;
        goto end;
    }

    u32 data_len = sector_count * RPMB_DATA_SZ;
    printf("RPMB: Reading 0x%" PRIx32 " bytes from partition %u, starting at sector %u\n", data_len, rpmb_part, start_sector);

    struct rpmb_stream_ctx rctx = { .rpmb_part = rpmb_part, .start_sector = start_sector };

    status = upload_stream(channel, "rpmb_read.bin", data_len, 0, rpmb_read_stream_cb, &rctx, "RPMB Read");

    if (status == STATUS_OK) {
        printf("Finished reading RPMB\n");
    } else {
        printf("RPMB read failed with error %d\n", status);
        set_error_msg("RPMB read failed");
        status = STATUS_ERR;
    }

end:
    return status;
}

int cmd_rpmb_write(struct com_channel_struct *channel, const char *xml) {
    printf("\n\n*** Enter [%s] Cmd ***\n\n", __func__);

    clear_error_msg();

    int status = STATUS_OK;
    xml_parser_t tree;
    u32 rpmb_part;
    u32 start_sector;
    u32 sector_count;

    XML_LOAD(tree, xml, "da/arg/partition", "da/arg/start_sector", "da/arg/sectors_count", NULL);

    rpmb_part = (u32)XML_ATOULL(tree, "da/arg/partition");
    start_sector = (u32)XML_ATOULL(tree, "da/arg/start_sector");
    sector_count = (u32)XML_ATOULL(tree, "da/arg/sectors_count");

    if (g_da_ctx.storage == STORAGE_UNKNOWN) {
        printf("Storage type unknown, cannot write RPMB!\n");
        set_error_msg("Storage type unknown, cannot write RPMB");
        status = STATUS_ERR;
        goto end;
    }

    if (rpmb_part >= MAX_RPMB_PARTS) {
        printf("Invalid RPMB partition %u (maximum %u)\n",
            rpmb_part, MAX_RPMB_PARTS - 1);
        status = STATUS_ERR;
        goto end;
    }

    if (rpmb_is_initialized(rpmb_part) == false) {
        printf("RPMB partition %u not initialized!\n", rpmb_part);
        set_error_msg("RPMB partition not initialized");
        status = STATUS_ERR;
        goto end;
    }

    u32 data_len = sector_count * RPMB_DATA_SZ;
    printf("RPMB: Writing 0x%" PRIx32 " bytes to partition %u, starting at sector %u\n", data_len, rpmb_part, start_sector);

    struct rpmb_stream_ctx rctx = { .rpmb_part = rpmb_part, .start_sector = start_sector };

    status = download_stream(channel, "rpmb_write.bin", 32 * 1024, rpmb_write_stream_cb, &rctx, "RPMB Write");

    if (status == STATUS_OK) {
        printf("Finished writing RPMB\n");
    } else {
        printf("RPMB write failed with error %d\n", status);
        set_error_msg("RPMB write failed");
        status = STATUS_ERR;
    }

end:
    return status;
}
