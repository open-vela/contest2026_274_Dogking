/****************************************************************************
 * vendor/allwinnertech/chips/a733/a733_image_header.S
 *
 * ARM64 Linux Image-compatible header for the vendor U-Boot `booti` path.
 * The first instruction branches over the 64-byte header to NuttX __start.
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

    .section .image_header, "ax"
    .balign  8
    .global  _image_start
    .type    _image_start, %function

_image_start:
    b       __start
    nop
    .quad   0x00200000                /* DRAM base + 2 MiB = 0x40200000 */
    .quad   _image_file_end - _image_start /* bytes present in Image */
    .quad   0                         /* U-Boot relocates relative to DRAM */
    .quad   0
    .quad   0
    .quad   0
    .word   0x644d5241                /* ARM64 Image magic: "ARMd" */
    .word   0
    .size   _image_start, . - _image_start
