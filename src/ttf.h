#pragma once
#include <stdint.h>
typedef struct TtfFont TtfFont;
TtfFont *ttf_load(const char *path);
void     ttf_free(TtfFont *f);
/* Render one glyph. Returns malloc'd 8-bit alpha bitmap (caller frees).
 * w/h = bitmap dimensions. xoff/yoff = pixel offset from pen origin. */
uint8_t *ttf_render_glyph(TtfFont *f, uint32_t cp, int px,
                           int *w, int *h, int *xoff, int *yoff);
int      ttf_glyph_advance(TtfFont *f, uint32_t cp, int px);
void     ttf_line_metrics(TtfFont *f, int px, int *asc, int *desc, int *gap);
