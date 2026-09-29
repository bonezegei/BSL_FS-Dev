#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsl.h"

// Platform-independent DLL/SO symbol exporting helper
#if defined(_WIN32) || defined(_WIN64)
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Object Property Helpers ---

// Helper to add a native function to a language object
void add_native_method(Value* obj, const char* name, Value (*native_ptr)(int, Value*, Env**), int param_count) {
    Env* prop = (Env*)malloc(sizeof(Env));
    if (prop) {
        memset(prop, 0, sizeof(Env));
        strncpy(prop->name, name, 63);
        prop->name[63] = '\0';
        
        Function* f = (Function*)malloc(sizeof(Function));
        if (f) {
            memset(f, 0, sizeof(Function));
            f->is_native = 1;
            f->native_ptr = native_ptr;
            strncpy(f->name, name, 63);
            f->name[63] = '\0';
            f->param_count = param_count;
            
            prop->val = make_func(f);
            prop->next = obj->obj_props;
            obj->obj_props = prop;
        }
    }
}

// Helper to store internal state properties inside the object
void add_property(Value* obj, const char* name, Value val) {
    Env* prop = (Env*)malloc(sizeof(Env));
    if (prop) {
        memset(prop, 0, sizeof(Env));
        strncpy(prop->name, name, 63);
        prop->name[63] = '\0';
        prop->val = val;
        prop->next = obj->obj_props;
        obj->obj_props = prop;
    }
}

// Helper to fetch hidden properties 
Value get_property(Value* obj, const char* name) {
    if (obj->type != VAL_OBJ) return make_null();
    Env* curr = obj->obj_props;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr->val;
        curr = curr->next;
    }
    return make_null();
}

// --- File Object Methods ---

Value bsl_file_type(int argc, Value* argv, Env** envp) {
    if (argc < 1) return make_null();
    return get_property(&argv[0], "__type");
}

Value bsl_file_data(int argc, Value* argv, Env** envp) {
    if (argc < 1) return make_null();
    return get_property(&argv[0], "__data");
}

Value bsl_file_size(int argc, Value* argv, Env** envp) {
    if (argc < 1) return make_null();
    return get_property(&argv[0], "__size");
}

Value bsl_file_raw(int argc, Value* argv, Env** envp) {
    if (argc < 1) return make_null();
    return get_property(&argv[0], "__raw");
}

// --- FS Module Methods ---

Value bsl_fs_openFile(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_null();
    
    const char* filename = argv[1].str;
    FILE* f = fopen(filename, "rb");
    if (!f) return make_null();
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = (char*)malloc(fsize + 1);
    if (!buffer) {
        fclose(f);
        return make_null();
    }
    
    size_t read_size = fread(buffer, 1, fsize, f);
    fclose(f);
    buffer[read_size] = '\0';
    
    // Simple heuristic to determine type: 0 = binary, 1 = text
    int is_text = 1;
    for (size_t i = 0; i < read_size; i++) {
        if (buffer[i] == '\0') {
            is_text = 0;
            break;
        }
    }
    
    // Instantiate File object
    Value file_obj = make_obj();
    
    // Construct BSL raw byte array (VAL_ARR)
    Array* raw_arr = (Array*)malloc(sizeof(Array));
    if (raw_arr) {
        raw_arr->length = (int)read_size;
        raw_arr->elements = (read_size > 0) ? (Value*)malloc(sizeof(Value) * read_size) : NULL;
        if (raw_arr->elements || read_size == 0) {
            for (size_t i = 0; i < read_size; i++) {
                raw_arr->elements[i] = make_num((unsigned char)buffer[i]);
            }
        }
        add_property(&file_obj, "__raw", make_arr(raw_arr));
    } else {
        add_property(&file_obj, "__raw", make_null());
    }

    // Store hidden state properties 
    add_property(&file_obj, "__data", make_str(buffer));
    add_property(&file_obj, "__type", make_num(is_text ? 1.0 : 0.0));
    add_property(&file_obj, "__size", make_num((double)fsize));
    
    // Attach instance methods
    add_native_method(&file_obj, "type", bsl_file_type, 0);
    add_native_method(&file_obj, "data", bsl_file_data, 0);
    add_native_method(&file_obj, "size", bsl_file_size, 0);
    add_native_method(&file_obj, "raw",  bsl_file_raw, 0);
    
    return file_obj;
}

// --- Factory Constructor ---

EXPORT Value fs(int argc, Value* argv, Env** envp) {
    Value obj = make_obj();
    
    // Attach native methods to the constructed instance
    add_native_method(&obj, "openFile", bsl_fs_openFile, 1);
    
    return obj;
}

#ifdef __cplusplus
}
#endif