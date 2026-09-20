/****************************************************************************
 * vendor/allwinnertech/boards/a733/cubie-a7z/scripts/dramboot.ld
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

OUTPUT_ARCH(aarch64)
ENTRY(_image_start)
EXTERN(_image_start)

PHDRS
{
  text PT_LOAD;
}

SECTIONS
{
  . = 0x40200000;

  .image_header :
  {
    KEEP(*(.image_header))
  } :text

  . = ALIGN(4096);

  .text :
  {
    _stext = .;
    KEEP(*(.start .start.*))
    *(.text .text.*)
    *(.fixup)
    *(.gnu.warning)
  } :text

  . = ALIGN(4096);
  .init_section :
  {
    _sinit = ABSOLUTE(.);
    KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)
           SORT_BY_INIT_PRIORITY(.ctors.*)))
    KEEP(*(.init_array EXCLUDE_FILE(*crtbegin.o *crtbegin?.o
                                    *crtend.o *crtend?.o) .ctors))
    _einit = ABSOLUTE(.);
  } :text

  . = ALIGN(4096);
  .vector :
  {
    _vector_start = .;
    KEEP(*(.exc_vector_table))
    KEEP(*(.exc_vector_table.*))
    KEEP(*(.vectors))
    _vector_end = .;
  } :text

  . = ALIGN(4096);
  _etext = .;
  _sztext = _etext - _stext;

  .rodata :
  {
    _srodata = .;
    *(.rodata .rodata.*)
    *(.data.rel.ro .data.rel.ro.*)
    _erodata = .;
  } :text
  _szrodata = _erodata - _srodata;
  _eronly = .;

  . = ALIGN(4096);
  .data :
  {
    _sdata = .;
    *(.data.page_aligned)
    *(.data .data.*)
    . = ALIGN(8);
    *(.data.rel .data.rel.*)
    CONSTRUCTORS
    _edata = .;
  } :text

  _image_file_end = .;

  .bss (NOLOAD) :
  {
    . = ALIGN(16);
    _sbss = .;
    *(.bss .bss.* COMMON)
    . = ALIGN(16);
    _ebss = .;
  } :text
  _szbss = _ebss - _sbss;

  /* Secondary CPUs write their boot stacks before enabling the data cache.
   * Never share a cache line with BSS or the CPU0-managed heap boundary.
   * Page alignment also keeps this invariant across application BSS growth.
   */
  . = ALIGN(4096);
  .initstack (NOLOAD) :
  {
    _s_initstack = .;
    *(.initstack)
    . = ALIGN(4096);
    _e_initstack = .;
    g_idle_topstack = .;
  } :text

  _szdata = _e_initstack - _sdata;
  _image_end = ALIGN(4096);

  ASSERT(_image_end <= 0x48000000,
         "A733 image overlaps the BL31 reserved region")
  ASSERT((g_cpu_idlestackalloc & 4095) == 0,
         "A733 per-CPU idle stacks must start on a page boundary")
  ASSERT((g_idle_topstack & 4095) == 0,
         "A733 idle stack and heap must not share a cache line")

  /DISCARD/ :
  {
    *(.exit.text .exit.data .exitcall.exit)
    *(.eh_frame .note .note.* .note.GNU-stack)
  }

  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .comment 0 : { *(.comment) }
}
