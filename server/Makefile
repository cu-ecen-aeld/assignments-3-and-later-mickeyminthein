CROSS_COMPILE ?= 
CC ?= $(CROSS_COMPILE)gcc

CFLAGS ?= -Wall -g
LDFLAGS ?=
LIBS ?= -lpthread -lrt
TARGET := aesdsocket

all: $(TARGET)

$(TARGET): aesdsocket.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) aesdsocket.c $(LIBS)

clean:
	rm -f $(TARGET)
