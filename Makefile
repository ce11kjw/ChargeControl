CC      ?= gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE
LDFLAGS = -lsqlite3 -lpthread -lm

# Auto-detect SQLite3 include/lib paths via pkg-config (works on macOS Homebrew + Linux)
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS   := $(shell pkg-config --libs   sqlite3 2>/dev/null)
ifneq ($(SQLITE_CFLAGS),)
    CFLAGS  += $(SQLITE_CFLAGS)
    LDFLAGS += $(SQLITE_LIBS)
else
    # Fallback: common Homebrew prefixes on macOS
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        HOMEBREW_PREFIX := $(shell brew --prefix sqlite3 2>/dev/null)
        ifneq ($(HOMEBREW_PREFIX),)
            CFLAGS  += -I$(HOMEBREW_PREFIX)/include
            LDFLAGS += -L$(HOMEBREW_PREFIX)/lib
        endif
    endif
endif

SRC     = src/main.c src/charge_control.c src/stats.c \
          src/snapshot_daemon.c src/config.c src/cJSON.c
TARGET  = charge_control

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
