CC = gcc
CFLAGS = -Wall -O2
LIBS = -lusb-1.0 -lm

TARGETS = libusb_reader data_logger

all: $(TARGETS)

libusb_reader: libusb_reader.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

data_logger: data_logger.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean
