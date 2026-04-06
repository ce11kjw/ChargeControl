# ChargeControl – Makefile
# Build native binary or cross-compile for Android ARM64 (NDK)
#
# Usage:
#   make                   – native build
#   make CROSS=aarch64-linux-android-  – Android ARM64 cross-compile (NDK)
#   make clean

# ------------------------------------------------------------------ #
# Toolchain                                                           #
# ------------------------------------------------------------------ #
CROSS    ?=
CC        = $(CROSS)gcc
AR        = $(CROSS)ar
STRIP     = $(CROSS)strip

# ------------------------------------------------------------------ #
# Directories                                                         #
# ------------------------------------------------------------------ #
SRC_DIR   = src
BUILD_DIR = build
SQLITE_DIR = $(SRC_DIR)/sqlite
CJSON_DIR  = $(SRC_DIR)/cjson

# ------------------------------------------------------------------ #
# Source files                                                        #
# ------------------------------------------------------------------ #
SRCS = \
    $(SRC_DIR)/main.c            \
    $(SRC_DIR)/config.c          \
    $(SRC_DIR)/charge_control.c  \
    $(SRC_DIR)/stats.c           \
    $(SRC_DIR)/snapshot_daemon.c \
    $(SRC_DIR)/http_server.c     \
    $(CJSON_DIR)/cJSON.c

# ------------------------------------------------------------------ #
# SQLite: use embedded amalgamation if present, system lib otherwise  #
# ------------------------------------------------------------------ #
SQLITE_AMALG = $(SQLITE_DIR)/sqlite3.c

ifneq ($(wildcard $(SQLITE_AMALG)),)
    # Embedded amalgamation – compile and link statically
    SRCS    += $(SQLITE_AMALG)
    SQLITE_CFLAGS  = -I$(SQLITE_DIR) -DSQLITE_OMIT_LOAD_EXTENSION
    SQLITE_LDFLAGS =
else
    # Fall back to system SQLite
    SQLITE_CFLAGS  =
    SQLITE_LDFLAGS = -lsqlite3
endif

# ------------------------------------------------------------------ #
# Flags                                                               #
# ------------------------------------------------------------------ #
CFLAGS  = -Wall -Wextra -O2 -std=c11 \
          -I$(SRC_DIR) -I$(CJSON_DIR) \
          $(SQLITE_CFLAGS) \
          -D_GNU_SOURCE \
          -D_POSIX_C_SOURCE=200809L

LDFLAGS = -lpthread -lm $(SQLITE_LDFLAGS)

TARGET  = $(BUILD_DIR)/chargecontrol

# ------------------------------------------------------------------ #
# Object files                                                        #
# ------------------------------------------------------------------ #
OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(notdir $(SRCS)))

# ------------------------------------------------------------------ #
# Rules                                                               #
# ------------------------------------------------------------------ #
.PHONY: all clean

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Pattern rules for each source directory
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(CJSON_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SQLITE_DIR)/%.c
	$(CC) $(CFLAGS) -Wno-unused-parameter -Wno-unused-function -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

# ------------------------------------------------------------------ #
# Convenience: download SQLite amalgamation (requires curl + unzip)   #
# ------------------------------------------------------------------ #
SQLITE_VER     = 3450100
SQLITE_ZIP_URL = https://www.sqlite.org/2024/sqlite-amalgamation-$(SQLITE_VER).zip

sqlite-download:
	mkdir -p $(SQLITE_DIR)
	curl -L -o /tmp/sqlite-amal.zip $(SQLITE_ZIP_URL)
	cd /tmp && unzip -o sqlite-amal.zip
	cp /tmp/sqlite-amalgamation-$(SQLITE_VER)/sqlite3.c $(SQLITE_DIR)/sqlite3.c
	cp /tmp/sqlite-amalgamation-$(SQLITE_VER)/sqlite3.h $(SQLITE_DIR)/sqlite3.h
	@echo "SQLite amalgamation downloaded to $(SQLITE_DIR)/"
