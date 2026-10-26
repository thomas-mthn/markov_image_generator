#include "libc.h"

#pragma function(strlen)
size_t strlen(const char* str){
    const char* ptr = str;
    while(*ptr)
        ptr += 1;
    return ptr - str;
}

#ifdef __GNUC__
__attribute__((optimize("no-tree-loop-distribute-patterns")))
#endif
#pragma function(memset)
void* memset(void *s,int c,size_t n){
    char *p = s;
    while(n--)
        *p++ = (char)c;
    return s;
}

#pragma function(memcpy)
void* memcpy(void* dest,const void* src,size_t n){
    char* d = dest;
    const char* s = src;
    while(n--)
        *d++ = *s++;
    return dest;
}

#pragma function(memmove)
void* memmove(void* dest,const void* src,size_t count){
	char* d = (char*)dest;
    char* s = (char*)src;

    if(d == s || count == 0)
        return dest;

    if(d < s){
		for(size_t i = 0;i < count;i++)
            d[i] = s[i];
    } 
	else{
        for(size_t i = count; i > 0; i--)
            d[i - 1] = s[i - 1];
    }
    return dest;
}

size_t tStrlen(const char* str){
    return strlen(str);
}

void* tMemset(void *s,int c,size_t n){
    return memset(s,c,n);
}
           
void* tMemcpy(void* dest,const void* src,size_t n){
    return memcpy(dest,src,n);
}

void* tMemmove(void* dest,const void* src,size_t count){
    return memmove(dest,src,count);
}
