/* NetTerm firmware: everything in the 64 KiB RAM at 0 (nt_soc.sv); the stack
   at its top, growing down. */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0, LENGTH = 64K }
SECTIONS {
    .text : { *(.text.start) *(.text .text.*) } > RAM
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) } > RAM
    .data : { *(.data .data.* .sdata .sdata.*) } > RAM
    .bss (NOLOAD) : { __bss_start = .; *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); __bss_end = .; } > RAM
    __stack_top = ORIGIN(RAM) + LENGTH(RAM);
    /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }
}
