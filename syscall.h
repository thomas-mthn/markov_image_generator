#include "langext.h"

#define PROT_READ  0x01
#define PROT_WRITE 0x02

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20

#define O_RDONLY   0
#define O_WRONLY   (1 << 0)
#define O_CREAT    (1 << 6)
#define O_TRUNC    (1 << 9)
#define O_NONBLOCK (1 << 11)

structure(DirectoryEntry){
    unsigned long ino;
    unsigned long offset;
    unsigned short reclen;
    char name[];
};

structure(KernelStat){
    unsigned int       st_mode;
    unsigned long      st_ino;
    unsigned long      st_dev;

    unsigned long      st_rdev;
    unsigned long      st_nlink; 

    unsigned int       st_uid;  
    unsigned int       st_gid;

    long long          st_size;

    long               st_atim_tv_sec;
    long               st_atim_tv_nsec;
    long               st_mtim_tv_sec;
    long               st_mtim_tv_nsec;
    long               st_ctim_tv_sec;
    long               st_ctim_tv_nsec;

    long               st_blksize;

    long long          st_blocks;
    uint8              __padding[32];
};

structure(TimeSpec){
    long seconds;
    long nano_seconds;
};

long systemWrite(unsigned file_descriptor,void* buffer,size_t buffer_size);
long systemOpen(char* filename,int flags,int mode);
long systemFileStat(unsigned file_descriptor,KernelStat* stat);
long systemClose(unsigned file_descriptor);
long systemRead(unsigned file_descriptor,void* buffer,size_t buffer_size);
void* systemMemoryMap(void* address,size_t length,int protection,int flags,int fd,long offset);
long systemGetdents(unsigned file_descriptor,DirectoryEntry* buffer,int buffer_length);

long systemMemoryUnmap(void* address,size_t length);
long systemNanoSleep(TimeSpec* requested,TimeSpec* remaining);
