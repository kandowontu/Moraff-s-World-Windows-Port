#ifndef MR_BIOS_FONT_H
#define MR_BIOS_FONT_H

#include <stddef.h>
#include <stdint.h>

/* DUNSMALL and the other QuickBASIC modules ultimately print through
 * BRUN30's BIOS INT 10h/AH=09h path.  This exposes the printable part of the
 * IBM 8x8 BIOS character generator used by that service. */
const uint8_t *mr_bios_font_glyph(uint8_t character);
int mr_bios_font_self_test(char *error, size_t error_size);

#endif /* MR_BIOS_FONT_H */
