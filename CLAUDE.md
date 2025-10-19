# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

sectorlisp is a 512-byte implementation of LISP that bootstraps John McCarthy's meta-circular evaluator on bare metal. The project has three main components:

1. **lisp.lisp** - Pure LISP meta-circular evaluator written as a single expression using only essential functions (CONS, CAR, CDR, QUOTE, ATOM, EQ, LAMBDA, COND)
2. **lisp.c** - Portable C reference implementation with readline interface for POSIX systems
3. **sectorlisp.S** - 512-byte i8086 assembly implementation that boots from BIOS as a master boot record

## Building

```sh
# Build all targets (C REPL + bootable binary)
make

# Build just the C REPL
make lisp

# Clean build artifacts
make clean
```

After building, you get:
- `lisp` - Interactive C REPL executable
- `sectorlisp.bin` - Bootable master boot record (512 bytes)
- `sectorlisp.bin.dbg` - Debug version with symbols

## Running

```sh
# Run the C implementation
./lisp

# Run in Blinkenlights emulator (recommended)
curl --compressed https://justine.lol/blinkenlights/blinkenlights-latest.com >blinkenlights.com
chmod +x blinkenlights.com
./blinkenlights.com -rt sectorlisp.bin

# Run in QEMU
qemu-system-i386 -nographic -fda sectorlisp.bin
```

## Testing

Tests are located in the `test/` directory. To run tests:

```sh
cd test
make test1      # Basic tests
make eval10     # Test older evaluator version
make eval15     # Test current evaluator version
```

Tests require qemu, cc, wc, and nc. For best results, resize terminal to 80x25.

## Architecture

### C Implementation (lisp.c)

The C implementation is a minimal LISP interpreter with:
- **String interning**: All symbols stored once in memory (M array points to second half of RAM)
- **RAM layout**: 100000-element array divided in two - first half for heap, second half (M) for interned strings
- **REPL**: Uses bestline library (bestline.c/h) for readline-like interface with history
- **Builtin symbols**: Predefined at fixed offsets (kT=4, kQuote=6, kCond=12, kRead=17, etc.)

Key functions:
- `Intern()` - String interning for symbols
- `GetChar()/GetToken()` - Tokenizer with lookahead
- `Cons()` - Allocates cons cells from RAM array
- `Eval()` - Core evaluator implementing McCarthy's eval
- `Apply()` - Function application including builtins

### Assembly Implementation (sectorlisp.S)

The assembly version is extremely size-constrained:
- Boots at address 0x7c00 (standard BIOS boot location)
- Uses segment registers: cs=ds=es=ss=0x7c00>>4
- Stack grows from 0x8000 downward
- NULL pointer points to "NIL" string at start of code
- Builtin symbols must be in specific order (ATOM last, EQ second-last, CONS third-last)
- `.partition` flag (line 42) controls whether partition table is included

### LISP Evaluator (lisp.lisp)

Pure LISP implementation with six bound functions (ASSOC, EVCON, PAIRLIS, EVLIS, APPLY, EVAL) that implement a complete meta-circular evaluator. This is the canonical implementation - both C and assembly implementations exist to bootstrap this code.

## Build Configuration

- Compiler flags: `-std=gnu89 -w -O`
- Assembler: gnu89 standard is required for proper compilation
- Linker script: `sectorlisp.lds` creates flat binary starting at address 0
- Object copy: `sectorlisp.bin` created by stripping symbols from `sectorlisp.bin.dbg`

## Code Style

- C code uses K&R style with minimal whitespace
- Functions often omit explicit return types (defaults to int)
- Global variables: `cx` (negative memory use), `dx` (lookahead char), `RAM` (memory array)
- Assembly uses AT&T syntax with Intel 8086 16-bit real mode instructions
