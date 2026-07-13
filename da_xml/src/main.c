/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <types.h>
#include <drivers/uart.h>
#include <protocol_functions.h>
#include <commands.h>
#include <logo.h>

extern u8 __bss_start[], __bss_end[];

__attribute__((used, section(".pointer_table")))
volatile pointer_table_t PTR_TABLE = {
    .magic = 0x54525450,

    .uart_base              = 0x00000000,
    .register_major_command = 0x00000000,
    .malloc                 = 0x00000000,
    .free                   = 0x00000000,
    .mmc_get_card           = 0x00000000,
    .ufs_get_lu             = 0x00000000,
    .ufs_get_tag            = 0x00000000,
    .ufs_queuecommand       = 0x00000000,
    .ufs_put_tag            = 0x00000000,
};

u32 init_pointers(void) {
    if (PTR_TABLE.magic != 0x54525450) {
        return 1;
    }

    mtk_uart_set_base(PTR_TABLE.uart_base);
    register_major_command  = (void *)(uptr)PTR_TABLE.register_major_command;
    malloc                  = (void *)(uptr)PTR_TABLE.malloc;
    free                    = (void *)(uptr)PTR_TABLE.free;
    mmc_get_card            = (void *)(uptr)PTR_TABLE.mmc_get_card;
    ufs_get_lu              = (void *)(uptr)PTR_TABLE.ufs_get_lu;
    ufs_get_tag             = (void *)(uptr)PTR_TABLE.ufs_get_tag;
    ufs_queuecommand        = (void *)(uptr)PTR_TABLE.ufs_queuecommand;
    ufs_put_tag             = (void *)(uptr)PTR_TABLE.ufs_put_tag;

    return 0;
}

__attribute__((section(".text.main"))) int main(void) {
    // Zero out BSS or printf callbacks won't work :D
    memset(__bss_start, 0, __bss_end - __bss_start);

    // Init functions pointers or we're screwed
    if(init_pointers()) {
        // This is pretty much unrecoverable,
        // so better to just go back to the caller.
        goto out;
    }

    // Register UART callback
    printf_register_cb(uart_putc);

    // BANNER TIME!!!
    printf(banner);

    printf("> Registering commands...\n");

    const char *ver1 = "1";
    register_major_command(CMD_ACK, ver1, (HHANDLE)cmd_ack);
    register_major_command(CMD_DA_CTX, ver1, (HHANDLE)cmd_da_ctx);
    register_major_command(CMD_READMEM, ver1, (HHANDLE)cmd_readmem);
    register_major_command(CMD_WRITEMEM, ver1, (HHANDLE)cmd_writemem);
    register_major_command(CMD_READREGISTER, ver1, (HHANDLE)cmd_readregister);
    register_major_command(CMD_WRITEREGISTER, ver1, (HHANDLE)cmd_writeregister);
    register_major_command(CMD_KEY_DERIVE, ver1, (HHANDLE)cmd_key_derive);
    register_major_command(CMD_SEJ, ver1, (HHANDLE)cmd_sej_aes);
    register_major_command(CMD_RPMB_INIT, ver1, (HHANDLE)cmd_rpmb_init);
    register_major_command(CMD_RPMB_READ, ver1, (HHANDLE)cmd_rpmb_read);
    register_major_command(CMD_RPMB_WRITE, ver1, (HHANDLE)cmd_rpmb_write);

    printf("\nAll done! See you on the other side :)\n\n\n");

out:
    return 0;
}
