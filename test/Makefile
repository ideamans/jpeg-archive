CC = gcc
CFLAGS = -I../ -I../src -Wall -O2
LDFLAGS = -L../ -ljpegarchive -ljpeg -lm

# Path to mozjpeg
MOZJPEG_PREFIX ?= /usr/local/opt/mozjpeg
CFLAGS += -I$(MOZJPEG_PREFIX)/include
LDFLAGS += -L$(MOZJPEG_PREFIX)/lib

all: test_subsampling

test_subsampling: test_subsampling.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

run: test_subsampling
	./test_subsampling

clean:
	rm -f test_subsampling

.PHONY: all run clean