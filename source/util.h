/* util.h -- misc utility functions
 *
 * Copyright (C) 2021 fgsfds, Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

#ifndef __UTIL_H__
#define __UTIL_H__

#include <stdint.h>

#include "config.h"
#ifdef DEBUG_LOG
int debugPrintf(char *text, ...);
#else
#define debugPrintf(...) ((void)0)
#endif

void cpu_boost(int on);

int ret0(void);
int retm1(void);

static inline void* armGetTlsRw(void) {
  void* ret;
  // This register changes independently of C variables. Without volatile,
  // GCC can reuse a pre-write MRS result across armSetTlsRw/callbacks.
  __asm__ volatile ("mrs %x[data], s3_3_c13_c0_2" : [data] "=r" (ret) : : "memory");
  return ret;
}

static inline void armSetTlsRw(void *addr) {
  __asm__ volatile ("msr s3_3_c13_c0_2, %0" : : "r"(addr) : "memory");
}

static inline uint64_t umin(uint64_t a, uint64_t b) {
  return (a < b) ? a : b;
}

#endif
