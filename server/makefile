#MAKEFILE MODIFIED FOR YOCTO
#AUTHOR - SUDARSHAN J

CROSS_COMPILE=
CC 	?= $(CROSS_COMPILE)gcc
CFLAGS	?= -g -Werror -Wall
LDFLAGS ?= -pthread -lrt
SRC = aesdsocket.c
OUTPUT = aesdsocket

all:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(OUTPUT) $(SRC) 

clean:
	-rm -f *.o $(OUTPUT) *.elf *.map