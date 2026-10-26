#ifndef STORAGE_H
#define STORAGE_H

#include "langext.h"
#include "string.h"

structure(MemoryArena);

structure(FileContent){
    size_t size;
    void* content;
};

structure(DirEntryList){
    DirEntryList* next;
    String name;
};

DirEntryList* storageDirEntries(char* path);
bool storageFileExist(char* path);
FileContent storageFileRead(MemoryArena* arena,char* path);
bool storageFileReadStatic(void* data,size_t size,char* path);
void storageFileWrite(void* data,size_t size,char* path);

#endif
