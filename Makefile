CC = aarch64-linux-gnu-gcc
CFLAGS = -static -O2 -s
TARGET = src/battd
MODULE_DIR = module
VER = $(shell grep "version=" module/module.prop | head -1 | cut -d= -f2)
OUTPUT = ChargeControl-$(VER).zip

all: $(TARGET)

$(TARGET): src/battd.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) $(OUTPUT) ChargeControl.zip

package: $(TARGET)
	rm -f $(OUTPUT)
	cp $(TARGET) $(MODULE_DIR)/bin/battd
	cd $(MODULE_DIR) && zip -r ../$(OUTPUT) .

.PHONY: all clean package
