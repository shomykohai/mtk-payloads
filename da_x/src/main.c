/*
 * SPDX-License-Identifier: AGPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Shomy
 */

#include <types.h>
#include <libc.h>
#include <debug.h>
#include <security/sej.h>
#include <drivers/uart.h>
#include <logo.h>
#include <hal.h>
#include <protocol.h>
#include <commands.h>


extern uint8_t __bss_start[], __bss_end[];

int (* volatile register_device_ctrl)(u32 /*ctrl_code*/, HHANDLE /*handle*/);

__attribute__((used, section(".pointer_table")))
volatile pointer_table_t PTR_TABLE = {
    .magic = 0x54525450,

    .uart_base              = 0x00000000,
    .register_device_ctrl   = 0x00000000,
    .malloc                 = 0x00000000,
    .free                   = 0x00000000,
    .mmc_get_card           = 0x00000000,
};

u32 init_pointers(void) {

    if (PTR_TABLE.magic != 0x54525450)
        return 1;

    mtk_uart_set_base(PTR_TABLE.uart_base);
    register_device_ctrl    = (void *)(uptr)PTR_TABLE.register_device_ctrl;
    malloc                  = (void *)(uptr)PTR_TABLE.malloc;
    free                    = (void *)(uptr)PTR_TABLE.free;
    mmc_get_card            = (void *)(uptr)PTR_TABLE.mmc_get_card;

    return 0;
}

__attribute__ ((section(".text.main"), used)) int main(void) {
    // Clear BSS or good luck getting static working hehe
    memset(__bss_start, 0, __bss_end - __bss_start);

    if (init_pointers() != 0) {
        return 1;
    }

    // Init UART print callback before we print the banner
    printf_register_cb(uart_putc);

    // Banner time!!
    printf(banner);

    printf("> Built at %s %s\n", __DATE__, __TIME__);

    printf("> Registering commands\n");
    register_device_ctrl(0xF0000,(void*)cmd_ack);
    register_device_ctrl(0xF0001,(void*)cmd_setup_da_ctx);
    register_device_ctrl(0xF0002,(void*)cmd_readmem);
    register_device_ctrl(0xF0003,(void*)cmd_writemem);
    register_device_ctrl(0xF0004,(void*)cmd_readregister);
    register_device_ctrl(0xF0005,(void*)cmd_writeregister);
    register_device_ctrl(0xF0006,(void*)cmd_key_derive);
    register_device_ctrl(0xF0007,(void*)cmd_sej_aes);
    register_device_ctrl(0xF0008,(void*)cmd_rpmb_init);
    register_device_ctrl(0xF0009,(void*)cmd_rpmb_read);
    register_device_ctrl(0xF000A,(void*)cmd_rpmb_write);

    printf("\nAll done! See you on the other side :)\n\n\n");

    return 0;
}
