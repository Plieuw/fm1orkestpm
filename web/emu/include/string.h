/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The few C library strings the engine uses, for the freestanding WebAssembly
 * build. A browser build has no libc, and pulling in WASI just for memset would
 * add imports the module does not need. */
#ifndef ORCH_EMU_STRING_H
#define ORCH_EMU_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);

#endif
