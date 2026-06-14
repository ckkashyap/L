#pragma once
#include "picolisp.h"
void coro_init(void);
void coro_prims_register(void);
void gc_mark_coros(void);
