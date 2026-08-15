CC = aarch64-linux-gnu-gcc
CFLAGS = -static -O2 -s
TARGET = src/battd
MODULE_DIR = module
OUTPUT = ChargeControl.zip

all: $(TARGET)

$(TARGET): src/battd.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGET) $(OUTPUT)

package: $(TARGET)
	rm -f $(OUTPUT)
	cd $(MODULE_DIR) && zip -r ../$(OUTPUT) .

.PHONY: all clean package
