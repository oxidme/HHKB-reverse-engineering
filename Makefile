CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS = -framework IOKit -framework CoreFoundation

TOOLS = hhkb_probe hhkb_dump hhkb_write hhkb_fwdump

.PHONY: all clean
all: $(TOOLS)

$(TOOLS): %: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(TOOLS) *.dSYM
