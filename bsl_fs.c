#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsl.h"

// Platform-independent DLL/SO symbol exporting helper[cite: 2]
#if defined(_WIN32) || defined(_WIN64)
    #define EXPORT __declspec(dllexport)
#else
    #define EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Object Property Helpers ---

// Helper to add a native function to a language object[cite: 2]
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
            
            prop->val = make_func(f); //[cite: 1]
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
    if (obj->type != VAL_OBJ) return make_null(); //[cite: 1]
    Env* curr = obj->obj_props;
    while (curr) {
        if (strcmp(curr->name, name) == 0) return curr->val;
        curr = curr->next;
    }
    return make_null(); //[cite: 1]
}

// --- File Object Methods ---

Value bsl_file_type(int argc, Value* argv, Env** envp) {
    if (argc < 1) return make_null(); //[cite: 1]
    return get_property(&argv[0], "__type");
}

Value bsl_file_data(int argc, Value* argv, Env** envp) {
    if (argc < 1) return make_null(); //[cite: 1]
    return get_property(&argv[0], "__data");
}

Value bsl_file_size(int argc, Value* argv, Env** envp) {
    if (argc < 1) return make_null(); //[cite: 1]
    return get_property(&argv[0], "__size");
}

// --- FS Module Methods ---

Value bsl_fs_openFile(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_null(); //[cite: 1]
    
    const char* filename = argv[1].str;
    FILE* f = fopen(filename, "rb");
    if (!f) return make_null(); //[cite: 1]
    
    // Read entire file
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* buffer = (char*)malloc(fsize + 1);
    if (!buffer) {
        fclose(f);
        return make_null(); //[cite: 1]
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
    Value file_obj = make_obj(); //[cite: 1]
    
    // Store hidden state properties 
    add_property(&file_obj, "__data", make_str(buffer)); //[cite: 1]
    add_property(&file_obj, "__type", make_num(is_text ? 1.0 : 0.0)); //[cite: 1]
    add_property(&file_obj, "__size", make_num((double)fsize)); //[cite: 1]
    
    // Attach instance methods[cite: 2]
    add_native_method(&file_obj, "type", bsl_file_type, 0);
    add_native_method(&file_obj, "data", bsl_file_data, 0);
    add_native_method(&file_obj, "size", bsl_file_size, 0);
    
    return file_obj;
}

// --- Factory Constructor ---

EXPORT Value fs(int argc, Value* argv, Env** envp) {
    Value obj = make_obj(); //[cite: 1]
    
    // Attach native methods to the constructed instance[cite: 2]
    add_native_method(&obj, "openFile", bsl_fs_openFile, 1);
    
    return obj;
}

#ifdef __cplusplus
}
#endif