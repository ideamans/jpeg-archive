CC ?= gcc
CFLAGS += -std=c99 -Wall -O3
LDFLAGS += -lm
MAKE ?= make
PREFIX ?= /usr/local

# mozjpeg build paths
DEPS_DIR = deps
MOZJPEG_BUILD_DIR = $(DEPS_DIR)/building/mozjpeg
MOZJPEG_PREFIX = $(DEPS_DIR)/built/mozjpeg

# mozjpeg libraries and headers
LIBJPEG = $(MOZJPEG_PREFIX)/lib/libturbojpeg.a
JPEGLIB_H = $(MOZJPEG_PREFIX)/include/jpeglib.h
CFLAGS += -I$(MOZJPEG_PREFIX)/include

LIBIQA = src/iqa/build/release/libiqa.a

all: jpeg-recompress jpeg-compare jpeg-hash libjpegarchive.a

$(LIBIQA):
	cd src/iqa; RELEASE=1 $(MAKE)

$(JPEGLIB_H): $(LIBJPEG)

jpeg-recompress: jpeg-recompress.c src/util.o src/edit.o src/smallfry.o $(LIBIQA) $(LIBJPEG) $(JPEGLIB_H)
	$(CC) $(CFLAGS) -o $@ $< src/util.o src/edit.o src/smallfry.o $(LIBIQA) $(LIBJPEG) $(LDFLAGS)

jpeg-compare: jpeg-compare.c src/util.o src/hash.o src/edit.o src/smallfry.o $(LIBIQA) $(LIBJPEG) $(JPEGLIB_H)
	$(CC) $(CFLAGS) -o $@ $< src/util.o src/hash.o src/edit.o src/smallfry.o $(LIBIQA) $(LIBJPEG) $(LDFLAGS)

jpeg-hash: jpeg-hash.c src/util.o src/hash.o $(LIBJPEG) $(JPEGLIB_H)
	$(CC) $(CFLAGS) -o $@ $< src/util.o src/hash.o $(LIBJPEG) $(LDFLAGS)

jpegarchive.o: jpegarchive.c jpegarchive.h src/util.o src/edit.o src/smallfry.o $(LIBIQA) $(JPEGLIB_H)
	$(CC) $(CFLAGS) -c -o $@ $<

libjpegarchive.a: jpegarchive.o src/util.o src/edit.o src/smallfry.o
	ar rcs $@ jpegarchive.o src/util.o src/edit.o src/smallfry.o

%.o: %.c %.h $(JPEGLIB_H)
	$(CC) $(CFLAGS) -c -o $@ $<

test: jpeg-recompress test/test.c src/util.o src/edit.o src/hash.o test/libjpegarchive.c libjpegarchive.a $(LIBIQA) $(LIBJPEG)
	$(CC) $(CFLAGS) -o test/test test/test.c src/util.o src/edit.o src/hash.o $(LIBJPEG) $(LDFLAGS)
	$(CC) $(CFLAGS) -o test/libjpegarchive test/libjpegarchive.c libjpegarchive.a $(LIBIQA) $(LIBJPEG) $(LDFLAGS)
	cd test; ./test.sh

install: all
	mkdir -p $(PREFIX)/bin
	cp jpeg-archive $(PREFIX)/bin/
	cp jpeg-recompress $(PREFIX)/bin/
	cp jpeg-compare $(PREFIX)/bin/
	cp jpeg-hash $(PREFIX)/bin/

build: $(LIBJPEG)

$(LIBJPEG):
	@mkdir -p $(MOZJPEG_BUILD_DIR)
	@if [ ! -d "$(MOZJPEG_BUILD_DIR)/.git" ]; then \
		git clone --branch v4.1.5 --depth 1 https://github.com/mozilla/mozjpeg.git $(MOZJPEG_BUILD_DIR); \
	fi
	@cd $(MOZJPEG_BUILD_DIR) && \
		mkdir -p build && \
		cd build && \
		CMAKE_ARGS="-G\"Unix Makefiles\" \
			-DCMAKE_BUILD_TYPE=Release \
			-DCMAKE_INSTALL_PREFIX=$(abspath $(MOZJPEG_PREFIX)) \
			-DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
			-DWITH_JPEG8=1 \
			-DENABLE_SHARED=0 \
			-DENABLE_STATIC=1 \
			-DBUILD_SHARED_LIBS=0 \
			-DPNG_SUPPORTED=0"; \
		case "$(shell uname -m)" in \
			aarch64|arm64) CMAKE_ARGS="$$CMAKE_ARGS -DWITH_SIMD=0" ;; \
		esac; \
		eval cmake $$CMAKE_ARGS .. && \
		$(MAKE) && \
		$(MAKE) install

clean:
	rm -rf jpeg-recompress jpeg-compare jpeg-hash libjpegarchive.a jpegarchive.o test/test test/libjpegarchive src/*.o src/iqa/build $(DEPS_DIR)

.PHONY: test test-libjpegarchive-build install clean build
