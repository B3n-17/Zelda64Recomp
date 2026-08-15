TARGET   = build/mod.elf
MOD_NRM  = build/oot_recomp_inventory_editor.nrm

ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := ld.lld
endif

# RecompModTool, built from lib/N64ModernRuntime/N64Recomp/RecompModTool. There is
# no prebuilt copy in the tree; `cmake --build build-win --target RecompModTool`
# produces this.
MODTOOL ?= ../../build-win/RecompModTool.exe

# Same requirement as patches_oot: a BUILT decomp checkout, because the headers
# include generated ones from build/ and extracted/.
OOT_DIR ?= ../../lib/oot-decomp
OOT_VERSION_DIR ?= ntsc-1.0

SYMS_DIR ?= ../../Zelda64RecompSyms

# Identical to the patches build, and for the same reasons - see the comment on
# -fno-jump-tables in patches_oot/Makefile, which applies verbatim here.
CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fno-jump-tables \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset -fno-builtin-strlen \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt

CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS \
			-DPLATFORM_N64=1 -DPLATFORM_GC=0 -DPLATFORM_IQUE=0 \
			-DOOT_VERSION=NTSC_1_0 -DOOT_REVISION=0 -DOOT_REGION=REGION_US \
			-DLIBULTRA_VERSION=LIBULTRA_VERSION_I -DLIBULTRA_PATCH=1 \
			-DDEBUG_FEATURES=0 -DNDEBUG -DF3DEX_GBI_2 \
			-I include \
			-I $(OOT_DIR) -I $(OOT_DIR)/include -I $(OOT_DIR)/include/libc -I $(OOT_DIR)/src \
			-I $(OOT_DIR)/extracted/$(OOT_VERSION_DIR) -I $(OOT_DIR)/build/$(OOT_VERSION_DIR) \
			-I ../../lib/rt64/include

# --emit-relocs is what makes this a mod rather than a patch: RecompModTool reads
# the relocations back out to resolve imports and to relocate the mod into mod
# memory. Without it the tool sees a mod with no imports and no way to be placed.
LDFLAGS  := -nostdlib -T mod.ld -Map build/mod.map --unresolved-symbols=ignore-all --emit-relocs --no-rosegment

ifeq ($(wildcard $(OOT_DIR)/include/ultra64.h),)
$(error OoT decomp not found at OOT_DIR=$(OOT_DIR). Clone zeldaret/oot there, or pass OOT_DIR=/path/to/oot)
endif
ifeq ($(wildcard $(OOT_DIR)/build/$(OOT_VERSION_DIR)),)
$(error OoT decomp at OOT_DIR=$(OOT_DIR) has not been built. Run `make VERSION=$(OOT_VERSION_DIR) REGION=US` in it first)
endif

C_SRCS := $(wildcard src/*.c)
C_OBJS := $(addprefix build/,$(C_SRCS:.c=.o))
C_DEPS := $(addprefix build/,$(C_SRCS:.c=.d))

all: $(MOD_NRM)

$(MOD_NRM): $(TARGET) mod.toml
	$(MODTOOL) mod.toml build

$(TARGET): $(C_OBJS) mod.ld
	@mkdir -p $(dir $@)
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

build/%.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MP -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf build

-include $(C_DEPS)

.PHONY: all clean
