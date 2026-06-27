#pragma once
#include <stdint.h>
void det_init(void);                 /* parse exe data segments, protect, install handler */
void det_init_region(void* base, unsigned long len); /* protect an explicit region (e.g. a server's shared-state arena) */
void det_set_fiber(void* f);         /* tell detector the current fiber (for park_clk) */
void det_park(void* f);              /* snapshot version clock at offload park + reprotect */
void det_reprotect(void);            /* reprotect before resuming a fiber */
extern long det_conflicts;           /* count of detected cross-fiber conflicts */
extern int  det_on;
/* per-fiber field the detector needs */
struct det_fib { uint64_t park_clk; int active; };
extern long det_faults;
