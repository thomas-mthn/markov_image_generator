#include "storage.h"
#include "memory.h"

#ifdef __linux__
#include "syscall.h"
#elif defined(_MSC_VER)
#include "../win32/w_kernel.h"
#endif

DirEntryList* storageDirEntries(char* path){
#ifdef __linux__
    int dir = systemOpen(path,0,0);
    char* buffer = memoryArenaAllocate(&g_arena_frame,0x10000);
    int n = systemGetdents(dir,(void*)buffer,0x10000);
    if(n <= 0)
        return 0;
    DirEntryList* list = 0;
    while(n > 0){
        DirectoryEntry* entry = (void*)buffer;
        buffer += entry->reclen;
        n -= entry->reclen;
        DirEntryList* node = memoryArenaAllocate(&g_arena_frame,sizeof *node);
        node->name = stringMake((char*)entry->name);
        node->next = list;
        list = node;
    }
    return list;
#endif
}

bool storageFileExist(char* path){
#ifdef __linux__
    int file = systemOpen(path,0,0);
    if(file < 0)
        return false;
    systemClose(file);
    return true;
#elif defined(_MSC_VER)
    void* file = CreateFileA(path,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
    if(!file)
        return false;
    CloseHandle(file);
    return true;
#endif
}

FileContent storageFileRead(MemoryArena* arena,char* path){
#ifdef __linux__
    int file = systemOpen(path,0,0);

	if(file < 0)
		return (FileContent){0};
    
    KernelStat stat;
    systemFileStat(file,&stat);
	unsigned file_size = stat.st_size;
	char* content = memoryArenaAllocate(arena,file_size);
    
    systemRead(file,content,file_size);
    systemClose(file);
    
	return (FileContent){.content = content,.size = file_size};
#elif defined(_MSC_VER)
    void* file = CreateFileA(path,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	if(file == INVALID_HANDLE_VALUE)
		return (FileContent){0};
	unsigned file_size = GetFileSize(file,0);
    char* content = memoryArenaAllocate(arena,file_size);
    ReadFile(file,content,file_size,0,0);
    CloseHandle(file);
    return (FileContent){.content = content,.size = file_size};
#endif
}

bool storageFileReadStatic(void* data,size_t size,char* path){
#ifdef __linux__
    int file = systemOpen(path,0,0);

	if(file < 0)
		return false;

    systemRead(file,data,size);
    systemClose(file);
    
	return true;
#elif defined(_MSC_VER)
    void* file = CreateFileA(path,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
	if(file == INVALID_HANDLE_VALUE)
		return false;

    ReadFile(file,data,size,0,0);
    CloseHandle(file);

    return true;
#endif
}

void storageFileWrite(void* data,size_t size,char* path){
#ifdef __linux__
    int file = systemOpen(path,O_WRONLY | O_TRUNC | O_CREAT,0644);

    systemWrite(file,data,size);
    systemClose(file);
#elif defined(_MSC_VER)
    void* file = CreateFileA(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);

    WriteFile(file,data,size,0,0);
    CloseHandle(file);
#endif
}
