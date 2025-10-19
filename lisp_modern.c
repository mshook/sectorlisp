/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
│ vi: set et ft=c ts=2 sts=2 sw=2 fenc=utf-8                               :vi │
╞══════════════════════════════════════════════════════════════════════════════╡
│ Copyright 2020 Justine Alexandra Roberts Tunney                              │
│                                                                              │
│ Permission to use, copy, modify, and/or distribute this software for         │
│ any purpose with or without fee is hereby granted, provided that the         │
│ above copyright notice and this permission notice appear in all copies.      │
│                                                                              │
│ THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL                │
│ WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED                │
│ WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE             │
│ AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL         │
│ DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR        │
│ PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER               │
│ TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR             │
│ PERFORMANCE OF THIS SOFTWARE.                                                │
╚─────────────────────────────────────────────────────────────────────────────*/

// Modernized version of sectorlisp - same behavior, conventional C style

#include "bestline.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <locale.h>
#include <limits.h>

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Type Definitions and Constants                                           ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// LISP objects are represented as 32-bit integers:
// - Negative values represent cons cells (index into heap)
// - Non-negative values represent atoms (offset into symbol table)
typedef int32_t lisp_object_t;

// Predefined symbol offsets in the symbol table
#define SYMBOL_NIL     0
#define SYMBOL_T       4
#define SYMBOL_QUOTE   6
#define SYMBOL_COND    12
#define SYMBOL_READ    17
#define SYMBOL_PRINT   22
#define SYMBOL_ATOM    28
#define SYMBOL_CAR     33
#define SYMBOL_CDR     37
#define SYMBOL_CONS    41
#define SYMBOL_EQ      46

// Predefined symbols that get initialized into the symbol table
#define BUILTIN_SYMBOLS "NIL\0T\0QUOTE\0COND\0READ\0PRINT\0ATOM\0CAR\0CDR\0CONS\0EQ"

// Memory size: 32768 elements (0100000 octal in original)
#define MEMORY_SIZE 32768

// Check if a LISP object is a cons cell vs an atom
#define IS_CONS(obj) ((obj) < 0)
#define IS_ATOM(obj) ((obj) >= 0)

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Global State                                                              ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// The LISP machine memory is divided into two sections:
// - First half (indices 0 to MEMORY_SIZE/2-1): heap for cons cells
// - Second half (indices MEMORY_SIZE/2 to MEMORY_SIZE-1): symbol table
static int32_t memory[MEMORY_SIZE];

// Pointer to the symbol table (second half of memory)
static int32_t *symbol_table = memory + MEMORY_SIZE / 2;

// Heap allocation pointer (grows downward from middle, stores negative values)
static int32_t heap_ptr;

// Lookahead character for the lexer
static int lookahead_char;

// Input line state for readline interface
static char *input_line = NULL;
static char *input_pos = NULL;

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Function Prototypes                                                       ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Input/Output
static int get_char(void);
static void print_char(int ch);
static int get_token(void);

// Parsing
static lisp_object_t intern_symbol(void);
static lisp_object_t get_object(int ch);
static lisp_object_t get_list(void);
static lisp_object_t add_list(lisp_object_t obj);
static lisp_object_t read_expression(void);

// Printing
static void print_atom(lisp_object_t obj);
static void print_list(lisp_object_t obj);
static void print_object(lisp_object_t obj);
static void print_expression(lisp_object_t obj);
static void print_newline(void);

// LISP Primitives
static lisp_object_t car(lisp_object_t obj);
static lisp_object_t cdr(lisp_object_t obj);
static lisp_object_t cons(lisp_object_t car_val, lisp_object_t cdr_val);

// Evaluator
static lisp_object_t assoc(lisp_object_t key, lisp_object_t alist);
static lisp_object_t evlis(lisp_object_t forms, lisp_object_t env);
static lisp_object_t pairlis(lisp_object_t keys, lisp_object_t values, lisp_object_t env);
static lisp_object_t evcon(lisp_object_t clauses, lisp_object_t env);
static lisp_object_t apply(lisp_object_t fn, lisp_object_t args, lisp_object_t env);
static lisp_object_t eval(lisp_object_t expr, lisp_object_t env);
static lisp_object_t gc(lisp_object_t obj, int mark, int offset);

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Input/Output Functions                                                    ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Get next character from input, managing readline and lookahead
// Returns the previous lookahead character and updates lookahead
static int get_char(void) {
  int current_char, temp;

  // Get a new line if needed
  if (input_line == NULL || *input_pos == '\0') {
    if (input_line != NULL) {
      free(input_line);
      input_line = NULL;
      input_pos = NULL;
    }

    input_line = bestlineWithHistory("* ", "sectorlisp");
    if (input_line == NULL) {
      print_char('\n');
      exit(0);
    }
    input_pos = input_line;
  }

  // Read next character from current line
  if (*input_pos != '\0') {
    current_char = *input_pos++ & 255;
  } else {
    current_char = '\n';
  }

  // Swap: return old lookahead, store new char as lookahead
  temp = lookahead_char;
  lookahead_char = current_char;
  return temp;
}

// Output a single character
static void print_char(int ch) {
  fputwc(ch, stdout);
}

// Get next token from input stream
// Tokens are delimited by whitespace or parentheses
// Returns the delimiter character that ended the token
static int get_token(void) {
  int ch;
  int i = 0;

  // Skip whitespace and collect non-delimiter characters
  do {
    ch = get_char();
    if (ch > ' ') {
      memory[i++] = ch;
    }
  } while (ch <= ' ' || (ch > ')' && lookahead_char > ')'));

  memory[i] = 0; // Null-terminate the token
  return ch;
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Symbol Interning                                                          ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Intern a symbol: find existing symbol in table or add new one
// Symbols are stored as null-terminated strings in the symbol table
// Returns the offset into the symbol table where the symbol is stored
static lisp_object_t intern_symbol(void) {
  int i, j, x;

  // Search for existing symbol
  for (i = 0; (x = symbol_table[i++]) != 0; ) {
    // Compare current symbol table entry with token in memory
    for (j = 0; ; ++j) {
      if (x != memory[j]) break;
      if (!x) return i - j - 1; // Found match
      x = symbol_table[i++];
    }
    // Skip to end of this symbol
    while (x != 0) {
      x = symbol_table[i++];
    }
  }

  // Symbol not found, add it to the table
  j = 0;
  x = --i; // Start position for new symbol
  while ((symbol_table[i++] = memory[j++]) != 0) {
    // Copy symbol into table
  }
  return x;
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Parser                                                                    ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Parse a list element and add it to the current list being built
static lisp_object_t add_list(lisp_object_t obj) {
  return cons(obj, get_list());
}

// Parse a list (sequence of objects terminated by ')')
static lisp_object_t get_list(void) {
  int ch = get_token();
  if (ch == ')') {
    return 0; // NIL - empty list
  }
  return add_list(get_object(ch));
}

// Parse a LISP object (either an atom or a list)
// ch is the first character/delimiter of the object
static lisp_object_t get_object(int ch) {
  if (ch == '(') {
    return get_list();
  }
  return intern_symbol();
}

// Read a complete LISP expression from input
static lisp_object_t read_expression(void) {
  return get_object(get_token());
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Printer                                                                   ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Print an atom (symbol) by looking up its string in the symbol table
static void print_atom(lisp_object_t obj) {
  int ch;
  for (;;) {
    ch = symbol_table[obj++];
    if (ch == 0) break;
    print_char(ch);
  }
}

// Print a list, handling proper lists and dotted pairs
static void print_list(lisp_object_t obj) {
  print_char('(');
  print_object(car(obj));

  while ((obj = cdr(obj)) != 0) {
    if (IS_CONS(obj)) {
      // Proper list - continue printing elements
      print_char(' ');
      print_object(car(obj));
    } else {
      // Dotted pair - print the dot and final element
      print_char(L'∙');
      print_object(obj);
      break;
    }
  }

  print_char(')');
}

// Print a LISP object (dispatches to print_atom or print_list)
static void print_object(lisp_object_t obj) {
  if (IS_CONS(obj)) {
    print_list(obj);
  } else {
    print_atom(obj);
  }
}

// Public interface for printing an expression
static void print_expression(lisp_object_t obj) {
  print_object(obj);
}

// Print a newline
static void print_newline(void) {
  print_char('\n');
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ LISP Primitives                                                           ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Get the first element (car) of a cons cell
static lisp_object_t car(lisp_object_t obj) {
  return symbol_table[obj];
}

// Get the second element (cdr) of a cons cell
static lisp_object_t cdr(lisp_object_t obj) {
  return symbol_table[obj + 1];
}

// Construct a new cons cell with given car and cdr
// Allocates from the heap (growing downward from middle of memory)
static lisp_object_t cons(lisp_object_t car_val, lisp_object_t cdr_val) {
  symbol_table[--heap_ptr] = cdr_val;
  symbol_table[--heap_ptr] = car_val;
  return heap_ptr;
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Garbage Collection                                                        ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Copy cons cells recursively, adjusting pointers
// Used for compacting the heap after evaluation
static lisp_object_t gc(lisp_object_t obj, int mark, int offset) {
  if (obj < mark) {
    return cons(gc(car(obj), mark, offset),
                gc(cdr(obj), mark, offset)) + offset;
  } else {
    return obj;
  }
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Evaluator Helper Functions                                               ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Evaluate a list of expressions, returning list of results
static lisp_object_t evlis(lisp_object_t forms, lisp_object_t env) {
  if (forms != 0) {
    lisp_object_t result = eval(car(forms), env);
    return cons(result, evlis(cdr(forms), env));
  } else {
    return 0;
  }
}

// Create association list by pairing keys with values
static lisp_object_t pairlis(lisp_object_t keys, lisp_object_t values,
                             lisp_object_t env) {
  if (keys != 0) {
    return cons(cons(car(keys), car(values)),
                pairlis(cdr(keys), cdr(values), env));
  } else {
    return env;
  }
}

// Look up a key in an association list
static lisp_object_t assoc(lisp_object_t key, lisp_object_t alist) {
  if (alist == 0) {
    return 0;
  }
  if (key == car(car(alist))) {
    return cdr(car(alist));
  }
  return assoc(key, cdr(alist));
}

// Evaluate conditional clauses until one is true
static lisp_object_t evcon(lisp_object_t clauses, lisp_object_t env) {
  if (eval(car(car(clauses)), env) != 0) {
    return eval(car(cdr(car(clauses))), env);
  } else {
    return evcon(cdr(clauses), env);
  }
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Function Application                                                      ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Apply a function to arguments
static lisp_object_t apply(lisp_object_t fn, lisp_object_t args,
                           lisp_object_t env) {
  // Lambda function: (LAMBDA params body)
  if (IS_CONS(fn)) {
    lisp_object_t params = car(cdr(fn));
    lisp_object_t body = car(cdr(cdr(fn)));
    lisp_object_t new_env = pairlis(params, args, env);
    return eval(body, new_env);
  }

  // Symbol that needs to be evaluated to get actual function
  if (fn > SYMBOL_EQ) {
    return apply(eval(fn, env), args, env);
  }

  // Built-in functions
  if (fn == SYMBOL_EQ) {
    return (car(args) == car(cdr(args))) ? SYMBOL_T : 0;
  }
  if (fn == SYMBOL_CONS) {
    return cons(car(args), car(cdr(args)));
  }
  if (fn == SYMBOL_ATOM) {
    return IS_CONS(car(args)) ? 0 : SYMBOL_T;
  }
  if (fn == SYMBOL_CAR) {
    return car(car(args));
  }
  if (fn == SYMBOL_CDR) {
    return cdr(car(args));
  }
  if (fn == SYMBOL_READ) {
    return read_expression();
  }
  if (fn == SYMBOL_PRINT) {
    if (args != 0) {
      print_expression(car(args));
    } else {
      print_newline();
    }
    return 0;
  }

  return 0; // Should not reach here
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Evaluator                                                                 ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

// Evaluate a LISP expression in an environment
static lisp_object_t eval(lisp_object_t expr, lisp_object_t env) {
  int saved_heap_ptr, new_heap_ptr, final_heap_ptr;

  // Atoms are variables - look them up in environment
  if (IS_ATOM(expr)) {
    return assoc(expr, env);
  }

  // (QUOTE x) returns x unevaluated
  if (car(expr) == SYMBOL_QUOTE) {
    return car(cdr(expr));
  }

  // Save heap state for garbage collection
  saved_heap_ptr = heap_ptr;

  // (COND ...) evaluates conditional clauses
  if (car(expr) == SYMBOL_COND) {
    expr = evcon(cdr(expr), env);
  } else {
    // Function application: evaluate function and arguments, then apply
    expr = apply(car(expr), evlis(cdr(expr), env), env);
  }

  // Garbage collection: compact the heap
  new_heap_ptr = heap_ptr;
  expr = gc(expr, saved_heap_ptr, saved_heap_ptr - new_heap_ptr);

  // Move compacted data to final location
  final_heap_ptr = heap_ptr;
  while (final_heap_ptr < new_heap_ptr) {
    symbol_table[--saved_heap_ptr] = symbol_table[--new_heap_ptr];
  }
  heap_ptr = saved_heap_ptr;

  return expr;
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Main Program                                                              ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

int main(void) {
  size_t i;

  // Initialize locale for Unicode support
  setlocale(LC_ALL, "");

  // Configure bestline (readline library)
  bestlineSetXlatCallback(bestlineUppercase);

  // Initialize symbol table with built-in symbols
  for (i = 0; i < sizeof(BUILTIN_SYMBOLS); ++i) {
    symbol_table[i] = BUILTIN_SYMBOLS[i];
  }

  // REPL: Read-Eval-Print Loop
  for (;;) {
    heap_ptr = 0;
    print_expression(eval(read_expression(), 0));
    print_newline();
  }

  return 0;
}
