CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra

all: phosh-greeter-gtk

phosh-greeter-gtk: phosh-greeter.c
	$(CC) $(CFLAGS) $< -o $@ $$(pkg-config --cflags --libs gtk4)

clean:
	rm -f phosh-greeter-gtk

.PHONY: all clean
