#include "memory.h"
#include "libc.h"
#include "tmath.h"

#ifdef __linux__

#include "syscall.h"

#elif defined(_MSC_VER)

#include "win32/w_kernel.h"
#include "win32/w_user.h"
#include "win32/w_main.h"

#endif

#if defined(__linux__) || defined(_MSC_VER)
#define OS_BACKED true
#else
#define OS_BACKED false
#endif

MemoryArena g_arena_frame = {.min_capacity = 0x1000000};

static void errorBox(void){
#if !defined(__wasm__) && !defined(__linux__)
    showCursor(true);
    char* error_string = 0;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        0,
        GetLastError(),
        1024,
        error_string,
        0,
        0
    );
    MessageBoxA(0,error_string,"error",0);
#endif
}

static size_t alignValue(size_t value,int alignment){
    return (value + alignment - 1) & ~(alignment - 1);
}

void virtualFree(void* address,size_t size){
#if !OS_BACKED
    tFree(address);
#elif defined(__linux__)
    systemMemoryUnmap(address,size);
#elif defined(_MSC_VER)
    if(!VirtualFree(address,0,MEM_RELEASE))
        errorBox();
#else
    tFree(address);
#endif
}

void* virtualAllocate(size_t size){
#if !OS_BACKED  
    return tMallocZero(size);
#elif defined(__linux__)
    return systemMemoryMap(0,size,PROT_READ | PROT_WRITE,MAP_PRIVATE | MAP_ANONYMOUS,-1,0);
#elif defined(_MSC_VER)
    if(!size)
        return nullptr;
    void* mem = VirtualAlloc(0,size,MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE);
    if(!mem)
        errorBox();
    return mem;
#else
    return tMalloc(size);
#endif
}

#define PAGE_SIZE 0x1000

structure(ArenaChunkHeader){
    void* previous;
    size_t size;
};

static int pages_allocated;

void* memoryArenaAllocate(MemoryArena* arena,size_t size){
    size = alignValue(size,sizeof(void*));
    if(arena->pointer + size > arena->capacity){
        if(!arena->data && arena->min_capacity)
            size = tMax(size,arena->min_capacity);
        
        size_t allocation_size = alignValue(size + sizeof(ArenaChunkHeader),PAGE_SIZE);
        ArenaChunkHeader* new_data = virtualAllocate(allocation_size);
        
        new_data->previous = arena->data;
        new_data->size = allocation_size;
        
        arena->capacity = allocation_size;
        arena->data = (void*)new_data;
        arena->pointer = sizeof(ArenaChunkHeader);
    }
    void* allocation = arena->data + arena->pointer;
    arena->pointer += size;
    return allocation;
}

void* memoryArenaAllocateZero(MemoryArena* arena,size_t size){
    void* memory = memoryArenaAllocate(arena,size);
    tMemset(memory,0,size);
    return memory;
}

void memoryArenaFree(MemoryArena* arena){
    ArenaChunkHeader* header = (void*)arena->data;
    if(!header)
        return;
    while(header && header->previous){
        void* previous = header->previous;
        virtualFree(header,header->size);
        header = previous;
    }
    
    size_t min_cap = alignValue(arena->min_capacity + sizeof(ArenaChunkHeader),PAGE_SIZE);
    
    if(!arena->min_capacity || header->size != min_cap){
        virtualFree(header,header->size);
        arena->capacity = 0;
        arena->data = 0;
        arena->pointer = 0;
    }
    else{
        arena->capacity = min_cap;
        arena->data = (void*)header;
        arena->pointer = sizeof *header;
    }
}

structure(MemoryBlock){
	MemoryBlock* previous;
    MemoryBlock* next;
    MemoryBlock* next_free;
    union{
        size_t size;
        enum{
            MEMBLOCK_FREE = 1 << 0,
        } flags;
    };
};

#if !OS_BACKED
static char memory[0x4000000];
static char* memory_ptr = memory;
#endif

static AllocatorFreeList generic;

static size_t blockSize(MemoryBlock* block){
    return block->size & ~(sizeof(void*) - 1);
}

void* allocatorFreeListAlloc(AllocatorFreeList* free_list,size_t size){
    if(!size)
        return nullptr;
    //alignment
    size = alignValue(size,sizeof(void*));
    MemoryBlock* block_previous = 0;
	for(MemoryBlock* block = free_list->free_list;block;block = block->next_free){
		if(blockSize(block) >= size){
            if(block_previous)
                block_previous->next_free = block->next_free;
            else
                free_list->free_list = block->next_free;
            block->flags &= ~MEMBLOCK_FREE;

			if(blockSize(block) < size + sizeof(MemoryBlock))
				return block + 1;
			
			MemoryBlock* block_new = (void*)((char*)(block + 1) + size);
			block_new->size = blockSize(block) - size - sizeof(MemoryBlock);
			block_new->next = block->next;
			block_new->previous = block;
			
			if(block->next)
				block->next->previous = block_new;

			block->next = block_new;
			block->size = size;

			block_new->flags |= MEMBLOCK_FREE;
            block_new->next_free = free_list->free_list;
            free_list->free_list = block_new;
			return block + 1;
		}
		block_previous = block;
	}
#if OS_BACKED
	MemoryBlock* block = memoryArenaAllocate(&free_list->arena,sizeof(MemoryBlock) + size);
#else
    MemoryBlock* block;
    if(free_list == &generic){
        block = (void*)memory_ptr;
        memory_ptr += (sizeof(MemoryBlock) + size);
    }
    else{
        block = memoryArenaAllocate(&free_list->arena,sizeof(MemoryBlock) + size);
    }
#endif
    block->size = size;
	block->next = 0;
    block->next_free = 0;
    block->flags &= ~MEMBLOCK_FREE;
	if(free_list->block_list){
		block->previous = free_list->tail;
		free_list->tail->next = block;
	}
	else{
		block->previous = 0;
		free_list->block_list = block;
	};
    free_list->tail = block;
	return block + 1;
}

void allocatorFreeListFree(AllocatorFreeList* allocator,void* address){
    if(!address)
        return;
    MemoryBlock* header = (MemoryBlock*)address - 1;
    intptr_t page = (intptr_t)header / PAGE_SIZE;
    if(header->next && (header->next->flags & MEMBLOCK_FREE) && (intptr_t)header->next / PAGE_SIZE == page){
		if(allocator->free_list == header->next){
			allocator->free_list = header->next->next_free;
		}
		else{
			for(MemoryBlock* block = allocator->free_list;;block = block->next_free){
				if(block->next_free == header->next){
					block->next_free = header->next->next_free;
					break;
				}
			}
		}
		if(header->next->next)
			header->next->next->previous = header;
        header->size += blockSize(header->next) + sizeof(MemoryBlock);
		header->next_free = header->next->next_free;
        header->next = header->next->next;
    }
    if(header->previous && (header->previous->flags & MEMBLOCK_FREE) && (intptr_t)header->previous / PAGE_SIZE == page){
		if(header->next)
			header->next->previous = header->previous;
		header->previous->next = header->next;
		header->previous->size += blockSize(header) + sizeof(MemoryBlock);
		return;
    }
	header->flags |= MEMBLOCK_FREE;
    header->next_free = allocator->free_list;
    allocator->free_list = header;
}

void* tMalloc(size_t size){
    return allocatorFreeListAlloc(&generic,size);
}

void tFree(void* address){
    allocatorFreeListFree(&generic,address);
}

void* tMallocZero(size_t size){
    char* memory = tMalloc(size);
    tMemset(memory,0,size);
    return memory;
}

void* allocatorFreeListAllocZero(AllocatorFreeList* free_list,size_t size){
    char* memory = allocatorFreeListAlloc(free_list,size);
    tMemset(memory,0,size);
    return memory;
}

void allocatorFreeListFreeAll(AllocatorFreeList* free_list){
    free_list->free_list = 0;
    free_list->block_list = 0;
    memoryArenaFree(&free_list->arena);
}
