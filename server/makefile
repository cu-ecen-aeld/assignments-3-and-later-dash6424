# Makefile for socket program.
# Author - Daanish Shariff

CC=gcc
CFLAGS=-Wall -Werror

all: aesdsocket.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket -lpthread

clean:
	rm -f *.o aesdsocket
