#include "opencl.h"
#include "linux.h"
#include "library.h"
#include "storage.h"
#include "print.h"
#include "main.h"
#include "model_audio.h"

#define CL_PROGRAM_BUILD_LOG 0x1183

#define CL_PLATFORM_PROFILE     0x0900
#define CL_PLATFORM_VERSION     0x0901
#define CL_PLATFORM_NAME        0x0902
#define CL_PLATFORM_VENDOR      0x0903
#define CL_PLATFORM_EXTENSIONS  0x0904

#define CL_DEVICE_NAME   0x102B

static int (stdcall *clGetPlatformIDs)(unsigned n_entries,void** platform,unsigned* n_platform_id);
static int (stdcall *clGetDeviceIDs)(void* platform,uint64 device_type,unsigned num_entries,void** devices,unsigned* num_devices);
static void* (stdcall *clCreateCommandQueueWithProperties)(void* context,void* device,const void** properties,int* errcode_ret);
static void* (stdcall *clCreateProgramWithSource)(void* context,unsigned count,const char** strings,const size_t* lengths,int* errcode_ret);
static void* (stdcall *clCreateContext)(
    const void** properties,
    unsigned n_devices,
    const void** devices,
    void (stdcall* pfn_notify)(const char* errinfo, const void* private_info,size_t cb, void* user_data),
    void* user_data,
    int* errcode_ret
);
static int (stdcall *clBuildProgram)(
    void* program,
    unsigned num_devices,
    const void** device_list,
    const char* options,
    void (stdcall* pfn_notify)(void* program, void* user_data),
    void* user_data
);
static void* (stdcall *clCreateKernel)(void* program,const char* kernel_name,int* errcode_ret);
static int (stdcall *clEnqueueNDRangeKernel)(
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
static int (stdcall *clEnqueueReadBuffer)(
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
static int (stdcall *clEnqueueWriteBuffer)(
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
static int (stdcall *clSetKernelArg)(
    void* kernel,
    unsigned arg_index,
    size_t arg_size_t,
    const void* arg_value
);
static void* (stdcall *clCreateBuffer)(
    void* context,
    uint64 flags,
    size_t size_t,
    void* host_ptr,
    int* errcode_ret
);
static int (stdcall *clFinish)(void* command_queue);

static void* g_opencl_lib;

static void* g_cl_context;
static void* g_cl_command_queue;

static void* cl_inference;
static void* cl_inference_bf;
static void* cl_vram_buffers;
static void* cl_indices;
static void* cl_graph_buffers[0x10];

static void* cl_audio_inference;
static void* cl_audio_buffer;
static void* cl_audio_indices;
static void* cl_audio_graph_buffers[0x10];

#define SFX_GENWIDTH 0x800
#define SFX_GENSIZE (SFX_GENWIDTH * SFX_GENWIDTH)

void initLibOpenCL(void){
#ifdef _MSC_VER
    g_opencl_lib = libraryLoad("OpenCL.dll");
#elif defined(__linux__)
    g_opencl_lib = libraryLoad("libOpenCL.so");
#endif
    if(!g_opencl_lib){
        print((String)STRING_LITERAL("opencl: library not found\n"));
        return;
    }
    struct{
        char* name;
        funcptr_t* fn_ptr;
    } functions[] =  {
        {.name = "clGetPlatformIDs",.fn_ptr = (funcptr_t*)&clGetPlatformIDs},
        {.name = "clGetDeviceIDs",.fn_ptr = (funcptr_t*)&clGetDeviceIDs},
        {.name = "clCreateCommandQueueWithProperties",.fn_ptr = (funcptr_t*)&clCreateCommandQueueWithProperties},
        {.name = "clCreateProgramWithSource",.fn_ptr = (funcptr_t*)&clCreateProgramWithSource},
        {.name = "clCreateContext",.fn_ptr = (funcptr_t*)&clCreateContext},
        {.name = "clCreateKernel",.fn_ptr = (funcptr_t*)&clCreateKernel},
        {.name = "clEnqueueNDRangeKernel",.fn_ptr = (funcptr_t*)&clEnqueueNDRangeKernel},
        {.name = "clEnqueueReadBuffer",.fn_ptr = (funcptr_t*)&clEnqueueReadBuffer},
        {.name = "clEnqueueWriteBuffer",.fn_ptr = (funcptr_t*)&clEnqueueWriteBuffer},
        {.name = "clSetKernelArg",.fn_ptr = (funcptr_t*)&clSetKernelArg},
        {.name = "clCreateBuffer",.fn_ptr = (funcptr_t*)&clCreateBuffer},
        {.name = "clFinish",.fn_ptr = (funcptr_t*)&clFinish},
        {.name = "clBuildProgram",.fn_ptr = (funcptr_t*)&clBuildProgram},
    };
    for(int i = countof(functions);i--;){
        *functions[i].fn_ptr = libraryFunctionLoad(g_opencl_lib,functions[i].name);
        if(!*functions[i].fn_ptr){
            print((String)STRING_LITERAL("opencl: function load error\n"));
            print((String)STRING_LITERAL("function: "));
            print(stringMake(functions[i].name));
            goto unload;
        }
    }

    void* platforms[20];
    void* devices[20];
    unsigned n_platform = 0;
    unsigned n_device = 0;

    int platform_error = clGetPlatformIDs(20,platforms,&n_platform);
    if(platform_error){
        print((String)STRING_LITERAL("opencl: platform error\n"));
        goto unload;
    }
    clGetDeviceIDs(platforms[0],CL_DEVICE_TYPE_ALL,1,devices,&n_device);

    int context_error;
    
    g_cl_context = clCreateContext(0,1,(void**)&devices[0],0,0,&context_error);

    if(!g_cl_context){
        print((String)STRING_LITERAL("opencl: no context\n"));
        goto unload;
    }
    
    g_cl_command_queue = clCreateCommandQueueWithProperties(g_cl_context,devices[0],0,0);

    FileContent file = storageFileRead(&g_startup_arena,"gpgpu.cl");
    FileContent file_audio = storageFileRead(&g_startup_arena,"gpgpu_audio.cl");
    String source_file = stringConcat(0,(String){.data = file.content,.size = file.size},(String)STRING_LITERAL("\0"));
    String source_file_audio = stringConcat(0,(String){.data = file_audio.content,.size = file_audio.size},(String)STRING_LITERAL("\0"));
    
    void* g_cl_program = clCreateProgramWithSource(g_cl_context,1,(char**)&source_file.data,0,0);
    void* g_cl_program_audio = clCreateProgramWithSource(g_cl_context,1,(char**)&source_file_audio.data,0,0);
    int build_error = clBuildProgram(g_cl_program,0,0,"-Werror",0,0);
    int build_error_audio = clBuildProgram(g_cl_program_audio,0,0,"-Werror",0,0);
    cl_inference    = clCreateKernel(g_cl_program,"inference",0);
    cl_inference_bf = clCreateKernel(g_cl_program,"inferenceBruteForce",0);

    cl_audio_inference = clCreateKernel(g_cl_program_audio,"inferenceBruteForce",0);

    if(!cl_inference || !cl_inference_bf || !cl_audio_inference){
        print((String)STRING_LITERAL("opencl: one or more kernels failed to build\n"));
        goto unload;
    }
    
    if(build_error || build_error_audio){
        print((String)STRING_LITERAL("opencl: build error\n"));
        goto unload;
    }
    cl_vram_buffers = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,SFX_GENSIZE * sizeof(int) * 2,0,0);
    cl_indices = clCreateBuffer(g_cl_context,CL_MEM_WRITE_ONLY,SFX_GENSIZE * sizeof(int),0,0);

    cl_audio_buffer = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,SFX_GENSIZE * sizeof(int) * 2,0,0);
    cl_audio_indices = clCreateBuffer(g_cl_context,CL_MEM_WRITE_ONLY,SFX_GENSIZE * sizeof(int),0,0);
    return;
 unload:
    libraryUnload(g_opencl_lib);
    g_opencl_lib = 0;
}

void clBruteForce(int* data,int* indices,int mipmap,int total_width,int total_height,bool inpaint){
    int gen_width = total_width >> mipmap;
    int gen_height = total_height >> mipmap;
    for(int i = 0;i < 0x10;i++){
        if(!graph_color[i].n_nodes)
            continue;
        if(cl_graph_buffers[i])
            continue;
        cl_graph_buffers[i] = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,graph_color[i].n_nodes * sizeof(GraphNode),0,0);
        clEnqueueWriteBuffer(g_cl_command_queue,cl_graph_buffers[i],true,0,graph_color[i].n_nodes * sizeof(GraphNode),graph_color[i].entry,0,0,0);
    }
    clSetKernelArg(cl_inference_bf,0,sizeof(void*),cl_graph_buffers + mipmap);
    clSetKernelArg(cl_inference_bf,1,sizeof(void*),&cl_vram_buffers);
    clSetKernelArg(cl_inference_bf,2,sizeof(void*),&cl_indices);
    clSetKernelArg(cl_inference_bf,3,sizeof(int),&mipmap);

    clSetKernelArg(cl_inference_bf,4,sizeof(int),&total_width);
    clSetKernelArg(cl_inference_bf,5,sizeof(int),&total_height);
    int n_node = graph_color[mipmap].n_nodes;
    clSetKernelArg(cl_inference_bf,6,sizeof(int),&n_node);
    unsigned rnd_offset = tRnd();
    clSetKernelArg(cl_inference_bf,7,sizeof(int),&rnd_offset);
    int inpaint_i = inpaint;
    clSetKernelArg(cl_inference_bf,8,sizeof(int),&inpaint_i);
    
    clEnqueueWriteBuffer(g_cl_command_queue,cl_vram_buffers,true,0,total_width * total_height * sizeof(int) * 2,data,0,0,0);
    clEnqueueWriteBuffer(g_cl_command_queue,cl_indices,true,0,gen_width * gen_height * sizeof(int),indices,0,0,0);
    size_t n_output = gen_width * gen_height;
    size_t n_local = 1;
    clEnqueueNDRangeKernel(g_cl_command_queue,cl_inference_bf,1,0,&n_output,0,0,0,0);
    
    clEnqueueReadBuffer(g_cl_command_queue,cl_vram_buffers,true,0,total_width * total_height * sizeof(int) * 2,data,0,0,0);
}

void clAudioBruteForce(int* data,int* indices,int mipmap,int total_size,bool inpaint){
    int gen_size = total_size >> mipmap;
    for(int i = 0;i < 0x10;i++){
        if(!graph_audio[i].n_nodes)
            continue;
        if(cl_audio_graph_buffers[i])
            continue;
        cl_audio_graph_buffers[i] = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,graph_audio[i].n_nodes * sizeof(GraphAudioNode),0,0);
        clEnqueueWriteBuffer(g_cl_command_queue,cl_audio_graph_buffers[i],true,0,graph_audio[i].n_nodes * sizeof(GraphAudioNode),graph_audio[i].entry,0,0,0);
    }
    clSetKernelArg(cl_audio_inference,0,sizeof(void*),cl_audio_graph_buffers + mipmap);
    clSetKernelArg(cl_audio_inference,1,sizeof(void*),&cl_audio_buffer);
    clSetKernelArg(cl_audio_inference,2,sizeof(void*),&cl_audio_indices);
    clSetKernelArg(cl_audio_inference,3,sizeof(int),&mipmap);

    clSetKernelArg(cl_audio_inference,4,sizeof(int),&total_size);
    int n_node = graph_audio[mipmap].n_nodes;
    clSetKernelArg(cl_audio_inference,5,sizeof(int),&n_node);
    unsigned rnd_offset = tRnd();
    clSetKernelArg(cl_audio_inference,6,sizeof(int),&rnd_offset);
    int inpaint_i = inpaint;
    clSetKernelArg(cl_audio_inference,7,sizeof(int),&inpaint_i);
    
    clEnqueueWriteBuffer(g_cl_command_queue,cl_audio_buffer,true,0,total_size * sizeof(int) * 2,data,0,0,0);
    clEnqueueWriteBuffer(g_cl_command_queue,cl_audio_indices,true,0,gen_size * sizeof(int),indices,0,0,0);
    size_t n_output = gen_size;
    size_t n_local = 1;
    clEnqueueNDRangeKernel(g_cl_command_queue,cl_audio_inference,1,0,&n_output,0,0,0,0);
    
    clEnqueueReadBuffer(g_cl_command_queue,cl_audio_buffer,true,0,total_size * sizeof(int) * 2,data,0,0,0);
}
