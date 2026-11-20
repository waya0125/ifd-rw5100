VERSION  = 0.1.0

CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -Wall -Wextra -std=c11 -D_DEFAULT_SOURCE -fPIC -pthread
PKGS     = libusb-1.0 libpcsclite
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS_USB = $(shell pkg-config --libs libusb-1.0)

# pcscd がドライバーを探す場所（Debian・Ubuntu では /usr/lib/pcsc/drivers）
USBDROPDIR ?= $(shell pkg-config --variable=usbdropdir libpcsclite)
UDEVDIR    ?= /usr/lib/udev/rules.d
DESTDIR    ?=

BUILD   = build
BUNDLE  = $(BUILD)/ifd-rw5100.bundle
SO      = $(BUNDLE)/Contents/Linux/libifd-rw5100.so
PLIST   = $(BUNDLE)/Contents/Info.plist

all: $(SO) $(PLIST) $(BUILD)/rw5100-probe

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/rw5100.o: src/rw5100.c src/rw5100.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/ifdhandler.o: src/ifdhandler.c src/rw5100.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(SO): $(BUILD)/ifdhandler.o $(BUILD)/rw5100.o
	mkdir -p $(dir $@)
	$(CC) -shared -pthread -Wl,-soname,libifd-rw5100.so -o $@ $^ $(LDLIBS_USB)

$(PLIST): src/Info.plist.in
	mkdir -p $(dir $@)
	sed 's/@VERSION@/$(VERSION)/g' $< > $@

$(BUILD)/rw5100-probe: tools/rw5100-probe.c $(BUILD)/rw5100.o
	$(CC) $(CFLAGS) -Isrc -o $@ $^ $(LDLIBS_USB)

install: $(SO) $(PLIST)
	install -d $(DESTDIR)$(USBDROPDIR)/ifd-rw5100.bundle/Contents/Linux
	install -m 0644 $(PLIST) $(DESTDIR)$(USBDROPDIR)/ifd-rw5100.bundle/Contents/Info.plist
	install -m 0755 $(SO) $(DESTDIR)$(USBDROPDIR)/ifd-rw5100.bundle/Contents/Linux/libifd-rw5100.so
	install -d $(DESTDIR)$(UDEVDIR)
	install -m 0644 udev/92-ifd-rw5100.rules $(DESTDIR)$(UDEVDIR)/92-ifd-rw5100.rules

uninstall:
	rm -rf $(DESTDIR)$(USBDROPDIR)/ifd-rw5100.bundle
	rm -f $(DESTDIR)$(UDEVDIR)/92-ifd-rw5100.rules

clean:
	rm -rf $(BUILD)

.PHONY: all install uninstall clean
