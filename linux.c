#include <sys/socket.h>
#include <pthread.h>

#include "linux.h"
#include "main.h"
#include "model_audio.h"
#include "langext.h"
#include "syscall.h"
#include "library.h"
#include "libc.h"
#include "opencl.h"
#include "print.h"
#include "storage.h"

typedef struct{
    unsigned size;
    int  width;
    int  height;
    uint16 planes;
    uint16 bit_count;
    unsigned compression;
    unsigned size_image;
    int  xpels_per_meter;
    int  ypels_per_meter;
    unsigned clr_used;
    unsigned clr_important;
} BitmapInfoHeader;

#pragma pack(push, 1) 
typedef struct{
  uint16 bfType;
  unsigned bfSize;
  uint16  bfReserved1;
  uint16  bfReserved2;
  unsigned bfOffBits;
} BitmapFileHeader;
#pragma pack(pop)

#define INADDR_ANY 0
#define SERVER_PORT 80

structure(SockAddr){
    uint16  sa_family;
    char    sa_data[14];
};

#define STRING_LITERAL(s) ((String){.size = sizeof(s) - 1,.data = (char*)(s)})

static void internetAddressCreate(SockAddr* addr,int family,int port,unsigned ip){
    ((uint16*)addr)[0] = family;
    ((uint16*)addr)[1] = port;
    ((unsigned*)addr)[1] = ip;
}

static uint16 endianChange(uint16 v){
    uint16 temp = ((uint8*)&v)[0];
    ((uint8*)&v)[0] = ((uint8*)&v)[1];
    ((uint8*)&v)[1] = temp;
    return v;
}

#define REQUEST_BUFFER_SIZE 0x1000000

bool is_occupied;

#define IMAGE_SIZE_MAX (1 << 11)

static int image[IMAGE_SIZE_MAX * IMAGE_SIZE_MAX * 2];
static int g_indices[IMAGE_SIZE_MAX * IMAGE_SIZE_MAX];

static char source_folder[] = "/home/gravechest/code_projects/markov_image/";
static char source_default[] = "main.html";

static void httpSendOk(size_t client){
    char http_ok[] = "HTTP/1.1 200 OK\r\n\r\n";
    send(client,http_ok,sizeof http_ok,0);
}

static void httpSendServiceUnavailable(size_t client){
    char http_su[] = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
    send(client,http_su,sizeof http_su,0);
}

static void httpSendUnprocessableContent(size_t client){
    char http_su[] = "HTTP/1.1 422 Unprocessable Content\r\n\r\n";
    send(client,http_su,sizeof http_su,0);
}

static String httpContentGet(String request_buffer){
    String content = stringInString(request_buffer,STRING_LITERAL("\r\n\r\n"));
    content.data += 4;
    content.size -= 4;
    return content;
}

static bool httpContentReceive(size_t client,String* request_buffer){
    char* content_length_string = stringInString(*request_buffer,STRING_LITERAL("Content-Length:")).data;
    if(!content_length_string)
        return false;
    content_length_string += 16;
    int content_length = 0;
    while(*content_length_string >= '0' && *content_length_string <= '9'){
        content_length *= 10;
        content_length += *content_length_string - '0';
        content_length_string += 1;
    }
    String content = httpContentGet(*request_buffer);
    if(content.size != content_length){
        int n_bytes_recv = content.size;
        while(n_bytes_recv < content_length){
            int amount = recv(client,request_buffer->data + request_buffer->size,content_length - n_bytes_recv,0);
            request_buffer->size += amount;
            n_bytes_recv += amount;
            if(amount <= 0){
                if(amount != 0){
                    print(STRING_LITERAL("error"));
                }
                return false;
            }
            
            print(STRING_LITERAL("bytes recieved so far: "));
            printNumberNL(n_bytes_recv);
            print(STRING_LITERAL("bytes to recieve: "));
            printNumberNL(content_length);
        }
    }
    print(STRING_LITERAL("received\n"));
    return true;
}

structure(PNG_Image){
   void* opaque;    
   uint32  version;   
   uint32  width;     /* Image width in pixels (columns) */
   uint32  height;    /* Image height in pixels (rows) */
   uint32  format;    /* Image format as defined below */
   uint32  flags;     /* A bit mask containing informational flags */
   uint32  colormap_entries;
   uint32  warning_or_error;

   char         message[64];
};

#define PNG_IMAGE_VERSION 1
#define PNG_FORMAT_RGBA 3

static int (*png_image_begin_read_from_memory)(PNG_Image* image,void* png_data,size_t png_size);
static void (*png_image_free)(PNG_Image* image);
static int (*png_image_finish_read)(PNG_Image* image,void* background,void* buffer,int stride,void* color_map);
static int (*png_image_write_to_memory)(PNG_Image* image,void* memory,size_t* memory_bytes,int convert_to_8_bit,void* buffer,int stride,void* color_map);

static void libPNG_Init(void){
    void* lib = libraryLoad("libpng.so");
    if(!lib){
        print(STRING_LITERAL("could not load png libary"));
        return;
    }
    struct{
        char* name;
        funcptr_t* fn_ptr;
    } functions[] =  {
        {.name = "png_image_begin_read_from_memory",.fn_ptr = (funcptr_t*)&png_image_begin_read_from_memory},
        {.name = "png_image_free",.fn_ptr = (funcptr_t*)&png_image_free},
        {.name = "png_image_finish_read",.fn_ptr = (funcptr_t*)&png_image_finish_read},
        {.name = "png_image_write_to_memory",.fn_ptr = (funcptr_t*)&png_image_write_to_memory},
    };
    for(int i = countof(functions);i--;){
        *functions[i].fn_ptr = libraryFunctionLoad(lib,functions[i].name);
        if(!*functions[i].fn_ptr){
            print(STRING_LITERAL("could not load png libary functions"));
            return;
        }
    }
}

static char* pngDecode(void *png_data,size_t png_size,unsigned* out_width,unsigned* out_height){
    PNG_Image image = {
        .version = PNG_IMAGE_VERSION,
        .format = PNG_FORMAT_RGBA
    };
    
    if(!png_image_begin_read_from_memory(&image,png_data,png_size)) 
        return 0;

    char* out_pixels = tMalloc(image.width * image.height * 4);
    if(!out_pixels){
        png_image_free(&image);
        return 0;
    }

    if(!png_image_finish_read(&image,0,out_pixels,0,0)){
        tFree(out_pixels);
        png_image_free(&image);
        return 0;
    }
    *out_width  = image.width;
    *out_height = image.height;

    return out_pixels;
}

static char* pngEncode(char* image_data,int height,int width,int* png_size){
    PNG_Image image = {
        .version = PNG_IMAGE_VERSION,
        .width = width,
        .height = height,
        .format = PNG_FORMAT_RGBA,
    };
    size_t memory_size = 0;
    if(!png_image_write_to_memory(&image,0,&memory_size,0,image_data,0,0))
       return 0;
    char* png_data = tMalloc(memory_size);
    if(!png_image_write_to_memory(&image,png_data,&memory_size,0,image_data,0,0))
        return 0;

    *png_size = memory_size;
    
    return png_data;
}

static void pngSend(MemoryArena* arena,int client,char* image_data,int image_width,int image_height){
    int out_length;
    uint8* png = (uint8*)pngEncode(image_data,image_height,image_width,&out_length);

    char http_ok[] = "HTTP/1.1 200 OK\r\n";
    char http_content_type[] = "Content-Type: image/png\r\n";
    char content_length_string[] = "Content-Length: ";

    char* response = memoryArenaAllocate(arena,256);
    char* response_begin = response;
    for(int i = 0;i < countof(http_ok) - 1;i++)
        *response++ = http_ok[i];
        
    for(int i = 0;i < countof(http_content_type) - 1;i++)
        *response++ = http_content_type[i];
            
    for(int i = 0;i < countof(content_length_string) - 1;i++)
        *response++ = content_length_string[i];
    char buffer[0x10];
    String content_length = numberToString(buffer,out_length);
    for(int i = 0;i < content_length.size;i++)
        *response++ = content_length.data[i];
    *response++ = '\r';
    *response++ = '\n';
    *response++ = '\r';
    *response++ = '\n';

    send(client,response_begin,response - response_begin,0);
    send(client,png,out_length,0);

    tFree(png);
}

static void modelInvoke(int width,int height,int mipmap){
    for(int i = 0;i < 4;i++){
        if(width * height < 0x10000)
            generateColor(image,g_indices,0,width * height,mipmap,width,height,false,true); 
        else
            clBruteForce(image,g_indices,mipmap,width,height,false);
    }
}

static void requestTrain(int client,char* request_buffer,MemoryArena* client_arena,int total){
    String request_string = (String){.data = request_buffer,.size = total};
    if(!httpContentReceive(client,&request_string))
        return;

    String content = httpContentGet(request_string);

    for(int i = 0;i < 0x10;i++){
        if(!graph_color[i].entry)
            graph_color[i].entry = tMalloc(sizeof(GraphNode) * 0x4000000 >> i * 2);

        for(int j = 0;j < countof(weights);j++){
            for(int c = 0;c < 3;c++)
                graph_color[i].weights_inverse[j * 3 + c] = fixedDivR(FIXED_ONE,weights[j]) >> 4;
        }
        graph_color[i].n_candidates_inference = 0x100;
        graph_color[i].n_search_inference = 16;
        graph_color[i].n_difference_threshold = 0x100;
        graph_color[i].n_neighbours = 0x10;
    }
    int height;
    int width;
    int image_components;
    uint8* image = pngDecode(content.data,content.size,&width,&height);
    if(!image){
        int file = systemOpen(content.data,0,0);
        if(file == -1){
            print(STRING_LITERAL("incorrect image file\n"));
            return;
        }
        KernelStat stat;
        systemFileStat(file,&stat);
        int size = stat.st_size;
        int* png_data = memoryArenaAllocate(client_arena,size);
        systemRead(file,png_data,size);
        systemClose(file);
        image = pngDecode((uint8*)png_data,size,&width,&height);
        if(!image){
            print(STRING_LITERAL("incorrect image file\n"));
            return;
        }
    }
    if(width <= 0 || width > IMAGE_SIZE_MAX || height <= 0 || height > IMAGE_SIZE_MAX){
        print(STRING_LITERAL("incorrect image size\n"));
        httpSendUnprocessableContent(client);
        return;
    }
    int* image_layered = memoryArenaAllocate(client_arena,height * width * sizeof(int) * 2);
    for(int j = 0;j < width * height;j++)
        image_layered[j] = ((int*)image)[j];
    tFree(image);
    Texture texture = {.data = (int*)image_layered,.width = width,.height = height};
    generateMipmaps(&texture);

    for(int i = 0x10;i--;){
        extractImageColor((int*)image_layered,width,height,i,0,(width >> i) * (height >> i));
        print(STRING_LITERAL("mipmap: "));
        printNumber(i);
        print(STRING_LITERAL("\n"));
    }
    httpSendOk(client);
}

static void requestGet(int client,char* request_buffer,MemoryArena* client_arena,int total){
    int request_length = 0;
    char* sub_path = request_buffer + 5;
    while(*sub_path++ != ' ')
        request_length += 1;
    char* path;
    if(!request_length)
        path = memoryArenaAllocate(client_arena,(sizeof(source_folder) - 1) + (sizeof(source_default) - 1) + 1);
    else
        path = memoryArenaAllocate(client_arena,(sizeof(source_folder) - 1) + request_length + 1);
            
    for(int i = 0;i < (sizeof(source_folder) - 1);i++)
        path[i] = source_folder[i];

    if(!request_length){
        request_length = (sizeof(source_default) - 1);
        for(int i = 0;i < (sizeof(source_default) - 1);i++)
            path[(sizeof(source_folder) - 1) + i] = source_default[i];
    }
    else{
        for(int i = 0;i < request_length;i++)
            path[(sizeof(source_folder) - 1) + i] = request_buffer[5 + i];
    }
            
    path[(sizeof(source_folder) - 1) + request_length] = 0;

    FileContent content = storageFileRead(client_arena,path);
    
    String data = {.data = content.content,.size = content.size};
    print(STRING_LITERAL("GET: "));
    print((String){.data = path,.size = request_length + (sizeof(source_folder) - 1)});
    print(STRING_LITERAL("\n"));
    if(!data.data){
        char http_not_found[] = "HTTP/1.1 403 Not Found\r\n";
        char* response = memoryArenaAllocate(client_arena,256);
        char* response_begin = response;
        for(int i = 0;i < countof(http_not_found) - 1;i++)
            *response++ = http_not_found[i];
        *response++ = '\r';
        *response++ = '\n';
        *response++ = '\r';
        *response++ = '\n';
        send(client,response_begin,response - response_begin,0);
    }
    else{
        char http_ok[] = "HTTP/1.1 200 OK\r\n";
        char content_length_string[] = "Content-Length: ";
        char* response = memoryArenaAllocate(client_arena,data.size + 256);
        char* response_begin = response;
        for(int i = 0;i < countof(http_ok) - 1;i++)
            *response++ = http_ok[i];
        for(int i = 0;i < countof(content_length_string) - 1;i++)
            *response++ = content_length_string[i];

        char buffer[0x10];
        String content_length = numberToString(buffer,data.size);
        for(int i = 0;i < content_length.size;i++)
            *response++ = content_length.data[i];
        *response++ = '\r';
        *response++ = '\n';
        *response++ = '\r';
        *response++ = '\n';
        for(int i = 0;i < data.size;i++)
            *response++ = data.data[i];
        send(client,response_begin,response - response_begin,0);
    }
}

static void requestTrainDelete(int client,char* request_buffer,MemoryArena* client_arena,int total){
    for(int i = 0;i < 0x10;i++){
        tFree(graph_color[i].entry);
        graph_color[i].entry = 0;
        graph_color[i].n_nodes = 0;
        graph_color[i].n_color_randrange = 0;
        for(int j = 0;j < countof(graph_color[i].color_randindex);j++)
            graph_color[i].color_randindex[j] = 0;
        for(int j = 0;j < countof(graph_color[i].n_color_distribution);j++)
            graph_color[i].n_color_distribution[j] = 0;
    }
    httpSendOk(client);
}

static void requestRemix(int client,char* request_buffer,MemoryArena* client_arena,int total){
    String request_string = (String){.data = request_buffer,.size = total};
    if(!httpContentReceive(client,&request_string))
        return;

    String content = httpContentGet(request_string);

    struct{
        int width;
        int height;
        int mipmap;
        int start;
    } *meta_data = (void*)content.data;
    int* data = (void*)(content.data + sizeof(*meta_data));
    print(STRING_LITERAL("width: "));
    printNumber(meta_data->width);
    print(STRING_LITERAL(" height: "));
    printNumber(meta_data->height);
    print(STRING_LITERAL(" mipmap: "));
    printNumber(meta_data->mipmap);
    print(STRING_LITERAL(" start: "));
    print(meta_data->start ? STRING_LITERAL("true\n") : STRING_LITERAL("false\n"));
    if(meta_data->width < 0 || meta_data->width >= IMAGE_SIZE_MAX || meta_data->height < 0 || meta_data->height >= IMAGE_SIZE_MAX){
        print(STRING_LITERAL("incorrect image size\n"));
        return;
    }  
    if(meta_data->start){
        for(int i = 0;i < meta_data->width * meta_data->height;i++)
            image[i] = data[i];

        generateMipmaps(&(Texture){.width = meta_data->width,.height = meta_data->height,.data = image});
        generateColorInit(image,meta_data->mipmap + 1,meta_data->width,meta_data->height);
    }
                
    int mipmap_width  = meta_data->width >> meta_data->mipmap;
    int mipmap_height = meta_data->height >> meta_data->mipmap;
    int mipmap_size = mipmap_width * mipmap_height;
                
    generateColorInitLayer(image,g_indices,meta_data->mipmap,meta_data->width,meta_data->height);
    modelInvoke(meta_data->width,meta_data->height,meta_data->mipmap);
                 
    int* image_data = memoryArenaAllocate(client_arena,mipmap_size * sizeof(*image_data));
    int mipmap_offset = mipmapOffset(meta_data->width,meta_data->height,meta_data->mipmap);

    for(int i = 0;i < mipmap_size;i++){
        image_data[i] = 0x80000000;
        image_data[i] |= (image[mipmap_offset + i] >> 16 & 0xFF) << 16;
        image_data[i] |= (image[mipmap_offset + i] >> 8  & 0xFF) << 8;
        image_data[i] |= (image[mipmap_offset + i] >> 0  & 0xFF) << 0;
    }
    pngSend(client_arena,client,(void*)image_data,mipmap_width,mipmap_height);
}

static void requestOutpaint(int client,char* request_buffer,MemoryArena* client_arena,int total){
    String request_string = (String){.data = request_buffer,.size = total};
    if(!httpContentReceive(client,&request_string))
        return;

    String content = httpContentGet(request_string);

    struct{
        int width;
        int height;
        int mipmap;
        int start;
    } *meta_data = (void*)content.data;
    int* data = (void*)(content.data + sizeof(*meta_data));
    print(STRING_LITERAL("width: "));
    printNumber(meta_data->width);
    print(STRING_LITERAL(" height: "));
    printNumber(meta_data->height);
    print(STRING_LITERAL(" mipmap: "));
    printNumber(meta_data->mipmap);
    print(STRING_LITERAL(" start: "));
    print(meta_data->start ? STRING_LITERAL("true\n") : STRING_LITERAL("false\n"));
    if(meta_data->width < 0 || meta_data->width >= IMAGE_SIZE_MAX || meta_data->height < 0 || meta_data->height >= IMAGE_SIZE_MAX){
        print(STRING_LITERAL("incorrect image size\n"));
        return;
    }
    int width = meta_data->width * 2;
    int height = meta_data->height * 2;     
    if(meta_data->start){
        for(int i = 0;i < width * height;i++)
            image[i] = 0;

        for(int i = 0;i < meta_data->width * meta_data->height;i++){
            int x = i / meta_data->width + meta_data->width / 2;
            int y = i % meta_data->width + meta_data->width / 2;
            image[x * width + y] = data[i] | 0x01000000;
        }
        generateMipmaps(&(Texture){.width = width,.height = height,.data = image});
        generateColorInit(image,meta_data->mipmap + 1,width,height);
    }
                
    int mipmap_width  = width >> meta_data->mipmap;
    int mipmap_height = height >> meta_data->mipmap;
    int mipmap_size = mipmap_width * mipmap_height;
                
    generateColorInitLayer(image,g_indices,meta_data->mipmap,width,height);
    modelInvoke(width,height,meta_data->mipmap);

    int* image_data = memoryArenaAllocate(client_arena,mipmap_size * sizeof(*image_data));
    int mipmap_offset = mipmapOffset(width,height,meta_data->mipmap);

    for(int i = 0;i < mipmap_size;i++){
        image_data[i] = 0x80000000;
        image_data[i] |= (image[mipmap_offset + i] >> 16 & 0xFF) << 16;
        image_data[i] |= (image[mipmap_offset + i] >> 8  & 0xFF) << 8;
        image_data[i] |= (image[mipmap_offset + i] >> 0  & 0xFF) << 0;
    }

    pngSend(client_arena,client,(void*)image_data,mipmap_width,mipmap_height);
}

static void requestGenerate(int client,char* request_buffer,MemoryArena* client_arena,int total){
    String request_string = (String){.data = request_buffer,.size = total};
    if(!graph_color[0].entry){
        httpSendServiceUnavailable(client);
        return;
    }
    String content = httpContentGet(request_string);
    struct{
        int size;
        int quality;
        int mipmap;
        int start;
    } *meta_data = (void*)content.data;

    print(STRING_LITERAL("size: "));
    printNumber(meta_data->size);
    print(STRING_LITERAL(" quality: "));
    printNumber(meta_data->quality);
    print(STRING_LITERAL(" mipmap: "));
    printNumber(meta_data->mipmap);
    print(STRING_LITERAL(" start: "));
    print(meta_data->start ? STRING_LITERAL("true\n") : STRING_LITERAL("false\n"));
    for(int i = 0;i < 0x10;i++)
        graph_color[i].n_search_inference = meta_data->quality;

    int mipmap_size = meta_data->size >> meta_data->mipmap;

    int* image_data = memoryArenaAllocate(client_arena,mipmap_size * mipmap_size * sizeof(*image_data));

    if(meta_data->start)
        generateColorInit(image,meta_data->mipmap + 1,meta_data->size,meta_data->size);
            
    int mipmap_offset = mipmapOffset(meta_data->size,meta_data->size,meta_data->mipmap);

    generateColorInitLayer(image,g_indices,meta_data->mipmap,meta_data->size,meta_data->size);
    modelInvoke(meta_data->size,meta_data->size,meta_data->mipmap);

    for(int i = 0;i < mipmap_size * mipmap_size;i++)
        image_data[i] = image[mipmap_offset + i] | 0x80000000;
    pngSend(client_arena,client,(void*)image_data,mipmap_size,mipmap_size);
}

static int clientHandler(void* arg){
    MemoryArena client_arena = {0};
    unsigned timeout_recv = 5000;
    unsigned timeout_send = 5000;
    char* request_buffer = memoryArenaAllocate(&client_arena,REQUEST_BUFFER_SIZE);
    size_t client = *(size_t*)arg;
    setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout_recv,sizeof timeout_recv);
    setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout_send,sizeof timeout_send);
    int total = 0;
    while(total < REQUEST_BUFFER_SIZE){
        int n = recv(client,request_buffer + total,REQUEST_BUFFER_SIZE - total,0);
        if(n <= 0) 
            goto connection_closed;
        total += n;
        String g = stringInString((String){.data = request_buffer,.size = total},STRING_LITERAL("\r\n\r\n"));
        if(total >= 4 && stringInString((String){.data = request_buffer,.size = total},STRING_LITERAL("\r\n\r\n")).data) 
            break;
    }
    String request_string = (String){.data = request_buffer,.size = total};
    enum{
        REQUEST_GET,
        REQUEST_TRAIN,
        REQUEST_TRAIN_DELETE,
        REQUEST_REMIX,
        REQUEST_OUTPAINT,
        REQUEST_GENERATE,
    };

    struct{
        String name;
        void (*function)(int,char*,MemoryArena*,int);
    } request_table[] = {
        [REQUEST_GET] = {.name = STRING_LITERAL("GET "),.function = requestGet},
        [REQUEST_TRAIN] = {.name = STRING_LITERAL("TRAIN "),.function = requestTrain},
        [REQUEST_TRAIN_DELETE] = {.name = STRING_LITERAL("TRAIN_DELETE "),.function = requestTrainDelete},
        [REQUEST_REMIX] = {.name = STRING_LITERAL("REMIX "),.function = requestRemix},
        [REQUEST_OUTPAINT] = {.name = STRING_LITERAL("OUTPAINT "),.function = requestOutpaint},
        [REQUEST_GENERATE] = {.name = STRING_LITERAL("GENERATE "),.function = requestGenerate},
    };

    for(int i = countof(request_table);i--;){
        if(stringCompareSizeInsensitive(request_string,request_table[i].name)){
            request_table[i].function(client,request_buffer,&client_arena,total);
            break;
        }
    }
connection_closed:
    systemClose(client);
    memoryArenaFree(&client_arena);
    print(STRING_LITERAL("destroy: "));
    printNumberNL(client);
    return 0;
}
#if 0
#define SO_REUSEADDR 2
#endif

MemoryArena g_startup_arena;

structure(AudioFile){
    int size;
    int16* data;
};

static AudioFile wavLoad(char* path){
    FileContent content = storageFileRead(&g_startup_arena,path);
    String data = stringInString((String){.data = content.content,.size = content.size},(String)STRING_LITERAL("data"));
    data = stringForwardSlice(data,8);
    int length = data.size / sizeof(int16);
    AudioFile result = {.data = tMalloc(length * sizeof *result.data),.size = length};
    for(int j = length;j--;)
        result.data[j] = ((int16*)data.data)[j];
    return result;
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

int main(void){
    libPNG_Init();
    initLibOpenCL();
    SockAddr server_address;
    int server_socket = socket(AF_INET,SOCK_STREAM,0);
    internetAddressCreate(&server_address,AF_INET,endianChange(SERVER_PORT),INADDR_ANY);
    int opt = 1;
    setsockopt(server_socket,SOL_SOCKET,SO_REUSEPORT,&opt,sizeof opt);
    bind(server_socket,(struct sockaddr*)&server_address,sizeof(struct sockaddr));
    listen(server_socket,SOMAXCONN);
#if 0
    AudioFile sample = wavLoad("sfx/sample.wav");

    audioModelInit();

    int16* train = tMalloc(sample.size * 8);
    tMemcpy(train,sample.data,sample.size * 2);
    audioMipmaps16(train,sample.size);
    
    for(int i = 0x10;i--;){
        audioExtract(train,sample.size,i,0,sample.size >> i);
        print(STRING_LITERAL("mipmap: "));
        printNumber(i);
        print(STRING_LITERAL("\n"));
    }

    int gen_size = 0x100000;
    
    int* synth = tMalloc(gen_size * sizeof(*synth) * 2);

    audioInitGenerate(synth,10,gen_size);

    for(int i = 10;i--;){
        audioInitLayerGenerate(synth,g_indices,i,gen_size);
        for(int j = 0x10;j--;){
            printNumberNL(gen_size >> i);
#if 0
            if(gen_size >> i < 0x4000)
                audioGenerate(synth,g_indices,0,gen_size,i,gen_size,false,true);
            else
#endif
            clAudioBruteForce(synth,g_indices,i,gen_size,false);
        }
    }

    WavHeader* header = storageFileRead(&g_startup_arena,"sfx/sample.wav").content;
    header->dataSize = gen_size * 2;
    header->data[0] = 'd';
    header->data[1] = 'a';
    header->data[2] = 't';
    header->data[3] = 'a';

    for(int i = 0;i < gen_size;i++)
        ((int16*)synth)[i] = synth[i] & 0xFFFF;
    
    char* export = tMalloc(sizeof *header + gen_size * 2);
    tMemcpy(export,header,sizeof *header);
    tMemcpy(export + sizeof *header,synth,gen_size * 2);
    storageFileWrite(export,sizeof *header + gen_size * 2,"sfx/export.wav");
#endif
    memoryArenaFree(&g_startup_arena);
    
    for(;;){
        size_t* client = tMalloc(sizeof(size_t));
        *client = accept(server_socket,(struct sockaddr*)INADDR_ANY,0);
        pthread_t thread;
        pthread_create(&thread,0,(void*)clientHandler,client);
    }
}
