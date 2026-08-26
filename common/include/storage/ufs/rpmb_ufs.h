/*
 * SPDX-License-Identifier: MIT
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#ifndef RPMB_UFS_H
#define RPMB_UFS_H

#include <types.h>

typedef void *(*ufs_get_lu_fn)(u32 rpmb_part);
typedef int (*ufs_get_tag_fn)(void *ufs, u32 *tag);
typedef int (*ufs_queuecommand_fn)(void *ufs, void *cmd);
typedef void (*ufs_put_tag_fn)(void *ufs, u32 tag);
typedef int (*ufs_read_desc_fn)(
    void *ufs,
    u32 desc_id,
    u32 index,
    u32 selector,
    u8 *buffer,
    u32 length
);

int rpmb_ufs_setup(
    ufs_get_lu_fn get_lu,
    ufs_get_tag_fn get_tag,
    ufs_queuecommand_fn queuecommand,
    ufs_put_tag_fn put_tag,
    ufs_read_desc_fn read_desc
);

#endif /* RPMB_UFS_H */
