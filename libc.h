#ifndef LIBC_H
#define LIBC_H

#include "langext.h"

size_t tStrlen(const char* str);
void* tMemset(void *s,int c,size_t n);
void* tMemcpy(void* dest,const void* src,size_t n);
void* tMemmove(void* dest,const void* src,size_t count);

#endif
