.macro glabel label
    .global \label
    .balign 4
    \label:
.endm

.section .data

#Add Audio Data to ROM

glabel pbank_start
.incbin "asm/bank_instr.ptr"
.balign 16
glabel pbank_end

glabel wbank_start
.incbin "asm/bank_instr.wbk"
.balign 16
glabel wbank_end

glabel sng_menu_start
.incbin "asm/sng_menu.bin"
.balign 16
glabel sng_menu_end
