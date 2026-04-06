CC      ?= gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11 -D_GNU_SOURCE
LDFLAGS = -lsqlite3 -lpthread -lm
SRC     = src/main.c src/charge_control.c src/stats.c \
          src/snapshot_daemon.c src/config.c src/cJSON.c
TARGET  = charge_control

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
