NDK          ?= $(ANDROID_NDK_HOME)
API          ?= 35
TARGET_TRIPLE := aarch64-linux-android$(API)
TOOLCHAIN    := $(NDK)/toolchains/llvm/prebuilt/linux-x86_64

CC    := $(TOOLCHAIN)/bin/$(TARGET_TRIPLE)-clang
STRIP := $(TOOLCHAIN)/bin/llvm-strip

SYSROOT  := $(TOOLCHAIN)/sysroot
CFLAGS   = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE \
           --sysroot=$(SYSROOT) \
           -fPIE -fstack-protector-strong
LDFLAGS  = -pie --sysroot=$(SYSROOT) \
           -lsqlite3 -lpthread -lm

SRC    = src/main.c src/charge_control.c src/stats.c \
         src/snapshot_daemon.c src/config.c src/cJSON.c
TARGET = charge_control

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
