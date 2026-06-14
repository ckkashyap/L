#pragma once
#include <stdint.h>
void      gfx_prims_register(void);
/* Used by tinygl_bridge.c to write its pixel buffer into the offscreen surface.
 * Returns pointer to XRGB8888 row-major pixel data. stride_bytes is row stride. */
uint32_t *gfx_get_offscreen_pixels(int *w, int *h, int *stride_bytes);
void      gfx_mark_pixels_dirty(void);
