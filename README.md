# BSL_FS-Dev
BSL File System Library

gcc -shared -O2 -o fs.dll bsl_fs.c -lkernel32

gcc -shared -fPIC -O2 -o fs.so bsl_fs.c