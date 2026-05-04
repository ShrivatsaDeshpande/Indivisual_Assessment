CC=gcc
CFLAGS=-O0 -Wall -Wextra -std=c11 -Ishared
LDFLAGS=-lrt

all: build/victim build/attacker

build/victim: victim/victim.c shared/shared_mem.h
	mkdir -p build
	$(CC) $(CFLAGS) -o build/victim victim/victim.c $(LDFLAGS)

build/attacker: attacker/attacker.c shared/shared_mem.h
	mkdir -p build
	$(CC) $(CFLAGS) -o build/attacker attacker/attacker.c $(LDFLAGS)

clean:
	rm -rf build/*
