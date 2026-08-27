/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#ifndef DA_XML_PROTOCOL_FUNCTIONS_H
#define DA_XML_PROTOCOL_FUNCTIONS_H

#include <types.h>
#include <libc.h>
#include <debug.h>
#include <stream.h>
#include <xml.h>

typedef struct __attribute__((packed)) pointer_table_t {
    u32 magic;
    u32 uart_base;
    u32 register_major_command;
    u32 clear_error_msg;
    u32 set_error_msg;
    u32 malloc;
    u32 free;
    u32 mmc_get_card;
    u32 ufs_get_lu;
    u32 ufs_get_tag;
    u32 ufs_queuecommand;
    u32 ufs_put_tag;
} pointer_table_t;

struct com_channel_struct {
  int (*read)(u8 *buffer, u32 *length);
  int (*write)(u8 *buffer, u32 length);
  int (*log_to_pc)(const u8 *buffer, u32 length);
  int (*log_to_uart)(const u8 *buffer, u32 length);
};

#define STATUS_OK (0x00000000)
#define STATUS_ERR (0xC0010001)

typedef int (*HHANDLE)(struct com_channel_struct* /* channel */, const char* /* xml */);

// Protocol functions
extern void (*volatile register_major_command)(const char *, const char *, HHANDLE);
extern void (*volatile clear_error_msg)(void);
extern void (*volatile set_error_msg)(const char *, ...);
int download(struct com_channel_struct *channel, const char *filename, char **data_buf, u32 *data_len, const char *desc);
int upload(struct com_channel_struct *channel, const char *filename, const char *data_buf, u32 data_len, const char *desc);
int download_stream(struct com_channel_struct *channel, const char *filename, u32 chunk_size, data_stream_cb cb, void *ctx, const char *desc);
int upload_stream(struct com_channel_struct *channel, const char *filename, u32 size, u32 chunk_size, data_stream_cb cb, void *ctx, const char *desc);

// DA logging - registers the channel globally and hooks up printf -> log_to_pc
void da_log_register(struct com_channel_struct *channel);

// Memory
extern void *(*volatile malloc)(size_t size);
extern void (*volatile free)(void *ptr);

// Storage
extern void *(*volatile mmc_get_card)(int card_id);
extern void *(*volatile ufs_get_lu)(u32 rpmb_part);
extern int (*volatile ufs_get_tag)(void *ufs, u32 *tag);
extern int (*volatile ufs_queuecommand)(void *ufs, void *cmd);
extern void (*volatile ufs_put_tag)(void *ufs, u32 tag);

// XML
extern char *(*volatile mxmlGetNodeText)(void* /* tree */, const char* /* path */);
extern void *(*volatile mxmlLoadString)(void*, const char*, void* /* callback */); // OPAQUE = 2

extern struct com_channel_struct *global_channel;

#endif //DA_XML_PROTOCOL_FUNCTIONS_H
