#ifndef GDI_H
#define GDI_H

#include "langext.h"

#if __GNUC__
#define DLLIMPORT(RETURN) __attribute__((dllimport,stdcall)) RETURN
#else
#define DLLIMPORT(RETURN) _declspec(dllimport) RETURN stdcall
#endif

#define CBM_INIT 0x04

#define PFD_DRAW_TO_WINDOW 0x00000004
#define PFD_SUPPORT_OPENGL 0x00000020
#define PFD_DOUBLEBUFFER 0x00000001
#define PFD_TYPE_RGBA 0
#define PFD_MAIN_PLANE 0

#define PS_SOLID 0
#define PS_NULL 5

typedef struct{
    int x;
    int y;
} Point2;

typedef struct{
    uint16 size;
    uint16 version;
    uint32 flags;
    uint8 pixel_type;
    uint8 color_bits;
    uint8 red_bits;
    uint8 red_shift;
    uint8 green_bits;
    uint8 green_shift;
    uint8 blue_bits;
    uint8 blue_shift;
    uint8 alpha_bits;
    uint8 alpha_shift;
    uint8 accum_bits;
    uint8 accum_red_bits;
    uint8 accum_green_bits;
    uint8 accum_blue_bits;
    uint8 accum_alpha_bits;
    uint8 depth_bits;
    uint8 stencil_bits;
    uint8 aux_buffers;
    uint8 layer_type;
    uint8 reserved;
    uint32 layer_mask;
    uint32 visible_mask;
    uint32 damage_mask;
} PixelFormatDescriptor;

typedef struct{
    uint8 blue;
    uint8 green;
    uint8 red;
    uint8 reserved;
} RgbQuad;

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

typedef struct{
    BitmapInfoHeader bmi_header;
    RgbQuad bmi_colors[1];
} BitmapInfo;

typedef struct{
    long bmType;
    long bmWidth;
    long bmHeight;
    long bmWidthBytes;
    uint16 bmPlanes;
    uint16 bmBitsPixel;
    void* bmBits;
} Bitmap;

#ifdef __cplusplus
extern "C"{
#endif
DLLIMPORT(int) TextOutA(void* dc,int x,int y,const char* string,int c);
DLLIMPORT(int) GetDIBits(void* dc,Bitmap* hbm,unsigned start,unsigned cLines,void* lpvBits,BitmapInfo* lpbmi,unsigned usage);
DLLIMPORT(int) StretchDIBits(
    void*   hdc,
    int x_dest,
    int y_dest,
    int dest_width,
    int dest_height,
    int x_src,
    int y_src,
    int src_width,
    int src_height,
    void* bits,
    BitmapInfo* bmi,
    unsigned usage,
    unsigned rop
);
DLLIMPORT(int) StretchBlt(
    void* dc_dest,
    int x_dest,
    int y_dest,
    int w_dest,
    int h_dest,
    void* dc_src,
    int x_src,
    int y_src,
    int w_src,
    int h_src,
    unsigned rop
);
DLLIMPORT(void*) CreateBitmap(int width,int height,unsigned planes,unsigned bit_count,void* bits);
DLLIMPORT(void*) CreateDIBitmap(void* dc,BitmapInfoHeader* pbmih,unsigned flInit,void* bits,BitmapInfo* bmi,unsigned usage);
DLLIMPORT(int) ChoosePixelFormat(void* dc,PixelFormatDescriptor* pfd);
DLLIMPORT(int) DescribePixelFormat(void* dc,int pixel_format,unsigned n_bytes,PixelFormatDescriptor* pfd);
DLLIMPORT(int) SetPixelFormat(void* dc,int format,PixelFormatDescriptor* pfd);
DLLIMPORT(int) SwapBuffers(void* device_context);
DLLIMPORT(int) MoveToEx(void* dc,int x,int y,Point2* previous_point);
DLLIMPORT(int) LineTo(void* dc,int x,int y);
DLLIMPORT(void*) CreatePen(int style,int width,unsigned color);
DLLIMPORT(void*) SelectObject(void* dc,void* object);
DLLIMPORT(int) DeleteObject(void* object);
DLLIMPORT(void*) CreateSolidBrush(unsigned color);
DLLIMPORT(int) Ellipse(void* dc,int left,int top,int right,int bottom);
DLLIMPORT(int) Polygon(void* dc,Point2* point,int n_point);
DLLIMPORT(void*) CreateCompatibleDC(void* dc);
DLLIMPORT(void*) CreateCompatibleBitmap(void* dc,int cx,int cy);
DLLIMPORT(int) DeleteDC(void* dc);
#ifdef __cplusplus
}
#endif
#endif
