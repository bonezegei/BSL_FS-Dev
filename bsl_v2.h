#ifndef BSL_H
#define BSL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum { 
    VAL_NULL, 
    VAL_NUM, 
    VAL_STR, 
    VAL_FUNC, 
    VAL_ARR, 
    VAL_OBJ, 
    VAL_BLOB 
} ValueType;

typedef struct Node Node;
typedef struct Function Function;
typedef struct Env Env;
typedef struct Array Array;
typedef struct Blob Blob;

struct Blob {
    unsigned char *data;
    size_t size;
};

typedef struct Value {
    ValueType type;
    double num;
    char *str;
    Function *func;
    Array *arr;
    Env *obj_props;
    Blob *blob;
} Value;

struct Array { 
    Value *elements; 
    int length; 
};

struct Env { 
    char name[64]; 
    Value val; 
    Env *next; 
    Env *parent; 
};

typedef Value (*NativeFn)(int argc, Value* argv, Env** envp);

struct Function {
    char name[64];
    char params[8][64];
    int param_count;
    Node *body;
    Env *closure;
    Node *classdef;
    int is_native;
    NativeFn native_ptr;
};

/* Constructors aligned with 7-field Value layout */
static inline Value make_null(){ Value v={VAL_NULL,0,NULL,NULL,NULL,NULL,NULL}; return v; }
static inline Value make_num(double x){ Value v={VAL_NUM,x,NULL,NULL,NULL,NULL,NULL}; return v; }
static inline Value make_str(const char *s){ Value v={VAL_STR,0,(char*)s,NULL,NULL,NULL,NULL}; return v; }
static inline Value make_func(Function *f){ Value v={VAL_FUNC,0,NULL,f,NULL,NULL,NULL}; return v; }
static inline Value make_arr(Array *a){ Value v={VAL_ARR,0,NULL,NULL,a,NULL,NULL}; return v; }
static inline Value make_obj(){ Value v={VAL_OBJ,0,NULL,NULL,NULL,NULL,NULL}; return v; }
static inline Value make_blob(Blob *b){ Value v={VAL_BLOB,0,NULL,NULL,NULL,NULL,b}; return v; }

#if defined(_WIN32)
    #define EXPORT_API __declspec(dllexport)
#else
    #define EXPORT_API __attribute__((visibility("default")))
#endif

typedef Value (*CallInterpFn)(Function*, int, Value*, Env**);
static CallInterpFn g_call_interp = NULL;

EXPORT_API void bzg_native_init(void* fn) {
    g_call_interp = (CallInterpFn)fn;
}

#endif