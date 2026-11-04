/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_lowputc.S
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#include <nuttx/config.h>

#include "arm64_macro.inc"

#define A733_UART0_BASE 0x02500000
#define UART_THR        0x00
#define UART_LSR        0x14
#define UART_LSR_THRE   0x20

    .file "a733_lowputc.S"

GTEXT(arm64_lowputc)
SECTION_FUNC(text, arm64_lowputc)
    ldr x15, =A733_UART0_BASE
1:
    ldr w2, [x15, #UART_LSR]
    tst w2, #UART_LSR_THRE
    b.eq 1b
    str w0, [x15, #UART_THR]
    ret

