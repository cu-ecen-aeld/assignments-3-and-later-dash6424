all: aesdsocket

CROSS_COMPILE =
CC = gcc

aesdsocket: aesdsocket.c
		$(CROSS_COMPILE)$(CC) aesdsocket.c -Wall -Werror -g -o aesdsocket

clean:
	rm -rf *.o aesdsocket