/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <types.h>
#include <protocol_functions.h>
#include <nanoprintf.h>
#include <debug.h>
#include <libc.h>

#define XML_CMD_BUFF_LEN 0x80000
#define CMD_RESULT_BUFF_LEN 64

#define DA_LOG_BUF_SIZE 512

struct com_channel_struct *global_channel;

static int  da_log_pos = 0;
char da_log_buf[DA_LOG_BUF_SIZE];

void (*volatile register_major_command)(const char *, const char *, HHANDLE);
void (*volatile clear_error_msg)(void);
void (*volatile set_error_msg)(const char *, ...);

void *(*volatile malloc)(size_t size);
void (*volatile free)(void *ptr);

char *(*volatile mxmlGetNodeText)(void*, const char*);
void *(*volatile mxmlLoadString)(void*, const char*, void*);

void *(*volatile mmc_get_card)(int card_id);
void *(*volatile ufs_get_lu)(u32 rpmb_part);
int (*volatile ufs_get_tag)(void *ufs, u32 *tag);
int (*volatile ufs_queuecommand)(void *ufs, void *cmd);
void (*volatile ufs_put_tag)(void *ufs, u32 tag);

static void da_log_flush(void) {
    if (da_log_pos == 0 || global_channel == NULL)
        return;

    global_channel->log_to_pc((const u8 *)da_log_buf, (u32)da_log_pos);
    da_log_pos = 0;
}

static void da_log_putc(int ch, void *ctx) {
    (void)ctx;

    if (da_log_pos >= DA_LOG_BUF_SIZE)
        da_log_flush();

    da_log_buf[da_log_pos++] = (char)ch;

    if (ch == '\n')
        da_log_flush();
}

void da_log_register(struct com_channel_struct *channel) {
    if (global_channel != NULL)
        return;

    global_channel = channel;
    printf_register_cb(da_log_putc);
}

int download(struct com_channel_struct *channel, const char *filename, char **data_buf, u32 *data_len, const char *desc) {
    printf("Download host file: %s\n", filename);

    if (!channel || !data_buf || !data_len) {
        printf("Invalid arguments to download\n");
        return STATUS_ERR;
    }

    u32 packet_length = 0x200000;
    char xml[XML_CMD_BUFF_LEN] = {0};
    const char *cstr_cmd = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                           "<host>"
                           "<version>1.0</version>"
                           "<command>CMD:DOWNLOAD-FILE</command>"
                           "<arg><checksum>CHK_NO</checksum>"
                           "<info>%s</info>"
                           "<source_file>%s</source_file>"
                           "<packet_length>0x%x</packet_length></arg>"
                           "</host>";

    int xml_len = npf_snprintf(xml, sizeof(xml), cstr_cmd, desc, filename, packet_length);

    // + 1 for accounting of \0
    if (channel->write((u8 *)xml, (u32)xml_len + 1) != 0) {
        printf("Failed to send download command\n");
        return STATUS_ERR;
    }

    char result[CMD_RESULT_BUFF_LEN] = {0};
    u32 len = sizeof(result);
    if (channel->read((u8 *)result, &len) != 0) {
        printf("Failed to read first response\n");
        return STATUS_ERR;
    }

    char *vec[2] = {0};
    split(result, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) {
        printf("Host reported error: %s\n", result);
        return STATUS_ERR;
    }

    char buf_total_length[CMD_RESULT_BUFF_LEN] = {0};
    len = sizeof(buf_total_length);
    if (channel->read((u8 *)buf_total_length, &len) != 0) {
        printf("Failed to read total length\n");
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    split(buf_total_length, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) {
        printf("Host reported error on length: %s\n", buf_total_length);
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    u32 total_length = atoui(vec[1]);
    printf("Download: total_length=0x%x\n", total_length);

    if (*data_buf != NULL) {
        if (*data_len <= total_length) {
            printf("Buffer too small: have %u, need %u\n", *data_len, total_length);
            channel->write((u8 *)"ERR", 4);
            return STATUS_ERR;
        }
        *data_len = total_length;
    } else {
        *data_len = total_length;
        *data_buf = (char *)malloc(total_length + 4);
        if (*data_buf == NULL) {
            printf("Failed to allocate %u bytes for %s\n", total_length, desc);
            channel->write((u8 *)"ERR", 4);
            return STATUS_ERR;
        }
        memset(*data_buf, 0, total_length + 4);
    }

    channel->write((u8 *)"OK", 3);

    u32 xfered = 0;
    while (xfered < total_length) {
        len = sizeof(result);
        if (channel->read((u8 *)result, &len) != 0) {
            printf("Failed to read chunk signal\n");
            channel->write((u8 *)"ERR", 4);
            return STATUS_ERR;
        }

        split(result, vec, 2, '@');
        if (strncmp(vec[0], "OK", 2) != 0) {
            printf("Host cancelled or errored: %s\n", result);
            channel->write((u8 *)"ERR", 4);
            return STATUS_ERR;
        }

        channel->write((u8 *)"OK", 3);

        u32 chunk = total_length - xfered;
        if (chunk > packet_length)
            chunk = packet_length;

        if (channel->read((u8 *)*data_buf + xfered, &chunk) != 0) {
            printf("Failed to read data chunk at offset 0x%x\n", xfered);
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

int upload(struct com_channel_struct *channel, const char *filename, const char *buf, u32 size, const char *desc) {
    printf("Upload host file: %s\n", filename);

    if (!channel || !filename) {
        printf("Invalid arguments to upload\n");
        return STATUS_ERR;
    }

    u32 packet_length = 0x200000;
    char xml[XML_CMD_BUFF_LEN] = {0};
    const char *cstr_cmd = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                           "<host>"
                           "<version>1.0</version>"
                           "<command>CMD:UPLOAD-FILE</command>"
                           "<arg><checksum>CHK_NO</checksum>"
                           "<info>%s</info>"
                           "<target_file>%s</target_file>"
                           "<packet_length>0x%x</packet_length></arg>"
                           "</host>";

    int xml_len = npf_snprintf(xml, sizeof(xml), cstr_cmd, desc, filename, packet_length);

    if (channel->write((u8 *)xml, (u32)xml_len + 1) != 0) {
        printf("Failed to send upload command\n");
        return STATUS_ERR;
    }

    char result[CMD_RESULT_BUFF_LEN] = {0};
    u32 len = sizeof(result);
    if (channel->read((u8 *)result, &len) != 0) {
        printf("Failed to read response\n");
        return STATUS_ERR;
    }

    char *vec[2] = {0};
    split(result, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) {
        printf("Host reported error: %s\n", result);
        return STATUS_ERR;
    }

    npf_snprintf(result, sizeof(result), "OK@0x%" PRIx32, size);
    channel->write((u8 *)result, (u32)strlen(result) + 1);

    len = sizeof(result);
    if (channel->read((u8 *)result, &len) != 0) {
        printf("Failed to read size ack\n");
        return STATUS_ERR;
    }

    u32 xfered = 0;
    u32 left = size;
    while (left > 0) {
        channel->write((u8 *)"OK", 3);

        len = sizeof(result);
        if (channel->read((u8 *)result, &len) != 0) {
            printf("Failed to read chunk ack\n");
            return STATUS_ERR;
        }

        u32 todo = left > packet_length ? packet_length : left;
        channel->write((u8 *)((uintptr_t)buf + xfered), todo);
        xfered += todo;
        left -= todo;

        len = sizeof(result);
        if (channel->read((u8 *)result, &len) != 0) {
            printf("Failed to read chunk status\n");
            return STATUS_ERR;
        }

        split(result, vec, 2, '@');
        if (strncmp(vec[0], "OK", 2) != 0) {
            printf("Host error during upload: %s\n", result);
            return STATUS_ERR;
        }
    }

    printf("Uploaded %u bytes for %s\n", xfered, desc);
    return STATUS_OK;
}

int download_stream(struct com_channel_struct *channel, const char *filename, u32 chunk_size, data_stream_cb cb, void *ctx, const char *desc) {
    printf("Download stream host file: %s\n", filename);

    if (!channel) {
        printf("Invalid arguments to download_stream\n");
        return STATUS_ERR;
    }

    u32 packet_length = 0x200000;
    if (chunk_size == 0) chunk_size = packet_length;
    if (chunk_size > packet_length) chunk_size = packet_length;

    char xml[XML_CMD_BUFF_LEN] = {0};
    const char *cstr_cmd = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                           "<host>"
                           "<version>1.0</version>"
                           "<command>CMD:DOWNLOAD-FILE</command>"
                           "<arg><checksum>CHK_NO</checksum>"
                           "<info>%s</info>"
                           "<source_file>%s</source_file>"
                           "<packet_length>0x%x</packet_length></arg>"
                           "</host>";

    int xml_len = npf_snprintf(xml, sizeof(xml), cstr_cmd, desc, filename, chunk_size);

    if (channel->write((u8 *)xml, (u32)xml_len + 1) != 0) {
        printf("Failed to send download command\n");
        return STATUS_ERR;
    }

    char result[CMD_RESULT_BUFF_LEN] = {0};
    u32 len = sizeof(result);
    if (channel->read((u8 *)result, &len) != 0) {
        printf("Failed to read first response\n");
        return STATUS_ERR;
    }

    char *vec[2] = {0};
    split(result, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) {
        printf("Host reported error: %s\n", result);
        return STATUS_ERR;
    }

    char buf_total_length[CMD_RESULT_BUFF_LEN] = {0};
    len = sizeof(buf_total_length);
    if (channel->read((u8 *)buf_total_length, &len) != 0) {
        printf("Failed to read total length\n");
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    split(buf_total_length, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) {
        printf("Host reported error on length: %s\n", buf_total_length);
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    u32 total_length = atoui(vec[1]);
    printf("Download stream: total_length=0x%x\n", total_length);

    u8 *buf = (u8 *)malloc(chunk_size);
    if (!buf) {
        printf("Failed to allocate %u bytes for %s stream\n", chunk_size, desc);
        channel->write((u8 *)"ERR", 4);
        return STATUS_ERR;
    }

    channel->write((u8 *)"OK", 3);

    u32 xfered = 0;
    int status = STATUS_OK;

    while (xfered < total_length) {
        len = sizeof(result);
        if (channel->read((u8 *)result, &len) != 0) {
            printf("Failed to read chunk signal\n");
            channel->write((u8 *)"ERR", 4);
            status = STATUS_ERR;
            break;
        }

        split(result, vec, 2, '@');
        if (strncmp(vec[0], "OK", 2) != 0) {
            printf("Host cancelled or errored: %s\n", result);
            channel->write((u8 *)"ERR", 4);
            status = STATUS_ERR;
            break;
        }

        channel->write((u8 *)"OK", 3);

        u32 chunk = total_length - xfered;
        if (chunk > chunk_size)
            chunk = chunk_size;

        if (channel->read(buf, &chunk) != 0) {
            printf("Failed to read data chunk at offset 0x%x\n", xfered);
            channel->write((u8 *)"ERR", 4);
            status = STATUS_ERR;
            break;
        }

        if (cb) {
            status = cb(xfered, buf, chunk, ctx);
            if (status != 0) {
                channel->write((u8 *)"ERR", 4);
                break;
            }
        }

        xfered += chunk;
        channel->write((u8 *)"OK", 3);
    }

    free(buf);

    if (status == STATUS_OK) {
        printf("Downloaded %u bytes via stream for %s\n", xfered, desc);
    }
    return status;
}

int upload_stream(struct com_channel_struct *channel, const char *filename, u32 size, u32 chunk_size, data_stream_cb cb, void *ctx, const char *desc) {
    printf("Upload stream host file: %s\n", filename);

    if (!channel || !filename) {
        printf("Invalid arguments to upload_stream\n");
        return STATUS_ERR;
    }

    u32 packet_length = 0x200000;
    if (chunk_size == 0) chunk_size = packet_length;
    if (chunk_size > packet_length) chunk_size = packet_length;

    char xml[XML_CMD_BUFF_LEN] = {0};
    const char *cstr_cmd = "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                           "<host>"
                           "<version>1.0</version>"
                           "<command>CMD:UPLOAD-FILE</command>"
                           "<arg><checksum>CHK_NO</checksum>"
                           "<info>%s</info>"
                           "<target_file>%s</target_file>"
                           "<packet_length>0x%x</packet_length></arg>"
                           "</host>";

    int xml_len = npf_snprintf(xml, sizeof(xml), cstr_cmd, desc, filename, chunk_size);

    if (channel->write((u8 *)xml, (u32)xml_len + 1) != 0) {
        printf("Failed to send upload command\n");
        return STATUS_ERR;
    }

    char result[CMD_RESULT_BUFF_LEN] = {0};
    u32 len = sizeof(result);
    if (channel->read((u8 *)result, &len) != 0) {
        printf("Failed to read response\n");
        return STATUS_ERR;
    }

    char *vec[2] = {0};
    split(result, vec, 2, '@');
    if (strncmp(vec[0], "OK", 2) != 0) {
        printf("Host reported error: %s\n", result);
        return STATUS_ERR;
    }

    npf_snprintf(result, sizeof(result), "OK@0x%" PRIx32, size);
    channel->write((u8 *)result, (u32)strlen(result) + 1);

    len = sizeof(result);
    if (channel->read((u8 *)result, &len) != 0) {
        printf("Failed to read size ack\n");
        return STATUS_ERR;
    }

    u8 *buf = (u8 *)malloc(chunk_size);
    if (!buf) {
        printf("Failed to allocate %u bytes for %s stream\n", chunk_size, desc);
        return STATUS_ERR;
    }

    u32 xfered = 0;
    u32 left = size;
    int status = STATUS_OK;

    while (left > 0) {
        channel->write((u8 *)"OK", 3);

        len = sizeof(result);
        if (channel->read((u8 *)result, &len) != 0) {
            printf("Failed to read chunk ack\n");
            status = STATUS_ERR;
            break;
        }

        u32 todo = left > chunk_size ? chunk_size : left;

        if (cb) {
            status = cb(xfered, buf, todo, ctx);
            if (status != 0) break;
        }

        channel->write(buf, todo);
        xfered += todo;
        left -= todo;

        len = sizeof(result);
        if (channel->read((u8 *)result, &len) != 0) {
            printf("Failed to read chunk status\n");
            status = STATUS_ERR;
            break;
        }

        split(result, vec, 2, '@');
        if (strncmp(vec[0], "OK", 2) != 0) {
            printf("Host error during upload: %s\n", result);
            status = STATUS_ERR;
            break;
        }
    }

    free(buf);

    if (status == STATUS_OK) {
        printf("Uploaded %u bytes via stream for %s\n", xfered, desc);
    }
    return status;
}
