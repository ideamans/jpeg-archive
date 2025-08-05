SRCDIR=./source
SRC= \
	$(SRCDIR)/bmp.c \
	$(SRCDIR)/hptime.c \
	$(SRCDIR)/main.c \
	$(SRCDIR)/test_convolve.c \
	$(SRCDIR)/test_decimate.c \
	$(SRCDIR)/test_mse.c \
	$(SRCDIR)/test_psnr.c \
	$(SRCDIR)/test_ssim.c \
	$(SRCDIR)/test_ms_ssim.c \
	$(SRCDIR)/test_fast_ssim.c

OBJ = $(SRC:.c=.o)

INCLUDES = -I./include -I../include
CC = gcc

ifeq ($(RELEASE),1)
OUTDIR=../build/release
CFLAGS=-O2 -Wall
else
OUTDIR=../build/debug
CFLAGS=-g -Wall
endif

OUT = $(OUTDIR)/test

LFLAGS=-L$(OUTDIR)

# OS detection for platform-specific libraries
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LIBS=$(OUTDIR)/libiqa.a -lm -lrt
else ifeq ($(UNAME_S),Darwin)
    LIBS=$(OUTDIR)/libiqa.a -lm
else
    # Default for other Unix-like systems
    LIBS=$(OUTDIR)/libiqa.a -lm
endif

.c.o:
	$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

$(OUT): $(OBJ)
	mkdir -p $(OUTDIR)
	$(CC) $(INCLUDES) $(CFLAGS) $(LFLAGS) $^ $(LIBS) -o $@
	cp ./resources/*.bmp $(OUTDIR)
	mv $(OBJ) $(OUTDIR)

clean:
	rm -f $(OUTDIR)/*.o $(OUT) $(SRCDIR)/*.o
	rm -f $(OUTDIR)/*.bmp

