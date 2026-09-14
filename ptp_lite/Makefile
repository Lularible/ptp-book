# ==================== compiler configuration ====================

# x86 compiler (default)
CC = gcc

# ARM compilers
CC_ARM64 = aarch64-linux-gnu-gcc
CC_ARM32 = arm-linux-gnueabihf-gcc

# ==================== build options ====================

# x86 build options
CFLAGS = -Wall -Wextra -O2 -std=gnu11
LDFLAGS = -lrt -lm

# ARM64 build options
CFLAGS_ARM64 = -Wall -Wextra -O2 -std=gnu11
LDFLAGS_ARM64 = -lrt -lm

# ARM32 build options
CFLAGS_ARM32 = -Wall -Wextra -O2 -std=gnu11
LDFLAGS_ARM32 = -lrt -lm

# ==================== x86 targets (default) ====================

all: ptp_master ptp_slave

ptp_master: ptp_master.c ptp_message.c ptp_servo.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ptp_slave: ptp_slave.c ptp_message.c ptp_servo.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ==================== ARM64 targets ====================

arm64: ptp_master_arm64 ptp_slave_arm64

ptp_master_arm64: ptp_master.c ptp_message.c ptp_servo.c
	$(CC_ARM64) $(CFLAGS_ARM64) -o $@ $^ $(LDFLAGS_ARM64)

ptp_slave_arm64: ptp_slave.c ptp_message.c ptp_servo.c
	$(CC_ARM64) $(CFLAGS_ARM64) -o $@ $^ $(LDFLAGS_ARM64)

# ==================== ARM32 targets ====================

arm32: ptp_master_arm32 ptp_slave_arm32

ptp_master_arm32: ptp_master.c ptp_message.c ptp_servo.c
	$(CC_ARM32) $(CFLAGS_ARM32) -o $@ $^ $(LDFLAGS_ARM32)

ptp_slave_arm32: ptp_slave.c ptp_message.c ptp_servo.c
	$(CC_ARM32) $(CFLAGS_ARM32) -o $@ $^ $(LDFLAGS_ARM32)

# ==================== clean targets ====================

clean:
	rm -f ptp_master ptp_slave *.o

clean-arm64:
	rm -f ptp_master_arm64 ptp_slave_arm64 *.o

clean-arm32:
	rm -f ptp_master_arm32 ptp_slave_arm32 *.o

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