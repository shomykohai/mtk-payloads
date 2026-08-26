/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#ifndef DA_XML_COMMANDS_H
#define DA_XML_COMMANDS_H

#include <protocol_functions.h>

#define CMD_ACK "CMD:EXT-ACK"
#define CMD_DA_CTX "CMD:EXT-DA-CTX"
#define CMD_READMEM "CMD:EXT-READ-MEM"
#define CMD_WRITEMEM "CMD:EXT-WRITE-MEM"
#define CMD_READREGISTER "CMD:EXT-READ-REG"
#define CMD_WRITEREGISTER "CMD:EXT-WRITE-REG"
#define CMD_KEY_DERIVE "CMD:EXT-KEY-DERIVE"
#define CMD_SEJ "CMD:EXT-SEJ"
#define CMD_RPMB_INIT "CMD:EXT-RPMB-INIT"
#define CMD_RPMB_READ "CMD:EXT-RPMB-READ"
#define CMD_RPMB_WRITE "CMD:EXT-RPMB-WRITE"
#define CMD_RPMB_INFO "CMD:EXT-RPMB-INFO"


int cmd_ack(struct com_channel_struct *channel, const char *xml);
int cmd_da_ctx(struct com_channel_struct *channel, const char* xml);
int cmd_readmem(struct com_channel_struct *channel, const char *xml);
int cmd_writemem(struct com_channel_struct *channel, const char *xml);
int cmd_readregister(struct com_channel_struct *channel, const char *xml);
int cmd_writeregister(struct com_channel_struct *channel, const char *xml);
int cmd_key_derive(struct com_channel_struct *channel, const char *xml);
int cmd_sej_aes(struct com_channel_struct *channel, const char *xml);
int cmd_rpmb_init(struct com_channel_struct *channel, const char *xml);
int cmd_rpmb_read(struct com_channel_struct *channel, const char *xml);
int cmd_rpmb_write(struct com_channel_struct *channel, const char *xml);
int cmd_rpmb_info(struct com_channel_struct *channel, const char *xml);

#endif //DA_XML_COMMANDS_H
