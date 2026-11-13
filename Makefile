CC = cc
CFLAGS = -D_POSIX_C_SOURCE=200809L -std=c11 -Wall -Wextra -Wpedantic -Werror -O2

.PHONY: all clean

all: banker

banker: banker.c
	$(CC) $(CFLAGS) banker.c -o banker

clean:
	rm -f banker result.txt
