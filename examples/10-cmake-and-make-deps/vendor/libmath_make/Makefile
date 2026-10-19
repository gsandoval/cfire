CC ?= clang
AR ?= ar
BUILD_DIR ?= .
CFLAGS ?= -O2 -Wall -Iinclude

all: $(BUILD_DIR)/libvecmath.a

$(BUILD_DIR)/vecmath.o: src/vecmath.c include/vecmath.h
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c src/vecmath.c -o $(BUILD_DIR)/vecmath.o

$(BUILD_DIR)/libvecmath.a: $(BUILD_DIR)/vecmath.o
	$(AR) rcs $(BUILD_DIR)/libvecmath.a $(BUILD_DIR)/vecmath.o

clean:
	rm -f $(BUILD_DIR)/vecmath.o $(BUILD_DIR)/libvecmath.a
