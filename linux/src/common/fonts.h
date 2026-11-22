/*
 * The baked SDF fonts (see src/art/mkfont.py), linked straight into the binary.
 * Compile with -DFONT_DIR="\"/abs/path/to/src/common/fonts\"".
 *
 *   font_scrawl   Silver Scrawl           (titles: thick, messy; derived from Bowlby One)
 *   font_caption  Michroma                (tiny wide captions)
 *   font_text     Inter Medium            (readable sentences)
 *   font_mono     JetBrains Mono Medium   (code and boot logs; ASCII)
 * All SIL Open Font License 1.1; see fonts/LICENSES.md.
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
SG_EMBED(font_scrawl, "scrawl.sdf");
SG_EMBED(font_caption, "caption.sdf");
SG_EMBED(font_text, "text.sdf");
SG_EMBED(font_mono, "mono.sdf");
