# ==================== compiler configuration ====================

# Per-architecture compilers, overridable: make arm64 CC_ARM64=my-aarch64-gcc
CC_X86   ?= gcc
CC_ARM64 ?= aarch64-linux-gnu-gcc
CC_ARM32 ?= arm-linux-gnueabihf-gcc

# ==================== build options ====================

# One set of flags for all three architectures. The code is warning free on
# x86-64, arm64 and arm32 - int64_t is printed with PRId64, so the ILP32 arm32
# target reports no -Wformat diagnostics either.
#
# -D_GNU_SOURCE is not needed: -std=gnu11 already turns on _DEFAULT_SOURCE,
# which is what <net/if.h> (struct ifreq) and <netinet/in.h> (struct ip_mreqn)
# require. Adding -D_GNU_SOURCE would work too, but would not change a thing.
CFLAGS  = -Wall -Wextra -O2 -std=gnu11
LDFLAGS = -lrt -lm

# arm32 is the only target whose ABI has to be spelled out; both knobs follow
# the hard-float gnueabihf triple by default and can be overridden per board:
#   make arm32 ARM32_MCPU=cortex-a7 ARM32_MFLOAT=softfp
ARM32_MCPU   ?= cortex-a9
ARM32_MFLOAT ?= hard
CFLAGS_ARM32 = $(CFLAGS) -mcpu=$(ARM32_MCPU) -mfloat-abi=$(ARM32_MFLOAT)

# ==================== sources ====================

HEADERS    = $(wildcard *.h)
COMMON_SRC = ptp_message.c

# The master never touches the servo, so ptp_servo.c is NOT linked into it. It
# used to be, which left pi_servo_init()/pi_servo_sample() as dead symbols in
# ptp_master (`nm ptp_master | grep servo` must stay empty).
MASTER_SRC = ptp_master.c $(COMMON_SRC)
SLAVE_SRC  = ptp_slave.c $(COMMON_SRC) ptp_servo.c

BIN_X86   = ptp_master ptp_slave
BIN_ARM64 = ptp_master_arm64 ptp_slave_arm64
BIN_ARM32 = ptp_master_arm32 ptp_slave_arm32

# Toolchain selection: the two rules below build all six binaries, and these
# target-specific variables say which compiler and flags each of them gets.
CC_FOR     = $(CC_X86)
CFLAGS_FOR = $(CFLAGS)
ptp_master         ptp_slave:         CC_FOR = $(CC_X86)
ptp_master_arm64   ptp_slave_arm64:   CC_FOR = $(CC_ARM64)
ptp_master_arm32   ptp_slave_arm32:   CC_FOR = $(CC_ARM32)
ptp_master_arm32   ptp_slave_arm32:   CFLAGS_FOR = $(CFLAGS_ARM32)

# ==================== x86 targets (default) ====================

all: $(BIN_X86)

# ==================== ARM64 targets ====================

arm64: $(BIN_ARM64)

# ==================== ARM32 targets ====================

arm32: $(BIN_ARM32)

# ==================== link rules (all architectures) ====================

# Every target names its sources in the recipe instead of using $^, because
# the prerequisite list also carries $(HEADERS): editing a header must trigger
# a rebuild, but the header itself must not end up on the command line.
ptp_master ptp_master_arm64 ptp_master_arm32: $(MASTER_SRC) $(HEADERS)
	$(CC_FOR) $(CFLAGS_FOR) -o $@ $(MASTER_SRC) $(LDFLAGS)

ptp_slave ptp_slave_arm64 ptp_slave_arm32: $(SLAVE_SRC) $(HEADERS)
	$(CC_FOR) $(CFLAGS_FOR) -o $@ $(SLAVE_SRC) $(LDFLAGS)

# ==================== clean targets ====================
# *.o is never produced (the link rules go .c -> binary); kept as a defensive glob.
clean:
	rm -f $(BIN_X86) *.o

clean-arm64:
	rm -f $(BIN_ARM64) *.o

clean-arm32:
	rm -f $(BIN_ARM32) *.o

clean-all: clean clean-arm64 clean-arm32

# ==================== auxiliary targets ====================

# build all architectures
all-arch: all arm64 arm32

# show help
help:
	@echo "PTP-lite build options:"
	@echo "  make           - build the x86 version (default)"
	@echo "  make arm64     - build the ARM64 version"
	@echo "  make arm32     - build the ARM32 version"
	@echo "  make all-arch  - build all architecture versions"
	@echo ""
	@echo "Clean options:"
	@echo "  make clean         - clean the x86 version"
	@echo "  make clean-arm64   - clean the ARM64 version"
	@echo "  make clean-arm32   - clean the ARM32 version"
	@echo "  make clean-all     - clean all versions"

.PHONY: all arm64 arm32 clean clean-arm64 clean-arm32 clean-all all-arch help
