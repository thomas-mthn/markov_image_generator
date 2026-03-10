#ifndef OPENCL_H
#define OPENCL_H

#include "langext.h"

#define CL_DEVICE_TYPE_CPU (1 << 1)
#define CL_DEVICE_TYPE_GPU (1 << 2)

#define CL_MEM_READ_WRITE  (1 << 0)
#define CL_MEM_WRITE_ONLY  (1 << 1)
#define CL_MEM_READ_ONLY   (1 << 2)
#define CL_MEM_USE_HOST_PTR   (1 << 3)
#define CL_MEM_ALLOC_HOST_PTR (1 << 4)

extern int (stdcall *clGetPlatformIDs)(unsigned n_entries,void** platform,unsigned* n_platform_id);
extern int (stdcall *clGetDeviceIDs)(void* platform,uint64 device_type,unsigned num_entries,void** devices,unsigned* num_devices);
extern void* (stdcall *clCreateCommandQueueWithProperties)(void* context,void* device,const void** properties,int* errcode_ret);
extern void* (stdcall *clCreateProgramWithSource)(void* context,unsigned count,const char** strings,const size_t* lengths,int* errcode_ret);
extern void* (stdcall *clCreateContext)(
    const void** properties,
    unsigned n_devices,
    const void** devices,
    void (stdcall* pfn_notify)(const char* errinfo, const void* private_info,size_t cb, void* user_data),
    void* user_data,
    int* errcode_ret
);
extern int (stdcall *clBuildProgram)(
    void* program,
    unsigned num_devices,
    const void** device_list,
    const char* options,
    void (stdcall* pfn_notify)(void* program, void* user_data),
    void* user_data
);
extern void* (stdcall *clCreateKernel)(void* program,const char* kernel_name,int* errcode_ret);
extern int (stdcall *clEnqueueNDRangeKernel)(
    void* command_queue,
    void* kernel,
    unsigned work_dim,
    const size_t* global_work_offset,
    const size_t* global_work_size_t,
    const size_t* local_work_size_t,
    unsigned num_events_in_wait_list,
    const void** event_wait_list,
    void** event
);
extern int (stdcall *clEnqueueReadBuffer)(
    void* command_queue,
    void* buffer,
    int blocking_read,
    size_t offset,
    size_t size_t,
    void* ptr,
    unsigned num_events_in_wait_list,
    const void** event_wait_list,
    void** event
);
extern int (stdcall *clEnqueueWriteBuffer)(
    void* command_queue,
    void* buffer,
    int blocking_write,
    size_t offset,
    size_t size_t,
    const void* ptr,
    unsigned num_events_in_wait_list,
    const void** event_wait_list,
    void** event
);
extern int (stdcall *clSetKernelArg)(
    void* kernel,
    unsigned arg_index,
    size_t arg_size_t,
    const void* arg_value
);
extern void* (stdcall *clCreateBuffer)(
    void* context,
    uint64 flags,
    size_t size_t,
    void* host_ptr,
    int* errcode_ret
);
extern int (stdcall *clFinish)(void* command_queue);

void initLibOpenCL(void);

#endif
