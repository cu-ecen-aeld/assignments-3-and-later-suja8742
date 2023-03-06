#Maefile for socket programming 
# Author - Sudarshan J

CC=gcc

all: aesdsocket.c
		 $(CROSS_COMPILE)$(CC) aesdsocket.c -Wall -Werror -o aesdsocket -lpthread -lrt
		 

clean:
	rm -rf *.o aesdsocket
