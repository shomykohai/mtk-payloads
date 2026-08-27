/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy, R0rt1z2
 */

#include <types.h>
#include <libc.h>
#include <xml.h>
#include <heap.h>
#include <commands.h>
#include <debug.h>
#include <mmio.h>

#define STATUS_OK   0x00000000
#define STATUS_ERR  0xC0010001

#define MXML_TYPE_ELEMENT  1
#define MXML_TYPE_TEXT     2
#define MXML_TYPE_OPAQUE   3

int download(struct com_channel_struct *channel, const char *filename, char **data_buf, u32 *data_len, const char *desc) {
    printf("Download host file: %s\n", filename);

    if (!channel || !data_buf || !data_len) {
        printf("Invalid arguments to download\n");
        return STATUS_ERR;
    }

    u32 packet_length = 0x200000;
    char xml[XML_CMD_BUFF_LEN] = {0};
    int xml_len = npf_snprintf(xml, sizeof(xml),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<host><version>1.0</version>"
        "<command>CMD:DOWNLOAD-FILE</command>"
        "<arg><checksum>CHK_NO</checksum>"
        "<info>%s</info><source_file>%s</source_file>"
        "<packet_length>0x%x</packet_length></arg></host>",
        desc, filename, (unsigned int)packet_length);

    if (channel->write((u8 *)xml, (u32)xml_len + 1) != 0) {
        printf("Failed to send download command\n");
        return STATUS_ERR;
    }

    char result[CMD_RESULT_BUFF_LEN] = {0};
    u32 len = sizeof(result);
    if (channel->read((u8 *)result, &len) != 0) return STATUS_ERR;

    char *vec[2] = {0};
    split(result, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) return STATUS_ERR;

    char buf_total_length[CMD_RESULT_BUFF_LEN] = {0};
    len = sizeof(buf_total_length);
    if (channel->read((u8 *)buf_total_length, &len) != 0) {
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    split(buf_total_length, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) {
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    u32 total_length = atoui(vec[1]);

    if (*data_len <= total_length) {
        printf("Buffer too small: have %u, need %u\n", *data_len, total_length + 1);
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    *data_len = total_length;
    memset(*data_buf, 0, total_length + 4);

    channel->write((u8 *)"OK", 3);

    u32 xfered = 0;
    while (xfered < total_length) {
        len = sizeof(result);
        if (channel->read((u8 *)result, &len) != 0 || strncmp(result, "OK", 2) != 0) {
            channel->write((u8 *)"ERR", 4);
            return STATUS_ERR;
        }
        channel->write((u8 *)"OK", 3);

        u32 chunk = (total_length - xfered > packet_length) ? packet_length : (total_length - xfered);

        if (channel->read((u8 *)*data_buf + xfered, &chunk) != 0) {
            channel->write((u8 *)"ERR", 4);
            return STATUS_ERR;
        }
        xfered += chunk;

        channel->write((u8 *)"OK", 3);
    }

    (*data_buf)[total_length] = 0;
    printf("Downloaded %u bytes for %s\n", xfered, desc);
    return STATUS_OK;
}

int cmd_patch_mem(struct com_channel_struct *channel, const char *xml)
{
    xml_parser_t tree;

    XML_LOAD(tree, xml, "da/arg/address", "da/arg/length", NULL);

    u32 addr = XML_ATOULL(tree, "da/arg/address");
    u32 len  = XML_ATOULL(tree, "da/arg/length");

    printf("%s: patching %lu bytes at 0x%lx\n",
           __func__, len, addr);

    char *dst = (char *)(uptr)addr;

    /* + 4 or download fails on '*pdata_len <= total_length' */
    u32 size = len + 4;

    int status = download(channel, "mempatch.bin", &dst, &size, "memory patch");
    if (status != STATUS_OK) {
        printf("%s: download failed: 0x%lx\n", __func__, (unsigned long)status);
        return status;
    }

    /* Make downloaded code/data visible before the DA executes it. */
    invalidate_icache_range((uptr)addr, size);

    return STATUS_OK;
}

int cmd_call_function(struct com_channel_struct *channel, const char *xml)
{
    (void)channel;

    xml_parser_t tree;

    XML_LOAD(tree, xml, "da/arg/address", NULL);

    u32 addr = XML_ATOULL(tree, "da/arg/address");

    printf("%s: scheduling call to 0x%lx\n", __func__, addr);

    get_cmd_dpc()->cb = (cmd_dpc_cb)(uptr)addr;

    return STATUS_OK;
}
