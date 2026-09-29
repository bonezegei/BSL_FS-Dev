#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bsl_v2.h"

#if defined(_WIN32) || defined(_WIN64)
    #define EXPORT __declspec(dllexport)
    #include <windows.h>
    #include <winioctl.h>
#else
    #define EXPORT __attribute__((visibility("default")))
    #include <dirent.h>
    #include <sys/stat.h>
    #include <sys/statvfs.h>
    #include <time.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <mntent.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Internal Helper Utilities ---

static char* bsl_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static const char* lookup_env_str(Env* env, const char* name) {
    while (env) {
        if (strcmp(env->name, name) == 0 && env->val.type == VAL_STR && env->val.str) {
            return env->val.str;
        }
        env = env->parent;
    }
    return NULL;
}

static char* get_script_dir_from_cmdline(void) {
#if defined(_WIN32) || defined(_WIN64)
    char* cmd = GetCommandLineA();
    if (!cmd) return NULL;

    char temp[1024];
    strncpy(temp, cmd, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char* token = strtok(temp, " ");
    while (token != NULL) {
        if (token[0] == '"') token++;
        size_t len = strlen(token);
        if (len > 0 && token[len - 1] == '"') token[len - 1] = '\0';

        if (strstr(token, ".bzg") || strstr(token, ".bsl")) {
            char* path = bsl_strdup(token);
            char* last_slash = strrchr(path, '/');
            char* last_bslash = strrchr(path, '\\');
            char* delim = (last_slash > last_bslash) ? last_slash : last_bslash;

            if (delim) {
                *delim = '\0';
                return path;
            } else {
                free(path);
                return bsl_strdup(".");
            }
        }
        token = strtok(NULL, " ");
    }
#else
    FILE* fp = fopen("/proc/self/cmdline", "r");
    if (fp) {
        char arg[1024];
        size_t n = fread(arg, 1, sizeof(arg) - 1, fp);
        fclose(fp);
        if (n > 0) {
            size_t offset = 0;
            while (offset < n) {
                char* current_arg = arg + offset;
                if (strstr(current_arg, ".bzg") || strstr(current_arg, ".bsl")) {
                    char* path = bsl_strdup(current_arg);
                    char* last_slash = strrchr(path, '/');
                    char* last_bslash = strrchr(path, '\\');
                    char* delim = (last_slash > last_bslash) ? last_slash : last_bslash;

                    if (delim) {
                        *delim = '\0';
                        return path;
                    } else {
                        free(path);
                        return bsl_strdup(".");
                    }
                }
                offset += strlen(current_arg) + 1;
            }
        }
    }
#endif
    return NULL;
}

// Windows helper: Detect SSD vs HDD via Storage Seek Penalty IOCTL
#if defined(_WIN32) || defined(_WIN64)
static const char* detect_drive_media_type(char drive_letter) {
    char dev_path[10];
    snprintf(dev_path, sizeof(dev_path), "\\\\.\\%c:", drive_letter);

    HANDLE hDev = CreateFileA(dev_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDev == INVALID_HANDLE_VALUE) {
        return "Fixed Drive";
    }

    STORAGE_PROPERTY_QUERY query;
    memset(&query, 0, sizeof(query));
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor;
    DWORD bytesReturned = 0;

    BOOL result = DeviceIoControl(hDev, IOCTL_STORAGE_QUERY_PROPERTY,
                                  &query, sizeof(query),
                                  &descriptor, sizeof(descriptor),
                                  &bytesReturned, NULL);
    CloseHandle(hDev);

    if (result && bytesReturned >= sizeof(descriptor)) {
        return descriptor.IncursSeekPenalty ? "HDD" : "SSD";
    }
    return "Fixed Drive";
}
#endif

// --- Object Property Helpers ---

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
    
    int is_text = 1;
    for (size_t i = 0; i < read_size; i++) {
        if (buffer[i] == '\0') {
            is_text = 0;
            break;
        }
    }
    
    Value file_obj = make_obj();
    
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

    add_property(&file_obj, "__data", make_str(buffer));
    add_property(&file_obj, "__type", make_num(is_text ? 1.0 : 0.0));
    add_property(&file_obj, "__size", make_num((double)fsize));
    
    add_native_method(&file_obj, "type", bsl_file_type, 0);
    add_native_method(&file_obj, "data", bsl_file_data, 0);
    add_native_method(&file_obj, "size", bsl_file_size, 0);
    add_native_method(&file_obj, "raw",  bsl_file_raw, 0);
    
    return file_obj;
}

Value bsl_fs_scriptDir(int argc, Value* argv, Env** envp) {
    Env* env = (envp && *envp) ? *envp : NULL;

    if (argc >= 2 && argv[1].type == VAL_STR && argv[1].str != NULL) {
        char* path = bsl_strdup(argv[1].str);
        char* last_slash = strrchr(path, '/');
        char* last_bslash = strrchr(path, '\\');
        char* delim = (last_slash > last_bslash) ? last_slash : last_bslash;
        if (delim) {
            *delim = '\0';
            return make_str(path);
        }
        free(path);
        return make_str(bsl_strdup("."));
    }

    const char* dir_vars[] = {"__dir__", "__DIR__", "SCRIPT_DIR", "script_dir", NULL};
    for (int i = 0; dir_vars[i]; i++) {
        const char* dir_val = lookup_env_str(env, dir_vars[i]);
        if (dir_val) {
            return make_str(bsl_strdup(dir_val));
        }
    }

    const char* file_vars[] = {"__file__", "__FILE__", "SCRIPT_PATH", "script_path", "__script__", NULL};
    for (int i = 0; file_vars[i]; i++) {
        const char* file_val = lookup_env_str(env, file_vars[i]);
        if (file_val) {
            char* path = bsl_strdup(file_val);
            char* last_slash = strrchr(path, '/');
            char* last_bslash = strrchr(path, '\\');
            char* delim = (last_slash > last_bslash) ? last_slash : last_bslash;

            if (delim) {
                *delim = '\0';
                return make_str(path);
            }
            free(path);
            return make_str(bsl_strdup("."));
        }
    }

    char* cmdline_dir = get_script_dir_from_cmdline();
    if (cmdline_dir) {
        return make_str(cmdline_dir);
    }

    return make_str(bsl_strdup("."));
}

Value bsl_fs_list(int argc, Value* argv, Env** envp) {
    const char* target_path = ".";
    if (argc >= 2 && argv[1].type == VAL_STR && argv[1].str != NULL) {
        target_path = argv[1].str;
    }

    int capacity = 16;
    int count = 0;
    Value* elements = (Value*)malloc(sizeof(Value) * capacity);

#if defined(_WIN32) || defined(_WIN64)
    char clean_path[MAX_PATH];
    strncpy(clean_path, target_path, sizeof(clean_path) - 1);
    clean_path[sizeof(clean_path) - 1] = '\0';

    size_t len = strlen(clean_path);
    while (len > 1 && (clean_path[len - 1] == '\\' || clean_path[len - 1] == '/')) {
        clean_path[len - 1] = '\0';
        len--;
    }

    char search_path[MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s\\*", clean_path);

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(search_path, &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
                continue;
            }

            Value item = make_obj();
            int is_dir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
            unsigned long long size64 = ((unsigned long long)findData.nFileSizeHigh << 32) | findData.nFileSizeLow;

            SYSTEMTIME stUTC, stLocal;
            FileTimeToSystemTime(&findData.ftLastWriteTime, &stUTC);
            SystemTimeToTzSpecificLocalTime(NULL, &stUTC, &stLocal);

            char date_buf[64];
            snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d %02d:%02d:%02d",
                     stLocal.wYear, stLocal.wMonth, stLocal.wDay,
                     stLocal.wHour, stLocal.wMinute, stLocal.wSecond);

            add_property(&item, "kind", make_str(is_dir ? "dir" : "file"));
            add_property(&item, "isDir", make_num(is_dir ? 1.0 : 0.0));
            add_property(&item, "filename", make_str(bsl_strdup(findData.cFileName)));
            add_property(&item, "fileSize", make_num(is_dir ? 0.0 : (double)size64));
            add_property(&item, "date", make_str(bsl_strdup(date_buf)));

            if (count >= capacity) {
                capacity *= 2;
                elements = (Value*)realloc(elements, sizeof(Value) * capacity);
            }
            elements[count++] = item;
        } while (FindNextFileA(hFind, &findData) != 0);

        FindClose(hFind);
    }
#else
    DIR* dir = opendir(target_path);
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", target_path, entry->d_name);

            struct stat st;
            memset(&st, 0, sizeof(st));
            int stat_res = stat(full_path, &st);

            int is_dir = 0;
            if (stat_res == 0) {
                is_dir = S_ISDIR(st.st_mode);
            }

            char date_buf[64] = {0};
            if (stat_res == 0) {
                struct tm* tm_info = localtime(&st.st_mtime);
                if (tm_info) {
                    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d %H:%M:%S", tm_info);
                }
            }

            Value item = make_obj();
            add_property(&item, "kind", make_str(is_dir ? "dir" : "file"));
            add_property(&item, "isDir", make_num(is_dir ? 1.0 : 0.0));
            add_property(&item, "filename", make_str(bsl_strdup(entry->d_name)));
            add_property(&item, "fileSize", make_num(is_dir ? 0.0 : (double)st.st_size));
            add_property(&item, "date", make_str(bsl_strdup(date_buf)));

            if (count >= capacity) {
                capacity *= 2;
                elements = (Value*)realloc(elements, sizeof(Value) * capacity);
            }
            elements[count++] = item;
        }
        closedir(dir);
    }
#endif

    Array* res_arr = (Array*)malloc(sizeof(Array));
    res_arr->length = count;
    res_arr->elements = elements;

    return make_arr(res_arr);
}

Value bsl_fs_exists(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_num(0.0);
    const char* path = argv[1].str;

#if defined(_WIN32) || defined(_WIN64)
    DWORD attr = GetFileAttributesA(path);
    int exists = (attr != INVALID_FILE_ATTRIBUTES);
#else
    int exists = (access(path, F_OK) == 0);
#endif

    return make_num(exists ? 1.0 : 0.0);
}

Value bsl_fs_writeFile(int argc, Value* argv, Env** envp) {
    if (argc < 3 || argv[1].type != VAL_STR || argv[1].str == NULL || argv[2].type != VAL_STR || argv[2].str == NULL) {
        return make_num(0.0);
    }
    const char* path = argv[1].str;
    const char* content = argv[2].str;
    int append = 0;

    if (argc >= 4 && argv[3].type == VAL_NUM) {
        append = (argv[3].num != 0.0);
    }

    FILE* f = fopen(path, append ? "ab" : "wb");
    if (!f) return make_num(0.0);

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);

    return make_num(written == len ? 1.0 : 0.0);
}

Value bsl_fs_deleteFile(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_num(0.0);
    const char* path = argv[1].str;

#if defined(_WIN32) || defined(_WIN64)
    BOOL res = DeleteFileA(path);
    return make_num(res ? 1.0 : 0.0);
#else
    int res = remove(path);
    return make_num(res == 0 ? 1.0 : 0.0);
#endif
}

Value bsl_fs_copyFile(int argc, Value* argv, Env** envp) {
    if (argc < 3 || argv[1].type != VAL_STR || argv[1].str == NULL || argv[2].type != VAL_STR || argv[2].str == NULL) {
        return make_num(0.0);
    }
    const char* src = argv[1].str;
    const char* dst = argv[2].str;

#if defined(_WIN32) || defined(_WIN64)
    BOOL res = CopyFileA(src, dst, FALSE);
    return make_num(res ? 1.0 : 0.0);
#else
    FILE* in = fopen(src, "rb");
    if (!in) return make_num(0.0);
    FILE* out = fopen(dst, "wb");
    if (!out) { fclose(in); return make_num(0.0); }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return make_num(1.0);
#endif
}

Value bsl_fs_moveFile(int argc, Value* argv, Env** envp) {
    if (argc < 3 || argv[1].type != VAL_STR || argv[1].str == NULL || argv[2].type != VAL_STR || argv[2].str == NULL) {
        return make_num(0.0);
    }
    const char* src = argv[1].str;
    const char* dst = argv[2].str;

#if defined(_WIN32) || defined(_WIN64)
    BOOL res = MoveFileExA(src, dst, MOVEFILE_REPLACE_EXISTING);
    return make_num(res ? 1.0 : 0.0);
#else
    int res = rename(src, dst);
    return make_num(res == 0 ? 1.0 : 0.0);
#endif
}

Value bsl_fs_makeDir(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_num(0.0);
    const char* path = argv[1].str;

#if defined(_WIN32) || defined(_WIN64)
    BOOL res = CreateDirectoryA(path, NULL);
    return make_num(res ? 1.0 : 0.0);
#else
    int res = mkdir(path, 0755);
    return make_num(res == 0 ? 1.0 : 0.0);
#endif
}

Value bsl_fs_removeDir(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_num(0.0);
    const char* path = argv[1].str;

#if defined(_WIN32) || defined(_WIN64)
    BOOL res = RemoveDirectoryA(path);
    return make_num(res ? 1.0 : 0.0);
#else
    int res = rmdir(path);
    return make_num(res == 0 ? 1.0 : 0.0);
#endif
}

Value bsl_fs_cwd(int argc, Value* argv, Env** envp) {
    char buf[1024] = {0};
#if defined(_WIN32) || defined(_WIN64)
    GetCurrentDirectoryA(sizeof(buf), buf);
#else
    if (!getcwd(buf, sizeof(buf))) {
        snprintf(buf, sizeof(buf), ".");
    }
#endif
    return make_str(bsl_strdup(buf));
}

Value bsl_fs_absPath(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_str(bsl_strdup("."));
    const char* path = argv[1].str;
    char buf[1024] = {0};

#if defined(_WIN32) || defined(_WIN64)
    GetFullPathNameA(path, sizeof(buf), buf, NULL);
#else
    if (!realpath(path, buf)) {
        if (path[0] == '/') {
            strncpy(buf, path, sizeof(buf) - 1);
        } else {
            char cwd_buf[1024] = {0};
            if (getcwd(cwd_buf, sizeof(cwd_buf))) {
                snprintf(buf, sizeof(buf), "%s/%s", cwd_buf, path);
            } else {
                strncpy(buf, path, sizeof(buf) - 1);
            }
        }
    }
#endif
    return make_str(bsl_strdup(buf));
}

Value bsl_fs_joinPath(int argc, Value* argv, Env** envp) {
    if (argc < 3 || argv[1].type != VAL_STR || argv[1].str == NULL || argv[2].type != VAL_STR || argv[2].str == NULL) {
        return make_str(bsl_strdup(""));
    }
    const char* p1 = argv[1].str;
    const char* p2 = argv[2].str;

    char buf[2048];
    size_t len1 = strlen(p1);

#if defined(_WIN32) || defined(_WIN64)
    char sep = '\\';
#else
    char sep = '/';
#endif

    if (len1 > 0 && (p1[len1 - 1] == '/' || p1[len1 - 1] == '\\')) {
        snprintf(buf, sizeof(buf), "%s%s", p1, p2);
    } else {
        snprintf(buf, sizeof(buf), "%s%c%s", p1, sep, p2);
    }

    return make_str(bsl_strdup(buf));
}

Value bsl_fs_extName(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_str(bsl_strdup(""));
    const char* path = argv[1].str;

    const char* dot = strrchr(path, '.');
    const char* slash1 = strrchr(path, '/');
    const char* slash2 = strrchr(path, '\\');
    const char* last_slash = (slash1 > slash2) ? slash1 : slash2;

    if (dot && (!last_slash || dot > last_slash)) {
        return make_str(bsl_strdup(dot));
    }

    return make_str(bsl_strdup(""));
}

Value bsl_fs_baseName(int argc, Value* argv, Env** envp) {
    if (argc < 2 || argv[1].type != VAL_STR || argv[1].str == NULL) return make_str(bsl_strdup(""));
    const char* path = argv[1].str;

    const char* slash1 = strrchr(path, '/');
    const char* slash2 = strrchr(path, '\\');
    const char* last_slash = (slash1 > slash2) ? slash1 : slash2;

    if (last_slash) {
        return make_str(bsl_strdup(last_slash + 1));
    }

    return make_str(bsl_strdup(path));
}

// --- DISK MANAGER / DRIVES INFO ---

Value bsl_fs_drives(int argc, Value* argv, Env** envp) {
    int capacity = 8;
    int count = 0;
    Value* elements = (Value*)malloc(sizeof(Value) * capacity);

#if defined(_WIN32) || defined(_WIN64)
    char drive_buffer[512] = {0};
    DWORD len = GetLogicalDriveStringsA(sizeof(drive_buffer) - 1, drive_buffer);

    if (len > 0 && len <= sizeof(drive_buffer)) {
        char* drive = drive_buffer;
        while (*drive) {
            UINT type = GetDriveTypeA(drive);
            const char* type_str = "Unknown";
            const char* media_type = "Unknown";

            switch (type) {
                case DRIVE_REMOVABLE: type_str = "USB / Removable"; media_type = "Flash Drive"; break;
                case DRIVE_FIXED:     type_str = "Fixed Disk"; media_type = detect_drive_media_type(drive[0]); break;
                case DRIVE_REMOTE:    type_str = "Network Drive"; media_type = "Network"; break;
                case DRIVE_CDROM:     type_str = "CD/DVD ROM"; media_type = "Optical"; break;
                case DRIVE_RAMDISK:   type_str = "RAM Disk"; media_type = "RAM"; break;
            }

            char vol_name[MAX_PATH] = {0};
            char fs_name[MAX_PATH] = {0};
            GetVolumeInformationA(drive, vol_name, sizeof(vol_name), NULL, NULL, NULL, fs_name, sizeof(fs_name));

            ULARGE_INTEGER freeBytesAvailable, totalBytes, totalFreeBytes;
            double total_size = 0.0;
            double free_space = 0.0;

            if (GetDiskFreeSpaceExA(drive, &freeBytesAvailable, &totalBytes, &totalFreeBytes)) {
                total_size = (double)totalBytes.QuadPart;
                free_space = (double)freeBytesAvailable.QuadPart;
            }

            Value item = make_obj();
            add_property(&item, "mountPoint", make_str(bsl_strdup(drive)));
            add_property(&item, "volumeName", make_str(bsl_strdup(vol_name[0] ? vol_name : "Local Disk")));
            add_property(&item, "fsType", make_str(bsl_strdup(fs_name[0] ? fs_name : "Unknown")));
            add_property(&item, "driveType", make_str(bsl_strdup(type_str)));
            add_property(&item, "mediaType", make_str(bsl_strdup(media_type)));
            add_property(&item, "totalBytes", make_num(total_size));
            add_property(&item, "freeBytes", make_num(free_space));

            if (count >= capacity) {
                capacity *= 2;
                elements = (Value*)realloc(elements, sizeof(Value) * capacity);
            }
            elements[count++] = item;

            drive += strlen(drive) + 1;
        }
    }
#else
    FILE* fp = setmntent("/proc/mounts", "r");
    if (fp) {
        struct mntent* entry;
        while ((entry = getmntent(fp)) != NULL) {
            if (strncmp(entry->mnt_fsname, "/dev/", 5) == 0) {
                struct statvfs stat;
                double total_size = 0.0;
                double free_space = 0.0;

                if (statvfs(entry->mnt_dir, &stat) == 0) {
                    total_size = (double)stat.f_blocks * stat.f_frsize;
                    free_space = (double)stat.f_bavail * stat.f_frsize;
                }

                Value item = make_obj();
                add_property(&item, "mountPoint", make_str(bsl_strdup(entry->mnt_dir)));
                add_property(&item, "volumeName", make_str(bsl_strdup(entry->mnt_fsname)));
                add_property(&item, "fsType", make_str(bsl_strdup(entry->mnt_type)));
                add_property(&item, "driveType", make_str(bsl_strdup("Fixed / Removable")));
                add_property(&item, "mediaType", make_str(bsl_strdup("Block Device")));
                add_property(&item, "totalBytes", make_num(total_size));
                add_property(&item, "freeBytes", make_num(free_space));

                if (count >= capacity) {
                    capacity *= 2;
                    elements = (Value*)realloc(elements, sizeof(Value) * capacity);
                }
                elements[count++] = item;
            }
        }
        endmntent(fp);
    }
#endif

    Array* res_arr = (Array*)malloc(sizeof(Array));
    res_arr->length = count;
    res_arr->elements = elements;

    return make_arr(res_arr);
}

// --- DELETED FILE SCANNING (FILE CARVING) ---

Value bsl_fs_scanDeleted(int argc, Value* argv, Env** envp) {
    if (argc < 3 || argv[1].type != VAL_STR || argv[2].type != VAL_STR) {
        return make_null();
    }

    const char* drive_letter = argv[1].str; // "C" on Windows or "/dev/sda1" on Linux
    const char* file_extension = argv[2].str;

    double scan_limit_mb = 100.0;
    if (argc >= 4 && argv[3].type == VAL_NUM) {
        scan_limit_mb = argv[3].num;
    }

    unsigned char sig[8] = {0};
    size_t sig_len = 0;

    if (strcmp(file_extension, "png") == 0) {
        sig[0] = 0x89; sig[1] = 0x50; sig[2] = 0x4E; sig[3] = 0x47;
        sig_len = 4;
    } else if (strcmp(file_extension, "jpg") == 0 || strcmp(file_extension, "jpeg") == 0) {
        sig[0] = 0xFF; sig[1] = 0xD8; sig[2] = 0xFF;
        sig_len = 3;
    } else if (strcmp(file_extension, "pdf") == 0) {
        sig[0] = 0x25; sig[1] = 0x50; sig[2] = 0x44; sig[3] = 0x46;
        sig_len = 4;
    } else {
        return make_null();
    }

    int capacity = 16;
    int count = 0;
    Value* elements = (Value*)malloc(sizeof(Value) * capacity);

#if defined(_WIN32) || defined(_WIN64)
    char raw_path[32];
    snprintf(raw_path, sizeof(raw_path), "\\\\.\\%c:", drive_letter[0]);

    HANDLE hDrive = CreateFileA(raw_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDrive == INVALID_HANDLE_VALUE) {
        free(elements);
        return make_null();
    }

    DWORD sector_size = 512;
    unsigned char* buffer = (unsigned char*)malloc(sector_size);
    DWORD bytesRead = 0;

    unsigned long long max_sectors = (unsigned long long)((scan_limit_mb * 1024.0 * 1024.0) / sector_size);

    for (unsigned long long sector = 0; sector < max_sectors; sector++) {
        if (!ReadFile(hDrive, buffer, sector_size, &bytesRead, NULL) || bytesRead == 0) {
            break;
        }

        if (memcmp(buffer, sig, sig_len) == 0) {
            Value item = make_obj();
            unsigned long long byte_offset = sector * sector_size;

            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "Restored_0x%llX.%s", byte_offset, file_extension);

            add_property(&item, "filename", make_str(bsl_strdup(name_buf)));
            add_property(&item, "sectorOffset", make_num((double)sector));
            add_property(&item, "byteOffset", make_num((double)byte_offset));
            add_property(&item, "ext", make_str(bsl_strdup(file_extension)));

            if (count >= capacity) {
                capacity *= 2;
                elements = (Value*)realloc(elements, sizeof(Value) * capacity);
            }
            elements[count++] = item;
        }
    }

    free(buffer);
    CloseHandle(hDrive);
#else
    char dev_path[256];
    if (strncmp(drive_letter, "/dev/", 5) == 0) {
        snprintf(dev_path, sizeof(dev_path), "%s", drive_letter);
    } else {
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", drive_letter);
    }

    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) {
        free(elements);
        return make_null();
    }

    size_t sector_size = 512;
    unsigned char* buffer = (unsigned char*)malloc(sector_size);
    unsigned long long max_sectors = (unsigned long long)((scan_limit_mb * 1024.0 * 1024.0) / sector_size);

    for (unsigned long long sector = 0; sector < max_sectors; sector++) {
        ssize_t bytesRead = read(fd, buffer, sector_size);
        if (bytesRead <= 0) break;

        if (memcmp(buffer, sig, sig_len) == 0) {
            Value item = make_obj();
            unsigned long long byte_offset = sector * sector_size;

            char name_buf[128];
            snprintf(name_buf, sizeof(name_buf), "Restored_0x%llX.%s", byte_offset, file_extension);

            add_property(&item, "filename", make_str(bsl_strdup(name_buf)));
            add_property(&item, "sectorOffset", make_num((double)sector));
            add_property(&item, "byteOffset", make_num((double)byte_offset));
            add_property(&item, "ext", make_str(bsl_strdup(file_extension)));

            if (count >= capacity) {
                capacity *= 2;
                elements = (Value*)realloc(elements, sizeof(Value) * capacity);
            }
            elements[count++] = item;
        }
    }

    free(buffer);
    close(fd);
#endif

    Array* res_arr = (Array*)malloc(sizeof(Array));
    res_arr->length = count;
    res_arr->elements = elements;

    return make_arr(res_arr);
}

// Extract raw file sectors from drive to destination path
Value bsl_fs_recoverFile(int argc, Value* argv, Env** envp) {
    if (argc < 5 || argv[1].type != VAL_STR || argv[2].type != VAL_NUM || argv[3].type != VAL_NUM || argv[4].type != VAL_STR) {
        return make_num(0.0);
    }

    const char* drive_letter = argv[1].str;
    double sector_offset = argv[2].num;
    double bytes_to_extract = argv[3].num;
    const char* dest_file = argv[4].str;

#if defined(_WIN32) || defined(_WIN64)
    char raw_path[32];
    snprintf(raw_path, sizeof(raw_path), "\\\\.\\%c:", drive_letter[0]);

    HANDLE hDrive = CreateFileA(raw_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hDrive == INVALID_HANDLE_VALUE) return make_num(0.0);

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)(sector_offset * 512.0);
    SetFilePointerEx(hDrive, li, NULL, FILE_BEGIN);

    FILE* out = fopen(dest_file, "wb");
    if (!out) {
        CloseHandle(hDrive);
        return make_num(0.0);
    }

    char chunk[4096];
    double remaining = bytes_to_extract;
    DWORD bytesRead = 0;

    while (remaining > 0) {
        DWORD read_size = (remaining > sizeof(chunk)) ? (DWORD)sizeof(chunk) : (DWORD)remaining;
        if (!ReadFile(hDrive, chunk, read_size, &bytesRead, NULL) || bytesRead == 0) break;
        fwrite(chunk, 1, bytesRead, out);
        remaining -= bytesRead;
    }

    fclose(out);
    CloseHandle(hDrive);
    return make_num(1.0);
#else
    char dev_path[256];
    if (strncmp(drive_letter, "/dev/", 5) == 0) {
        snprintf(dev_path, sizeof(dev_path), "%s", drive_letter);
    } else {
        snprintf(dev_path, sizeof(dev_path), "/dev/%s", drive_letter);
    }

    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) return make_num(0.0);

    off_t offset = (off_t)(sector_offset * 512.0);
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return make_num(0.0);
    }

    FILE* out = fopen(dest_file, "wb");
    if (!out) {
        close(fd);
        return make_num(0.0);
    }

    char chunk[4096];
    double remaining = bytes_to_extract;

    while (remaining > 0) {
        size_t read_size = (remaining > sizeof(chunk)) ? sizeof(chunk) : (size_t)remaining;
        ssize_t bytesRead = read(fd, chunk, read_size);
        if (bytesRead <= 0) break;
        fwrite(chunk, 1, bytesRead, out);
        remaining -= bytesRead;
    }

    fclose(out);
    close(fd);
    return make_num(1.0);
#endif
}

// --- Module Export ---

EXPORT Value fs(int argc, Value* argv, Env** envp) {
    Value obj = make_obj();

    // Basic & Directory Operations
    add_native_method(&obj, "openFile",    bsl_fs_openFile,    1);
    add_native_method(&obj, "scriptDir",   bsl_fs_scriptDir,   0);
    add_native_method(&obj, "list",        bsl_fs_list,        1);
    add_native_method(&obj, "exists",      bsl_fs_exists,      1);
    add_native_method(&obj, "writeFile",   bsl_fs_writeFile,   3);
    add_native_method(&obj, "deleteFile",  bsl_fs_deleteFile,  1);
    add_native_method(&obj, "copyFile",    bsl_fs_copyFile,    2);
    add_native_method(&obj, "moveFile",    bsl_fs_moveFile,    2);
    add_native_method(&obj, "makeDir",     bsl_fs_makeDir,     1);
    add_native_method(&obj, "removeDir",   bsl_fs_removeDir,   1);
    add_native_method(&obj, "cwd",         bsl_fs_cwd,         0);

    // Path Utilities
    add_native_method(&obj, "absPath",     bsl_fs_absPath,     1);
    add_native_method(&obj, "joinPath",    bsl_fs_joinPath,    2);
    add_native_method(&obj, "extName",     bsl_fs_extName,     1);
    add_native_method(&obj, "baseName",    bsl_fs_baseName,    1);

    // Disk Manager & Recovery Features
    add_native_method(&obj, "drives",      bsl_fs_drives,      0);
    add_native_method(&obj, "scanDeleted", bsl_fs_scanDeleted, 3);
    add_native_method(&obj, "recoverFile", bsl_fs_recoverFile, 4);

    return obj;
}

#ifdef __cplusplus
}
#endif