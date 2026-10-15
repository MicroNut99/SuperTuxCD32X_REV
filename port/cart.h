/*
 * OpenJazz CD32X - the RAM cart as read-only graphics memory (J3).
 *
 * On the console the SH2 can READ the 4 MB RAM cart but not write it; writes go through
 * the 68000 (OpenLara lesson 1). So decoded graphics that never change after loading
 * (tile sets, sprites, fonts, menu and scene pictures) are put there once, and the
 * 256 KB SDRAM keeps only what changes. cart_write() is the only way in; the PC build
 * makes the cart read-only memory, so any other write crashes at once (a test).
 *
 * The block table (who owns which part) lives in SDRAM: CART_BLOCKS entries.
 */
#ifndef OJ32X_CART_H
#define OJ32X_CART_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef OJ32X_MARS
#define CART_BLOCKS 16      /* J5: the 32X never allocates in the cart (the SH2 cannot write it) */
#else
#define CART_BLOCKS 1024
#endif

void  cart_init(void);
void *cart_alloc(size_t bytes);        /* 4-byte aligned; NULL when full */
void  cart_free(void *p);              /* NULL is fine */
void  cart_write(void *dst, const void *src, size_t bytes);
bool  cart_contains(const void *p);
size_t cart_used(void);
size_t cart_peak(void);
size_t cart_size(void);
int    cart_blocks(void);

#ifdef __cplusplus
}
#endif

#endif
