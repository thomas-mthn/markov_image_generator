#ifndef LIBRARY_H
#define LIBRARY_H

#include "langext.h"

void* libraryLoad(char* name);
void libraryUnload(void* library);
funcptr_t libraryFunctionLoad(void* library,char* name);

#endif

