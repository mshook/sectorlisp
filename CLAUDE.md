# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

sectorlisp is a 512-byte implementation of LISP that bootstraps John McCarthy's meta-circular evaluator on bare metal. The project has five main components:

1. **lisp.lisp** - Pure LISP meta-circular evaluator written as a single expression using only essential functions (CONS, CAR, CDR, QUOTE, ATOM, EQ, LAMBDA, COND)
2. **lisp.c** - Portable C reference implementation with readline interface for POSIX systems (K&R style)
3. **lisp_modern.c** - Modern C99 version with conventional programming practices (same behavior as lisp.c)
4. **lisp_gdb.c** - GDB-friendly version with explicit data structures (tagged unions, real pointers) for easier debugging
5. **sectorlisp.S** - 512-byte i8086 assembly implementation that boots from BIOS as a master boot record

## Building

```sh
# Build all targets (C REPL + bootable binary)
make

# Build just the original C REPL
make lisp

# Build the modern C99 version with debug symbols
make lisp_modern

# Build the GDB-friendly version (manual build)
gcc -std=c99 -g -Wall -Wextra -O0 -o lisp_gdb lisp_gdb.c bestline.c -lm

# Clean build artifacts
make clean
```

After building, you get:
- `lisp` - Interactive C REPL executable (original K&R style)
- `lisp_modern` - Interactive C REPL executable (modern C99 with debug symbols)
- `lisp_gdb` - GDB-friendly REPL with explicit data structures for debugging
- `sectorlisp.bin` - Bootable master boot record (512 bytes)
- `sectorlisp.bin.dbg` - Debug version with symbols

## Running

```sh
# Run the original C implementation
./lisp

# Run the modern C99 implementation (identical behavior)
./lisp_modern

# Run the GDB-friendly version (identical behavior, easier to debug)
./lisp_gdb

# Debug with GDB (see GDB_TUI_CHEATSHEET.md and lisp_gdb_cheatsheet.md)
gdb -tui ./lisp_modern
gdb -tui ./lisp_gdb

# Run in Blinkenlights emulator (recommended for sectorlisp.bin)
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

### Modern C Implementation (lisp_modern.c)

A modernized version of lisp.c with identical behavior but conventional C99 programming practices:
- **Explicit function signatures**: All functions have proper prototypes with typed parameters
- **Better naming**: `get_char()` vs `GetChar()`, `heap_ptr` vs `cx`, `symbol_table` vs `M` macro
- **Type definitions**: `lisp_object_t` typedef, `IS_CONS()` and `IS_ATOM()` macros
- **Documentation**: Extensive comments explaining memory layout, algorithms, and data structures
- **Named constants**: `SYMBOL_CAR`, `SYMBOL_CONS`, etc. instead of magic numbers
- **Debug symbols**: Compiled with `-g` flag for GDB debugging
- **Modern C standards**: C99 features including `int32_t`, `size_t`, `bool`, `<wchar.h>`

Key architectural improvements for readability:
- Grouped functions by category (I/O, parsing, primitives, evaluator)
- Static functions for internal implementation details
- Comprehensive header comments on complex algorithms
- Same memory layout and semantics as lisp.c

### GDB-Friendly C Implementation (lisp_gdb.c)

A debugging-optimized version of the LISP interpreter with explicit data structures:
- **Tagged unions**: Each object has a visible `type` field (TYPE_NIL, TYPE_ATOM, TYPE_CONS)
- **Real C pointers**: Uses actual pointers instead of array indices for car/cdr
- **Explicit heap**: Array of `lisp_object_t` structs instead of flat integer array
- **String table**: Separate array of string pointers for interned symbols
- **Mark bit**: Each object has a `marked` field for garbage collection (currently disabled)
- **Same semantics**: Identical LISP behavior to lisp.c, just more debuggable

Key benefits for GDB:
- `p *expr` shows type, data, and structure
- `p expr->data.symbol` shows actual symbol strings
- `p *expr->data.pair.car` follows pointers naturally
- Can set breakpoints and examine structures intuitively

See **lisp_gdb_cheatsheet.md** for GDB commands specific to lisp_gdb's data structures.

**Known Issues & Fixes:**
- Fixed infinite recursion in `apply()` when function evaluates to NIL (commit 4ab5fbd)
  - Bug: Undefined symbols in environment returned NIL, causing `apply(NIL, ...)` → `apply(eval(NIL), ...)` → infinite loop
  - Fix: Added explicit TYPE_NIL check at start of apply() to return NIL gracefully
- Garbage collection currently disabled due to pointer relocation issues
  - Heap size (50000 objects) is sufficient for current test cases
  - Mark-and-sweep infrastructure present but commented out

### LISP Evaluator (lisp.lisp)

Pure LISP implementation with six bound functions (ASSOC, EVCON, PAIRLIS, EVLIS, APPLY, EVAL) that implement a complete meta-circular evaluator. This is the canonical implementation - both C and assembly implementations exist to bootstrap this code.

The file **metacircular.lisp** contains just the metacircular evaluator extracted from lisp.lisp for easier testing and experimentation.

## Example LISP Programs

The repository includes several example programs demonstrating pure LISP programming:

- **firstatom.lisp** - Find first atom in a nested list structure
  - Input: `((A) B C)` → Output: `A`
  - Demonstrates recursive function binding through closures
  - Pattern: `((LAMBDA (FF X) (FF X)) (QUOTE (LAMBDA ...)) (QUOTE data))`

- **flatten.lisp** - Flatten nested list structure completely
  - Input: `((A) B C)` → Output: `(A B C)`
  - Input: `((TO BE) OR ((NOT) TO BE))` → Output: `(TO BE OR NOT TO BE)`
  - Defines APPEND helper inline to concatenate lists
  - Recursively processes both atoms and sublists

- **metacircular.lisp** - Pure LISP metacircular evaluator
  - Extracted from lisp.lisp for easier testing
  - Evaluates: `((LAMBDA (FF X) (FF X)) (QUOTE (LAMBDA ...)) (QUOTE ((A) B C)))`
  - Returns: `A`

All examples follow the pattern of binding functions in the environment and then calling them, similar to firstatom.lisp.

## Debugging

**GDB_TUI_CHEATSHEET.md** provides a comprehensive guide for debugging lisp_modern with GDB:
- TUI (Text User Interface) mode for visual debugging
- Switching between source and assembly views
- Setting breakpoints and stepping through code
- Examining LISP objects and memory
- Example debugging sessions

**lisp_gdb_cheatsheet.md** provides GDB commands specific to lisp_gdb:
- Inspecting LISP objects with explicit types
- Following pointers through car/cdr
- Examining the heap and symbol table
- Conditional breakpoints on object types
- Example debugging scenarios

Quick start (lisp_modern):
```sh
gdb -tui ./lisp_modern
(gdb) break eval
(gdb) run
(gdb) layout split    # View source and assembly
```

Quick start (lisp_gdb):
```sh
gdb -tui ./lisp_gdb
(gdb) break eval
(gdb) run
* (CAR '(A B C))
(gdb) p *expr                    # See the expression structure
(gdb) p expr->type               # 0=NIL, 1=ATOM, 2=CONS
(gdb) p *expr->data.pair.car     # Follow the CAR pointer
```

Key TUI shortcuts:
- `Ctrl-x a` - Toggle TUI mode
- `layout src` - Source code view
- `layout asm` - Assembly view
- `layout split` - Both source and assembly
- `Ctrl-x 2` - Toggle between layouts

## Build Configuration

- **lisp.c** compiler flags: `-std=gnu89 -w -O`
- **lisp_modern.c** compiler flags: `-std=c99 -g -Wall -Wextra -O2`
- **lisp_gdb.c** compiler flags: `-std=c99 -g -Wall -Wextra -O0` (no optimization for debugging)
- Assembler: gnu89 standard is required for proper compilation
- Linker script: `sectorlisp.lds` creates flat binary starting at address 0
- Object copy: `sectorlisp.bin` created by stripping symbols from `sectorlisp.bin.dbg`

## Code Style

- C code uses K&R style with minimal whitespace
- Functions often omit explicit return types (defaults to int)
- Global variables: `cx` (negative memory use), `dx` (lookahead char), `RAM` (memory array)
- Assembly uses AT&T syntax with Intel 8086 16-bit real mode instructions
