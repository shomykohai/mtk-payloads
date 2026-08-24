/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <types.h>
#include <libc.h>
#include <protocol.h>
#include <da.h>
#include <debug.h>

#define DA_LOG_BUF_SIZE 512

com_channel_struct* g_com_channel = NULL;

static char da_log_buf[DA_LOG_BUF_SIZE];
static int  da_log_pos = 0;

void *(* volatile malloc)(size_t size);;
void (* volatile free)(void *ptr);
void *(* volatile mmc_get_card)(int card_id);

static void da_log_flush(void)
{
    if (da_log_pos == 0 || g_com_channel == NULL)
        return;

    g_com_channel->log_to_pc((const u8 *)da_log_buf, (u32)da_log_pos);
    da_log_pos = 0;
}

static void da_log_putc(int ch, void *ctx)
{
    (void)ctx;

    if (da_log_pos >= DA_LOG_BUF_SIZE)
        da_log_flush();

    da_log_buf[da_log_pos++] = (char)ch;

    if (ch == '\n')
        da_log_flush();
}

void da_log_register(com_channel_struct *channel)
{
    if (g_com_channel != NULL)
        return;

    g_com_channel = channel;
    printf_register_cb(da_log_putc);
}


int download_data(com_channel_struct* channel, u8** dst, u64 size, const char* desc) {
    u32 packet_length = g_da_ctx.write_packet_size;
    u64 total_received = 0;
    u64 xfered = 0;
    u32 ack = 0;
    u32 ack_length = 4;
    u32 checksum = 0;

    if (size == 0) {
        printf("Length of %s download is invalid (%d bytes)!\n", desc, size);
        return STATUS_INVALID_PARAMETERS;
    }

    printf("Starting download of 0x%" PRIx32 " bytes for %s\n", (u32)size, desc);

    if (packet_length == 0)
        packet_length = 0x8000;

    if (*dst == NULL) {
        *dst = (u8*)malloc(size + 4);
        if (*dst == NULL) {
            printf("Failed to allocated %u bytes for %s\n", size, desc);
            return STATUS_MALLOC_FAILED;
        }
        memset(*dst, 0, size + 4);
    }

    while (total_received < size) {
        if (channel->read((u8*)&ack, &ack_length) != 0) {
            printf("Failed to read %s ack!\n", desc);
            return STATUS_DOWNLOAD_ACK_NOT_OK;
        }

        if (ack != 0) {
            printf("Host cancelled %s data transfer!\n", desc);
            return STATUS_DOWNLOAD_ACK_NOT_OK;
        }

        if (channel->read((u8*)&checksum, &ack_length) != 0) {
            printf("Failed to read %s checksum!\n", desc);
            return STATUS_DOWNLOAD_ACK_NOT_OK;
        }

        u32 to_receive = size - total_received;
        to_receive = to_receive > packet_length ? packet_length : to_receive;
        int status = channel->read((u8*)(*dst + total_received), &to_receive);

        channel->write((u8*)&status, 4);

        if (status != 0) {
            printf("Failed to read %s data packet!\n", desc);
            return status;
        }

        total_received += to_receive;
        xfered += to_receive;
    }

    // STATUS_OK
    channel->write((u8*)&ack, 4);

    printf("Download complete: received 0x%" PRIx32 " bytes for %s\n", (u32)xfered, desc);

    return ack;
}

int upload_data(com_channel_struct* channel, const u8* src, u64 size, const char* desc) {
    u32 packet_length = g_da_ctx.read_packet_size;
    if (packet_length == 0) packet_length = size;
    u64 total_sent = 0;
    u32 to_read = 4;

    if (size == 0) {
        printf("Length of %s upload is invalid (%d bytes)!\n", desc, size);
        return STATUS_INVALID_PARAMETERS;
    }

    printf("Starting upload of 0x%" PRIx32 " bytes for %s\n", (u32)size, desc);

    while (total_sent < size) {
        u32 to_send = (size - total_sent) > packet_length ? packet_length : (size - total_sent);
        int status = channel->write((u8*)(src + total_sent), to_send);
        if (status != 0) {
            printf("Failed to write %s data packet!\n", desc);
            return status;
        }

        u32 ack = 0;
        int ack_status = channel->read((u8*)&ack, &to_read);

        status = (ack_status != 0 || ack != 0) ? STATUS_UPLOAD_ACK_NOT_OK : 0;
        channel->write((u8*)&status, 4);

        if (status != 0) {
            printf("Host cancelled %s data transfer!\n", desc);
            return status;
        }

        total_sent += to_send;
    }

    printf("Upload complete: sent 0x%" PRIx32 " bytes for %s\n", (u32)total_sent, desc);

    return 0;
}

int download_data_stream(com_channel_struct* channel, u64 size, u32 chunk_size, data_stream_cb cb, void *ctx, const char* desc) {
    u64 total_received = 0;
    u32 ack = 0;
    u32 ack_length = 4;
    u32 checksum = 0;
    u32 zero = 0;
    int status = 0;

    if (size == 0) {
        printf("Length of %s download is invalid (%d bytes)!\n", desc, size);
        channel->write((u8*)&zero, 4);
        return 0;
    }

    if (chunk_size == 0) chunk_size = g_da_ctx.write_packet_size;
    if (chunk_size == 0) chunk_size = 0x10000;
    if (chunk_size > size) chunk_size = size;

    u8 *buf = malloc(chunk_size);
    if (!buf) return STATUS_MALLOC_FAILED;

    printf("Starting stream download of 0x%" PRIx32 " bytes for %s\n", (u32)size, desc);

    while (total_received < size) {
        status = channel->read((u8*)&ack, &ack_length);
        if (status != 0) {
            printf("Failed to read %s ack!\n", desc);
            status = STATUS_DOWNLOAD_ACK_NOT_OK;
            goto out;
        }

        if (ack != 0) {
            printf("Host cancelled %s data transfer!\n", desc);
            status = STATUS_DOWNLOAD_ACK_NOT_OK;
            goto out;
        }

        status = channel->read((u8*)&checksum, &ack_length);
        if (status != 0) {
            printf("Failed to read %s checksum!\n", desc);
            status = STATUS_DOWNLOAD_ACK_NOT_OK;
            goto out;
        }

        u32 to_receive = (size - total_received) > chunk_size ? chunk_size : (size - total_received);
        u32 chunk_received = 0;
        int chunk_status = 0;

        while (chunk_received < to_receive) {
            u32 read_len = to_receive - chunk_received;
            status = channel->read(buf, &read_len);
            if (status != 0) {
                printf("Failed to read %s data packet!\n", desc);
                chunk_status = status;
                break;
            }

            if (read_len == 0) {
                printf("Empty %s data packet, aborting!\n", desc);
                chunk_status = STATUS_DOWNLOAD_ACK_NOT_OK;
                break;
            }

            if (cb && chunk_status == 0) {
                int cb_status = cb(total_received + chunk_received, buf, read_len, ctx);
                if (cb_status != 0) {
                    printf("%s callback failed with status %d\n", desc, cb_status);
                    chunk_status = cb_status;
                }
            }

            chunk_received += read_len;
        }

        channel->write((u8*)&chunk_status, 4);

        if (chunk_status != 0) {
            status = chunk_status;
            goto out;
        }

        total_received += to_receive;
    }

    channel->write((u8*)&zero, 4);
    status = 0;

out:
    free(buf);
    return status;
}

int upload_data_stream(com_channel_struct* channel, u64 size, u32 chunk_size, data_stream_cb cb, void *ctx, const char* desc) {
    u64 total_sent = 0;
    u32 to_read = 4;
    u32 zero = 0;
    int status = 0;

    if (size == 0) {
        printf("Length of %s upload is invalid (%d bytes)!\n", desc, size);
        return STATUS_INVALID_PARAMETERS;
    }

    if (chunk_size == 0) chunk_size = g_da_ctx.read_packet_size;
    if (chunk_size == 0) chunk_size = 0x10000;
    if (chunk_size > size) chunk_size = size;

    u8 *buf = malloc(chunk_size);
    if (!buf) return STATUS_MALLOC_FAILED;

    printf("Starting stream upload of 0x%" PRIx32 " bytes for %s\n", (u32)size, desc);

    while (total_sent < size) {
        u32 to_send = (size - total_sent) > chunk_size ? chunk_size : (size - total_sent);

        if (cb) {
            status = cb(total_sent, buf, to_send, ctx);
            if (status != 0) {
                printf("%s callback failed with status %d\n", desc, status);
                channel->write((u8*)&status, 4);
                goto out;
            }
        }

        channel->write((u8*)&zero, 4);

        status = channel->write(buf, to_send);
        if (status != 0) {
            printf("Failed to write %s data packet!\n", desc);
            goto out;
        }

        u32 ack = 0;
        status = channel->read((u8*)&ack, &to_read);
        if (status != 0 || ack != 0) {
            status = STATUS_UPLOAD_ACK_NOT_OK;
            channel->write((u8*)&status, 4);
            printf("Host cancelled %s data transfer!\n", desc);
            goto out;
        }

        total_sent += to_send;
    }

    channel->write((u8*)&zero, 4);
    status = 0;

out:
    free(buf);
    return status;
}
