#CC := gcc
CFLAGS := -Wall -g
LIBS = -lpthread
TARGET := aesdsocket

CROSS_COMPILE ?= 
CC := $(CROSS_COMPILE)gcc

# Exact match for cross-compile argument
# ifeq ($(MAKECMDGOALS),aarch64-none-linux-gnu-)
# CC := aarch64-none-linux-gnu-gcc
# endif

all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) -o $(TARGET) aesdsocket.c $(LIBS)

clean:
	rm -f $(TARGET)
