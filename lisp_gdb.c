/*-*- mode:c;indent-tabs-mode:nil;c-basic-offset:2;tab-width:8;coding:utf-8 -*-│
  vi: set et ft=c ts=2 sts=2 sw=2 fenc=utf-8                               :vi │
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
#define _XOPEN_SOURCE 700
#include "bestline.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <limits.h>
#include <stdbool.h>
#include <wchar.h>

/*───────────────────────────────────────────────────────────────────────────│─╗
│ GDB-Friendly LISP Machine with Explicit Data Structures                  ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

/* LISP object types */
typedef enum {
  TYPE_NIL,   /* The empty list */
  TYPE_ATOM,  /* Symbol (interned string) */
  TYPE_CONS   /* Pair (car, cdr) */
} object_type_t;

/* Forward declaration */
struct lisp_object;

/* LISP object representation */
typedef struct lisp_object {
  object_type_t type;    /* Type tag for GDB visibility */
  bool marked;           /* GC mark bit */
  union {
    char *symbol;        /* For TYPE_ATOM: pointer to interned string */
    struct {
      struct lisp_object *car;
      struct lisp_object *cdr;
    } pair;              /* For TYPE_CONS: car and cdr pointers */
  } data;
} lisp_object_t;

/* Constants for builtin symbols (as string offsets) */
#define BUILTIN_NIL   0
#define BUILTIN_T     4
#define BUILTIN_QUOTE 6
#define BUILTIN_COND  12
#define BUILTIN_READ  17
#define BUILTIN_PRINT 22
#define BUILTIN_ATOM  28
#define BUILTIN_CAR   33
#define BUILTIN_CDR   37
#define BUILTIN_CONS  41
#define BUILTIN_EQ    46

/* Initial builtin symbols string */
#define BUILTIN_SYMBOLS "NIL\0T\0QUOTE\0COND\0READ\0PRINT\0ATOM\0CAR\0CDR\0CONS\0EQ"

/* Memory configuration */
#define HEAP_SIZE 50000
#define SYMBOL_TABLE_SIZE 10000

/* Global state */
static lisp_object_t heap[HEAP_SIZE];           /* Object heap */
static int heap_ptr = 0;                        /* Next free slot in heap */
static char *symbol_table[SYMBOL_TABLE_SIZE];   /* Interned strings */
static int symbol_count = 0;                    /* Number of interned symbols */
static char symbol_buffer[256];                 /* Buffer for reading symbols */
static int lookahead_char = 0;                  /* Lookahead character for parser */

/* Pointers to builtin symbol objects */
static lisp_object_t *nil_obj;
static lisp_object_t *t_obj;
static lisp_object_t *quote_obj;
static lisp_object_t *cond_obj;
static lisp_object_t *read_obj;
static lisp_object_t *print_obj;
static lisp_object_t *atom_obj;
static lisp_object_t *car_obj;
static lisp_object_t *cdr_obj;
static lisp_object_t *cons_obj;
static lisp_object_t *eq_obj;

/*───────────────────────────────────────────────────────────────────────────│─╗
│ String Interning                                                          ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

/* Intern a string: return existing pointer if already interned, else add it */
static char *intern_string(const char *str) {
  /* Search for existing string */
  for (int i = 0; i < symbol_count; i++) {
    if (strcmp(symbol_table[i], str) == 0) {
      return symbol_table[i];
    }
  }

  /* Not found, add new string */
  if (symbol_count >= SYMBOL_TABLE_SIZE) {
    fprintf(stderr, "Symbol table overflow\n");
    exit(1);
  }

  char *new_str = strdup(str);
  if (!new_str) {
    fprintf(stderr, "Out of memory\n");
    exit(1);
  }

  symbol_table[symbol_count] = new_str;
  return symbol_table[symbol_count++];
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Object Construction                                                       ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

/* Create atom from interned string pointer */
static lisp_object_t *make_atom(char *symbol) {
  if (heap_ptr >= HEAP_SIZE) {
    fprintf(stderr, "Heap overflow at %d objects\n", heap_ptr);
    exit(1);
  }

  lisp_object_t *obj = &heap[heap_ptr++];
  obj->type = TYPE_ATOM;
  obj->marked = false;
  obj->data.symbol = symbol;
  return obj;
}

/* Create cons cell */
static lisp_object_t *make_cons(lisp_object_t *car, lisp_object_t *cdr) {
  if (heap_ptr >= HEAP_SIZE) {
    fprintf(stderr, "Heap overflow at %d objects\n", heap_ptr);
    exit(1);
  }

  lisp_object_t *obj = &heap[heap_ptr++];
  obj->type = TYPE_CONS;
  obj->marked = false;
  obj->data.pair.car = car;
  obj->data.pair.cdr = cdr;
  return obj;
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Primitives                                                                ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

static lisp_object_t *car(lisp_object_t *obj) {
  /* In LISP, CAR of NIL is NIL */
  if (obj->type == TYPE_NIL) {
    return nil_obj;
  }
  if (obj->type != TYPE_CONS) {
    fprintf(stderr, "CAR of non-cons\n");
    exit(1);
  }
  return obj->data.pair.car;
}

static lisp_object_t *cdr(lisp_object_t *obj) {
  /* In LISP, CDR of NIL is NIL */
  if (obj->type == TYPE_NIL) {
    return nil_obj;
  }
  if (obj->type != TYPE_CONS) {
    fprintf(stderr, "CDR of non-cons\n");
    exit(1);
  }
  return obj->data.pair.cdr;
}

static lisp_object_t *cons(lisp_object_t *car_val, lisp_object_t *cdr_val) {
  return make_cons(car_val, cdr_val);
}

static bool is_atom(lisp_object_t *obj) {
  return obj->type != TYPE_CONS;
}

static bool eq(lisp_object_t *a, lisp_object_t *b) {
  /* For atoms, compare by pointer equality (since strings are interned) */
  if (a->type == TYPE_ATOM && b->type == TYPE_ATOM) {
    return a->data.symbol == b->data.symbol;
  }
  /* NIL == NIL by pointer equality too */
  return a == b;
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Mark-and-Sweep Garbage Collection                                        ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

/* Mark phase: recursively mark reachable objects */
static void mark_object(lisp_object_t *obj) {
  if (!obj || obj->marked) return;

  obj->marked = true;

  if (obj->type == TYPE_CONS) {
    mark_object(obj->data.pair.car);
    mark_object(obj->data.pair.cdr);
  }
}

/* Sweep phase: compact heap by moving live objects */
static lisp_object_t *sweep_and_relocate(lisp_object_t *obj, lisp_object_t *old_heap,
                                         lisp_object_t *new_heap) {
  if (!obj) return NULL;

  /* If this object is in the old heap and marked, find its new location */
  if (obj >= old_heap && obj < old_heap + HEAP_SIZE && obj->marked) {
    /* Calculate new address: scan old heap to find this object's new position */
    int new_index = 0;
    for (lisp_object_t *p = old_heap; p < obj; p++) {
      if (p->marked) new_index++;
    }
    return &new_heap[new_index];
  }

  return obj;
}

/* Copy marked objects to new location, updating all pointers */
static void compact_heap(void) {
  lisp_object_t temp_heap[HEAP_SIZE];
  int new_ptr = 0;

  /* Copy marked objects to temp heap */
  for (int i = 0; i < heap_ptr; i++) {
    if (heap[i].marked) {
      temp_heap[new_ptr] = heap[i];
      temp_heap[new_ptr].marked = false;  /* Unmark for next GC */
      new_ptr++;
    }
  }

  /* Update pointers in temp heap */
  for (int i = 0; i < new_ptr; i++) {
    if (temp_heap[i].type == TYPE_CONS) {
      temp_heap[i].data.pair.car =
        sweep_and_relocate(temp_heap[i].data.pair.car, heap, temp_heap);
      temp_heap[i].data.pair.cdr =
        sweep_and_relocate(temp_heap[i].data.pair.cdr, heap, temp_heap);
    }
  }

  /* Copy back to main heap */
  memcpy(heap, temp_heap, new_ptr * sizeof(lisp_object_t));

  /* Update builtin pointers */
  for (int i = 0; i < new_ptr; i++) {
    if (heap[i].type == TYPE_ATOM) {
      if (strcmp(heap[i].data.symbol, "NIL") == 0) nil_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "T") == 0) t_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "QUOTE") == 0) quote_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "COND") == 0) cond_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "READ") == 0) read_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "PRINT") == 0) print_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "ATOM") == 0) atom_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "CAR") == 0) car_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "CDR") == 0) cdr_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "CONS") == 0) cons_obj = &heap[i];
      else if (strcmp(heap[i].data.symbol, "EQ") == 0) eq_obj = &heap[i];
    }
  }

  heap_ptr = new_ptr;
}

/* Run garbage collection, marking roots and sweeping */
static void gc(lisp_object_t *root) {
  /* Mark builtin symbols */
  mark_object(nil_obj);
  mark_object(t_obj);
  mark_object(quote_obj);
  mark_object(cond_obj);
  mark_object(read_obj);
  mark_object(print_obj);
  mark_object(atom_obj);
  mark_object(car_obj);
  mark_object(cdr_obj);
  mark_object(cons_obj);
  mark_object(eq_obj);

  /* Mark root */
  mark_object(root);

  /* Sweep and compact */
  compact_heap();
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ I/O and Parsing                                                           ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

static void print_char(int c) {
  fputwc(c, stdout);
}

static int get_char(void) {
  int c, temp;
  static char *line = NULL;
  static char *ptr = NULL;

  /* Get line if needed */
  if (line || (line = ptr = bestlineWithHistory("* ", "sectorlisp_gdb"))) {
    if (*ptr) {
      c = *ptr++ & 255;
    } else {
      free(line);
      line = ptr = NULL;
      c = '\n';
    }
    temp = lookahead_char;
    lookahead_char = c;
    return temp;
  } else {
    print_char('\n');
    exit(0);
  }
}

/* Get next token into symbol_buffer, return delimiter character */
static int get_token(void) {
  int c;
  int i = 0;

  /* Skip whitespace and collect non-whitespace */
  do {
    c = get_char();
    if (c > ' ') {
      symbol_buffer[i++] = c;
    }
  } while (c <= ' ' || (c > ')' && lookahead_char > ')'));

  symbol_buffer[i] = '\0';
  return c;
}

/* Forward declarations */
static lisp_object_t *get_object(int c);
static lisp_object_t *get_list(void);

static lisp_object_t *add_list(lisp_object_t *obj) {
  return cons(obj, get_list());
}

static lisp_object_t *get_list(void) {
  int c = get_token();
  if (c == ')') return nil_obj;
  return add_list(get_object(c));
}

static lisp_object_t *get_object(int c) {
  if (c == '(') return get_list();
  /* Intern the symbol */
  char *sym = intern_string(symbol_buffer);
  /* Special case: NIL returns the singleton nil object */
  if (strcmp(sym, "NIL") == 0) {
    return nil_obj;
  }
  /* Create atom for other symbols */
  return make_atom(sym);
}

static lisp_object_t *read_expr(void) {
  return get_object(get_token());
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Printing                                                                  ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

static void print_object(lisp_object_t *obj);

static void print_atom(lisp_object_t *obj) {
  if (obj->type != TYPE_ATOM) return;
  /* Print character by character to match fputwc behavior */
  for (char *s = obj->data.symbol; *s; s++) {
    print_char(*s);
  }
}

static void print_list(lisp_object_t *obj) {
  print_char('(');
  print_object(car(obj));

  obj = cdr(obj);
  while (obj->type != TYPE_NIL) {
    if (obj->type == TYPE_CONS) {
      print_char(' ');
      print_object(car(obj));
      obj = cdr(obj);
    } else {
      /* Improper list */
      print_char(L'∙');
      print_object(obj);
      break;
    }
  }
  print_char(')');
}

static void print_object(lisp_object_t *obj) {
  if (obj->type == TYPE_NIL) {
    print_char('N');
    print_char('I');
    print_char('L');
  } else if (obj->type == TYPE_ATOM) {
    print_atom(obj);
  } else {
    print_list(obj);
  }
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Evaluator                                                                 ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

/* Forward declarations */
static lisp_object_t *eval(lisp_object_t *expr, lisp_object_t *env);
static lisp_object_t *apply(lisp_object_t *fn, lisp_object_t *args, lisp_object_t *env);

/* Assoc: look up key in association list */
static lisp_object_t *assoc(lisp_object_t *key, lisp_object_t *alist) {
  while (alist->type != TYPE_NIL) {
    lisp_object_t *pair = car(alist);
    if (eq(key, car(pair))) {
      return cdr(pair);
    }
    alist = cdr(alist);
  }
  return nil_obj;
}

/* Pairlis: create association list from two lists */
static lisp_object_t *pairlis(lisp_object_t *keys, lisp_object_t *values, lisp_object_t *env) {
  if (keys->type == TYPE_NIL) return env;
  return cons(cons(car(keys), car(values)),
              pairlis(cdr(keys), cdr(values), env));
}

/* Evlis: evaluate list of expressions */
static lisp_object_t *evlis(lisp_object_t *exprs, lisp_object_t *env) {
  if (exprs->type == TYPE_NIL) return nil_obj;
  return cons(eval(car(exprs), env),
              evlis(cdr(exprs), env));
}

/* Evcon: evaluate COND clauses */
static lisp_object_t *evcon(lisp_object_t *clauses, lisp_object_t *env) {
  lisp_object_t *clause = car(clauses);
  lisp_object_t *test = eval(car(clause), env);

  if (test->type != TYPE_NIL) {
    /* Test succeeded, evaluate consequent */
    return eval(car(cdr(clause)), env);
  } else {
    /* Test failed, try next clause */
    return evcon(cdr(clauses), env);
  }
}

/* Apply: apply function to arguments */
static lisp_object_t *apply(lisp_object_t *fn, lisp_object_t *args, lisp_object_t *env) {
  /* NIL cannot be applied */
  if (fn->type == TYPE_NIL) {
    fprintf(stderr, "Cannot apply NIL\n");
    return nil_obj;
  }

  /* Lambda: (LAMBDA params body) */
  if (fn->type == TYPE_CONS && car(fn)->type == TYPE_ATOM &&
      strcmp(car(fn)->data.symbol, "LAMBDA") == 0) {
    lisp_object_t *params = car(cdr(fn));
    lisp_object_t *body = car(cdr(cdr(fn)));
    lisp_object_t *new_env = pairlis(params, args, env);
    return eval(body, new_env);
  }

  /* Atom: check for builtins, else evaluate and recurse */
  if (fn->type == TYPE_ATOM) {
    if (eq(fn, eq_obj)) {
      return eq(car(args), car(cdr(args))) ? t_obj : nil_obj;
    }
    if (eq(fn, cons_obj)) {
      return cons(car(args), car(cdr(args)));
    }
    if (eq(fn, atom_obj)) {
      return is_atom(car(args)) ? t_obj : nil_obj;
    }
    if (eq(fn, car_obj)) {
      return car(car(args));
    }
    if (eq(fn, cdr_obj)) {
      return cdr(car(args));
    }
    if (eq(fn, read_obj)) {
      return read_expr();
    }
    if (eq(fn, print_obj)) {
      if (args->type != TYPE_NIL) {
        print_object(car(args));
      } else {
        print_char('\n');
      }
      return nil_obj;
    }

    /* Unknown atom, try evaluating */
    return apply(eval(fn, env), args, env);
  }

  /* Non-lambda cons: evaluate and try again */
  return apply(eval(fn, env), args, env);
}

/* Eval: evaluate expression in environment */
static lisp_object_t *eval(lisp_object_t *expr, lisp_object_t *env) {
  /* Atom: look up in environment */
  if (expr->type == TYPE_ATOM) {
    return assoc(expr, env);
  }

  /* NIL evaluates to itself */
  if (expr->type == TYPE_NIL) {
    return nil_obj;
  }

  /* List: special forms and function application */
  lisp_object_t *head = car(expr);

  /* QUOTE */
  if (eq(head, quote_obj)) {
    return car(cdr(expr));
  }

  /* COND */
  if (eq(head, cond_obj)) {
    return evcon(cdr(expr), env);
  }

  /* Function application */
  lisp_object_t *fn = head;
  lisp_object_t *args = evlis(cdr(expr), env);
  return apply(fn, args, env);
}

/*───────────────────────────────────────────────────────────────────────────│─╗
│ Initialization and REPL                                                   ─╬─│┼
╚────────────────────────────────────────────────────────────────────────────│*/

static void init_builtins(void) {
  /* Create special NIL object first */
  if (heap_ptr >= HEAP_SIZE) {
    fprintf(stderr, "Heap overflow\n");
    exit(1);
  }
  nil_obj = &heap[heap_ptr++];
  nil_obj->type = TYPE_NIL;
  nil_obj->marked = false;

  /* Intern NIL symbol for string comparison */
  intern_string("NIL");

  /* Parse remaining builtin symbols string */
  const char *symbols = BUILTIN_SYMBOLS;
  const char *ptr = symbols + 4;  /* Skip "NIL\0" */

  while (*ptr) {
    int len = strlen(ptr);
    char *interned = intern_string(ptr);

    /* Create atom for this builtin */
    lisp_object_t *obj = make_atom(interned);

    /* Save pointer to builtin objects */
    if (strcmp(interned, "T") == 0) t_obj = obj;
    else if (strcmp(interned, "QUOTE") == 0) quote_obj = obj;
    else if (strcmp(interned, "COND") == 0) cond_obj = obj;
    else if (strcmp(interned, "READ") == 0) read_obj = obj;
    else if (strcmp(interned, "PRINT") == 0) print_obj = obj;
    else if (strcmp(interned, "ATOM") == 0) atom_obj = obj;
    else if (strcmp(interned, "CAR") == 0) car_obj = obj;
    else if (strcmp(interned, "CDR") == 0) cdr_obj = obj;
    else if (strcmp(interned, "CONS") == 0) cons_obj = obj;
    else if (strcmp(interned, "EQ") == 0) eq_obj = obj;

    ptr += len + 1;
  }
}

int main(void) {
  setlocale(LC_ALL, "");
  bestlineSetXlatCallback(bestlineUppercase);

  /* Initialize builtin symbols */
  init_builtins();

  /* REPL */
  for (;;) {
    lisp_object_t *expr = read_expr();
    lisp_object_t *result = eval(expr, nil_obj);
    print_object(result);
    print_char('\n');
    fflush(stdout);

    /* GC disabled for now - heap is large enough */
    /* if (heap_ptr > HEAP_SIZE * 0.8) {
      gc(result);
    } */
  }

  return 0;
}
