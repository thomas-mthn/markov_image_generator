#include "langext.h"
#include "fixed.h"
#include "kernel.h"
#include "user.h"
#include "gdi.h"
#include "main.h"
#include "vec3.h"
#include "tmath.h"
#include "opencl.h"
#include "sfx.h"
#include "memory.h"

static void printNumber(int number){
    if(!GetConsoleWindow())
        AllocConsole();
	int length = 0;
	int length_copy;
	char buffer[0x10];
    bool negative = false;
	int number_copy;

    if(number < 0){
        length++;
        negative = true;
        number = -number;
    }
	number_copy = number;

    if(!number)
        length = 1;
    else{
        while(number_copy){
            number_copy /= 10;
            length++;
        }
    }
	length_copy = length;

    while(length--){
        buffer[length] = number % 10 + '0';
        number /= 10;
    }
    if(negative)
        buffer[0] = '-';
	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),buffer,length_copy,0,0);
}

static void print(const char* str){
    if(!GetConsoleWindow())
    	AllocConsole();
    const char* end = str;
    while(*end++);
    int l = end - str;
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),str,l,0,0);
}

static void printNumberNL(int number){
    printNumber(number);
    print("\n");
}

static Msg g_msg;
static void* g_window;
static void* g_context;
static WndClassA g_windowclass;

static int camera_x;
static int camera_y;
static int camera_zoom = FIXED_ONE;

#define GEN_SIZE (1 << 10)

#define WINDOW_SIZE_X 0x400
#define WINDOW_SIZE_Y 0x400

static unsigned image[0x1000000];
static int g_luminance_color[GEN_SIZE * GEN_SIZE * 2];

static void vramDraw(int mipmap){
    int size = GEN_SIZE >> mipmap;
    int offset = mipmapOffset(GEN_SIZE,GEN_SIZE,mipmap);
    for(int j = 0;j < VRAM_SIZE * VRAM_SIZE;j++){ 
        int x = fixedMulR(j / VRAM_SIZE * FIXED_ONE + camera_x,camera_zoom) / FIXED_ONE;
        int y = fixedMulR(j % VRAM_SIZE * FIXED_ONE + camera_y,camera_zoom) / FIXED_ONE;

        if(x < 0 || x >= size || y < 0 || y >= size){
            g_vram[j] = 0;
            continue;
        }

        g_vram[j] = g_luminance_color[offset + x * size + y];
    }
}

static Lresult stdcall windowMessageHandler(void* window,unsigned msg,Wparam wparam,Lparam lparam){
    switch(msg){
        case WM_MOUSEWHEEL:{
            int scroll = (int16)(wparam >> 16) / 120;
            if(scroll < 0){
                for(int i = 0;i < -scroll;i++)
                    fixedMul(&camera_zoom,FIXED_ONE + FIXED_ONE / 10);
            }
            else{
                for(int i = 0;i < scroll;i++)
                    fixedMul(&camera_zoom,FIXED_ONE - FIXED_ONE / 10);
            }
            vramDraw(0);
        } break;
    }
    return DefWindowProcA(window,msg,wparam,lparam);
}

structure(WavHeader){
    char riff[4];
    unsigned chunkSize;
    char wave[4];
    char fmt[4];
    unsigned subchunk1Size;
    uint16 audioFormat;
    uint16 numChannels;
    unsigned sampleRate;
    unsigned byteRate;
    uint16 blockAlign;
    uint16 bitsPerSample;
    char data[4];
    unsigned dataSize;
};

static int16* loadWavFile(char* filename,WavHeader* header){
    void* file = CreateFileA(filename,GENERIC_READ,0,0,OPEN_EXISTING,0x80,0);

    unsigned bytes_read;
    ReadFile(file,header,sizeof(WavHeader),&bytes_read,0);
    header->dataSize /= 8;
    int16* buffer = tMalloc(header->dataSize * 2);
    ReadFile(file,buffer,header->dataSize,&bytes_read,0);

    CloseHandle(file);
    return buffer;
}

static void* g_cl_context;
static void* g_cl_command_queue;
static void* g_cl_program;

#define SFX_GENSIZE 0x40000

static int16 g_sfx_output[SFX_GENSIZE * 2];

static void sfx(void){
    void* cl_inference_sfx = clCreateKernel(g_cl_program,"inferenceBruteForceSfx",0);
    WavHeader sfx_header;
    int16* sfx_data = loadWavFile("sfx/test.wav",&sfx_header);
  
    int weights[] = {
        FIXED_ONE * 6,
        FIXED_ONE * 5,
        FIXED_ONE * 4,
        FIXED_ONE * 3,
        FIXED_ONE * 2,
        FIXED_ONE,
        FIXED_ONE,
        FIXED_ONE * 2,
        FIXED_ONE * 3,
        FIXED_ONE * 4,
        FIXED_ONE * 5,
        FIXED_ONE * 6,

        FIXED_ONE * 6,
        FIXED_ONE * 5,
        FIXED_ONE * 4,
        FIXED_ONE * 3,
        FIXED_ONE * 2,
        FIXED_ONE,
        FIXED_ONE,
        FIXED_ONE * 2,
        FIXED_ONE * 3,
        FIXED_ONE * 4,
        FIXED_ONE * 5,
        FIXED_ONE * 6,
    };

    for(int i = 0;i < 0x10;i++){
        for(int j = 0;j < countof(weights);j++){
            graph_sfx[i].weights_inverse[j] = fixedDivR(FIXED_ONE,weights[j]) >> 4;
        }
        graph_sfx[i].n_candidates_inference = 0x40;
        graph_sfx[i].n_search_inference = 4;
        graph_sfx[i].n_neighbours = 16;
        graph_sfx[i].n_difference_threshold = 0x100;
    }

    generateMipmapsSfx(sfx_data,sfx_header.dataSize);

    for(int i = 0;i < 0x10;i++)
        extractImageColorSfx(sfx_data,sfx_header.dataSize,i,0,(sfx_header.dataSize >> i));

    void* cl_graph_buffers[0x10] = {0};
    void* cl_vram_buffers;
    for(int i = 0;i < 0x10;i++){
        if(!graph_sfx[i].n_nodes)
            continue;
        cl_graph_buffers[i] = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,graph_sfx[i].n_nodes * sizeof(SfxGraphNode),0,0);
        clEnqueueWriteBuffer(g_cl_command_queue,cl_graph_buffers[i],true,0,graph_sfx[i].n_nodes * sizeof(SfxGraphNode),graph_sfx[i].entry,0,0,0);
    }

    cl_vram_buffers = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,SFX_GENSIZE * sizeof(int16) * 2,0,0);
    void* cl_indices = clCreateBuffer(g_cl_context,CL_MEM_WRITE_ONLY,SFX_GENSIZE * sizeof(int),0,0);

    for(int i = 0;i < countof(g_sfx_output);i++)
        g_sfx_output[i] = tRnd() & 0xFFFFFF;
    //loop{
    for(int i = 16;i--;){
        printNumberNL(i);
        static int indices[SFX_GENSIZE];
        int mipmap_size = SFX_GENSIZE >> i;
        int mipmap_offset = mipmapOffsetSfx(SFX_GENSIZE,i);
        generateSfxInit(g_sfx_output,indices,i,SFX_GENSIZE);
        int n_generate_sample = 1 << 18;
        if(mipmap_size < 4 || !cl_graph_buffers[i])
            continue;
            
        clSetKernelArg(cl_inference_sfx,0,sizeof(void*),cl_graph_buffers + i);
        clSetKernelArg(cl_inference_sfx,1,sizeof(void*),&cl_vram_buffers);
        clSetKernelArg(cl_inference_sfx,2,sizeof(void*),&cl_indices);
        clSetKernelArg(cl_inference_sfx,3,sizeof(int),&i);
            
        int i_size = SFX_GENSIZE;
        clSetKernelArg(cl_inference_sfx,4,sizeof(int),&i_size);
        int n_node = graph_sfx[i].n_nodes;
        clSetKernelArg(cl_inference_sfx,5,sizeof(int),&n_node);
        /*
            
        clSetKernelArg(cl_inference_bf,0,sizeof(void*),cl_graph_buffers + i);
        clSetKernelArg(cl_inference_bf,1,sizeof(void*),&cl_vram_buffers);
        clSetKernelArg(cl_inference_bf,2,sizeof(void*),&cl_indices);
        clSetKernelArg(cl_inference_bf,3,sizeof(int),&i);
            
        int i_size = GEN_SIZE;
        clSetKernelArg(cl_inference_bf,4,sizeof(int),&i_size);
        int n_node = graph_color[i].n_nodes;
        clSetKernelArg(cl_inference_bf,5,sizeof(int),&n_node);
        */
        int error5 = clEnqueueWriteBuffer(g_cl_command_queue,cl_vram_buffers,true,0,SFX_GENSIZE * sizeof(int16) * 2,g_sfx_output,0,0,0);
            
        size_t n_output = SFX_GENSIZE >> i;
        size_t n_local = 1;
        int error6 = clEnqueueWriteBuffer(g_cl_command_queue,cl_indices,true,0,SFX_GENSIZE * sizeof(int),indices,0,0,0);
        for(int n = 0;n < 8;n++){
            int err = clEnqueueNDRangeKernel(g_cl_command_queue,cl_inference_sfx,1,0,&n_output,0,0,0,0);
            int d2 = 0;
        }
    }
    int error7 = clEnqueueReadBuffer(g_cl_command_queue,cl_vram_buffers,true,0,SFX_GENSIZE * sizeof(int16) * 2,g_sfx_output,0,0,0);
    int d = 0;
    sfx_header.dataSize = SFX_GENSIZE;
    void* output_wav = CreateFileA("output.wav",GENERIC_WRITE,0,0,CREATE_ALWAYS,0,0);
    WriteFile(output_wav,&sfx_header,sizeof(WavHeader),0,0);
    WriteFile(output_wav,g_sfx_output,sfx_header.dataSize,0,0);
    CloseHandle(output_wav);
    ExitProcess(0);
}

int main(void){
    initLibOpenCL();

    void* cl_file = CreateFileA("gpgpu.cl",GENERIC_READ,0,0,3,0,0);
    char* cl_source = tMalloc(GetFileSize(cl_file,0) + 1);
    ReadFile(cl_file,cl_source,GetFileSize(cl_file,0),0,0);
    cl_source[GetFileSize(cl_file,0)] = 0;
    CloseHandle(cl_file);

    void* platforms[20];
    void* devices[20];
    unsigned n_platform = 0;
    unsigned n_device = 0;

    clGetPlatformIDs(20,platforms,&n_platform);
    clGetDeviceIDs(platforms[0],CL_DEVICE_TYPE_GPU,1,devices,&n_device);
    g_cl_context       = clCreateContext(0,1,(const void**)&devices[0],0,0,0);
    g_cl_command_queue = clCreateCommandQueueWithProperties(g_cl_context,devices[0],0,0);
    g_cl_program       = clCreateProgramWithSource(g_cl_context,1,(const char**)&cl_source,0,0);
    clBuildProgram(g_cl_program,0,0,"-Werror",0,0);

    void* cl_inference     = clCreateKernel(g_cl_program,"inference",0);
    void* cl_inference_bf  = clCreateKernel(g_cl_program,"inferenceBruteForce",0);

    void* geurt = LoadImageA(0,"img/Ground_04.bmp",IMAGE_BITMAP,0,0,LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    
    //sfx();

    Bitmap bmp;
    GetObjectA(geurt,sizeof(Bitmap),&bmp);
    BitmapInfo bi = {0};
    int image_size = bmp.bmWidth;
    bi.bmi_header.size = sizeof(BitmapInfoHeader);
    bi.bmi_header.width = image_size;
    bi.bmi_header.height = image_size;
    bi.bmi_header.planes = 1;
    bi.bmi_header.bit_count = 32;
    unsigned* geurt_data = tMalloc(image_size * image_size * sizeof(unsigned));
    GetDIBits(GetDC(0),geurt,0,image_size,geurt_data,&bi,0);

    WavHeader sfx_header;
    uint8* sfx_data = loadWavFile("sfx/test.wav",&sfx_header);
  
    int weights[] = {
        FIXED_ONE,FIXED_ONE,FIXED_ONE,FIXED_ONE,
        FIXED_ONE + FIXED_ONE / 10 * 2,FIXED_ONE + FIXED_ONE / 10 * 2,FIXED_ONE + FIXED_ONE / 10 * 2,FIXED_ONE + FIXED_ONE / 10 * 2,
        FIXED_ONE,FIXED_ONE,FIXED_ONE,FIXED_ONE,
        FIXED_ONE + FIXED_ONE / 10 * 2,FIXED_ONE + FIXED_ONE / 10 * 2,FIXED_ONE + FIXED_ONE / 10 * 2,FIXED_ONE + FIXED_ONE / 10 * 2,
    };

    int weights_inverse[countof(weights) * 3];
    for(int i = 0;i < 0x10;i++){
        graph_color[i].entry = tMallocZero(sizeof(GraphNode) * 0x40000 >> i * 2);
        for(int j = 0;j < countof(weights);j++){
            for(int c = 0;c < 3;c++){
                graph_color[i].weights_inverse[j * 3 + c] = fixedDivR(FIXED_ONE,weights[j]) >> 4;
                weights_inverse[j * 3 + c] = graph_color[i].weights_inverse[j * 3 + c];
            }
        }
        graph_color[i].n_candidates_inference = 16;
        graph_color[i].n_search_inference = 16;
        graph_color[i].n_neighbours = 16;
        graph_color[i].n_difference_threshold = 0x400;
    }

	g_windowclass.wnd_proc = windowMessageHandler;
	g_windowclass.class_name = "a_class";

	RegisterClassA(&g_windowclass);

	g_window = CreateWindowExA(0,"a_class","markov",WS_VISIBLE | WS_SYSMENU | WS_MINIMIZEBOX,20,20,WINDOW_SIZE_X,WINDOW_SIZE_Y,0,0,g_windowclass.instance,0);

    Rect client;
    GetClientRect(g_window,&client);
    SetWindowPos(g_window,0,150,50,WINDOW_SIZE_X + (WINDOW_SIZE_X - client.right),WINDOW_SIZE_Y + (WINDOW_SIZE_Y - client.bottom),0);

    g_context = GetDC(g_window);

    int64 t = __rdtsc();

    for(int i = 0;i < image_size * image_size;i++){
        int x = i / image_size * FIXED_ONE * 2 / image_size - FIXED_ONE;
        int y = i % image_size * FIXED_ONE * 2 / image_size - FIXED_ONE;
        Vec3 color_grass_dark = pixelColorToColor(0x787878);
        Vec3 color_dirt = pixelColorToColor(0x705020);
        Vec3 color_grass = pixelColorToColor(0x40A040);
        /*
        color_grass_dark.x += tRnd() % 0x4000;
        color_grass_dark.y += tRnd() % 0x20000;
        color_grass_dark.z += tRnd() % 0x4000;
        
        color_dirt.x += tRnd() % 0x10000;
        color_dirt.y += tRnd() % 0x10000;
        color_dirt.z += tRnd() % 0x10000;

        color_grass.x += tRnd() % 0x8000;
        color_grass.y += tRnd() % 0x40000;
        color_grass.z += tRnd() % 0x8000;
        */
        int w = tClamp((tCos((fixedMulR(x,x) + fixedMulR(y,y)) * 4) + FIXED_ONE) / 2,0,FIXED_ONE);
        w = fixedMulR(w,w);
        w *= 2;
        Vec3 luminance;
        if(w > FIXED_ONE)
            luminance = vec3Mix(color_grass_dark,color_dirt,w - FIXED_ONE);
        else
            luminance = vec3Mix(color_grass,color_grass_dark,w);
        image[i] = colorToPixelColor(luminance);
        image[i] = geurt_data[i];
    }

    generateMipmaps(&(Texture){.data = image,.width = image_size,.height = image_size});

    for(int i = 0;i < 0x10;i++)
        extractImageColor(image,image_size,image_size,i,0,(image_size >> i) * (image_size >> i));
    /*
    for(GraphNode* node = graph_color->entry;node;node = node->next){
        for(int i = 0;i < 0x10;i++){
            if(!node->neighbours[i])
                continue;
            int is_symetrical = false;
            for(int j = 0;j < 0x10;j++){
                if(node->neighbours[i]->neighbours[j] == node)
                    is_symetrical = true;
            }
            if(!is_symetrical){
                int dummy = 0;
            }
        }
    }
    */
    
    int n_neighbours = 0;

    for(int i = 0;i < graph_color[0].n_nodes;i++){
        bool connections = false; 
        for(int j = 0;j < 0x10;j++){
            if(graph_color[0].entry[i].neighbours[j] == -1)
                continue;
            connections = true;
            bool bidirectional = false;

            for(int k = 0;k < 0x10;k++){
                if(graph_color[0].entry[graph_color[0].entry[i].neighbours[j]].neighbours[k] == i){
                    bidirectional = true;
                }
            }

            if(!bidirectional){
                //print("not bidirectional\n");
            }
            n_neighbours += 1;
        }
        if(!connections){
            //print("not connected\n");
        }
    }
    
    print("nodes = ");
    printNumberNL(graph_color[0].n_nodes);
    print("neigbours = ");
    printNumberNL(n_neighbours);
    print("avg neigbours = ");
    printNumberNL(n_neighbours / graph_color[0].n_nodes);
    
    for(int i = 0;i < image_size * image_size * 2;i++)
        g_luminance_color[i] = tRnd() & 0xFFFFFF;
    
    for(int i = 0;i < 0x10;i++){
        graph_color[i].n_candidates_inference = 16;
        graph_color[i].n_search_inference = 16;
        graph_color[i].n_neighbours = 16;
    }

    /*
    for(int i = 6;i--;)
        generateColor(i,image_size);
    */

    int texture_size = 1 << 7;
    Texture texture = {.data = tMallocZero(texture_size * texture_size * sizeof(int) * 2),.width = texture_size,.height = texture_size};

    for(int i = 0;i < texture_size * texture_size;i++){
        int x = i / texture_size;
        int y = i % texture_size;
        texture.data[i] = geurt_data[x * image_size + y];
    }

    generateMipmaps(&texture);

    BitmapInfo bitmapinfo = {
        .bmi_header = {
            .size = sizeof(BitmapInfo),
            .width = VRAM_SIZE,
            .height = VRAM_SIZE,
            .planes = 1,
            .bit_count = 32,
        },
    };
    //return 0;
    printNumberNL(__rdtsc() - t >> 24);

    void* cl_graph_buffers[0x10] = {0};
    void* cl_vram_buffers;
    for(int i = 0;i < 0x10;i++){
        if(!graph_color[i].n_nodes)
            continue;
        cl_graph_buffers[i] = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,graph_color[i].n_nodes * sizeof(GraphNode),0,0);
        clEnqueueWriteBuffer(g_cl_command_queue,cl_graph_buffers[i],true,0,graph_color[i].n_nodes * sizeof(GraphNode),graph_color[i].entry,0,0,0);
    }

    cl_vram_buffers = clCreateBuffer(g_cl_context,CL_MEM_READ_WRITE,GEN_SIZE * GEN_SIZE * sizeof(int) * 2,0,0);
    void* cl_indices = clCreateBuffer(g_cl_context,CL_MEM_WRITE_ONLY,GEN_SIZE * GEN_SIZE * sizeof(int),0,0);

    for(int i = 0;i < GEN_SIZE * GEN_SIZE * 2;i++)
        g_luminance_color[i] = tRnd() & 0xFFFFFF;
    loop{
    for(int i = 16;i--;){
        printNumberNL(i);
        static int indices[GEN_SIZE * GEN_SIZE];
        int mipmap_size = GEN_SIZE >> i;
        int mipmap_offset = mipmapOffset(GEN_SIZE,GEN_SIZE,i);
        generateColorInitLayer(g_luminance_color,indices,i,GEN_SIZE,FIXED_ONE / 100 * 2);
        int n_generate_sample = 1 << 18;
        if(mipmap_size < 4 || !cl_graph_buffers[i])
            continue;
        
        if(mipmap_size >= 256){
            
            clSetKernelArg(cl_inference,0,sizeof(void*),cl_graph_buffers + i);
            clSetKernelArg(cl_inference,1,sizeof(void*),&cl_vram_buffers);
            clSetKernelArg(cl_inference,2,sizeof(void*),&cl_indices);
            clSetKernelArg(cl_inference,3,sizeof(int),&i);
            
            int quality = 16;
            clSetKernelArg(cl_inference,4,sizeof(int),&quality);
            int i_size = GEN_SIZE;
            clSetKernelArg(cl_inference,5,sizeof(int),&i_size);
            int n_node = graph_color[i].n_nodes;
            clSetKernelArg(cl_inference,6,sizeof(int),&n_node);
            
            /*
            clSetKernelArg(cl_inference_bf,0,sizeof(void*),cl_graph_buffers + i);
            clSetKernelArg(cl_inference_bf,1,sizeof(void*),&cl_vram_buffers);
            clSetKernelArg(cl_inference_bf,2,sizeof(void*),&cl_indices);
            clSetKernelArg(cl_inference_bf,3,sizeof(int),&i);
            
            int i_size = GEN_SIZE;
            clSetKernelArg(cl_inference_bf,4,sizeof(int),&i_size);
            int n_node = graph_color[i].n_nodes;
            clSetKernelArg(cl_inference_bf,5,sizeof(int),&n_node);
            */
            int error5 = clEnqueueWriteBuffer(g_cl_command_queue,cl_vram_buffers,true,0,GEN_SIZE * GEN_SIZE * sizeof(int) * 2,g_luminance_color,0,0,0);
            
            size_t n_output = (GEN_SIZE >> i) * (GEN_SIZE >> i);
            size_t n_local = 1;
            clEnqueueWriteBuffer(g_cl_command_queue,cl_indices,true,0,GEN_SIZE * GEN_SIZE * sizeof(int),indices,0,0,0);
            for(int n = 0;n < tMin(1 << i * 2,256);n++){
                clEnqueueNDRangeKernel(g_cl_command_queue,cl_inference,1,0,&n_output,0,0,0,0);
                int error7 = clEnqueueReadBuffer(g_cl_command_queue,cl_vram_buffers,true,0,GEN_SIZE * GEN_SIZE * sizeof(int) * 2,g_luminance_color,0,0,0);
                
                vramDraw(i);
                while(PeekMessageA(&g_msg,g_window,0,0,0)){
                    GetMessageA(&g_msg,g_window,0,0);
                    DispatchMessageA(&g_msg);
                }
                StretchDIBits(g_context,0,0,WINDOW_SIZE_X,WINDOW_SIZE_Y,0,0,(1 << 10),VRAM_SIZE,g_vram,&bitmapinfo,0,0x00CC0020);
            }
            continue;
        }
        
        for(int j = 0;j < mipmap_size * mipmap_size;j += n_generate_sample){
            int amount = n_generate_sample;
			if(mipmap_size * mipmap_size < n_generate_sample)
				amount = mipmap_size * mipmap_size;
            for(int n = 0;n < tMin(1 << i * 2,256);n++)
                generateColor(g_luminance_color,indices,j,amount,i,GEN_SIZE,false);
            vramDraw(i);
            while(PeekMessageA(&g_msg,g_window,0,0,0)){
                GetMessageA(&g_msg,g_window,0,0);
                DispatchMessageA(&g_msg);
            }
            StretchDIBits(g_context,0,0,WINDOW_SIZE_X,WINDOW_SIZE_Y,0,0,VRAM_SIZE,VRAM_SIZE,g_vram,&bitmapinfo,0,0x00CC0020);
        }
        
        //Sleep(100);
        /*
        for(int j = 0;j < mipmap_size * mipmap_size;j += n_generate_sample){
            int amount = n_generate_sample;
			if(mipmap_size * mipmap_size < n_generate_sample)
				amount = mipmap_size * mipmap_size;
            
            //generateColor(g_luminance_color,indices + j,amount,i,VRAM_SIZE);
            for(int j = 0;j < VRAM_SIZE * VRAM_SIZE;j++){ 
                int x = j / VRAM_SIZE * mipmap_size / VRAM_SIZE;
                int y = j % VRAM_SIZE * mipmap_size / VRAM_SIZE;

                g_vram[j] = g_luminance_color[mipmap_offset + x * mipmap_size + y];
            }
            while(PeekMessageA(&g_msg,g_window,0,0,0)){
                GetMessageA(&g_msg,g_window,0,0);
                DispatchMessageA(&g_msg);
            }
            StretchDIBits(g_context,0,0,WINDOW_SIZE_X,WINDOW_SIZE_Y,0,0,VRAM_SIZE,VRAM_SIZE,g_vram,&bitmapinfo,0,0x00CC0020);
        }
        */
    }
    }

    loop{
        Point cursor;
        static Point cursor_prev;
        static int zoom;
        GetCursorPos(&cursor);
        ScreenToClient(g_window,&cursor);
        if((GetKeyState(VK_LBUTTON) & 0x80) && GetForegroundWindow() == g_window){
            camera_x += (cursor.y - cursor_prev.y) * FIXED_ONE;
            camera_y -= (cursor.x - cursor_prev.x) * FIXED_ONE;
            vramDraw(0);
        }

        zoom = camera_zoom;
        cursor_prev = cursor;
        /*
        for(int i = 8;i--;){
            generateDownExtend(i,VRAM_SIZE,texture);
            for(int k = 0;k < 1;k++){
                generateColorExtend(i,VRAM_SIZE,texture);
                for(int j = 0;j < VRAM_SIZE * VRAM_SIZE;j++){
                    int mipmap_size = VRAM_SIZE >> i;
                    int x = j / VRAM_SIZE * mipmap_size / VRAM_SIZE;
                    int y = j % VRAM_SIZE * mipmap_size / VRAM_SIZE;

                    g_vram[j] = g_luminance_color[mipmapOffset(VRAM_SIZE,i) + x * mipmap_size + y];
                }
                while(PeekMessageA(&g_msg,g_window,0,0,0)){
                    GetMessageA(&g_msg,g_window,0,0);
                    DispatchMessageA(&g_msg);
                }
                StretchDIBits(g_context,0,0,WINDOW_SIZE_X,WINDOW_SIZE_Y,0,0,VRAM_SIZE,VRAM_SIZE,g_vram,&bitmapinfo,0,0x00CC0020);
            }
        }
        */
        StretchDIBits(g_context,0,0,WINDOW_SIZE_X,WINDOW_SIZE_Y,0,0,VRAM_SIZE,VRAM_SIZE,g_vram,&bitmapinfo,0,0x00CC0020);
        while(PeekMessageA(&g_msg,g_window,0,0,0)){
            GetMessageA(&g_msg,g_window,0,0);
            DispatchMessageA(&g_msg);
        }
        Sleep(1);
    }
}