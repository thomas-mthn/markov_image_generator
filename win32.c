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
#include "image.h"

#define FD_SETSIZE 64
#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128

#define MSG_PEEK    0x02
#define MSG_WAITALL 0x08

structure(FD_Set){
    unsigned fd_count;
    size_t fd_array[FD_SETSIZE];
};

structure(TimeVal){
    long tv_sec;
    long tv_usec;
};

structure(SockAddr){
    uint16  sa_family;
    char    sa_data[14];
};

structure(WSA_Data){
    uint16             version;
    uint16             version_high;
#if defined(_WIN64) || !defined(__i386__)
    uint16     max_sockets;
    uint16     max_udp_dgram;
    char*              vendor_info;
    char               description[WSADESCRIPTION_LEN+1];
    char               system_status[WSASYS_STATUS_LEN+1];
#else
    char               description[WSADESCRIPTION_LEN+1];
    char               system_status[WSASYS_STATUS_LEN+1];
    uint16     max_sockets;
    uint16     max_udp_dgram;
    char*              vendor_info;
#endif
};

enumeration(SocketType){
    SOCK_STREAM = 1,
    SOCK_DGRAM,
    SOCK_RAW,
};

#define AF_INET 2
#define INADDR_ANY 0

int (stdcall *WsaStartup)(uint16 version_required,WSA_Data* WSA_data);
size_t (stdcall *Socket)(int af,int type,int protocol);
int (stdcall *Connect)(size_t socket,const SockAddr* name,int namelen);
int (stdcall *Send)(size_t socket,const char* buf,int len,int flags);
int (stdcall *Recv)(size_t socket,char* buf,int len,int flags);
int (stdcall *Bind)(size_t socket,const SockAddr* name,int namelen);
int (stdcall *Listen)(size_t socket,int backlog);
size_t (stdcall *Accept)(size_t socket,SockAddr *addr,int* addrlen);
int (stdcall *IoCtlSocket)(size_t socket,long cmd,unsigned* argp);
int (stdcall *Select)(int nfds,FD_Set* readfds,FD_Set* writefds,FD_Set* exceptfds,const TimeVal* timeout);
int (stdcall *CloseSocket)(size_t socket);
int (stdcall *SetSockOpt)(size_t socket,int level,int optname,const char* optval,int optlen);
int (stdcall *WsaGetLastError)(void);

structure(String){
    size_t size;
    char* data;
};

static String stringMake(const char* cstring){
    char* copy = cstring;
    while(*cstring++);
    size_t len = cstring - copy;
    return (String){.data = copy,.size = len};
}

#define STRING_LITERAL(s) ((String){.size = sizeof(s) - 1,.data = (char*)(s)})

static char buffer[0x1000000];
static char* buffer_ptr = buffer;

static void* serverMalloc(int size){
    if(buffer_ptr + size > buffer + sizeof(buffer))
        buffer_ptr = buffer;
    char* memory = buffer_ptr;
    buffer_ptr += size;
    return memory;
}

static void print(String str){
    if(!GetConsoleWindow())
    	AllocConsole();
    WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),str.data,str.size,0,0);
}

static String numberToString(int number){
    int content_length = 0;
    bool negative = false;
	int number_copy;

    if(number < 0){
        content_length += 1;
        negative = true;
        number = -number;
    }
	number_copy = number;

    if(!number)
        content_length = 1;
    else{
        while(number_copy){
            number_copy /= 10;
            content_length++;
        }
    }

    String string = {.data = serverMalloc(content_length),.size = content_length};

    while(content_length--){
        string.data[content_length] = number % 10 + '0';
        number /= 10;
    }
    if(negative)
        string.data[0] = '-';

    return string;
}

static void printNumber(int number){
    if(!GetConsoleWindow())
        AllocConsole();

    String string = numberToString(number);

	WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE),string.data,string.size,0,0);
}

static void printNumberNL(int number){
    printNumber(number);
    print(stringMake("\n"));
}

static void loadInternetFunctions(void){
	void* lib = LoadLibraryA("Ws2_32");
	*(FarProc*)&WsaStartup =  GetProcAddress(lib,"WSAStartup");
	*(FarProc*)&Socket =      GetProcAddress(lib,"socket");
	*(FarProc*)&Connect =     GetProcAddress(lib,"connect");
	*(FarProc*)&Send =        GetProcAddress(lib,"send");
	*(FarProc*)&Recv =        GetProcAddress(lib,"recv");
	*(FarProc*)&Bind =        GetProcAddress(lib,"bind");
	*(FarProc*)&Listen =      GetProcAddress(lib,"listen");
	*(FarProc*)&Accept =      GetProcAddress(lib,"accept");
	*(FarProc*)&IoCtlSocket = GetProcAddress(lib,"ioctlsocket");
	*(FarProc*)&Select =      GetProcAddress(lib,"select");
    *(FarProc*)&CloseSocket = GetProcAddress(lib,"closesocket");
    *(FarProc*)&SetSockOpt =  GetProcAddress(lib,"setsockopt");
    *(FarProc*)&WsaGetLastError = GetProcAddress(lib,"WSAGetLastError");
}

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

#define SERVER_PORT 80
#define SOMAXCONN 5

static String readFile(char* name){
    void* file = CreateFileA(name,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
    if(file == (void*)-1)
        return (String){0};

    int size = GetFileSize(file,0);
    String string = {.data = serverMalloc(size),.size = size};
    ReadFile(file,string.data,string.size,0,0);
    CloseHandle(file);
    return string;
}

static char source_folder[] = "C:/Users/admin/source/repos/markov_imagegen_fixed/markov_imagegen_fixed/";
static char source_default[] = "main.html";

static bool stringCompareSizeInsensitive(String string,String compare){
    if(string.size < compare.size)
        return false;
    repeat(compare.size){
        if(*string.data++ != *compare.data++)
            return false;
    }
    return true;
}

static String stringInString(String string_1,String string_2){
    for(int i = 0;i <= string_1.size - string_2.size;i++){
        if(stringCompareSizeInsensitive((String){.data = string_1.data + i,.size = string_1.size - i},string_2))
            return (String){.data = string_1.data + i,.size = string_1.size - i};
    }
    return (String){.data = nullptr,.size = 0};
}

#define IMAGE_SIZE_MAX (1 << 11)

static int image[IMAGE_SIZE_MAX * IMAGE_SIZE_MAX * 2];
static int g_indices[IMAGE_SIZE_MAX * IMAGE_SIZE_MAX];

static void httpSendOk(size_t client){
    char http_ok[] = "HTTP/1.1 200 OK\r\n\r\n";
    Send(client,http_ok,sizeof http_ok,0);
}

static void httpSendServiceUnavailable(size_t client){
    char http_su[] = "HTTP/1.1 503 Service Unavailable\r\n\r\n";
    Send(client,http_su,sizeof http_su,0);
}

static void httpSendUnprocessableContent(size_t client){
    char http_su[] = "HTTP/1.1 422 Unprocessable Content\r\n\r\n";
    Send(client,http_su,sizeof http_su,0);
}

#define SOL_SOCKET 0xffff

#define SO_SNDTIMEO 0x1005
#define SO_RCVTIMEO 0x1006

#define REQUEST_BUFFER_SIZE 0x1000000

bool is_occupied;

static String httpContentGet(String request_buffer){
    String content = stringInString(request_buffer,STRING_LITERAL("\r\n\r\n"));
    content.data += 4;
    content.size -= 4;
    return content;
}

static bool httpContentReceive(size_t client,String* request_buffer){
    char* content_length_string = stringInString(*request_buffer,STRING_LITERAL("Content-Length:")).data;
    content_length_string += 16;
    int content_length = 0;
    while(*content_length_string >= '0' && *content_length_string <= '9'){
        content_length *= 10;
        content_length += *content_length_string - '0';
        content_length_string += 1;
    }
    String content = httpContentGet(*request_buffer);
    if(content.size != content_length){
        int n_bytes_recv = 0;
        while(n_bytes_recv < content_length){
            int amount = Recv(client,request_buffer->data + request_buffer->size,content_length - n_bytes_recv,0);
            request_buffer->size += amount;
            n_bytes_recv += amount;
            if(amount < 0){
                if(amount != 0){
                    print(STRING_LITERAL("error code = "));
                    printNumberNL(WsaGetLastError());
                }
                return false;
            }

            print(STRING_LITERAL("bytes recieved so far: "));
            printNumberNL(n_bytes_recv);
        }
    }
    print(STRING_LITERAL("received\n"));
    return true;
}

static unsigned stdcall clientHandler(void* arg){
    unsigned timeout_recv = 5000;
    unsigned timeout_send = 5000;
    char* request_buffer = tMalloc(REQUEST_BUFFER_SIZE);
    size_t client = *(size_t*)arg;
    print(STRING_LITERAL("create"));
    printNumberNL(client);
    SetSockOpt(client,SOL_SOCKET,SO_RCVTIMEO,(const char*)&timeout_recv,sizeof timeout_recv);
    SetSockOpt(client,SOL_SOCKET,SO_SNDTIMEO,(const char*)&timeout_send,sizeof timeout_send);
    int total = 0;
    while(total < REQUEST_BUFFER_SIZE){
        int n = Recv(client,request_buffer + total,REQUEST_BUFFER_SIZE - total,0);
        if(n <= 0) 
            goto connection_closed;
        total += n;
        String g = stringInString((String){.data = request_buffer,.size = total},STRING_LITERAL("\r\n\r\n"));
        if(total >= 4 && stringInString((String){.data = request_buffer,.size = total},STRING_LITERAL("\r\n\r\n")).data) 
            break;
    }
    String request_string = (String){.data = request_buffer,.size = total};

    if(stringCompareSizeInsensitive(request_string,STRING_LITERAL("GET "))){
        int request_length = 0;
        char* sub_path = request_buffer + 5;
        while(*sub_path++ != ' ')
            request_length += 1;
        char* path;
        if(!request_length)
            path = serverMalloc((sizeof(source_folder) - 1) + (sizeof(source_default) - 1) + 1);
        else
            path = serverMalloc((sizeof(source_folder) - 1) + request_length + 1);
            
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
        String data = readFile(path);
        if(!data.data){
            char http_not_found[] = "HTTP/1.1 403 Not Found\r\n";
            char* response = serverMalloc(256);
            char* response_begin = response;
            for(int i = 0;i < countof(http_not_found) - 1;i++)
                *response++ = http_not_found[i];
            *response++ = '\r';
            *response++ = '\n';
            *response++ = '\r';
            *response++ = '\n';
            Send(client,response_begin,response - response_begin,0);
        }
        else{
            char http_ok[] = "HTTP/1.1 200 OK\r\n";
            char content_length_string[] = "Content-Length: ";
            char* response = serverMalloc(data.size + 256);
            char* response_begin = response;
            for(int i = 0;i < countof(http_ok) - 1;i++)
                *response++ = http_ok[i];
            for(int i = 0;i < countof(content_length_string) - 1;i++)
                *response++ = content_length_string[i];

            String content_length = numberToString(data.size);
            for(int i = 0;i < content_length.size;i++)
                *response++ = content_length.data[i];
            *response++ = '\r';
            *response++ = '\n';
            *response++ = '\r';
            *response++ = '\n';
            for(int i = 0;i < data.size;i++)
                *response++ = data.data[i];
            Send(client,response_begin,response - response_begin,0);
        }
    }
    if(stringCompareSizeInsensitive(request_string,STRING_LITERAL("TRAIN "))){
        if(!httpContentReceive(client,&request_string))
            goto connection_closed;

        String content = httpContentGet(request_string);

        for(int i = 0;i < 0x10;i++){
            if(!graph_color[i].entry)
                graph_color[i].entry = tMalloc(sizeof(GraphNode) * 0x40000 >> i * 2);

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
        uint8* image = pngDecode(content.data,content.size,&width,&height,&image_components,0);
        if(!image){
            void* file = CreateFileA(content.data,GENERIC_READ,0,0,OPEN_EXISTING,0,0);
            if(file == (void*)-1){
                print(STRING_LITERAL("incorrect image file\n"));
                goto connection_closed;
            }
            int size = GetFileSize(file,0);
            int* png_data = serverMalloc(size);
            ReadFile(file,png_data,size,0,0);
            CloseHandle(file);
            image = pngDecode(png_data,size,&width,&height,&image_components,0);
            if(!image){
                print(STRING_LITERAL("incorrect image file\n"));
                goto connection_closed;
            }
        }
        if(width * height > 0x10000 || width <= 0 || width > IMAGE_SIZE_MAX || height <= 0 || height > IMAGE_SIZE_MAX){
            print(STRING_LITERAL("incorrect image size\n"));
            httpSendUnprocessableContent(client);
            goto connection_closed;
        }
        int* image_layered = tMalloc(height * width * sizeof(int) * 2);
        for(int j = 0;j < width * height;j++)
            image_layered[j] = ((int*)image)[j];
        tFreeT(image);
        Texture texture = {.data = (int*)image_layered,.width = width,.height = height};
        generateMipmaps(&texture);

        for(int i = 0x10;i--;){
            extractImageColor((int*)image_layered,width,height,i,0,(width >> i) * (height >> i));
            print(STRING_LITERAL("mipmap: "));
            printNumber(i);
            print(STRING_LITERAL("\n"));
        }
        tFree(image_layered);
        httpSendOk(client);
        /*
        while(
        loop{
            int n_bytes_recv = Recv(client,request_buffer,sizeof(request_buffer),0);
            request_buffer[n_bytes_recv] = 0;
            printNumber(n_bytes_recv);
        }
        */
    }
    if(stringCompareSizeInsensitive(request_string,STRING_LITERAL("TRAIN_DELETE "))){
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
    if(stringCompareSizeInsensitive(request_string,STRING_LITERAL("REMIX "))){
        if(!httpContentReceive(client,&request_string))
            goto connection_closed;

        String content = httpContentGet(request_string);

        struct{
            int width;
            int height;
            int mipmap;
            int start;
        } *meta_data = (void*)content.data;
        int* data = (void*)(content.data + sizeof(meta_data));
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
            goto connection_closed;
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
        for(int i = 0;i < 2;i++)
            generateColor(image,g_indices,0,mipmap_size,meta_data->mipmap,meta_data->width,meta_data->height,false); 
                 
        int* image_data = serverMalloc(mipmap_size * 4);
        int mipmap_offset = mipmapOffset(meta_data->width,meta_data->height,meta_data->mipmap);

        for(int i = 0;i < mipmap_size;i++){
            image_data[i] = 0;
            image_data[i] |= (image[mipmap_offset + i] >> 0  & 0xFF) << 16;
            image_data[i] |= (image[mipmap_offset + i] >> 8  & 0xFF) << 8;
            image_data[i] |= (image[mipmap_offset + i] >> 16 & 0xFF) << 0;
        }

        BitmapFileHeader header = {
            header.bfType = 0x4D42,
            header.bfSize = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + mipmap_size * 4,
            header.bfOffBits = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader)
        };
        BitmapInfoHeader header_info = {
            .size = sizeof header_info,
            .width = mipmap_width,
            .height = -mipmap_height,
            .planes = 1,
            .bit_count = 32,
            .size_image = mipmap_size * 4,
        };
        char http_ok[] = "HTTP/1.1 200 OK\r\n";
        char content_length_string[] = "Content-Length: ";
        int content_length = sizeof(header) + sizeof(header_info) + mipmap_size * 4;
        char* response = serverMalloc(256);
        char* response_begin = response;
        for(int i = 0;i < countof(http_ok) - 1;i++)
            *response++ = http_ok[i];
        for(int i = 0;i < countof(content_length_string) - 1;i++)
            *response++ = content_length_string[i];

        String content_length_string2 = numberToString(content_length_string);
        for(int i = 0;i < content_length_string2.size;i++)
            *response++ = content_length_string2.data[i];
        *response++ = '\r';
        *response++ = '\n';
        *response++ = '\r';
        *response++ = '\n';

        Send(client,response_begin,response - response_begin,0);
        Send(client,(const char*)&header,sizeof header,0);
        Send(client,(const char*)&header_info,sizeof header_info,0);
        Send(client,(const char*)image_data,mipmap_size * 4,0);
    }
    if(stringCompareSizeInsensitive(request_string,STRING_LITERAL("OUTPAINT "))){
        if(!httpContentReceive(client,&request_string))
            goto connection_closed;

        String content = httpContentGet(request_string);

        struct{
            int width;
            int height;
            int mipmap;
            int start;
        } *meta_data = (void*)content.data;
        int* data = (void*)(content.data + sizeof(meta_data));
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
            goto connection_closed;
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
        for(int i = 0;i < 2;i++)
            generateColor(image,g_indices,0,mipmap_size,meta_data->mipmap,width,height,true); 
                 
        int* image_data = serverMalloc(mipmap_size * 4);
        int mipmap_offset = mipmapOffset(width,height,meta_data->mipmap);

        for(int i = 0;i < mipmap_size;i++){
            image_data[i] = 0;
            image_data[i] |= (image[mipmap_offset + i] >> 0  & 0xFF) << 16;
            image_data[i] |= (image[mipmap_offset + i] >> 8  & 0xFF) << 8;
            image_data[i] |= (image[mipmap_offset + i] >> 16 & 0xFF) << 0;
        }
        BitmapFileHeader header = {
            header.bfType = 0x4D42,
            header.bfSize = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + mipmap_size * 4,
            header.bfOffBits = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader)
        };
        BitmapInfoHeader header_info = {
            .size = sizeof header_info,
            .width = mipmap_width,
            .height = -mipmap_height,
            .planes = 1,
            .bit_count = 32,
            .size_image = mipmap_size,
        };
        char http_ok[] = "HTTP/1.1 200 OK\r\n";
        char content_length_string[] = "Content-Length: ";
        int content_length = sizeof(header) + sizeof(header_info) + mipmap_size * 4;
        char* response = serverMalloc(256);
        char* response_begin = response;
        for(int i = 0;i < countof(http_ok) - 1;i++)
            *response++ = http_ok[i];
        for(int i = 0;i < countof(content_length_string) - 1;i++)
            *response++ = content_length_string[i];

        String content_length_string2 = numberToString(content_length_string);
        for(int i = 0;i < content_length_string2.size;i++)
            *response++ = content_length_string2.data[i];
        *response++ = '\r';
        *response++ = '\n';
        *response++ = '\r';
        *response++ = '\n';

        Send(client,response_begin,response - response_begin,0);
        Send(client,(const char*)&header,sizeof header,0);
        Send(client,(const char*)&header_info,sizeof header_info,0);
        Send(client,(const char*)image_data,mipmap_size * 4,0);
    }
    if(stringCompareSizeInsensitive(request_string,STRING_LITERAL("GENERATE "))){
        if(!graph_color[0].entry){
            httpSendServiceUnavailable(client);
            goto connection_closed;
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

        int* image_data = serverMalloc(mipmap_size * mipmap_size * 4);

        if(meta_data->start)
            generateColorInit(image,meta_data->mipmap + 1,meta_data->size,meta_data->size);
            
        int mipmap_offset = mipmapOffset(meta_data->size,meta_data->size,meta_data->mipmap);
            
        generateColorInitLayer(image,g_indices,meta_data->mipmap,meta_data->size,meta_data->size);

        for(int i = 0;i < 2;i++)
            generateColor(image,g_indices,0,mipmap_size * mipmap_size,meta_data->mipmap,meta_data->size,meta_data->size,false); 

        for(int i = 0;i < mipmap_size * mipmap_size;i++)
            image_data[i] = image[mipmap_offset + i] | 0x80000000;

        int out_length;
        uint8* png = pngEncode((uint8*)image_data,mipmap_size * 4,mipmap_size,mipmap_size,4,&out_length);

        BitmapFileHeader header = {
            header.bfType = 0x4D42,
            header.bfSize = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + mipmap_size * mipmap_size * 4,
            header.bfOffBits = sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader)
        };
        BitmapInfoHeader header_info = {
            .size = sizeof header_info,
            .width = mipmap_size,
            .height = mipmap_size,
            .planes = 1,
            .bit_count = 32,
            .size_image = mipmap_size * mipmap_size,
        };
        char http_ok[] = "HTTP/1.1 200 OK\r\n";
        char http_content_type[] = "Content-Type: image/png\r\n";
        char content_length_string[] = "Content-Length: ";
        //int content_length = sizeof(header) + sizeof(header_info) + mipmap_size * mipmap_size * 4;
        char* response = serverMalloc(256);
        char* response_begin = response;
        for(int i = 0;i < countof(http_ok) - 1;i++)
            *response++ = http_ok[i];
        
        for(int i = 0;i < countof(http_content_type) - 1;i++)
            *response++ = http_content_type[i];
            
        for(int i = 0;i < countof(content_length_string) - 1;i++)
            *response++ = content_length_string[i];

        String content_length = numberToString(content_length_string);
        for(int i = 0;i < content_length.size;i++)
            *response++ = content_length.data[i];
        *response++ = '\r';
        *response++ = '\n';
        *response++ = '\r';
        *response++ = '\n';

        Send(client,response_begin,response - response_begin,0);
        Send(client,png,out_length,0);

        tFreeT(png);
        /*
        Send(client,response_begin,response - response_begin,0);
        Send(client,(const char*)&header,sizeof header,0);
        Send(client,(const char*)&header_info,sizeof header_info,0);
        Send(client,(const char*)image_data,mipmap_size * mipmap_size * 4,0);
        */
    }
connection_closed:
    CloseSocket(client);
    tFree(request_buffer);
    print(STRING_LITERAL("destroy"));
    printNumberNL(client);
    return 0;
}

int main(void){
    loadInternetFunctions();

    SystemInfo system_info;
	GetSystemInfo(&system_info);
	n_threads = tClamp(system_info.number_of_processors - 1,1,0x40);

    WSA_Data wsa_data;
    WsaStartup(2 | 2 << 8,&wsa_data);
    SockAddr server_address;
    size_t server_socket = Socket(AF_INET,SOCK_STREAM,0);
    internetAddressCreate(&server_address,AF_INET,endianChange(SERVER_PORT),INADDR_ANY);
    Bind(server_socket,&server_address,sizeof(SockAddr));
    Listen(server_socket,SOMAXCONN);

    loop{
        size_t* client = serverMalloc(sizeof(size_t));
        *client = Accept(server_socket,(SockAddr*)INADDR_ANY,0);

        CreateThread(0,0,clientHandler,client,0,0);
    }
}
