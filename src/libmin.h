/* libmin.h - the two C library functions this -nostdlib build provides
 * (libmin.c). GCC may emit calls to them on its own; main.c calls memset.
 * Their names are the standard ones on purpose: deviation DEV-MEM-04. */
#pragma once
#include <stddef.h>

void *memset(void *dst, int c, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
