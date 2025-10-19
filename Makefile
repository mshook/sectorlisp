CFLAGS = -std=gnu89 -w -O
CFLAGS_MODERN = -std=c99 -g -Wall -Wextra -O2

CLEANFILES =				\
	lisp				\
	lisp.o				\
	lisp_modern			\
	lisp_modern.o			\
	bestline.o			\
	sectorlisp.o			\
	sectorlisp.bin			\
	sectorlisp.bin.dbg

.PHONY:	all
all:	lisp				\
	sectorlisp.bin			\
	sectorlisp.bin.dbg

.PHONY:	clean
clean:;	$(RM) lisp lisp.o lisp_modern lisp_modern.o bestline.o sectorlisp.o sectorlisp.bin sectorlisp.bin.dbg

lisp: lisp.o bestline.o
lisp.o: lisp.c bestline.h

lisp_modern: lisp_modern.o bestline.o
	$(CC) $(CFLAGS_MODERN) -o $@ $^
lisp_modern.o: lisp_modern.c bestline.h
	$(CC) $(CFLAGS_MODERN) -c -o $@ $<

bestline.o: bestline.c bestline.h

sectorlisp.o: sectorlisp.S
	$(AS) -g -o $@ $<

sectorlisp.bin.dbg: sectorlisp.o sectorlisp.lds
	$(LD) -T sectorlisp.lds -o $@ $<

sectorlisp.bin: sectorlisp.bin.dbg
	objcopy -S -O binary sectorlisp.bin.dbg sectorlisp.bin
