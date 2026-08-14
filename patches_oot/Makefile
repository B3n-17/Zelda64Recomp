TARGET = patches.elf

# make has builtin defaults for CC and LD, so `?=` would never take effect when this
# Makefile is invoked directly. Only override when the value is make's own default,
# so an explicit CC=/LD= from the environment or CMake still wins.
ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := ld.lld
endif

# Path to a BUILT zeldaret/oot decomp checkout. A clone is not enough: the patches
# include generated headers from $(OOT_DIR)/build/$(OOT_VERSION_DIR) and
# $(OOT_DIR)/extracted/$(OOT_VERSION_DIR), which only exist after
# `make VERSION=ntsc-1.0 REGION=US` has been run in it.
#
# Defaults into the repo, beside lib/mm-decomp, so a self-contained checkout needs
# no override. Point somewhere else with `make OOT_DIR=/path/to/oot`.
OOT_DIR ?= ../lib/oot-decomp
OOT_VERSION_DIR ?= ntsc-1.0

# -fno-jump-tables is required, not an optimization choice. A switch compiled to a
# jump table emits an array of code addresses into .rodata with R_MIPS_32
# relocations into .recomp_patch, and N64Recomp cannot resolve those for patch
# functions - it aborts on an out-of-bounds read before printing anything. Branch
# chains cost a little speed and keep the recompiler working. This bites any patch
# that copies a decomp function containing a switch, which is easy to hit since
# RECOMP_PATCH requires copying the whole function.
CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fno-jump-tables \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset -fno-builtin-strlen \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt

# OoT's headers are modular (no monolithic z64.h) and its libc headers live in
# include/libc, which must be on the path explicitly under -nostdinc.
# The version defines have to match the decomp build for ntsc-1.0 US.
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS \
			-DPLATFORM_N64=1 -DPLATFORM_GC=0 -DPLATFORM_IQUE=0 \
			-DOOT_VERSION=NTSC_1_0 -DOOT_REVISION=0 -DOOT_REGION=REGION_US \
			-DLIBULTRA_VERSION=LIBULTRA_VERSION_I -DLIBULTRA_PATCH=1 \
			-DDEBUG_FEATURES=0 -DNDEBUG -DF3DEX_GBI_2 \
			-I $(OOT_DIR) -I $(OOT_DIR)/include -I $(OOT_DIR)/include/libc -I $(OOT_DIR)/src \
			-I $(OOT_DIR)/extracted/$(OOT_VERSION_DIR) -I $(OOT_DIR)/build/$(OOT_VERSION_DIR) \
			-I ../lib/rt64/include

LDFLAGS  := -nostdlib -T patches.ld -T syms.ld -Map patches.map --unresolved-symbols=ignore-all --emit-relocs

# Say which of the two is missing rather than letting clang emit a thousand
# "file not found" lines, which is what a wrong OOT_DIR looks like otherwise.
ifeq ($(wildcard $(OOT_DIR)/include/ultra64.h),)
$(error OoT decomp not found at OOT_DIR=$(OOT_DIR). Clone zeldaret/oot there, or pass OOT_DIR=/path/to/oot)
endif
ifeq ($(wildcard $(OOT_DIR)/build/$(OOT_VERSION_DIR)),)
$(error OoT decomp at OOT_DIR=$(OOT_DIR) has not been built. Run `make VERSION=$(OOT_VERSION_DIR) REGION=US` in it first)
endif

C_SRCS := $(wildcard *.c)
C_OBJS := $(C_SRCS:.c=.o)
C_DEPS := $(C_SRCS:.c=.d)

$(TARGET): $(C_OBJS) patches.ld syms.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

# -MP emits a phony target for every header, so a header that moves or disappears
# rebuilds the object instead of stopping the build with "No rule to make target".
# The dep files record absolute paths into OOT_DIR, so without it, pointing
# OOT_DIR somewhere new fails until the stale .d files are deleted by hand.
$(C_OBJS): %.o : %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MP -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(C_OBJS) $(TARGET) $(C_DEPS)

-include $(C_DEPS)

.PHONY: clean
