# ChargeControl Makefile - 简化版

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
LDFLAGS  = -pie --sysroot=$(SYSROOT) -lm

# 源文件（不使用 sqlite3）
SRC = src/main.c src/charge_control.c src/config.c src/cJSON.c src/stats.c src/snapshot_daemon.c
TARGET = charge_control

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	$(STRIP) $@
	@echo "✅ 编译完成: $(TARGET)"

clean:
	rm -f $(TARGET)
	@echo "✅ 清理完成"

.PHONY: all clean
