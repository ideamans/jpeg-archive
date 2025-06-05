CC ?= gcc
CFLAGS += -std=c99 -Wall -O3
LDFLAGS += -lm
MAKE ?= make
PREFIX ?= /usr/local

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
	# Linux (e.g. Ubuntu)
	MOZJPEG_PREFIX ?= /opt/mozjpeg
	CFLAGS += -I$(MOZJPEG_PREFIX)/include

	ifneq ("$(wildcard $(MOZJPEG_PREFIX)/lib64/libjpeg.a)","")
		LIBJPEG = $(MOZJPEG_PREFIX)/lib64/libjpeg.a
	else
		LIBJPEG = $(MOZJPEG_PREFIX)/lib/libjpeg.a
	endif
else ifeq ($(UNAME_S),Darwin)
	# Mac OS X
	MOZJPEG_PREFIX ?= /usr/local/opt/mozjpeg
	LIBJPEG = $(MOZJPEG_PREFIX)/lib/libjpeg.a
	CFLAGS += -I$(MOZJPEG_PREFIX)/include
else ifeq ($(UNAME_S),FreeBSD)
	# FreeBSD
	LIBJPEG = $(PREFIX)/lib/mozjpeg/libjpeg.so
	CFLAGS += -I$(PREFIX)/include/mozjpeg
else
	# Windows
	LIBJPEG = ../mozjpeg/libjpeg.a
	CFLAGS += -I../mozjpeg
endif

LIBIQA=src/iqa/build/release/libiqa.a

all: jpeg-recompress jpeg-compare jpeg-hash libjpegarchive.a

$(LIBIQA):
	cd src/iqa; RELEASE=1 $(MAKE)

jpeg-recompress: jpeg-recompress.c src/util.o src/edit.o src/smallfry.o $(LIBIQA)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBJPEG) $(LDFLAGS)

jpeg-compare: jpeg-compare.c src/util.o src/hash.o src/edit.o src/smallfry.o $(LIBIQA)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBJPEG) $(LDFLAGS)

jpeg-hash: jpeg-hash.c src/util.o src/hash.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBJPEG) $(LDFLAGS)

jpegarchive.o: jpegarchive.c jpegarchive.h
	$(CC) $(CFLAGS) -c -o $@ $<

libjpegarchive.a: jpegarchive.o src/util.o src/edit.o src/smallfry.o $(LIBIQA)
	mkdir -p tmp
	cd tmp && ar x ../$(LIBIQA)
	ar rcs $@ jpegarchive.o src/util.o src/edit.o src/smallfry.o tmp/*.o
	rm -rf tmp

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

test: test/test.c src/util.o src/edit.o src/hash.o $(LIBIQA)
	$(CC) $(CFLAGS) -o test/$@ $^ $(LIBJPEG) $(LDFLAGS)
	./test/$@

install: all
	mkdir -p $(PREFIX)/bin
	cp jpeg-archive $(PREFIX)/bin/
	cp jpeg-recompress $(PREFIX)/bin/
	cp jpeg-compare $(PREFIX)/bin/
	cp jpeg-hash $(PREFIX)/bin/

test-lib: libjpegarchive.a jpeg-recompress
	cd golang && go test -v ./...

test-memory-leaking: libjpegarchive.a jpeg-recompress
	cd golang && go test -v -run TestMemoryLeak
	cd golang && valgrind --leak-check=full --show-leak-kinds=all go test -run TestMemoryLeak

clean:
	rm -rf jpeg-recompress jpeg-compare jpeg-hash test/test src/*.o src/iqa/build jpegarchive.o libjpegarchive.a tmp/

.PHONY: test test-lib test-memory-leaking install clean
