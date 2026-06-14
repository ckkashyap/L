#include "ttf.h"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    TtfFont *f = ttf_load("deps/fonts/JetBrainsMono-Regular.ttf");
    if (!f) { fprintf(stderr, "FAIL: ttf_load returned NULL\n"); return 1; }
    int asc, desc, gap;
    ttf_line_metrics(f, 16, &asc, &desc, &gap);
    printf("Line metrics at 16px: asc=%d desc=%d gap=%d\n", asc, desc, gap);
    int adv = ttf_glyph_advance(f, 'A', 16);
    printf("Advance for 'A' at 16px: %d\n", adv);
    int w, h, xoff, yoff;
    uint8_t *bmp = ttf_render_glyph(f, 'A', 16, &w, &h, &xoff, &yoff);
    if (!bmp) { fprintf(stderr, "FAIL: ttf_render_glyph returned NULL\n"); ttf_free(f); return 1; }
    printf("Glyph 'A' at 16px: %dx%d bitmap, offset (%d,%d)\n", w, h, xoff, yoff);
    /* Print ASCII art */
    for (int y = 0; y < h && y < 20; y++) {
        for (int x = 0; x < w && x < 30; x++)
            putchar(bmp[y*w+x] > 127 ? '#' : '.');
        putchar('\n');
    }
    free(bmp);
    ttf_free(f);
    printf("PASS\n");
    return 0;
}
