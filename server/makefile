#Maefile for socket programming 
# Author - Sudarshan J

all: aesdsocket

CROSS_COMPILE =
CC = gcc

aesdsocket: aesdsocket.c
		 $(CROSS_COMPILE)$(CC) aesdsocket.c -Wall -Werror -o aesdsocket
		 

clean:
	rm -rf *.o aesdsocket
