CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -fPIC
PKGS     = libusb-1.0
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS))

BUILD = build

all: $(BUILD)/rw5100-probe

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/rw5100.o: src/rw5100.c src/rw5100.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/rw5100-probe: tools/rw5100-probe.c $(BUILD)/rw5100.o
	$(CC) $(CFLAGS) -Isrc -o $@ $^ $(LDLIBS)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
