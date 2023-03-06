#MAKEFILE MODIFIED FOR YOCTO
#AUTHOR - SUDARSHAN J

CROSS_COMPILE=
CC 	?= $(CROSS_COMPILE)gcc
CFLAGS	?= -g -Werror -Wall
LDFLAGS ?= -pthread -lrt
SRC = aesdsocket.c
OUTPUT = aesdsocket

all:aesdsocket

aesdsocket:aesdsocket.c
	$(CC) $(SRC) $(CFLAGS) -o $@ $(LDFLAGS)

.c.o: $(SRC)
	$(CC) $(CFLAGS) -c $<

clean:
	-rm -f *.o $(OUTPUT) *.elf *.map
