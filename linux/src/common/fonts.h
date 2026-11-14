/*
 * The baked SDF fonts (see src/art/mkfont.py), linked straight into the binary.
 * Compile with -DFONT_DIR="\"/abs/path/to/src/common/fonts\"".
 *
 *   font_display  Inter Display Bold      (titles)        SIL Open Font License 1.1
 *   font_text     Inter Medium            (labels)        SIL Open Font License 1.1
 *   font_mono     JetBrains Mono Medium   (system check)  SIL Open Font License 1.1 / Apache 2.0
 */
#pragma once
#include <stdint.h>
#ifdef _WIN32
#define SG_RODATA ".section .rdata,\"dr\"\n"
#else
#define SG_RODATA ".section .rodata\n"
#endif
#define SG_EMBED(sym, file) \
	__asm__(SG_RODATA ".balign 16\n.globl " #sym "\n" #sym ":\n.incbin \"" FONT_DIR "/" file "\"\n.byte 0\n.text\n"); \
	extern const uint8_t sym[]
SG_EMBED(font_display, "display.sdf");
SG_EMBED(font_text, "text.sdf");
SG_EMBED(font_mono, "mono.sdf");
