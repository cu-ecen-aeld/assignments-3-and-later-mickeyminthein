CC := gcc
CFLAGS := -Wall -g
TARGET := writer

CROSS_COMPILE ?= 
CC := $(CROSS_COMPILE)gcc

# Exact match for cross-compile argument
# ifeq ($(MAKECMDGOALS),aarch64-none-linux-gnu-)
# CC := aarch64-none-linux-gnu-gcc
# endif

all: $(TARGET)

$(TARGET): writer.c
	$(CC) $(CFLAGS) -o $(TARGET) writer.c


clean:
	rm -f writer

