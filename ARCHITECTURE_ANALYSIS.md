# SectorLISP Architecture Analysis

## Comparing sectorlisp.S (512 bytes) vs lisp.c (Portable)

A deep analysis of two implementations of the same LISP semantics with radically different constraints.

---

## 1. Object Representation: The Core Difference

### lisp.c: Sign-Based Type System
```c
int RAM[0100000];  // 100,000 integers (400KB on 32-bit)

// Objects are encoded as integers:
// - Positive/Zero = Atom (offset into string table M)
// - Negative = Cons cell (negative index into M)
// - 0 = NIL
```

**Key insight:** Type is determined by **sign bit**
- `if (x < 0)` → cons cell
- `if (x >= 0)` → atom
- Cons cells grow **downward** from center (cx starts at 0, decrements)
- Atoms stored in upper half (M = RAM + 50000)

### sectorlisp.S: Same Sign-Based System
```asm
# 16-bit registers, memory at 0x7c00 onwards
# %ax = object (negative = cons, positive = atom)
# %si = often holds cons cell pointer
# %di = often holds atom/data pointer
```

**Identical semantics, different scale:**
- 16-bit integers (64KB address space)
- Stack at 0x8000, grows down
- Code starts at 0x7c00 (BIOS boot address)
- **Clever trick:** NULL (0x0000) points to "NIL" string at start of code!

---

## 2. Memory Layout: The Address Space

### lisp.c Memory Map
```
RAM[0]      ← Token buffer (temporary)
  ...
RAM[50000]  ← M: String table starts here
  "NIL\0T\0QUOTE\0COND\0READ\0PRINT\0ATOM\0CAR\0CDR\0CONS\0EQ\0"
  ... more interned strings ...
RAM[100000] ← End

Cons cells grow from middle:
cx starts at 0 (middle of RAM)
cx--, cx-- with each Cons()
M[cx] = car, M[cx+1] = cdr
```

**Why this layout?**
1. Token buffer at start (small, reused)
2. String table at M (grows upward with intern)
3. Cons heap in middle (grows downward from M)
4. Clear separation: atoms above, cons below

### sectorlisp.S Memory Map
```
0x0000   ← NULL = "NIL\0" string (genius!)
0x7c00   ← Code starts (BIOS loads MBR here)
         → "NIL\0T\0QUOTE\0..." builtin strings
0x7dxx   ← Code (~400 bytes)
0x8000   ← Stack pointer (SP), grows down
         ← Cons cells built on stack!
```

**Key differences:**
- **No separate heap**: cons cells ARE stack frames
- Strings embedded in code segment
- NULL dereference = "NIL" (intentional)
- Total: 512 bytes including partition table

---

## 3. String Interning: Finding Atoms

### lisp.c: Linear Search in String Table
```c
Intern() {
  // Search M for existing string matching RAM[0..j]
  for (i = 0; (x = M[i++]);) {
    for (j = 0;; ++j) {
      if (x != RAM[j]) break;    // Mismatch
      if (!x) return i - j - 1;  // Found!
      x = M[i++];
    }
    while (x) x = M[i++];  // Skip rest of string
  }
  // Not found, copy RAM to M
  j = 0; x = --i;
  while ((M[i++] = RAM[j++]));
  return x;  // Return offset into M
}
```

**Complexity:** O(n) where n = number of interned strings
**Returns:** Offset into M (positive integer = atom)

### sectorlisp.S: Inline Search with `cmpsb`
```asm
Intern:  # di points to token, cx = length
    xor  %di,%di         # Start at beginning of memory
1:  pop  %si
    push %si             # Get token from stack
    rep cmpsb            # memcmp(di,si,cx)
    je   9f              # Found! Return offset in ax
    xor  %ax,%ax
2:  scasb                # rawmemchr(di,'\0')
    jne  2b              # Skip to next string
    jmp  1b              # Try next
8:  rep movsb            # Not found, copy token
9:  pop  %cx; ret
```

**Same algorithm, different implementation:**
- Uses `rep cmpsb` (hardware string compare)
- `scasb` (scan string for byte) to skip strings
- Returns offset in %ax

---

## 4. Cons Cells: Allocation Strategy

### lisp.c: Downward Allocation
```c
Cons(car, cdr) {
  M[--cx] = cdr;  // Pre-decrement
  M[--cx] = car;  // Allocate 2 words
  return cx;      // Return negative index
}

Car(x) { return M[x]; }     // x is negative, M[x] = car
Cdr(x) { return M[x + 1]; } // M[x+1] = cdr
```

**Structure:**
```
M[cx]   = car
M[cx+1] = cdr
cx -= 2 each allocation
```

### sectorlisp.S: Stack-Based Allocation
```asm
Cons:   # di=car, ax=cdr → ax=new cons
    xchg %di,%cx         # Save car in cx
    mov  %cx,(%di)       # Store car
    mov  %ax,(%bx,%di)   # Store cdr (%bx=2)
    lea  4(%di),%cx      # cx += 4
    xchg %di,%ax         # Return in ax
    ret
```

**Wait, where's the allocation?**
- Uses `%cx` as allocation pointer (like cx in lisp.c)
- `lea 4(%di),%cx` = cx += 4 bytes
- Cons cells grow upward in stack space!

---

## 5. Garbage Collection: Copy-and-Compact

### lisp.c: After Every Eval
```c
Eval(e, a) {
  A = cx;                    // Mark heap start
  if (Car(e) == kCond) {
    e = Evcon(Cdr(e), a);
  } else {
    e = Apply(...);
  }
  B = cx;                    // Mark heap after eval
  e = Gc(e, A, A - B);       // Recursive copy
  C = cx;                    // New heap position
  while (C < B)
    M[--A] = M[--B];         // Compact: move down
  cx = A;                    // Reset allocator
  return e;
}

Gc(x, m, k) {
  return x < m ?
    Cons(Gc(Car(x), m, k), Gc(Cdr(x), m, k)) + k
    : x;  // Atoms unchanged
}
```

**Algorithm:** Cheney-style copying collector
1. Mark heap position A before eval
2. Eval creates garbage between A and B
3. Gc() recursively copies reachable data
4. Compact by moving B→C to A→A+(C-B)
5. Reset cx to A

**Why "ABC"?** A=before, B=after, C=after-gc

### sectorlisp.S: Identical ABC Pattern
```asm
Eval:
    push %dx              # Save a
    push %cx              # Save A
    call Evlis
    call Apply
    pop  %dx              # Restore A
    mov  %cx,%si          # si = B
    call Gc
    mov  %dx,%di          # di = A
    sub  %si,%cx          # cx = C - B
    rep movsb             # Compact
    mov  %di,%cx          # cx = A + (C-B)
    pop  %dx              # Restore a
    ret
```

**Exactly the same algorithm!**
- Push A (in %cx) before eval
- Call Gc with A in %dx, B in %si
- Use `rep movsb` for compaction
- Restore cx to compacted position

---

## 6. The Evaluator: McCarthy's EVAL/APPLY

Both implement identical semantics:

### Eval Rules
1. `e >= 0` (atom) → Assoc(e, a) — lookup in environment
2. `(QUOTE x)` → x — return unevaluated
3. `(COND ...)` → Evcon — conditional
4. `(f args...)` → Apply(f, Evlis(args), a) — function application

### Apply Rules
1. `f < 0` (lambda) → Eval(body, Pairlis(params, args, env))
2. Builtin atoms:
   - `CAR`, `CDR`, `CONS`, `EQ`, `ATOM`
   - `READ`, `PRINT`
3. `f > kEq` (unknown) → Apply(Eval(f, a), x, a) — resolve and retry

---

## 7. Size Optimizations in sectorlisp.S

### Every Byte Counts: Clever Tricks

**1. NULL = "NIL" (saves 3 bytes)**
```asm
_start: .asciz "NIL"  # At address 0!
```
Dereferencing NULL gives you "NIL" string for free.

**2. Instruction Overlap (saves ~20 bytes)**
```asm
Assoc:
    scasw                 # Compare and increment
    jne 1b
    .byte 0xA9            # test opcode (skips next instruction)
Cadr:
    mov (%bx,%di),%di     # Gets executed if jumped here
    .byte 0x3C            # cmp opcode (makes next byte immediate)
Cdr:
    scasw                 # Increment di by 2
Car:
    mov (%di),%ax
    ret
```

**How it works:**
- Jumping to `Cadr` executes `mov` then `scasw` then `mov`
- But calling `Cdr` directly starts at `scasw`
- The `0xA9` and `0x3C` bytes are opcodes that "hide" the next instruction
- Saves defining three separate functions

**3. Register Conventions (implicit)**
```asm
%bx = 2  (always! set at boot)
(%bx,%di) = M[di+2] = Cdr(di)
```
Using `%bx=2` means `(%bx,%di)` gets Cdr for free.

**4. Partition Table Bytes as Code**
```asm
.byte 0x00            # Partition entry
.byte 0b11010010      # Reads as: add %dl,%dl
```
The partition table at 0x1BE is executable! Inserted `add %dl,%dl` operations are no-ops but satisfy partition requirements.

**5. No Separate Heap Initialization**
- Strings are in code segment (RO)
- Stack grows from 0x8000
- No malloc, no initialization

---

## 8. Input/Output: Terminal vs BIOS

### lisp.c: Modern OS (bestline)
```c
GetChar() {
  static char *l, *p;
  if (l || (l = p = bestlineWithHistory("* ", "sectorlisp"))) {
    if (*p) c = *p++ & 255;
    else { free(l); l = p = 0; c = '\n'; }
    t = dx; dx = c; return t;  // Lookahead
  }
  exit(0);
}

PrintChar(b) {
  fputwc(b, stdout);  // Unicode support
}
```

Features:
- Line editing with history
- Unicode output (fputwc)
- Lookahead in `dx`

### sectorlisp.S: BIOS Interrupts
```asm
GetChar:
    xor  %ax,%ax
    int  $0x16      # BIOS keyboard service
    mov  %ax,%bp    # Save for READ
    # Falls through to PutChar for echo

PutChar:
    mov  $0x0e,%ah
    int  $0x10      # BIOS video service
    xchg %dx,%ax    # Lookahead
    ret
```

Features:
- Direct BIOS calls (no OS!)
- CP-437 encoding (DOS code page)
- Lookahead in `%dx` (same as lisp.c!)
- Echo happens automatically

---

## 9. Builtin Symbols: Critical Ordering

### lisp.c: Offsets in String Table
```c
#define kT      4
#define kQuote  6
#define kCond   12
#define kRead   17
#define kPrint  22
#define kAtom   28
#define kCar    33
#define kCdr    37
#define kCons   41
#define kEq     46
```

These are byte offsets into:
```c
#define S "NIL\0T\0QUOTE\0COND\0READ\0PRINT\0ATOM\0CAR\0CDR\0CONS\0EQ"
```

### sectorlisp.S: Labels in Code Segment
```asm
_start: .asciz "NIL"
kT:     .asciz "T"
        ...
kCar:   .asciz "CAR"
kCdr:   .asciz "CDR"
kCons:  .asciz "CONS"  # Must be 3rd last!
kEq:    .asciz "EQ"    # Must be 2nd last!
kAtom:  .asciz "ATOM"  # Must be last!
```

**Why ordering matters:**
```asm
Builtin:
    cmp  $kAtom,%ax   # Is it >= ATOM?
    ja   .resolv      # If above, resolve symbol
    je   .ifAtom      # If equal, it's ATOM
    cmp  $kCons,%al   # 3rd last
    jb   Cdr          # Below CONS → must be CDR or CAR
```

The comparison chain relies on:
- ATOM being last (highest address)
- EQ being 2nd last
- CONS being 3rd last
- Everything else < CONS

---

## 10. Critical Differences

| Aspect | lisp.c | sectorlisp.S |
|--------|---------|--------------|
| **Size** | ~7KB compiled | **512 bytes** |
| **Platform** | POSIX + libc | Bare metal x86 |
| **I/O** | bestline (readline) | BIOS interrupts |
| **Memory** | 400KB RAM | ~30KB (0x8000) |
| **Heap** | Array M[] | Stack frames |
| **Strings** | Grows in M | Embedded in code |
| **GC** | After every eval | After every eval |
| **Encoding** | UTF-8/wchar | CP-437 |
| **Bootable** | No | Yes! |

---

## 11. Shared Design Decisions

Both implementations make **identical semantic choices:**

1. **Sign-based typing** (negative=cons, positive=atom)
2. **String interning** (symbols compared by pointer/offset)
3. **ABC garbage collection** (mark-copy-compact after eval)
4. **Lookahead character** (dx register/variable)
5. **NIL = 0** (empty list and false)
6. **Same builtins** (CONS, CAR, CDR, EQ, ATOM, QUOTE, COND, READ, PRINT)
7. **Same parsing** (recursive descent, no error handling)
8. **Same evaluation order** (Evlis evaluates left-to-right)

---

## 12. Why These Designs?

### The Sign Bit Choice
**Brilliance:** Type checking is a single comparison
```c
if (x < 0) { /* cons */ } else { /* atom */ }
```
- No tag bits needed
- No masking operations
- Works in 16-bit or 32-bit
- Cons cells can be anywhere < 0

### The String Table
**Benefit:** Symbol equality is pointer comparison
```c
if (x == y)  // Fast! No strcmp needed
```
- O(1) equality checks
- Saves space (no duplicate strings)
- Natural for McCarthy's LISP (eq vs equal)

### The ABC GC
**Simple and correct:**
- No mark bits (copying collector)
- Automatic compaction
- Preserves relative ordering
- Works with sign-based scheme

### Why Grow Downward?
**Answer:** Meet in the middle!
```
[  tokens  ][ strings→ ][ ←cons cells ][    stack    ]
0          50000                               100000
```
- Strings grow up from M
- Cons cells grow down toward M
- Maximum space utilization
- Collision detection = cx becomes positive

---

## 13. The Genius of sectorlisp.S

**Fitting McCarthy's LISP in 512 bytes requires:**

1. **No separate data segment** — strings in code
2. **No heap allocation** — use the stack
3. **Instruction overlap** — one byte = two meanings
4. **Leveraging BIOS** — free I/O
5. **NULL = "NIL"** — hardware helps you
6. **Careful register use** — %bx=2 throughout
7. **Partition table as code** — every byte counts

**The result:** A fully functional LISP that boots from disk!

---

## 14. Key Takeaways

1. **Same Algorithm, Different Scale**
   - lisp.c and sectorlisp.S implement identical semantics
   - One is readable, one is minimal
   - Both are brilliant in their own way

2. **Sign-Based Types Are Elegant**
   - Simple, fast, portable
   - Works in 16-bit and 32-bit
   - No complex tagged pointers

3. **Garbage Collection Is Essential**
   - Even in 512 bytes
   - ABC pattern is simple and correct
   - Happens after every eval

4. **McCarthy's LISP Is Minimal**
   - 7 special forms + 5 primitives
   - Meta-circular evaluator fits in pure LISP
   - Can bootstrap itself

5. **Constraints Breed Creativity**
   - sectorlisp.S uses every trick imaginable
   - NULL dereference as feature
   - Partition table as executable code
   - Instruction overlap

---

## 15. Further Reading

- **McCarthy's 1960 Paper:** "Recursive Functions of Symbolic Expressions"
- **lisp.lisp:** The meta-circular evaluator these implementations bootstrap
- **GDB_TUI_CHEATSHEET.md:** Debugging lisp_modern
- **lisp_gdb_cheatsheet.md:** Debugging lisp_gdb

Both implementations exist to bootstrap **lisp.lisp**, the pure LISP meta-circular evaluator. That's the real achievement — writing LISP in LISP, then building the minimal machine to run it.

---

*This analysis demonstrates that fundamental computational ideas transcend their implementation. McCarthy's LISP can live in 512 bytes or 7KB, in assembly or C, on bare metal or POSIX — the semantics remain the same.*
