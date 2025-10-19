# GDB TUI Mode Cheat Sheet for lisp_modern

Quick reference for debugging `lisp_modern` with GDB's Text User Interface.

## Starting TUI Mode

```bash
# Build with debug symbols (already in Makefile)
make lisp_modern

# Start in TUI mode
gdb -tui ./lisp_modern

# Or start normally and enable TUI later
gdb ./lisp_modern
(gdb) tui enable
# or: Ctrl-x a
```

## TUI Layouts

| Command | Description |
|---------|-------------|
| `layout src` | Show source code |
| `layout asm` | Show assembly code |
| `layout split` | Show both source and assembly |
| `layout regs` | Show registers + source/assembly |
| `tui disable` | Exit TUI mode (or `Ctrl-x a`) |

## Switching Between Source and Assembly

```gdb
# Toggle between source and assembly
Ctrl-x 2

# Explicitly switch layouts
layout src       # Source only
layout asm       # Assembly only
layout split     # Both source and assembly

# With registers
layout regs      # Registers + current layout
```

## TUI Window Navigation

| Key | Action |
|-----|--------|
| `Ctrl-x a` | Toggle TUI mode on/off |
| `Ctrl-x 1` | Single window (cycle through layouts) |
| `Ctrl-x 2` | Split window / toggle layout |
| `Ctrl-x o` | Switch active window |
| `Ctrl-l` | Refresh screen |
| `Ctrl-p` | Previous command in history |
| `Ctrl-n` | Next command in history |

## Scrolling in TUI Windows

```gdb
# First, make the source/asm window active
Ctrl-x o         # Switch to source/asm window

# Then scroll with arrow keys or:
Up/Down          # Scroll line by line
PgUp/PgDown      # Scroll page by page

# Switch back to command window
Ctrl-x o
```

## Common Debugging Commands

### Breakpoints
```gdb
# Set breakpoints
break main
break eval
break cons
break 156              # Break at line 156

# List and manage breakpoints
info breakpoints
delete 1               # Delete breakpoint #1
clear                  # Clear all breakpoints
```

### Running
```gdb
run                    # Start program
continue               # Continue execution (or 'c')
step                   # Step into (or 's')
next                   # Step over (or 'n')
finish                 # Run until current function returns
until 200              # Run until line 200
```

### Examining Data
```gdb
# Print variables
print heap_ptr
print/x memory[0]      # Print in hex
print symbol_table[0]

# Examine memory
x/10x &memory[0]       # 10 words in hex
x/10i read_expression  # 10 instructions (disassemble)

# Display expressions continuously
display heap_ptr
display lookahead_char

# Show info
info locals            # Local variables
info args              # Function arguments
info registers         # All registers
```

### Navigation
```gdb
backtrace              # Show call stack (or 'bt')
frame 2                # Switch to frame #2
up                     # Move up stack frame
down                   # Move down stack frame
list                   # Show source around current line
disassemble            # Show assembly of current function
```

## Example Debugging Session

```bash
$ gdb -tui ./lisp_modern

(gdb) break main
(gdb) run
(gdb) layout split        # View source and assembly
(gdb) break eval          # Break at evaluator
(gdb) continue

# When at eval breakpoint:
(gdb) print expr          # Print expression being evaluated
(gdb) print env           # Print environment
(gdb) layout asm          # Switch to assembly only
(gdb) stepi               # Step one assembly instruction
(gdb) layout src          # Back to source
(gdb) finish              # Complete current function
```

## Useful Settings

```gdb
# In ~/.gdbinit or at GDB prompt:
set disassembly-flavor intel    # Use Intel syntax instead of AT&T
set print pretty on             # Pretty-print structures
set pagination off              # Don't pause output
set history save on             # Save command history

# Focus follows active window
tui new-layout debug src 1 asm 1 status 0 cmd 1
```

## Quick Tips

1. **Screen corrupted?** Press `Ctrl-l` to refresh
2. **Can't type?** You might be in the source window - press `Ctrl-x o` to switch back
3. **Want Intel assembly?** Use `set disassembly-flavor intel`
4. **See function calls?** Use `layout split` and step through
5. **Lost?** Use `backtrace` to see where you are in the call stack

## Tracing LISP Execution

```gdb
# Trace through a LISP expression evaluation
break read_expression
run
# Type: (CONS (QUOTE A) (QUOTE B))
next                    # Step through parsing
break eval              # Set breakpoint at evaluator
continue
print/x expr            # See expression as hex value
layout split            # See both C and assembly
step                    # Step into eval
```

## Assembly Instruction Stepping

When in assembly view (`layout asm`):

```gdb
si        # Step one instruction (step into calls)
ni        # Next instruction (step over calls)
x/i $pc   # Examine instruction at program counter
info reg  # Show all registers
```

## Exiting

```gdb
quit      # Exit GDB (or Ctrl-d)
```
