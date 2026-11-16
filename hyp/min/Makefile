CC      ?= cc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -g -O0

kvm: main.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f kvm

.PHONY: clean
