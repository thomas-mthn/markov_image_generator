#ifndef MEMORY_H
#define MEMORY_H

#include "langext.h"

structure(MemoryBlock);

structure(MemoryArena){
    size_t min_capacity;
    size_t capacity;
    size_t pointer;
    char* data;
};

structure(AllocatorFreeList){
    MemoryArena arena;
    MemoryBlock* block_list;
    MemoryBlock* free_list;
    MemoryBlock* tail;
};

void* virtualAllocate(size_t size);
void virtualFree(void* address,size_t size);

void* memoryArenaAllocate(MemoryArena* arena,size_t size);
void* memoryArenaAllocateZero(MemoryArena* arena,size_t size);
void memoryArenaFree(MemoryArena* arena);

void* tMalloc(size_t size);
void tFree(void* address);
void* tMallocZero(size_t size);

void* allocatorFreeListAlloc(AllocatorFreeList* free_list,size_t size);
void allocatorFreeListFree(AllocatorFreeList* allocator,void* address);
void* allocatorFreeListAllocZero(AllocatorFreeList* free_list,size_t size);

void allocatorFreeListFreeAll(AllocatorFreeList* free_list);

extern MemoryArena g_arena_frame;

#endif

