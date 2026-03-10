#ifndef MEMORY_H
#define MEMORY_H

#ifdef __wasm__
#include <wasm_simd128.h>
#else
#include <intrin.h>
#include "kernel.h"
#endif

#ifdef __wasm__
static uint8 g_memory[0x4000000];
static uint8* g_memory_ptr = (uint8*)g_memory;
#else
static uint8* g_memory;
static uint8* g_memory_ptr;
#endif

static void* tMalloc(size_t size){
#ifndef __wasm__
    return VirtualAlloc(0,size,MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE);
#endif
    uint8* ret_ptr = g_memory_ptr;
    g_memory_ptr += size;
    return ret_ptr;
}

static void tFree(void* address){
#ifndef __wasm__
    VirtualFree(address,0,MEM_RELEASE);
#endif
}

static void* tMallocT(size_t size){
    return HeapAlloc(GetProcessHeap(),0,size);
}

static void* tReAlloc(void* ptr,size_t size){
    if(!ptr)
        return tMallocT(size);
    return HeapReAlloc(GetProcessHeap(),0,ptr,size);
}

static void tFreeT(void* ptr){
    HeapFree(GetProcessHeap(),0,ptr);
}

#endif