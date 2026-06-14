#include "picolisp.h"

#ifdef HAVE_TINYGL
#include <GL/gl.h>
#include <zbuffer.h>
#include "native_gfx.h"

/* The internal pixel format is fixed at compile time by TGL_FEATURE_RENDER_BITS:
 *   32-bit (C-Chads default): pbuf is XRGB8888
 *   16-bit (Bellard default): pbuf is RGB565
 * Bellard's ZB_open takes 7 args; C-Chads takes 4. */
#if TGL_FEATURE_RENDER_BITS == 32
#  define TGL_ZB_OPEN(w,h) ZB_open((w),(h),ZB_MODE_RGBA,NULL)
#else
#  define TGL_ZB_OPEN(w,h) ZB_open((w),(h),ZB_MODE_5R6G5B,0,NULL,NULL,NULL)
#endif

static ZBuffer *g_zb    = NULL;
static int      g_tgl_w = 0;
static int      g_tgl_h = 0;

/* (tgl-init w h)
 * Creates the TinyGL software framebuffer and initialises the GL context.
 * Must be called after the native gfx window is open. */
static Val prim_tgl_init(Val args, Val env) {
    (void)env;
    int w = INT_VAL(CAR(args)), h = INT_VAL(CADR(args));
    if (g_zb) { ZB_close(g_zb); g_zb = NULL; }
    g_zb = TGL_ZB_OPEN(w, h);
    if (!g_zb) return NIL_VAL;
    glInit(g_zb);
    g_tgl_w = g_zb->xsize;   /* ZB_open rounds down to multiple of 4 */
    g_tgl_h = g_zb->ysize;
    return NIL_VAL;
}

/* (tgl-clear) */
static Val prim_tgl_clear(Val args, Val env) {
    (void)args; (void)env;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    return NIL_VAL;
}

/* (tgl-present) -- copy TinyGL framebuffer → native offscreen surface → screen */
static Val prim_tgl_present(Val args, Val env) {
    (void)args; (void)env;
    if (!g_zb) return NIL_VAL;
    int out_w, out_h, stride;
    uint32_t *dst = gfx_get_offscreen_pixels(&out_w, &out_h, &stride);
    if (!dst) return NIL_VAL;
    int copy_w = g_tgl_w < out_w ? g_tgl_w : out_w;
    int copy_h = g_tgl_h < out_h ? g_tgl_h : out_h;
    /* Copy TinyGL framebuffer → output pixel buffer.
     * Handle both 32-bit (PIXEL=uint32_t) and 16-bit (PIXEL=uint16_t) modes.
     * Use g_zb->linesize for source row stride (may include padding). */
    for (int y = 0; y < copy_h; y++) {
        uint8_t  *src_row = (uint8_t*)g_zb->pbuf + y * g_zb->linesize;
        uint32_t *dst_row = (uint32_t*)((uint8_t*)dst + y * stride);
#if TGL_FEATURE_RENDER_BITS == 32
        memcpy(dst_row, src_row, (size_t)copy_w * 4);
#else
        /* 16-bit RGB565 → XRGB8888 */
        uint16_t *sp = (uint16_t*)src_row;
        for (int x = 0; x < copy_w; x++) {
            uint16_t p = sp[x];
            uint8_t r = (p >> 8) & 0xF8;
            uint8_t g = (p >> 3) & 0xFC;
            uint8_t b = (p << 3) & 0xF8;
            dst_row[x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
#endif
    }
    gfx_mark_pixels_dirty();
    return NIL_VAL;
}

/* (tgl-viewport x y w h) */
static Val prim_tgl_viewport(Val args, Val env) {
    (void)env;
    glViewport(INT_VAL(CAR(args)), INT_VAL(CADR(args)),
               INT_VAL(CADDR(args)), INT_VAL(CAR(CDDDR(args))));
    return NIL_VAL;
}

/* (tgl-matrix-mode "modelview"|"projection") */
static Val prim_tgl_matrix_mode(Val args, Val env) {
    (void)env;
    const char *m = str_ptr(STR_IDX(CAR(args)));
    if (strcmp(m, "modelview") == 0)        glMatrixMode(GL_MODELVIEW);
    else if (strcmp(m, "projection") == 0)  glMatrixMode(GL_PROJECTION);
    return NIL_VAL;
}

/* (tgl-load-identity) */
static Val prim_tgl_load_identity(Val args, Val env) {
    (void)args; (void)env;
    glLoadIdentity();
    return NIL_VAL;
}

/* (tgl-push-matrix) */
static Val prim_tgl_push_matrix(Val args, Val env) {
    (void)args; (void)env;
    glPushMatrix();
    return NIL_VAL;
}

/* (tgl-pop-matrix) */
static Val prim_tgl_pop_matrix(Val args, Val env) {
    (void)args; (void)env;
    glPopMatrix();
    return NIL_VAL;
}

/*
 * Numeric convention for tgl-rotate / tgl-translate / tgl-scale / tgl-color
 * / tgl-vertex / tgl-frustum / tgl-ortho:
 *
 *   Integer arguments are scaled by 1/100 before being passed to GL so that
 *   Lisp code can use integers for fractional values without needing a
 *   floating-point type.  E.g. (tgl-vertex 100 0 -150) → glVertex3f(1,0,-1.5)
 *
 * tgl-rotate's angle argument is likewise divided by 100 (degrees × 100).
 */

/* (tgl-rotate angle x y z)  -- all args ×0.01 */
static Val prim_tgl_rotate(Val args, Val env) {
    (void)env;
    glRotatef((float)INT_VAL(CAR(args))   / 100.0f,
              (float)INT_VAL(CADR(args))  / 100.0f,
              (float)INT_VAL(CADDR(args)) / 100.0f,
              (float)INT_VAL(CAR(CDDDR(args))) / 100.0f);
    return NIL_VAL;
}

/* (tgl-translate x y z)  -- args ×0.01 */
static Val prim_tgl_translate(Val args, Val env) {
    (void)env;
    glTranslatef((float)INT_VAL(CAR(args))   / 100.0f,
                 (float)INT_VAL(CADR(args))  / 100.0f,
                 (float)INT_VAL(CADDR(args)) / 100.0f);
    return NIL_VAL;
}

/* (tgl-scale x y z)  -- args ×0.01 */
static Val prim_tgl_scale(Val args, Val env) {
    (void)env;
    glScalef((float)INT_VAL(CAR(args))   / 100.0f,
             (float)INT_VAL(CADR(args))  / 100.0f,
             (float)INT_VAL(CADDR(args)) / 100.0f);
    return NIL_VAL;
}

/* (tgl-color r g b)  -- args ×0.01, i.e. 0-100 maps to 0.0-1.0 */
static Val prim_tgl_color(Val args, Val env) {
    (void)env;
    glColor3f((float)INT_VAL(CAR(args))   / 100.0f,
              (float)INT_VAL(CADR(args))  / 100.0f,
              (float)INT_VAL(CADDR(args)) / 100.0f);
    return NIL_VAL;
}

/* (tgl-begin "triangles"|"quads"|"lines") */
static Val prim_tgl_begin(Val args, Val env) {
    (void)env;
    const char *m = str_ptr(STR_IDX(CAR(args)));
    GLenum mode = GL_TRIANGLES;
    if      (strcmp(m, "quads")  == 0) mode = GL_QUADS;
    else if (strcmp(m, "lines")  == 0) mode = GL_LINES;
    glBegin(mode);
    return NIL_VAL;
}

/* (tgl-vertex x y z)  -- args ×0.01 */
static Val prim_tgl_vertex(Val args, Val env) {
    (void)env;
    glVertex3f((float)INT_VAL(CAR(args))   / 100.0f,
               (float)INT_VAL(CADR(args))  / 100.0f,
               (float)INT_VAL(CADDR(args)) / 100.0f);
    return NIL_VAL;
}

/* (tgl-end) */
static Val prim_tgl_end(Val args, Val env) {
    (void)args; (void)env;
    glEnd();
    return NIL_VAL;
}

/* (tgl-enable "depth-test"|"lighting") */
static Val prim_tgl_enable(Val args, Val env) {
    (void)env;
    const char *cap = str_ptr(STR_IDX(CAR(args)));
    if      (strcmp(cap, "depth-test") == 0) glEnable(GL_DEPTH_TEST);
    else if (strcmp(cap, "lighting")   == 0) glEnable(GL_LIGHTING);
    return NIL_VAL;
}

/* (tgl-disable "depth-test"|"lighting") */
static Val prim_tgl_disable(Val args, Val env) {
    (void)env;
    const char *cap = str_ptr(STR_IDX(CAR(args)));
    if      (strcmp(cap, "depth-test") == 0) glDisable(GL_DEPTH_TEST);
    else if (strcmp(cap, "lighting")   == 0) glDisable(GL_LIGHTING);
    return NIL_VAL;
}

/*
 * (tgl-frustum l r b t n f)  -- 6 args, all ×0.01
 */
static Val prim_tgl_frustum(Val args, Val env) {
    (void)env;
    Val a = args;
    float fl = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float fr = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float fb = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float ft = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float fn = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float ff = (float)INT_VAL(CAR(a)) / 100.0f;
    glFrustum((double)fl, (double)fr, (double)fb,
              (double)ft, (double)fn, (double)ff);
    return NIL_VAL;
}

/* (tgl-ortho l r b t n f)  -- 6 args, all ×0.01
 * glOrtho is an inline no-op in TinyGL; implement via glLoadMatrixf. */
static Val prim_tgl_ortho(Val args, Val env) {
    (void)env;
    Val a = args;
    float l = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float r = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float b = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float t = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float n = (float)INT_VAL(CAR(a)) / 100.0f; a = CDR(a);
    float f = (float)INT_VAL(CAR(a)) / 100.0f;
    /* Column-major ortho matrix */
    float m[16] = {
        2.0f/(r-l),  0,           0,            0,
        0,           2.0f/(t-b),  0,            0,
        0,           0,          -2.0f/(f-n),   0,
        -(r+l)/(r-l), -(t+b)/(t-b), -(f+n)/(f-n), 1
    };
    glLoadMatrixf(m);
    return NIL_VAL;
}

void tinygl_prims_register(void) {
    prim_register("tgl-init",          prim_tgl_init,          2, 2);
    prim_register("tgl-clear",         prim_tgl_clear,         0, 0);
    prim_register("tgl-present",       prim_tgl_present,       0, 0);
    prim_register("tgl-viewport",      prim_tgl_viewport,      4, 4);
    prim_register("tgl-frustum",       prim_tgl_frustum,       6, 6);
    prim_register("tgl-ortho",         prim_tgl_ortho,         6, 6);
    prim_register("tgl-matrix-mode",   prim_tgl_matrix_mode,   1, 1);
    prim_register("tgl-load-identity", prim_tgl_load_identity, 0, 0);
    prim_register("tgl-push-matrix",   prim_tgl_push_matrix,   0, 0);
    prim_register("tgl-pop-matrix",    prim_tgl_pop_matrix,    0, 0);
    prim_register("tgl-rotate",        prim_tgl_rotate,        4, 4);
    prim_register("tgl-translate",     prim_tgl_translate,     3, 3);
    prim_register("tgl-scale",         prim_tgl_scale,         3, 3);
    prim_register("tgl-color",         prim_tgl_color,         3, 3);
    prim_register("tgl-begin",         prim_tgl_begin,         1, 1);
    prim_register("tgl-vertex",        prim_tgl_vertex,        3, 3);
    prim_register("tgl-end",           prim_tgl_end,           0, 0);
    prim_register("tgl-enable",        prim_tgl_enable,        1, 1);
    prim_register("tgl-disable",       prim_tgl_disable,       1, 1);
}

#endif /* HAVE_TINYGL */
