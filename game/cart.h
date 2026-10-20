/* SuperTux CD32X - the 8 / 16 MB test builds' paged cart (game/cart.c) */
#ifndef STX_CART_H
#define STX_CART_H

#ifndef STX_CART_MB
#define STX_CART_MB 4                  /* build.sh CART_MB=8|16 sets 8 or 16; 4 = the normal build */
#endif

#if STX_CART_MB > 4
extern int cart_mb;                    /* what the probe found: 4, 8 or 16 */
int cart_probe(void);
int cart_page(int page);               /* window 1 to page 1-7 (pages the cart has) */
int cart_page_now(void);
int cart_max_page(void);
int cart_test(char lines[][32], int max);
int cart_pack_valid(const uint8_t *pack, uint32_t room, int li);
#endif

#endif
