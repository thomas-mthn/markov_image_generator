#include "library.h"

#ifdef __linux__
#include <dlfcn.h>
#elif defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else
#include "../langext.h"
#endif

void* libraryLoad(char* name){
#ifdef __linux__
    return dlopen(name,RTLD_NOW);
#elif defined(_MSC_VER)
    return LoadLibraryA(name);
#else
    return nullptr;
#endif
}

void libraryUnload(void* library){
#ifdef __linux__
    dlclose(library);
#elif defined(_MSC_VER)
    FreeLibrary(library);
#endif
}

funcptr_t libraryFunctionLoad(void* library,char* name){
#ifdef __linux__
    return (funcptr_t)dlsym(library,name);
#elif defined(_MSC_VER)
    return (funcptr_t)GetProcAddress(library,name);
#else
    return nullptr;
#endif
}
