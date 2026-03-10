#ifndef IMAGE_H
#define IMAGE_H

#include "langext.h"

uint8* pngEncode(uint8 *pixels,int stride_bytes,int x,int y,int n,int *out_len);
uint8* pngDecode(uint8 *buffer,int len,int *x,int *y,int *comp,int req_comp);

#endif