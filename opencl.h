#ifndef OPENCL_H
#define OPENCL_H

#include "langext.h"

#define CL_DEVICE_TYPE_DEFAULT (1 << 0)
#define CL_DEVICE_TYPE_CPU     (1 << 1)
#define CL_DEVICE_TYPE_GPU     (1 << 2)
#define CL_DEVICE_TYPE_ALL     0xFFFFFFFF

#define CL_MEM_READ_WRITE  (1 << 0)
#define CL_MEM_WRITE_ONLY  (1 << 1)
#define CL_MEM_READ_ONLY   (1 << 2)
#define CL_MEM_USE_HOST_PTR   (1 << 3)
#define CL_MEM_ALLOC_HOST_PTR (1 << 4)

void initLibOpenCL(void);
void clBruteForce(int* data,int* indices,int mipmap,int total_width,int total_height,bool inpaint);
void clAudioBruteForce(int* data,int* indices,int mipmap,int total_size,bool inpaint);

#endif
