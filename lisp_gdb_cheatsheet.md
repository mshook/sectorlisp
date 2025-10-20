# GDB Cheat Sheet for lisp_gdb

## Starting GDB

```bash
# Interactive debugging
gdb ./lisp_gdb
(gdb) run

# With input file
gdb ./lisp_gdb
(gdb) run < lisp.lisp

# TUI mode (split screen with source)
gdb -tui ./lisp_gdb
```

## Essential Breakpoints

```gdb
# Core evaluation functions
break eval
break apply
break read_expr

# Memory allocation
break make_cons
break make_atom

# Primitives
break car
break cdr
break cons

# String interning
break intern_string
```

## Inspecting LISP Objects

```gdb
# Print object with type
p *expr
p expr->type          # 0=NIL, 1=ATOM, 2=CONS

# For atoms
p expr->data.symbol   # String pointer
p *expr->data.symbol  # Character at string

# For cons cells
p *expr->data.pair.car
p *expr->data.pair.cdr

# Follow chain
p *expr->data.pair.cdr->data.pair.car
```

## Navigation & Stepping

```gdb
# Step into functions
step (or s)

# Step over functions
next (or n)

# Continue to next breakpoint
continue (or c)

# Finish current function
finish

# Step one assembly instruction
stepi (or si)
```

## Examining Memory & State

```gdb
# Global variables
p heap_ptr            # Current heap position
p symbol_count        # Number of symbols
p nil_obj             # NIL singleton
p *nil_obj

# Symbol table
p symbol_table[0]     # First symbol
p symbol_table[1]     # Second symbol
p symbol_count

# Heap
p heap[0]             # First object
p heap[heap_ptr-1]    # Last allocated object

# Print multiple objects
p heap[0]@10          # First 10 heap objects
```

## Backtrace & Call Stack

```gdb
# Show call stack
backtrace (or bt)
bt 20                 # Show 20 frames

# Navigate frames
frame 5               # Jump to frame 5
up                    # Go up one frame
down                  # Go down one frame

# See locals in current frame
info locals
info args
```

## Conditional Breakpoints

```gdb
# Break when heap is nearly full
break make_cons if heap_ptr > 40000

# Break on specific symbol
break make_atom if strcmp(symbol, "LAMBDA") == 0

# Break when evaluating atoms only
break eval if expr->type == 1
```

## Watchpoints

```gdb
# Break when variable changes
watch heap_ptr
watch expr->data.pair.car

# Break when memory location is read
rwatch heap[100]

# Break when memory is written
awatch symbol_table[0]
```

## Display (auto-print on each step)

```gdb
# Show value after each step
display heap_ptr
display expr->type
display *expr

# Show list of displays
info display

# Delete display
delete display 1
```

## TUI Mode Commands

```gdb
# Enter/exit TUI
Ctrl-x a

# Switch layouts
layout src           # Source code
layout asm           # Assembly
layout split         # Both
layout regs          # Registers

# Navigate TUI
Ctrl-x 2             # Change active window
Ctrl-x o             # Next window
Ctrl-L               # Refresh screen
```

## Useful Debugging Scenarios

### Trace a Single Expression

```gdb
(gdb) break read_expr
(gdb) run
* (CAR '(A B C))
(gdb) next
(gdb) p *expr                    # See parsed expression
(gdb) break eval
(gdb) continue
(gdb) p *expr                    # Expression being evaluated
(gdb) p *env                     # Environment
```

### Follow CONS Creation

```gdb
(gdb) break cons
(gdb) run
* (CONS 'A 'B)
(gdb) bt                         # See who called cons
(gdb) p *car_val
(gdb) p *cdr_val
(gdb) finish                     # Return from cons
(gdb) p $retval                  # See return value
```

### Debug Infinite Recursion

```gdb
(gdb) break apply
(gdb) run < input.lisp
(gdb) commands                   # Run on each breakpoint hit
>silent
>printf "fn=%s\n", fn->type == 1 ? fn->data.symbol : "CONS"
>continue
>end
```

### Examine Environment Bindings

```gdb
(gdb) break eval
(gdb) run
* ((LAMBDA (X) X) 'FOO)
(gdb) continue                   # Hit eval
(gdb) p *env                     # Empty environment
(gdb) continue                   # Hit eval in lambda body
(gdb) p *env                     # Should have X binding
(gdb) p *env->data.pair.car      # First binding (X . FOO)
```

### Print Entire List

```gdb
# Define helper function
define print_list
  set $obj = $arg0
  printf "("
  while $obj->type == 2
    if $obj->data.pair.car->type == 1
      printf "%s ", $obj->data.pair.car->data.symbol
    end
    set $obj = $obj->data.pair.cdr
  end
  printf ")\n"
end

# Use it
(gdb) print_list expr
```

## Quick Reference

| Command | Short | Description |
|---------|-------|-------------|
| `run` | `r` | Start program |
| `break` | `b` | Set breakpoint |
| `continue` | `c` | Continue execution |
| `next` | `n` | Step over |
| `step` | `s` | Step into |
| `print` | `p` | Print expression |
| `backtrace` | `bt` | Show call stack |
| `quit` | `q` | Exit GDB |
| `help` | `h` | Get help |

## Tips

- Use `set print pretty on` for nicer struct formatting
- Use `set print array on` to see array contents
- Use `directory /path/to/source` if source isn't found
- Use `list` to see current source code
- Use `info breakpoints` to see all breakpoints
- Use `delete` or `clear` to remove breakpoints
