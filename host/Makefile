# SuperTux CD32X - the game on the PC (game/host.c stands in for the 32X's platform layer,
# game/yt_host.c for Vic's SH2 drawing code):
#   make -C host
#   ST_DIR=<folder with IMAGE8.RAW and OJnnn.PAK> ST_OUT=/tmp/frames ST_FRAMES=300 ST_SAVE_EVERY=30 \
#     ST_PAD="0:-,20:RIGHT,40:RIGHT+C" host/stx_host
# Frames are written as PPM pictures of what the 32X would show (224 lines through the line table).
# make -C host asan: the same with the address and undefined-behaviour checkers (not shift-base:
# the game shifts negative fixed-point values left, which GCC defines - its manual, "Integers").
CC     ?= gcc
CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -I../port -I../yatssd -I../game -DOJ32X
SRC    := ../game/gen_level.c ../game/res.c ../game/level.c ../game/tux.c ../game/objects.c ../game/game.c ../game/wmap.c ../game/menus.c ../game/yt_layers.c ../game/yt_host.c ../game/host.c ../port/pack.c ../yatssd/font.c
DEPS   := $(wildcard ../game/*.h) ../port/pack.h ../port/plat.h ../yatssd/types.h
all: stx_host
stx_host: $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -o $@ $(SRC)
asan: $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize=pointer-overflow,shift-base -o stx_asan $(SRC)
clean:
	rm -f stx_host stx_asan
.PHONY: all clean asan
