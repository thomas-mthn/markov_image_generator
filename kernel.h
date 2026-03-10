#ifndef KERNEL_H
#define KERNEL_H

#include "langext.h"

#if __GNUC__
#define DLLIMPORT(RETURN) __attribute__((dllimport,stdcall)) RETURN
#else
#define DLLIMPORT(RETURN) _declspec(dllimport) RETURN stdcall
#endif

#define GENERIC_WRITE 0x40000000
#define GENERIC_READ 0x80000000

#define STD_OUTPUT_HANDLE -11

#define HEAP_ZERO_MEMORY    0x00000008

#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3

#define INFINITE -1

#define INVALID_HANDLE_VALUE ((void*)-1)

#define MEM_COMMIT 0x00001000
#define MEM_RESERVE 0x00002000
#define MEM_RESET 0x00080000
#define MEM_DECOMMIT 0x00004000
#define MEM_RELEASE 0x00008000
#define MEM_COALESCE_PLACEHOLDERS 0x00000001
#define MEM_PRESERVE_PLACEHOLDER 0x00000002

#define PAGE_NOACCESS          0x01 
#define PAGE_READONLY          0x02 
#define PAGE_READWRITE         0x04 
#define PAGE_WRITECOPY         0x08 

#define PAGE_EXECUTE           0x10 
#define PAGE_EXECUTE_READ      0x20 
#define PAGE_EXECUTE_READWRITE 0x40 
#define PAGE_EXECUTE_WRITECOPY 0x80 

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_ARGUMENT_ARRAY  0x00002000
#define FORMAT_MESSAGE_FROM_HMODULE    0x00000800
#define FORMAT_MESSAGE_FROM_STRING     0x00000400
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200

typedef int (stdcall *FarProc)();
typedef unsigned (stdcall *ThreadStartRoutine)(void* lpThreadParameter);

typedef union{
    struct{
        unsigned low_part;
        int      high_part;
    };
    long long quad_part;
} LargeInteger;

typedef struct{
	short x;
	short y;
} Coord;

typedef struct{
	unsigned length;
	void* security_descriptor;
	int   inherit_handle;
} SecurityAttributes;

typedef struct{
    void* debug_info;
    long lock_count;
    long recursion_count;
    void* owning_thread;
    void* lock_semaphore;
    size_t* spin_count;
} CriticalSection;

typedef struct{
    unsigned* internal;
    unsigned* internal_high;
    union{
        struct{
            uint16 offset;
            uint16 offset_high;
        };
        void* pointer;
    };
    void* event;
} Overlapped;

typedef struct{
    unsigned low_date_time;
    unsigned high_date_time;
} FileTime;

typedef struct{
    unsigned file_attributes;
    FileTime creation_time;
    FileTime last_access_time;
    FileTime last_write_time;
    unsigned file_size_high;
    unsigned file_size_low;
    unsigned reserved_0;
    unsigned reserved_1;
    char file_name[260];
    char alternative_file_name[14];
    unsigned file_type;
    unsigned creator_type;
    uint16 finder_flags;
} Win32FindDataA;

typedef struct{
    union{
        unsigned oem_id;
        struct{
            uint16 processor_architecture;
            uint16 reserved;
        };
    };
    unsigned page_size;
    void* minimum_application_address;
    void* maximum_application_address;
    unsigned* active_processor_mask;
    unsigned number_of_processors;
    unsigned processor_type;
    unsigned allocation_granularity;
    uint16 processor_level;
    uint16 processor_revision;
} SystemInfo;

#ifdef __cplusplus
extern "C"{
#endif
DLLIMPORT(FarProc) GetProcAddress(void* module,const char* proc_name);
DLLIMPORT(void) Sleep(unsigned milli_seconds);
DLLIMPORT(void*) FindFirstFileA(const char* file_name,Win32FindDataA* find_file_data);
DLLIMPORT(int) FindNextFileA(void* find_file,Win32FindDataA* find_file_data);
DLLIMPORT(unsigned) GetLastError(void);
DLLIMPORT(void*) GetModuleHandleA(const char* module_name);
DLLIMPORT(void*) LoadLibraryA(const char* file_name);
DLLIMPORT(int) FreeLibrary(void* module);
DLLIMPORT(void*) GetStdHandle(unsigned std_handle);
DLLIMPORT(int) SetConsoleTextAttribute(void* console_output,short attributes);
DLLIMPORT(int) WriteConsoleA(
	void* console_output,
	const void* buffer,
	unsigned number_of_chars_to_write,
	unsigned* number_of_chars_written,
	void* reserved
);
DLLIMPORT(int) SetConsoleCursorPosition(void* console_output,Coord cursor_position);
DLLIMPORT(int) GetObjectA(void* h,int c,void* pv);
DLLIMPORT(int) DeleteObject(void* object);
DLLIMPORT(unsigned) WaitForSingleObject(void* handle,unsigned miliseconds);
DLLIMPORT(unsigned) WaitForMultipleObjects(unsigned count,void** handles,int wait_all,unsigned miliseconds);
DLLIMPORT(void) ExitProcess(unsigned exit_code);
DLLIMPORT(int) CloseHandle(void* handle);
DLLIMPORT(void) InitializeCriticalSection(CriticalSection* critical_section);
DLLIMPORT(void) DeleteCriticalSection(CriticalSection* critical_section);
DLLIMPORT(void) LeaveCriticalSection(CriticalSection* critical_section);
DLLIMPORT(void) EnterCriticalSection(CriticalSection* critical_section);
DLLIMPORT(void*) CreateThread(
	SecurityAttributes* thread_attributes,
	unsigned stack_size,
	ThreadStartRoutine start_address,
	void* parameter,
	unsigned creation_flags,
	unsigned* thread_id
);

DLLIMPORT(void*) VirtualAlloc(void* address,size_t size,unsigned allocation_type,unsigned protect);
DLLIMPORT(int) VirtualFree(void* address,size_t size,unsigned free_type);
DLLIMPORT(void*) HeapAlloc(void* heap,unsigned flags,size_t size);
DLLIMPORT(void*) HeapReAlloc(void* heap,unsigned flags,void* mem,size_t size);
DLLIMPORT(int) HeapFree(void* heap,unsigned flags,void* mem);
DLLIMPORT(void*) GetProcessHeap();

DLLIMPORT(unsigned) GetFileSize(void* file,unsigned* file_size_high);
DLLIMPORT(void*) CreateFileA(
    const char* file_name,
    unsigned desired_access,
    unsigned share_mode,
    void* security_attributes,
    unsigned creation_disposition,
    unsigned flags,
    void* template_file
);
DLLIMPORT(int) WriteFile(void* file,void* buffer,unsigned bytes_to_read,unsigned* bytes_read,Overlapped* overlapped);
DLLIMPORT(int) ReadFile(void* file,void* buffer,unsigned bytes_to_read,unsigned* bytes_read,Overlapped* overlapped);
DLLIMPORT(int) QueryPerformanceFrequency(unsigned* frequency);
DLLIMPORT(int) QueryPerformanceCounter(unsigned* performance_count);
DLLIMPORT(void) OutputDebugStringA(const char* output_string);
DLLIMPORT(int) AllocConsole(void);
DLLIMPORT(void*) CreateWaitableTimerA(SecurityAttributes* timer_attributes,int manuel_reset,const char* timer_name);
DLLIMPORT(int) SetWaitableTimer(
    void* timer,
    LargeInteger* due_time,int period,
    void (stdcall *completion_routine)(void* arg_to_completion_routine,unsigned timer_low_value,unsigned timer_high_value),
    void* arg_to_completion_routine,
    int resume
);
DLLIMPORT(void*) GetConsoleWindow(void);
DLLIMPORT(void) GetSystemInfo(SystemInfo* system_info);

#ifdef __cplusplus
}
#endif

#endif
