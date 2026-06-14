/* heap.c -- Cell pool allocator and mark-sweep garbage collector.
 *
 * Design
 * ------
 * - g_cells[CELL_POOL_SIZE] is a static global array (~64 MB BSS).
 * - g_free_head is the index of the freelist head (UINT32_MAX = empty).
 * - Freelist: each free cell's cdr holds MAKE_CONS(next_idx); the last
 *   free cell's cdr is NIL_VAL.
 * - heap_init() chains all 4 M cells into the freelist at startup.
 * - heap_alloc_cell() pops from the freelist, triggering GC if empty.
 * - pl_cons(car, cdr) allocates a cell and fills its fields.
 * - Mark phase: iterative (explicit stack) to avoid deep-recursion overflow.
 *   Traverses all GC roots: root stack, all symbol values/plists, dynamic
 *   bind stack saved values, g_at, g_make_head, g_make_tail, vec pool.
 * - Sweep phase: linear walk of all 4 M cells; unmarked -> freelist;
 *   marked -> clear mark bit and count as live.
 * - VEC GC: unmarked vec slots have their data heap-freed during sweep.
 */

#include "picolisp.h"
#include "callbacks.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Global definitions
 * ===================================================================== */

Cell     g_cells[CELL_POOL_SIZE];
uint32_t g_free_head       = UINT32_MAX;
uint32_t g_n_cells_used    = 0;
/* g_cells_committed: how many cells have been chained into the freelist.
 * Cells [0, committed) are owned by the allocator; the rest are untouched.
 * We start with a small batch and extend lazily so that process startup
 * only causes a fraction of the 16 K page faults that touching all 64 MB
 * would trigger. */
uint32_t g_cells_committed = 0;

/* Batch sizes for lazy heap extension (in cells, not bytes). */
#define HEAP_INIT_CELLS   (256 * 1024)   /* 4 MB on startup (~2 ms)         */
#define HEAP_EXTEND_CELLS (256 * 1024)   /* 4 MB per extension               */

Val g_gc_roots[GC_ROOT_STACK_SIZE];
int g_gc_root_top = 0;

VecSlot  g_vec_pool[VEC_POOL_SIZE];
uint32_t g_vec_count = 0;

Val g_at        = NIL_VAL;   /* @ result register                          */
Val g_make_head = NIL_VAL;   /* (make ...) list head                         */
Val g_make_tail = NIL_VAL;   /* (make ...) list tail                         */

BindFrame g_bind_stack[BIND_STACK_SIZE];
int       g_bind_top = 0;

/* =========================================================================
 * Mark stack for iterative GC -- large enough for pathological deep lists.
 * ===================================================================== */
#define GC_MARK_STACK_SIZE (1 * 1024 * 1024)
static Val gc_mark_stack[GC_MARK_STACK_SIZE];
static int gc_mark_top = 0;

/* =========================================================================
 * External references provided by other translation units
 * ===================================================================== */
extern void pl_error_str(const char *msg);
extern void gc_mark_bignums(void);
extern void gc_sweep_bignums(void);
extern SymSlot   g_syms[];
extern uint32_t  g_sym_count;

/* =========================================================================
 * heap_extend -- chain cells [start, end) into the freelist.
 * ===================================================================== */
static void heap_extend(uint32_t start, uint32_t end)
{
    if (start >= end) return;
    /* Chain cells in ascending order so the freelist stays sorted, which
     * keeps allocation cache-local. */
    for (uint32_t i = start; i < end - 1; i++) {
        g_cells[i].cdr  = MAKE_CONS(i + 1);
        g_cells[i].car  = NIL_VAL;
        g_cells[i].mark = 0;
    }
    g_cells[end - 1].cdr  = NIL_VAL;
    g_cells[end - 1].car  = NIL_VAL;
    g_cells[end - 1].mark = 0;
    /* Prepend the new block to the existing freelist. */
    if (g_free_head != UINT32_MAX)
        g_cells[end - 1].cdr = MAKE_CONS(g_free_head);
    g_free_head       = start;
    g_cells_committed = end;
}

/* =========================================================================
 * heap_init -- commit only the first batch of cells.
 * The rest of the pool stays untouched until more cells are needed, so
 * startup only causes page faults for HEAP_INIT_CELLS cells rather than
 * all CELL_POOL_SIZE (saves ~35 ms on Windows due to fewer page faults).
 * ===================================================================== */
void heap_init(void) {
    uint32_t init = CELL_POOL_SIZE < HEAP_INIT_CELLS ? CELL_POOL_SIZE : HEAP_INIT_CELLS;
    heap_extend(0, init);
    g_n_cells_used = 0;
}

/* =========================================================================
 * mark_vec -- mark a VecSlot and all Val elements it contains
 * ===================================================================== */
void mark_vec(uint32_t idx) {
    if (idx >= g_vec_count) return;
    VecSlot *vs = &g_vec_pool[idx];
    if (vs->mark) return;           /* already visited                      */
    vs->mark = 1;
    for (uint32_t i = 0; i < vs->len; i++) {
        gc_mark_val(vs->data[i]);
    }
}

/* =========================================================================
 * gc_mark_val -- iterative mark of a single Val and everything reachable
 *               from it.  Uses the static gc_mark_stack[] to avoid
 *               unbounded C-stack growth on deeply nested lists.
 * ===================================================================== */
void gc_mark_val(Val v) {
    /* Push the seed value onto the work stack.  gc_mark_top may already be
     * non-zero if we were called while another mark traversal is in
     * progress (e.g., from mark_vec -> gc_mark_val re-entrancy).  We
     * capture the entry depth so we only process our own work items.      */
    int entry_top = gc_mark_top;

    if (gc_mark_top >= GC_MARK_STACK_SIZE) return;   /* overflow guard     */
    gc_mark_stack[gc_mark_top++] = v;

    while (gc_mark_top > entry_top) {
        Val cur = gc_mark_stack[--gc_mark_top];

        if (IS_CONS(cur)) {
            uint32_t idx = CONS_IDX(cur);
            if (idx < CELL_POOL_SIZE && !g_cells[idx].mark) {
                g_cells[idx].mark = 1;
                /* Push cdr first so that car is processed next (LIFO). */
                if (gc_mark_top + 1 < GC_MARK_STACK_SIZE)
                    gc_mark_stack[gc_mark_top++] = g_cells[idx].cdr;
                if (gc_mark_top + 1 < GC_MARK_STACK_SIZE)
                    gc_mark_stack[gc_mark_top++] = g_cells[idx].car;
            }
        } else if (IS_VEC(cur)) {
            mark_vec(VEC_IDX(cur));
        } else if (IS_PIPE(cur)) {
            mark_pipe(PIPE_IDX(cur));
        } else if (IS_CB(cur)) {
#ifdef HAVE_FFI
            uint32_t idx = CB_IDX(cur);
            if (idx && idx <= g_cb_count && g_callbacks[idx].alive)
                g_callbacks[idx].mark = 1;
            /* lambda is marked by gc_mark_callbacks() after root traversal */
#endif
        }
        /* INT, SYM, STR, BIG, NIL, PRIM: no cell to mark here.
         * BIG marking is handled by gc_mark_bignums() over all live roots. */
    }
}

/* =========================================================================
 * gc_mark_all_roots -- walk every GC root and mark reachable values
 * ===================================================================== */
static void gc_mark_all_roots(void) {
    /* 1. Explicit GC root stack (local variables pushed by PUSH_ROOT). */
    for (int i = 0; i < g_gc_root_top; i++)
        gc_mark_val(g_gc_roots[i]);

    /* 2. Every symbol's global value and property list. */
    for (uint32_t i = 1; i <= g_sym_count; i++) {
        gc_mark_val(g_syms[i].value);
        gc_mark_val(g_syms[i].plist);
    }

    /* 3. Dynamic bind stack -- saved values of rebound symbols. */
    for (int i = 0; i < g_bind_top; i++)
        gc_mark_val(g_bind_stack[i].saved);

    /* 4. @ register and (make ...) accumulator. */
    gc_mark_val(g_at);
    gc_mark_val(g_make_head);
    gc_mark_val(g_make_tail);

    /* 5. Coroutine values: fn, send/yield vals, and saved GC root slices
     *    of suspended coroutines. */
    gc_mark_coros();

    /* 6. FFI callbacks: mark alive slots and their lambda functions. */
#ifdef HAVE_FFI
    gc_mark_callbacks();
#endif

    /* 7. Bignums reachable from anything marked above. */
    gc_mark_bignums();
}

/* =========================================================================
 * gc_sweep_cells -- free all unmarked cells; rebuild freelist.
 * Only sweeps cells [0, g_cells_committed) so that uncommitted cells are
 * never touched (avoids spurious page faults on the unused portion).
 * ===================================================================== */
static void gc_sweep_cells(void) {
    g_free_head    = UINT32_MAX;
    g_n_cells_used = 0;

    /* Walk backwards so the freelist ends up in ascending-index order,
     * which gives better cache locality for subsequent allocations.       */
    for (uint32_t i = g_cells_committed; i-- > 0; ) {
        if (g_cells[i].mark) {
            /* Cell is live: clear the mark and count it. */
            g_cells[i].mark = 0;
            g_n_cells_used++;
        } else {
            /* Cell is garbage: prepend to freelist. */
            if (g_free_head == UINT32_MAX)
                g_cells[i].cdr = NIL_VAL;
            else
                g_cells[i].cdr = MAKE_CONS(g_free_head);
            g_cells[i].car = NIL_VAL;
            g_free_head = i;
        }
    }
}

/* =========================================================================
 * gc_sweep_vecs -- free data arrays of unmarked vec slots.
 * ===================================================================== */
static void gc_sweep_vecs(void) {
    for (uint32_t i = 0; i < g_vec_count; i++) {
        if (!g_vec_pool[i].mark && g_vec_pool[i].data != NULL) {
            free(g_vec_pool[i].data);
            g_vec_pool[i].data = NULL;
            g_vec_pool[i].len  = 0;
            g_vec_pool[i].cap  = 0;
        }
        g_vec_pool[i].mark = 0;   /* clear mark regardless                 */
    }
}

/* =========================================================================
 * gc_collect -- full mark-sweep collection cycle
 * ===================================================================== */
void gc_collect(void) {
    gc_mark_top = 0;          /* reset mark stack                           */

    /* Reset all bignum marks to 0 before marking.  big_from_int64 sets
     * mark=1 at allocation time to protect against mid-expression GC, but
     * that mark must be cleared here so that unreachable bignums (which
     * accumulated mark=1 from creation) are correctly collected. */
    extern void gc_clear_bignum_marks(void);
    gc_clear_bignum_marks();

    gc_mark_all_roots();
    gc_sweep_cells();
    gc_sweep_vecs();
    gc_sweep_bignums();
    gc_sweep_pipes();
#ifdef HAVE_FFI
    gc_sweep_callbacks();
#endif

    if (g_free_head == UINT32_MAX) {
        pl_error_str("out of memory: cell pool exhausted after GC");
    }
}

/* =========================================================================
 * heap_alloc_cell -- pop one cell from the freelist; extend or GC if empty.
 * Returns the cell index (never UINT32_MAX on success).
 * ===================================================================== */
uint32_t heap_alloc_cell(void) {
    if (g_free_head == UINT32_MAX) {
        /* Prefer extending the committed region over running GC: extending
         * is O(HEAP_EXTEND_CELLS) and avoids a full mark-sweep pass. */
        if (g_cells_committed < CELL_POOL_SIZE) {
            uint32_t end = g_cells_committed + HEAP_EXTEND_CELLS;
            if (end > CELL_POOL_SIZE) end = CELL_POOL_SIZE;
            heap_extend(g_cells_committed, end);
        } else {
            gc_collect();
        }
    }
    if (g_free_head == UINT32_MAX)
        pl_error_str("out of memory: cell pool exhausted");

    uint32_t idx  = g_free_head;
    Val      next = g_cells[idx].cdr;

    /* Advance freelist head: next is either MAKE_CONS(next_idx) or NIL. */
    if (IS_CONS(next))
        g_free_head = CONS_IDX(next);
    else
        g_free_head = UINT32_MAX;

    g_cells[idx].mark = 0;
    g_cells[idx].car  = NIL_VAL;
    g_cells[idx].cdr  = NIL_VAL;
    g_n_cells_used++;
    return idx;
}

/* =========================================================================
 * pl_cons -- allocate a new cons cell with the given car and cdr.
 * ===================================================================== */
Val pl_cons(Val car, Val cdr) {
    uint32_t idx = heap_alloc_cell();
    g_cells[idx].car = car;
    g_cells[idx].cdr = cdr;
    return MAKE_CONS(idx);
}
