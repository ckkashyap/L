#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* ttf.c -- Minimal TrueType parser and scanline rasterizer. ASCII 32-127. */
#include "ttf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- Big-endian readers ---- */
static uint16_t r16(const uint8_t *p) { return (uint16_t)((p[0]<<8)|p[1]); }
static int16_t  r16s(const uint8_t *p){ return (int16_t)r16(p); }
static uint32_t r32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

/* ---- Table directory ---- */
static const uint8_t *find_table(const uint8_t *data, size_t len,
                                  const char tag[4], uint32_t *out_len) {
    if (len < 12) return NULL;
    uint16_t ntables = r16(data + 4);
    for (uint16_t i = 0; i < ntables; i++) {
        const uint8_t *rec = data + 12 + i * 16;
        if (rec + 16 > data + len) break;
        if (memcmp(rec, tag, 4) == 0) {
            uint32_t offset = r32(rec + 8);
            uint32_t tlen   = r32(rec + 12);
            if (out_len) *out_len = tlen;
            if (offset + tlen > (uint32_t)len) return NULL;
            return data + offset;
        }
    }
    return NULL;
}

/* ---- TtfFont struct ---- */
struct TtfFont {
    uint8_t  *data;
    size_t    data_len;
    int       units_per_em;
    int       ascent, descent, line_gap;
    int       num_glyphs;
    int       index_to_loc_format;
    int       num_h_metrics;

    /* cmap format-4 segments */
    int        seg_count;
    uint16_t  *seg_end;
    uint16_t  *seg_start;
    int16_t   *seg_delta;
    uint16_t  *seg_range;
    uint16_t  *glyph_ids;
    int        glyph_id_count;
    const uint8_t *cmap_range_base; /* pointer to idRangeOffset array in data */

    /* loca: glyph offsets */
    uint32_t  *glyph_off;

    /* hmtx */
    uint16_t  *adv;
    int16_t   *lsb;

    /* glyf table pointer */
    const uint8_t *glyf;
    size_t         glyf_len;
};

TtfFont *ttf_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0) { fclose(f); return NULL; }
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    if ((long)fread(data, 1, (size_t)sz, f) != sz) { free(data); fclose(f); return NULL; }
    fclose(f);

    TtfFont *font = (TtfFont *)calloc(1, sizeof(TtfFont));
    if (!font) { free(data); return NULL; }
    font->data     = data;
    font->data_len = (size_t)sz;

    /* head */
    const uint8_t *head = find_table(data, (size_t)sz, "head", NULL);
    if (!head) { ttf_free(font); return NULL; }
    font->units_per_em        = (int)r16(head + 18);
    font->index_to_loc_format = (int)(int16_t)r16s(head + 50);

    /* hhea */
    const uint8_t *hhea = find_table(data, (size_t)sz, "hhea", NULL);
    if (!hhea) { ttf_free(font); return NULL; }
    font->ascent         = (int)r16s(hhea + 4);
    font->descent        = (int)r16s(hhea + 6);
    font->line_gap       = (int)r16s(hhea + 8);
    font->num_h_metrics  = (int)r16(hhea + 34);

    /* maxp */
    const uint8_t *maxp = find_table(data, (size_t)sz, "maxp", NULL);
    if (!maxp) { ttf_free(font); return NULL; }
    font->num_glyphs = (int)r16(maxp + 4);

    /* glyf */
    uint32_t glyf_len = 0;
    font->glyf = find_table(data, (size_t)sz, "glyf", &glyf_len);
    font->glyf_len = glyf_len;
    if (!font->glyf) { ttf_free(font); return NULL; }

    /* loca */
    uint32_t loca_len = 0;
    const uint8_t *loca = find_table(data, (size_t)sz, "loca", &loca_len);
    if (!loca) { ttf_free(font); return NULL; }
    font->glyph_off = (uint32_t *)malloc((size_t)(font->num_glyphs + 1) * sizeof(uint32_t));
    if (!font->glyph_off) { ttf_free(font); return NULL; }
    for (int i = 0; i <= font->num_glyphs; i++) {
        if (font->index_to_loc_format == 0) {
            if ((uint32_t)i * 2 + 2 > loca_len) { ttf_free(font); return NULL; }
            font->glyph_off[i] = (uint32_t)r16(loca + i * 2) * 2u;
        } else {
            if ((uint32_t)i * 4 + 4 > loca_len) { ttf_free(font); return NULL; }
            font->glyph_off[i] = r32(loca + i * 4);
        }
    }

    /* hmtx */
    const uint8_t *hmtx = find_table(data, (size_t)sz, "hmtx", NULL);
    if (!hmtx) { ttf_free(font); return NULL; }
    font->adv = (uint16_t *)malloc((size_t)font->num_h_metrics * sizeof(uint16_t));
    font->lsb = (int16_t  *)malloc((size_t)font->num_h_metrics * sizeof(int16_t));
    if (!font->adv || !font->lsb) { ttf_free(font); return NULL; }
    for (int i = 0; i < font->num_h_metrics; i++) {
        font->adv[i] = r16(hmtx + i * 4);
        font->lsb[i] = r16s(hmtx + i * 4 + 2);
    }

    /* cmap: find format 4 */
    const uint8_t *cmap_tbl = find_table(data, (size_t)sz, "cmap", NULL);
    if (!cmap_tbl) { ttf_free(font); return NULL; }
    uint16_t num_subtables = r16(cmap_tbl + 2);
    const uint8_t *fmt4 = NULL;
    /* Prefer platform 3 encoding 1 (Windows BMP Unicode), fall back to platform 0 */
    for (int pass = 0; pass < 2 && !fmt4; pass++) {
        for (uint16_t i = 0; i < num_subtables; i++) {
            const uint8_t *rec = cmap_tbl + 4 + i * 8;
            uint16_t plat = r16(rec), enc = r16(rec + 2);
            uint32_t off  = r32(rec + 4);
            int match = (pass == 0) ? (plat == 3 && enc == 1) : (plat == 0);
            if (match && r16(cmap_tbl + off) == 4) { fmt4 = cmap_tbl + off; break; }
        }
    }
    if (!fmt4) { ttf_free(font); return NULL; }

    uint16_t seg2      = r16(fmt4 + 6);
    font->seg_count    = (int)(seg2 / 2);
    font->seg_end      = (uint16_t *)malloc((size_t)font->seg_count * sizeof(uint16_t));
    font->seg_start    = (uint16_t *)malloc((size_t)font->seg_count * sizeof(uint16_t));
    font->seg_delta    = (int16_t  *)malloc((size_t)font->seg_count * sizeof(int16_t));
    font->seg_range    = (uint16_t *)malloc((size_t)font->seg_count * sizeof(uint16_t));
    if (!font->seg_end || !font->seg_start || !font->seg_delta || !font->seg_range)
        { ttf_free(font); return NULL; }

    const uint8_t *p = fmt4 + 14;
    for (int i = 0; i < font->seg_count; i++) font->seg_end[i]   = r16(p + i*2);
    p += (size_t)font->seg_count * 2 + 2; /* skip reservedPad */
    for (int i = 0; i < font->seg_count; i++) font->seg_start[i] = r16(p + i*2);
    p += (size_t)font->seg_count * 2;
    for (int i = 0; i < font->seg_count; i++) font->seg_delta[i] = r16s(p + i*2);
    p += (size_t)font->seg_count * 2;
    font->cmap_range_base = p;  /* points to idRangeOffset array */
    for (int i = 0; i < font->seg_count; i++) font->seg_range[i] = r16(p + i*2);
    p += (size_t)font->seg_count * 2;

    /* glyph id array */
    uint16_t total_len = r16(fmt4 + 2);
    int glyph_id_start = (int)(p - fmt4);
    font->glyph_id_count = ((int)total_len - glyph_id_start) / 2;
    if (font->glyph_id_count < 0) font->glyph_id_count = 0;
    font->glyph_ids = NULL;
    if (font->glyph_id_count > 0) {
        font->glyph_ids = (uint16_t *)malloc((size_t)font->glyph_id_count * sizeof(uint16_t));
        if (!font->glyph_ids) { ttf_free(font); return NULL; }
        for (int i = 0; i < font->glyph_id_count; i++)
            font->glyph_ids[i] = r16(p + i * 2);
    }

    return font;
}

void ttf_free(TtfFont *f) {
    if (!f) return;
    free(f->data);
    free(f->glyph_off);
    free(f->adv);
    free(f->lsb);
    free(f->seg_end);
    free(f->seg_start);
    free(f->seg_delta);
    free(f->seg_range);
    free(f->glyph_ids);
    free(f);
}

/* ---- cmap lookup: codepoint -> glyph index ---- */
static uint16_t cp_to_glyph(const TtfFont *f, uint32_t cp) {
    if (cp > 0xFFFF) return 0;
    uint16_t c = (uint16_t)cp;
    for (int i = 0; i < f->seg_count; i++) {
        if (c > f->seg_end[i]) continue;
        if (c < f->seg_start[i]) return 0;
        if (f->seg_range[i] == 0) {
            return (uint16_t)((c + (uint16_t)f->seg_delta[i]) & 0xFFFF);
        }
        /* idRangeOffset != 0: index into glyphIdArray */
        /* The spec says: glyphIdArray index = idRangeOffset[i]/2 + (c - startCode[i]) - (segCount - i) */
        int idx = (int)(f->seg_range[i] / 2) + (int)(c - f->seg_start[i]) - (f->seg_count - i);
        if (idx < 0 || idx >= f->glyph_id_count) return 0;
        uint16_t gid = f->glyph_ids[idx];
        if (gid == 0) return 0;
        return (uint16_t)((gid + (uint16_t)f->seg_delta[i]) & 0xFFFF);
    }
    return 0;
}

/* ---- Glyph outline point ---- */
typedef struct { float x, y; int on_curve; } Pt;

/* Decode a simple TrueType glyph into a flat array of contour points.
 * Returns number of points, or 0 for empty/composite glyphs.
 * *ends_out is malloc'd array of ncontours end-indices (inclusive). */
static int decode_simple_glyph(const TtfFont *f, uint16_t glyph_idx, float scale,
                                 Pt **pts_out, int **ends_out, int *ncontours_out) {
    *pts_out = NULL; *ends_out = NULL; *ncontours_out = 0;
    if ((int)glyph_idx >= f->num_glyphs) return 0;
    uint32_t off = f->glyph_off[glyph_idx];
    uint32_t end = f->glyph_off[glyph_idx + 1];
    if (off == end || off >= (uint32_t)f->glyf_len) return 0; /* empty glyph */

    const uint8_t *g = f->glyf + off;
    int16_t ncontours = r16s(g);
    if (ncontours == 0) return 0;

    /* ---- Composite glyph ---- */
    if (ncontours < 0) {
        /* Flag bits */
        #define COMP_WORDS     0x0001  /* args are int16, else int8 */
        #define COMP_XY_VALUES 0x0002  /* args are x/y offsets */
        #define COMP_MORE      0x0020  /* another component follows */
        #define COMP_SCALE     0x0008  /* one F2Dot14 scale follows */
        #define COMP_XY_SCALE  0x0040  /* separate x/y scales follow */
        #define COMP_2X2       0x0080  /* 2×2 matrix follows */

        const uint8_t *cp  = g + 10;
        const uint8_t *gend = f->glyf + f->glyf_len;

        Pt  *all_pts  = NULL;
        int *all_ends = NULL;
        int  total_pts = 0, total_nc = 0;
        int  cap_pts = 0, cap_nc = 0;

        uint16_t cflags;
        do {
            if (cp + 4 > gend) break;
            cflags = r16(cp); cp += 2;
            uint16_t cgid = r16(cp); cp += 2;

            float dx = 0.0f, dy = 0.0f;
            if (cflags & COMP_WORDS) {
                if (cp + 4 > gend) break;
                int16_t a1 = r16s(cp); cp += 2;
                int16_t a2 = r16s(cp); cp += 2;
                if (cflags & COMP_XY_VALUES) { dx = (float)a1 * scale; dy = (float)a2 * scale; }
            } else {
                if (cp + 2 > gend) break;
                int8_t a1 = (int8_t)*cp++;
                int8_t a2 = (int8_t)*cp++;
                if (cflags & COMP_XY_VALUES) { dx = (float)a1 * scale; dy = (float)a2 * scale; }
            }
            /* Skip optional transform */
            if      (cflags & COMP_2X2)     cp += 8;
            else if (cflags & COMP_XY_SCALE) cp += 4;
            else if (cflags & COMP_SCALE)    cp += 2;

            Pt  *cpts = NULL; int *cends = NULL; int cnc = 0;
            int  cnpts = decode_simple_glyph(f, cgid, scale, &cpts, &cends, &cnc);
            if (cnpts > 0 && cpts && cends) {
                if (total_pts + cnpts > cap_pts) {
                    cap_pts = (total_pts + cnpts) * 2 + 64;
                    Pt *tmp = (Pt *)realloc(all_pts, (size_t)cap_pts * sizeof(Pt));
                    if (!tmp) { free(cpts); free(cends); goto comp_done; }
                    all_pts = tmp;
                }
                for (int i = 0; i < cnpts; i++) {
                    all_pts[total_pts + i] = cpts[i];
                    all_pts[total_pts + i].x += dx;
                    all_pts[total_pts + i].y += dy;
                }
                if (total_nc + cnc > cap_nc) {
                    cap_nc = (total_nc + cnc) * 2 + 16;
                    int *tmp = (int *)realloc(all_ends, (size_t)cap_nc * sizeof(int));
                    if (!tmp) { free(cpts); free(cends); goto comp_done; }
                    all_ends = tmp;
                }
                for (int i = 0; i < cnc; i++)
                    all_ends[total_nc + i] = cends[i] + total_pts;
                total_pts += cnpts;
                total_nc  += cnc;
            }
            free(cpts); free(cends);
        } while (cflags & COMP_MORE);

        comp_done:
        if (total_pts == 0) { free(all_pts); free(all_ends); return 0; }
        *pts_out      = all_pts;
        *ends_out     = all_ends;
        *ncontours_out = total_nc;
        return total_pts;
    }

    /* ---- Simple glyph ---- */
    *ncontours_out = (int)ncontours;

    int *ends = (int *)malloc((size_t)ncontours * sizeof(int));
    if (!ends) return 0;
    for (int i = 0; i < (int)ncontours; i++) ends[i] = (int)r16(g + 10 + i * 2);
    int npts = ends[ncontours - 1] + 1;
    *ends_out = ends;

    uint16_t instr_len = r16(g + 10 + (int)ncontours * 2);
    const uint8_t *fp = g + 10 + (int)ncontours * 2 + 2 + instr_len;

    /* Decode flags */
    uint8_t *flags = (uint8_t *)malloc((size_t)npts);
    if (!flags) { free(ends); return 0; }
    for (int i = 0; i < npts; ) {
        uint8_t fl = *fp++;
        flags[i++] = fl;
        if (fl & 0x08) { /* REPEAT_FLAG */
            int rep = (int)*fp++;
            while (rep-- > 0 && i < npts) flags[i++] = fl;
        }
    }

    /* Decode x coordinates */
    float *xs = (float *)malloc((size_t)npts * sizeof(float));
    if (!xs) { free(flags); free(ends); return 0; }
    int cx = 0;
    for (int i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x02) { /* X_SHORT_VECTOR */
            int dx = (int)*fp++;
            cx += (fl & 0x10) ? dx : -dx;
        } else if (!(fl & 0x10)) { /* not X_IS_SAME_OR_POSITIVE_X_SHORT_VECTOR */
            cx += (int)r16s(fp); fp += 2;
        } /* else same as previous */
        xs[i] = (float)cx * scale;
    }

    /* Decode y coordinates and build Pt array */
    Pt *pts = (Pt *)malloc((size_t)npts * sizeof(Pt));
    if (!pts) { free(xs); free(flags); free(ends); return 0; }
    int cy = 0;
    for (int i = 0; i < npts; i++) {
        uint8_t fl = flags[i];
        if (fl & 0x04) { /* Y_SHORT_VECTOR */
            int dy = (int)*fp++;
            cy += (fl & 0x20) ? dy : -dy;
        } else if (!(fl & 0x20)) {
            cy += (int)r16s(fp); fp += 2;
        }
        pts[i].x        = xs[i];
        pts[i].y        = (float)cy * scale;
        pts[i].on_curve = (fl & 0x01) ? 1 : 0;
    }
    free(flags);
    free(xs);
    *pts_out = pts;
    return npts;
}

/* ---- Edge for scanline rasterizer ---- */
typedef struct { float x0, y0, x1, y1; } Edge;

static int edge_push(Edge **edges, int *ne, int *cap, float x0, float y0, float x1, float y1) {
    if (y0 == y1) return 1; /* horizontal -- skip */
    if (*ne >= *cap) {
        *cap = *cap ? *cap * 2 : 64;
        Edge *tmp = (Edge *)realloc(*edges, (size_t)*cap * sizeof(Edge));
        if (!tmp) return 0;
        *edges = tmp;
    }
    (*edges)[(*ne)++] = (Edge){x0, y0, x1, y1};
    return 1;
}

/* Flatten quadratic Bezier p0->p1(ctrl)->p2 into edges */
static int flatten_bezier(Edge **edges, int *ne, int *cap,
                            float x0, float y0, float x1, float y1,
                            float x2, float y2, int depth) {
    if (depth >= 8) {
        return edge_push(edges, ne, cap, x0, y0, x2, y2);
    }
    /* Check flatness: if midpoint of control polygon is close to curve midpoint */
    float mx = (x0 + 2.0f*x1 + x2) * 0.25f;
    float my = (y0 + 2.0f*y1 + y2) * 0.25f;
    float lx = (x0 + x2) * 0.5f;
    float ly = (y0 + y2) * 0.5f;
    float dx = mx - lx, dy = my - ly;
    if (dx*dx + dy*dy < 0.25f) { /* 0.5 pixel tolerance */
        return edge_push(edges, ne, cap, x0, y0, x2, y2);
    }
    float mx0 = (x0+x1)*0.5f, my0 = (y0+y1)*0.5f;
    float mx1 = (x1+x2)*0.5f, my1 = (y1+y2)*0.5f;
    float cmx = (mx0+mx1)*0.5f, cmy = (my0+my1)*0.5f;
    if (!flatten_bezier(edges, ne, cap, x0, y0, mx0, my0, cmx, cmy, depth+1)) return 0;
    return flatten_bezier(edges, ne, cap, cmx, cmy, mx1, my1, x2, y2, depth+1);
}

/* Convert one contour (pts[start..end]) to edges.
 * TrueType allows consecutive off-curve points with implicit on-curve
 * midpoints between them. We expand these first, then walk the clean
 * on/off/on sequence to avoid double-counting edges. */
static int contour_to_edges(const Pt *pts, int start, int end,
                              Edge **edges, int *ne, int *cap) {
    int count = end - start + 1;
    if (count < 2) return 1;

    /* Phase 1: expand to insert implicit on-curve midpoints */
    int max_exp = count * 2 + 2;
    Pt *exp = (Pt *)malloc((size_t)max_exp * sizeof(Pt));
    if (!exp) return 0;
    int nexp = 0;

    for (int i = 0; i < count; i++) {
        Pt p = pts[start + i];
        if (nexp > 0 && !p.on_curve && !exp[nexp - 1].on_curve) {
            Pt mid = { (exp[nexp-1].x + p.x) * 0.5f,
                        (exp[nexp-1].y + p.y) * 0.5f, 1 };
            exp[nexp++] = mid;
        }
        exp[nexp++] = p;
    }
    /* Handle wrap: last→first might both be off-curve */
    if (nexp > 1 && !exp[nexp-1].on_curve && !exp[0].on_curve) {
        Pt mid = { (exp[nexp-1].x + exp[0].x) * 0.5f,
                    (exp[nexp-1].y + exp[0].y) * 0.5f, 1 };
        exp[nexp++] = mid;
    }

    /* Phase 2: find first on-curve point */
    int first_on = -1;
    for (int i = 0; i < nexp; i++)
        if (exp[i].on_curve) { first_on = i; break; }
    if (first_on < 0) { free(exp); return 1; }

    /* Phase 3: walk on→off→on or on→on generating edges exactly once */
    Pt cur = exp[first_on];
    int ok = 1;
    for (int step = 1; step < nexp && ok; ) {
        int ci = (first_on + step) % nexp;
        Pt p = exp[ci];
        if (p.on_curve) {
            ok = edge_push(edges, ne, cap, cur.x, cur.y, p.x, p.y);
            cur = p;
            step++;
        } else {
            int ni = (first_on + step + 1) % nexp;
            Pt p2 = exp[ni]; /* guaranteed on-curve after expansion */
            ok = flatten_bezier(edges, ne, cap,
                                cur.x, cur.y, p.x, p.y, p2.x, p2.y, 0);
            cur = p2;
            step += 2;
        }
    }
    /* Close the contour */
    if (ok && (cur.x != exp[first_on].x || cur.y != exp[first_on].y))
        ok = edge_push(edges, ne, cap, cur.x, cur.y,
                        exp[first_on].x, exp[first_on].y);

    free(exp);
    return ok;
}

/* Scanline fill into 8-bit alpha bitmap. ox/oy = offset of (0,0) in bitmap coords. */
static uint8_t *scanfill(const Edge *edges, int ne,
                          int bw, int bh, float ox, float oy) {
    uint8_t *bmp = (uint8_t *)calloc((size_t)bw * (size_t)bh, 1);
    if (!bmp) return NULL;
    float *xs_buf = (float *)malloc((size_t)(ne + 4) * sizeof(float));
    if (!xs_buf) { free(bmp); return NULL; }

    for (int y = 0; y < bh; y++) {
        float fy = (float)y + oy + 0.5f;
        int nx = 0;
        for (int i = 0; i < ne; i++) {
            float ey0 = edges[i].y0, ey1 = edges[i].y1;
            /* Edge crosses scanline fy? */
            if ((ey0 <= fy && ey1 > fy) || (ey1 <= fy && ey0 > fy)) {
                float t = (fy - ey0) / (ey1 - ey0);
                xs_buf[nx++] = edges[i].x0 + t * (edges[i].x1 - edges[i].x0) - ox;
            }
        }
        /* Sort intersections (insertion sort -- small n) */
        for (int a = 1; a < nx; a++) {
            float v = xs_buf[a]; int b = a;
            while (b > 0 && xs_buf[b-1] > v) { xs_buf[b] = xs_buf[b-1]; b--; }
            xs_buf[b] = v;
        }
        /* Fill between pairs */
        for (int a = 0; a + 1 < nx; a += 2) {
            int x0 = (int)floorf(xs_buf[a]);
            int x1 = (int)ceilf (xs_buf[a+1]);
            if (x0 < 0)  x0 = 0;
            if (x1 > bw) x1 = bw;
            for (int x = x0; x < x1; x++)
                bmp[y * bw + x] = 255;
        }
    }
    free(xs_buf);
    return bmp;
}

/* Supersampling factor: rasterize at SS× resolution then box-filter down.
 * SS=4 gives 17 coverage levels (0..16 subpixels) for smooth antialiasing. */
#define GLYPH_SS 4

uint8_t *ttf_render_glyph(TtfFont *f, uint32_t cp, int px,
                            int *out_w, int *out_h, int *xoff, int *yoff) {
    if (!f || px <= 0) return NULL;
    uint16_t gid = cp_to_glyph(f, cp);
    /* Decode outline at SS× the target pixel size so the scanfill samples
     * a finer grid; we box-filter back down for antialiased coverage. */
    float scale    = (float)px / (float)(f->units_per_em > 0 ? f->units_per_em : 1);
    float ss_scale = scale * (float)GLYPH_SS;

    Pt *pts = NULL; int *ends = NULL; int ncontours = 0;
    int npts = decode_simple_glyph(f, gid, ss_scale, &pts, &ends, &ncontours);

    if (npts == 0) {
        /* Empty glyph (space, etc.) -- return 1x1 transparent */
        free(pts); free(ends);
        *out_w = 1; *out_h = 1;
        *xoff = 0; *yoff = 0;
        uint8_t *b = (uint8_t *)calloc(1, 1);
        return b;
    }

    /* Bounding box in SS coordinates */
    float minx_ss = pts[0].x, maxx_ss = pts[0].x;
    float miny_ss = pts[0].y, maxy_ss = pts[0].y;
    for (int i = 1; i < npts; i++) {
        if (pts[i].x < minx_ss) minx_ss = pts[i].x;
        if (pts[i].x > maxx_ss) maxx_ss = pts[i].x;
        if (pts[i].y < miny_ss) miny_ss = pts[i].y;
        if (pts[i].y > maxy_ss) maxy_ss = pts[i].y;
    }

    /* SS bitmap: 1-pixel output padding = GLYPH_SS pixels of SS padding */
    int pad   = GLYPH_SS;
    int bw_ss = (int)ceilf(maxx_ss - minx_ss) + 2 * pad;
    int bh_ss = (int)ceilf(maxy_ss - miny_ss) + 2 * pad;
    if (bw_ss < 1) bw_ss = 1;
    if (bh_ss < 1) bh_ss = 1;

    /* Build edges */
    int ne = 0, ecap = 0;
    Edge *edges = NULL;
    int start = 0;
    for (int c = 0; c < ncontours; c++) {
        if (!contour_to_edges(pts, start, ends[c], &edges, &ne, &ecap)) {
            free(pts); free(ends); free(edges);
            return NULL;
        }
        start = ends[c] + 1;
    }
    free(pts); free(ends);

    /* Rasterize at SS resolution */
    uint8_t *bmp_ss = scanfill(edges, ne, bw_ss, bh_ss,
                                minx_ss - (float)pad, miny_ss - (float)pad);
    free(edges);
    if (!bmp_ss) return NULL;

    /* Downsample: each GLYPH_SS × GLYPH_SS block → 1 output pixel.
     * Average the binary SS samples to get a fractional coverage value. */
    int bw = (bw_ss + GLYPH_SS - 1) / GLYPH_SS;
    int bh = (bh_ss + GLYPH_SS - 1) / GLYPH_SS;
    if (bw < 1) bw = 1;
    if (bh < 1) bh = 1;

    uint8_t *bmp = (uint8_t *)calloc((size_t)bw * (size_t)bh, 1);
    if (!bmp) { free(bmp_ss); return NULL; }

    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            int sum = 0, count = 0;
            for (int sy = 0; sy < GLYPH_SS; sy++) {
                int ry = y * GLYPH_SS + sy;
                if (ry >= bh_ss) break;
                for (int sx = 0; sx < GLYPH_SS; sx++) {
                    int rx = x * GLYPH_SS + sx;
                    if (rx >= bw_ss) break;
                    sum += bmp_ss[ry * bw_ss + rx];
                    count++;
                }
            }
            bmp[y * bw + x] = count ? (uint8_t)((sum + count / 2) / count) : 0;
        }
    }
    free(bmp_ss);

    *out_w = bw;
    *out_h = bh;
    /* xoff/yoff expressed in output (1×) pixel coordinates */
    *xoff  = (int)floorf(minx_ss / (float)GLYPH_SS) - 1;
    /* After Y-flip in blit, screen row 0 = glyph top (SS font y ≈ maxy_ss).
     * One output pixel of top padding, so subtract one extra. */
    *yoff  = -(int)ceilf(maxy_ss / (float)GLYPH_SS) - 1;
    return bmp;
}

int ttf_glyph_advance(TtfFont *f, uint32_t cp, int px) {
    if (!f || px <= 0) return px; /* fallback */
    uint16_t gid = cp_to_glyph(f, cp);
    float scale = (float)px / (float)(f->units_per_em > 0 ? f->units_per_em : 1);
    uint16_t adv_units;
    if ((int)gid < f->num_h_metrics)
        adv_units = f->adv[gid];
    else
        adv_units = f->num_h_metrics > 0 ? f->adv[f->num_h_metrics - 1] : 0;
    return (int)roundf((float)adv_units * scale);
}

void ttf_line_metrics(TtfFont *f, int px, int *asc, int *desc, int *gap) {
    if (!f || px <= 0) { if(asc)*asc=px; if(desc)*desc=0; if(gap)*gap=0; return; }
    float scale = (float)px / (float)(f->units_per_em > 0 ? f->units_per_em : 1);
    if (asc)  *asc  = (int)roundf((float)f->ascent   * scale);
    if (desc) *desc = (int)roundf((float)f->descent   * scale);
    if (gap)  *gap  = (int)roundf((float)f->line_gap  * scale);
}
