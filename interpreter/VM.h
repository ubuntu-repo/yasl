#pragma once

#include "hashtable/hashtable.h"
#include "yasl_conf.h"
#include "opcode.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define STACK_SIZE 100024
#define NUM_TYPES 13                                     // number of builtin types, each needs a vtable

#define vm_pushend(vm) vm_push(vm, YASL_END())
#define vm_pushundef(vm) vm_push(vm, YASL_UNDEF())
#define vm_pushfloat(vm, f) vm_push(vm, YASL_FLOAT(f))
#define vm_pushint(vm, i) vm_push(vm, YASL_INT(i))
#define vm_pushbool(vm, b) vm_push(vm, YASL_BOOL(b))
#define vm_pushstr(vm, s) vm_push(vm, YASL_STR(s))
#define vm_pushlist(vm, l) vm_push(vm, YASL_LIST(l))
#define vm_pushfn(vm, f) vm_push(vm, YASL_FN(f))

#define vm_popint(vm) (YASL_GETINT(vm_pop(vm)))
#define vm_popstr(vm) (YASL_GETSTR(vm_pop(vm)))
#define vm_poplist(vm) (YASL_GETLIST(vm_pop(vm)))

#define VM_PEEK(vm, offset) ((vm)->stack[offset])
#define vm_peek(vm) ((vm)->stack[(vm)->sp])

#define vm_peekint(vm, offset) (YASL_GETINT(VM_PEEK(vm, offset)))
#define vm_peekstr(vm, offset) (YASL_GETSTR(VM_PEEK(vm, offset)))
#define vm_peeklist(vm, offset) (YASL_GETLIST(VM_PEEK(vm, offset)))
#define vm_peektable(vm, offset) (YASL_GETTABLE(VM_PEEK(vm, offset)))
#define vm_peekcfn(vm, offset) (YASL_GETCFN(VM_PEEK(vm, offset)))

#define BUFFER_SIZE 256
#define NCODE(vm)    ((vm)->code[(vm)->pc++])     // get next bytecode

#define GT(a, b) ((a) > (b))
#define GE(a, b) ((a) >= (b))
#define COMP(vm, a, b, f, str)  do {\
                            if (a.type == Y_INT && b.type == Y_INT) {\
                                c = f(a.value.ival, b.value.ival);\
                            }\
                            else if (a.type == Y_FLOAT && b.type == Y_INT) {\
                                c = f(a.value.dval, (yasl_float)b.value.ival);\
                            }\
                            else if (a.type == Y_INT && b.type == Y_FLOAT) {\
                                c = f((yasl_float)a.value.ival, (b).value.dval);\
                            }\
                            else if (a.type == Y_FLOAT && b.type == Y_FLOAT) {\
                                c = f(a.value.dval, (b).value.dval);\
                            }\
                            else {\
                                printf("TypeError: %s not supported for operands of types %s and %s.\n", str,\
                                        YASL_TYPE_NAMES[a.type], YASL_TYPE_NAMES[b.type]);\
                                return YASL_TYPE_ERROR;\
                            }\
                            vm_pushbool(vm, c);} while(0);

struct VM {
	struct YASL_Object *globals;          // variables, see "constant.c" for details on YASL_Object.
	size_t num_globals;
	struct YASL_Object *stack;            // stack
	unsigned char *code;           // bytecode
	size_t pc;                     // program counter
	int sp;                        // stack pointer
	int fp;                        // frame pointer
	int next_fp;
	int lp;                        // foreach pointer
	String_t *special_strings[NUM_SPECIAL_STRINGS];
	struct Table **builtins_htable;   // htable of builtin methods
};

void vm_init(struct VM *vm, unsigned char *code, int pc0, size_t datasize);

void vm_cleanup(struct VM *vm);

int vm_stringify_top(struct VM *vm);

struct YASL_Object vm_pop(struct VM *vm);
void vm_push(struct VM *vm, struct YASL_Object val);

int vm_run(struct VM *vm);

struct Table *undef_builtins(struct VM *vm);
struct Table *float_builtins(struct VM *vm);
struct Table *int_builtins(struct VM *vm);
struct Table *bool_builtins(struct VM *vm);
struct Table *str_builtins(struct VM *vm);
struct Table *list_builtins(struct VM *vm);
struct Table *table_builtins(struct VM *vm);
