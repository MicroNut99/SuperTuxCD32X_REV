# SuperTux CD32X - the SH2 program (R1b), linked as a 32X cartridge image for the CD32X loader.
#   make -C mars EXTRA_DEFS="-DOJ32X_MUSIC=1"     (build.sh does this)
# Results: mars/stx32x.elf / .bin / .map, mars/OPENJAZZ.32X, mars/report.txt (sizes).
#
# Hurrican CD32X's mars/Makefile with game/ new. The start-up and platform layer are the
# OpenJazz port's J19b files (proven on the console), unchanged, compiled with exactly the flags
# of the J19b engine build (PORT_*): port/mars/crt0_oj.s, port/mars/oj.ld, port/plat_mars.c,
# port/mars_syscalls.c, port/pack.c, port/mars_boottest.cpp, and tools/make_oj_rom.py for the
# cartridge image (module name "OpenJazz 32X"; build.sh puts it on the disc as OPENLARA.32X).
# The game (game/) runs after main() is called, after the boot path has finished.
#
# Switch passed by build.sh (the platform layer's own, no file is edited):
#   OJ32X_MUSIC=1      CD audio tracks (the J19b music build)
#   (OJ32X_DIAG_TEXT=1 kept the Genesis text on until C++ start-up in the first builds; R1c is a
#   release without it - docs/STX_HANDOFF.md, diagnostics)
#
# Vic's tiler (yatssd/, MIT, unchanged): dtiles.c, dsprite.c, draw.c, font.c, sh2_fixed.s.
# Its hw_32x.c is NOT linked (it would set up the VDP, line table and interrupts - the boot
# path's job); game/yt_mars.c gives it the few things it needs (game/yt.h).

TC      ?= /opt/toolchains/sega/sh-elf/bin/sh-elf-
CC      := $(TC)gcc
CXX     := $(TC)g++
SIZE    := $(TC)size
ARCH    ?= -m2 -mb

ROOT  := ..
PORT  := $(ROOT)/port
GAME  := $(ROOT)/game
YT    := $(ROOT)/yatssd

OBJDIR := obj

# ---- the J19b engine build's settings (mars/Makefile of the OpenJazz port, J19b) ----
PORT_OPT  := $(ARCH) -Os -ffunction-sections -fdata-sections
PORT_DEFS := -DOJ32X -DOJ32X_MARS -DOJ_SDL1 -DPORTABLE -DNDEBUG \
             -DOJ_VERSION=\"git-oj32x\" -DOJ_DATE=\"2026-10-06\" -D__32X__ -DMARS \
             -DOJ_URL=\"http://alister.eu/jazz/oj/\" -DOJ_BUGREPORT=\"https://github.com/AlisterT/openjazz/issues\" \
             -DLIBXMP_CORE_PLAYER -DLIBXMP_STATIC -DXMP_SYM_VISIBILITY \
             -DOJ32X_SCROLL -DOJ32X_FAST_CODE -DOJ32X_PANEL_DIFF -DOJ32X_FLIP_ASYNC -DOJ32X_SLAVE_DRAWS=1 $(EXTRA_DEFS)
PORT_INCS := -I$(PORT)/include -I$(PORT)
PORT_CXXFLAGS := -std=gnu++14 $(PORT_OPT) $(PORT_DEFS) $(PORT_INCS) -fno-threadsafe-statics -Wall -Wno-unused-result -Wno-narrowing -MMD -MP
PORT_CFLAGS   := -std=gnu11 $(PORT_OPT) $(PORT_DEFS) $(PORT_INCS) -w -MMD -MP

# ---- the game (new): speed-optimised, the same switches ----
GAME_CFLAGS := -std=gnu11 $(ARCH) -O2 -ffunction-sections -fdata-sections $(PORT_DEFS) $(PORT_INCS) -I$(YT) -I$(GAME) -Wall -MMD -MP
# ---- Vic's files: as they are (their own warnings are not ours to fix), with YATSSD's own
# "release" flags (yatssd/Makefile, without -flto). -fno-align-loops / -jumps / -labels are
# REQUIRED: his drawing functions live in .data (they run from SDRAM), where the assembler pads
# an alignment with 0x0000 instead of NOPs - the CPU runs into it: R1c's red screen "ILLEGAL
# INSTRUCTION, PC 06000A26" (draw_handle_layercmd). tools/check_sdram_code.py checks the result.
YT_CFLAGS   := -std=gnu11 $(ARCH) -Os -fomit-frame-pointer -ffast-math -funroll-loops \
               -fno-align-loops -fno-align-jumps -fno-align-labels -ffunction-sections -fdata-sections \
               -D__32X__ -DMARS -I$(YT) -w -MMD -MP

PORT_OBJS := $(OBJDIR)/plat_mars.o $(OBJDIR)/mars_syscalls.o $(OBJDIR)/pack.o $(OBJDIR)/mars_boottest.o
GAME_OBJS := $(addprefix $(OBJDIR)/,res.o level.o tux.o objects.o game.o wmap.o menus.o yt_layers.o yt_mars.o gen_level.o)
YT_OBJS   := $(OBJDIR)/dtiles.o $(OBJDIR)/dsprite.o $(OBJDIR)/draw.o $(OBJDIR)/font.o $(OBJDIR)/sh2_fixed.o
OBJS := $(GAME_OBJS) $(YT_OBJS) $(PORT_OBJS)
LD_SCRIPT := $(PORT)/mars/oj.ld

all: OPENJAZZ.32X report.txt
	@echo; cat report.txt

# linked as the J19b engine is linked
stx32x.elf: $(OBJDIR)/crt0_oj.o $(OBJS) $(LD_SCRIPT)
	$(CXX) $(ARCH) -nostartfiles -T $(LD_SCRIPT) -Wl,--gc-sections -Wl,-Map,stx32x.map \
		-o $@ $(OBJDIR)/crt0_oj.o $(OBJS) -lstdc++ -lm -lc -lgcc

stx32x.bin: stx32x.elf
	$(TC)objcopy -O binary -j .sdcode -j .data -j .text $< $@

OPENJAZZ.32X: stx32x.elf stx32x.bin $(ROOT)/tools/make_oj_rom.py $(ROOT)/tools/mars_boot.bin
	python3 $(ROOT)/tools/check_sdram_code.py stx32x.elf $(TC)objdump
	python3 $(ROOT)/tools/make_oj_rom.py $(ROOT)/tools/mars_boot.bin stx32x.elf stx32x.bin $@

report.txt: stx32x.elf
	@{ echo "== SH2 sections (bytes)"; $(SIZE) -A stx32x.elf | grep -E "sdcode|data|text|bss|Total"; } > $@

$(OBJDIR)/crt0_oj.o: $(PORT)/mars/crt0_oj.s
	@mkdir -p $(OBJDIR)
	$(CC) $(ARCH) -c $< -o $@

# R3: the game's busy code runs from SDRAM, not from the cart (the SH2 fetches cart ROM slowly,
# and its 4 KB cache is shared with Vic's loops): the rules, the level, Tux, the bad guys, the
# drawing calls. Compiled without the loop / jump / label alignments (in .data the assembler
# pads with 0x0000 - see YT_CFLAGS) and without -ffunction-sections, then .text renamed into
# .data (the linker script copies .data to SDRAM). tools/check_sdram_code.py checks them too.
HOT_OBJS := $(addprefix $(OBJDIR)/,res.o level.o tux.o objects.o game.o yt_layers.o)
HOT_CFLAGS := $(filter-out -ffunction-sections,$(GAME_CFLAGS)) -fno-align-loops -fno-align-jumps -fno-align-labels -fno-align-functions
$(HOT_OBJS): $(OBJDIR)/%.o: $(GAME)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(HOT_CFLAGS) -c $< -o $@.text.o
	$(TC)objcopy --rename-section .text=.data.hot_$* $@.text.o $@
	@rm -f $@.text.o
$(OBJDIR)/%.o: $(GAME)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(GAME_CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: $(YT)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(YT_CFLAGS) -c $< -o $@
$(OBJDIR)/sh2_fixed.o: $(YT)/sh2_fixed.s
	@mkdir -p $(OBJDIR)
	$(CC) $(ARCH) -c $< -o $@
$(OBJDIR)/%.o: $(PORT)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(PORT_CFLAGS) -c $< -o $@
$(OBJDIR)/%.o: $(PORT)/%.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(PORT_CXXFLAGS) -c $< -o $@

clean:
	rm -rf obj stx32x.elf stx32x.bin stx32x.map OPENJAZZ.32X report.txt
.PHONY: all clean

$(OBJS) $(OBJDIR)/crt0_oj.o: Makefile
-include $(OBJS:.o=.d)
