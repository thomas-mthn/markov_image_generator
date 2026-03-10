#include "image.h"
#include "memory.h"
#include "tmath.h"

static void* tMemset(void *s,int c,size_t n){
    uint8* p = s;
    while(n--)
        *p++ = (uint8)c;
    return s;
}

static void* tMemmove(void* dst,void* src,size_t n){
    uint8* d = (uint8*)dst;
    uint8* s = (uint8*)src;

    if(d == s || n == 0)
        return dst;

    if(d < s){
        for(size_t i = 0; i < n; i++)
            d[i] = s[i];
    } 
    else{
        for(size_t i = n;i != 0;i--)
            d[i - 1] = s[i - 1];
    }

    return dst;
}

static void* tMemcpy(void* dest,void* src,size_t n){
    uint8* d = dest;
    uint8* s = src;
    while(n--)
        *d++ = *s++;

    return dest;
}

static int stbi__flip_vertically_on_write = 0;
static int stbi_write_png_compression_level = 8;

#define STBIW_MEMMOVE(a,b,sz) tMemmove(a,b,sz)

static unsigned zHash(uint8 *data){
    unsigned hash = data[0] + (data[1] << 8) + (data[2] << 16);
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash;
}

#define ZHASH 16384

static uint8 paeth(int a,int b,int c){
    int p = a + b - c;
    int pa = tAbs(p - a);
    int pb = tAbs(p - b);
    int pc = tAbs(p - c);
    if(pa <= pb && pa <= pc) 
        return a;
    if(pb <= pc) 
        return b;
    return c;
}

static void pngLineEncode(uint8 *pixels,int stride_bytes,int width,int height,int y,int n,int filter_type,int8 *line_buffer){
    static int mapping[] = {0,1,2,3,4};
    static int firstmap[] = {0,1,0,5,6};
    int *mymap = (y != 0) ? mapping : firstmap;
    int type = mymap[filter_type];
    uint8 *z = pixels + stride_bytes * (stbi__flip_vertically_on_write ? height-1-y : y);
    int signed_stride = stbi__flip_vertically_on_write ? -stride_bytes : stride_bytes;

    if(!type){
        tMemcpy(line_buffer, z, width*n);
        return;
    }

    // first loop isn't optimized since it's just one pixel
    for(int i = 0;i < n;++i){
        switch(type){
            case 1: 
                line_buffer[i] = z[i]; 
                break;
            case 2: 
                line_buffer[i] = z[i] - z[i-signed_stride]; 
                break;
            case 3: 
                line_buffer[i] = z[i] - (z[i-signed_stride]>>1); 
                break;
            case 4: 
                line_buffer[i] = (int8) (z[i] - paeth(0,z[i-signed_stride],0)); 
                break;
            case 5: 
                line_buffer[i] = z[i]; 
                break;
            case 6: 
                line_buffer[i] = z[i];
                break;
        }
    }
    switch(type){
        case 1: 
            for(int i = n;i < width*n;++i) 
                line_buffer[i] = z[i] - z[i-n]; 
            break;
        case 2: 
            for(int i = n;i < width*n;++i) 
                line_buffer[i] = z[i] - z[i-signed_stride]; 
            break;
        case 3: 
            for(int i = n;i < width*n;++i) 
                line_buffer[i] = z[i] - ((z[i-n] + z[i-signed_stride])>>1); 
            break;
        case 4: 
            for(int i = n;i < width*n;++i) 
                line_buffer[i] = z[i] - paeth(z[i-n], z[i-signed_stride], z[i-signed_stride-n]); 
            break;
        case 5: 
            for(int i = n;i < width*n;++i) 
                line_buffer[i] = z[i] - (z[i-n]>>1); 
            break;
        case 6: 
            for(int i = n;i < width*n;++i) 
                line_buffer[i] = z[i] - paeth(z[i-n], 0,0); 
            break;
    }
}

#define STBIW_REALLOC(p,newsz) tReAlloc(p,newsz)
#define STBI_REALLOC_SIZED(p,oldsz,newsz) tReAlloc(p,newsz)

#define stbiw__sbraw(a) ((int *) (void *) (a) - 2)
#define stbiw__sbm(a)   stbiw__sbraw(a)[0]
#define stbiw__sbn(a)   stbiw__sbraw(a)[1]
#define stbiw__sbgrow(a,n)  sbGrowf((void **) &(a), (n), sizeof(*(a)))
#define stbiw__sbneedgrow(a,n)  ((a)==0 || stbiw__sbn(a)+n >= stbiw__sbm(a))
#define stbiw__sbmaybegrow(a,n) (stbiw__sbneedgrow(a,(n)) ? stbiw__sbgrow(a,n) : 0)
#define stbiw__sbpush(a, v)      (stbiw__sbmaybegrow(a,1), (a)[stbiw__sbn(a)++] = (v))
#define stbiw__sbcount(a)        ((a) ? stbiw__sbn(a) : 0)

#define STBIW_REALLOC_SIZED(p,oldsz,newsz) STBIW_REALLOC(p,newsz)

static void* sbGrowf(void **arr,int increment,int itemsize){
    int m = *arr ? 2 * stbiw__sbm(*arr) + increment : increment+1;
    void* p = STBIW_REALLOC_SIZED(*arr ? stbiw__sbraw(*arr) : 0,*arr ? (stbiw__sbm(*arr) * itemsize + sizeof(int)*2) : 0,itemsize * m + sizeof(int)*2);
    if(p){
        if(!*arr) 
            ((int*)p)[1] = 0;
        *arr = (void*)((int*)p + 2);
        stbiw__sbm(*arr) = m;
    }
    return *arr;
}

static uint8 *zlibFlushf(uint8 *data,unsigned *bitbuffer,int *bitcount){
   while(*bitcount >= 8){
      stbiw__sbpush(data,(uint8)*bitbuffer);
      *bitbuffer >>= 8;
      *bitcount -= 8;
   }
   return data;
}

#define stbiw__zlib_flush() (out = zlibFlushf(out, &bitbuf, &bitcount))

#define stbiw__zlib_add(code,codebits) \
      (bitbuf |= (code) << bitcount,bitcount += (codebits),stbiw__zlib_flush())

static unsigned zlibCountm(uint8 *a,uint8 *b,int limit){
    int i;
    for(i = 0;i < limit && i < 258;++i)
        if(a[i] != b[i]) 
            break;
    return i;
}

static int zlibBitrev(int code,int codebits){
    int res = 0;
    while(codebits--){
        res = (res << 1) | (code & 1);
        code >>= 1;
    }
    return res;
}

#define stbiw__zlib_huffa(b,c)  stbiw__zlib_add(zlibBitrev(b,c),c)
// default huffman tables
#define stbiw__zlib_huff1(n)  stbiw__zlib_huffa(0x30 + (n), 8)
#define stbiw__zlib_huff2(n)  stbiw__zlib_huffa(0x190 + (n)-144, 9)
#define stbiw__zlib_huff3(n)  stbiw__zlib_huffa(0 + (n)-256,7)
#define stbiw__zlib_huff4(n)  stbiw__zlib_huffa(0xc0 + (n)-280,8)
#define stbiw__zlib_huff(n)  ((n) <= 143 ? stbiw__zlib_huff1(n) : (n) <= 255 ? stbiw__zlib_huff2(n) : (n) <= 279 ? stbiw__zlib_huff3(n) : stbiw__zlib_huff4(n))
#define stbiw__zlib_huffb(n) ((n) <= 143 ? stbiw__zlib_huff1(n) : stbiw__zlib_huff2(n))

#define stbiw__sbfree(a)         ((a) ? tFreeT(stbiw__sbraw(a)),0 : 0)

static uint8* zlibCompress(uint8* data,int data_len,int *out_len,int quality){
    static uint16 lengthc[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258,259};
    static uint8  lengtheb[]= {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5, 0};
    static uint16 distc[]   = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577, 32768 };
    static uint8  disteb[]  = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
    unsigned bitbuf = 0;
    int bitcount = 0;
    uint8 *out = NULL;
    uint8 ***hash_table = tMallocT(ZHASH * sizeof(uint8**));
    if(hash_table == NULL)
        return NULL;
    if(quality < 5) 
        quality = 5;

    ((((out) == 0 || ((int*)(void*)(out)-2)[1] + (1) >= ((int*)(void*)(out)-2)[0]) ? sbGrowf((void**)&(out),(1),sizeof(*(out))) : 0),(out)[((int*)(void*)(out)-2)[1]++] = (0x78));   // DEFLATE 32K window
    stbiw__sbpush(out, 0x5e);   // FLEVEL = 1
    stbiw__zlib_add(1,1);  // BFINAL = 1
    stbiw__zlib_add(1,2);  // BTYPE = 1 -- fixed huffman

    for(int i = 0;i < ZHASH;++i)
        hash_table[i] = NULL;

    int i = 0;
    while(i < data_len-3){
        // hash next 3 bytes of data to be compressed
        int h = zHash(data+i)&(ZHASH-1), best=3;
        uint8 *bestloc = 0;
        uint8 **hlist = hash_table[h];
        int n = stbiw__sbcount(hlist);
        for(int j = 0;j < n;++j){
            if(hlist[j]-data > i-32768) { // if entry lies within window
            int d = zlibCountm(hlist[j], data+i, data_len-i);
            if(d >= best){ 
                best = d; 
                bestloc = hlist[j]; 
            }
            }
        }
        // when hash table entry is too long, delete half the entries
        if(hash_table[h] && stbiw__sbn(hash_table[h]) == 2 * quality){
            STBIW_MEMMOVE(hash_table[h], hash_table[h] + quality, sizeof(hash_table[h][0])*quality);
            stbiw__sbn(hash_table[h]) = quality;
        }
        stbiw__sbpush(hash_table[h],data+i);

        if(bestloc){
            // "lazy matching" - check match at *next* byte, and if it's better, do cur byte as literal
            h = zHash(data+i+1)&(ZHASH-1);
            hlist = hash_table[h];
            n = stbiw__sbcount(hlist);
            for(int j = 0;j < n;++j){
                if(hlist[j]-data > i-32767){
                    int e = zlibCountm(hlist[j],data + i + 1,data_len - i - 1);
                    if(e > best){ // if next match is better, bail on current match
                        bestloc = NULL;
                        break;
                    }
                }
            }
        }

        if(bestloc){
            int d = (int) (data+i - bestloc); // distance back
            int j;
            for(j = 0;best > lengthc[j + 1] - 1;++j);
            stbiw__zlib_huff(j+257);
            if (lengtheb[j]) 
                stbiw__zlib_add(best - lengthc[j], lengtheb[j]);
            for(j = 0;d > distc[j+1]-1;++j);
            stbiw__zlib_add(zlibBitrev(j,5),5);
            if (disteb[j]) 
                stbiw__zlib_add(d - distc[j], disteb[j]);
            i += best;
        } 
        else{
            stbiw__zlib_huffb(data[i]);
            ++i;
        }
    }
    // write out final bytes
    for (;i < data_len;++i)
        stbiw__zlib_huffb(data[i]);
    stbiw__zlib_huff(256); // end of block
    // pad with 0 bits to byte boundary
    while (bitcount)
        stbiw__zlib_add(0,1);

    for (i = 0;i < ZHASH;++i)
        (void) stbiw__sbfree(hash_table[i]);
    tFreeT(hash_table);

    // store uncompressed instead if compression was worse
    if (stbiw__sbn(out) > data_len + 2 + ((data_len+32766)/32767)*5) {
        stbiw__sbn(out) = 2;  // truncate to DEFLATE 32K window and FLEVEL = 1
        for (int j = 0;j < data_len;){
            int blocklen = data_len - j;
            if(blocklen > 32767) 
                blocklen = 32767;
            stbiw__sbpush(out, data_len - j == blocklen); // BFINAL = ?, BTYPE = 0 -- no compression
            stbiw__sbpush(out,(uint8)(blocklen)); // LEN
            stbiw__sbpush(out,(uint8)(blocklen >> 8));
            stbiw__sbpush(out,(uint8)(~blocklen)); // NLEN
            stbiw__sbpush(out,(uint8)(~blocklen >> 8));
            tMemcpy(out+stbiw__sbn(out), data+j, blocklen);
            stbiw__sbn(out) += blocklen;
            j += blocklen;
        }
    }

    {
        // compute adler32 on input
        unsigned s1 = 1,s2 = 0;
        int blocklen = (int)(data_len % 5552);
        int j = 0;
        while(j < data_len){
            for(i=0; i < blocklen;++i){ 
            s1 += data[j+i]; 
            s2 += s1; 
            }
            s1 %= 65521; s2 %= 65521;
            j += blocklen;
            blocklen = 5552;
        }
        stbiw__sbpush(out,(uint8)(s2 >> 8));
        stbiw__sbpush(out,(uint8)(s2));
        stbiw__sbpush(out,(uint8)(s1 >> 8));
        stbiw__sbpush(out,(uint8)(s1));
    }
    *out_len = stbiw__sbn(out);
    // make returned pointer freeable
    STBIW_MEMMOVE(stbiw__sbraw(out), out, *out_len);
    return (uint8 *) stbiw__sbraw(out);
}

#define stbiw__wpng4(o,a,b,c,d) ((o)[0]=(uint8)(a),(o)[1]=(uint8)(b),(o)[2]=(uint8)(c),(o)[3]=(uint8)(d),(o)+=4)
#define stbiw__wp32(data,v) stbiw__wpng4(data, (v)>>24,(v)>>16,(v)>>8,(v));
#define stbiw__wptag(data,s) stbiw__wpng4(data, s[0],s[1],s[2],s[3])

static unsigned crc32(uint8 *buffer,int len){
    static unsigned crc_table[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0eDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
        0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
        0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
        0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
        0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
        0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
        0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
        0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
        0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
        0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
        0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
        0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
        0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
        0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
        0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
        0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
        0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
        0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
        0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
        0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
        0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
        0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
        0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
        0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
        0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
        0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
    };

    unsigned crc = ~0u;

    for(int i=0; i < len;++i)
        crc = (crc >> 8) ^ crc_table[buffer[i] ^ (crc & 0xff)];
    return ~crc;
}

static void wpcrc(uint8 **data, int len){
    unsigned crc = crc32(*data - len - 4, len+4);
    stbiw__wp32(*data, crc);
}

uint8 *pngEncode(uint8 *pixels,int stride_bytes,int x,int y,int n,int *out_len){
    int force_filter = -1;
    int ctype[5] = {-1,0,4,2,6};
    uint8 sig[8] = {137,80,78,71,13,10,26,10};
    uint8 *out,*o, *filt, *zlib;
    int8 *line_buffer;
    int zlen;

    if(stride_bytes == 0)
        stride_bytes = x * n;

    if(force_filter >= 5)
        force_filter = -1;

    filt = tMallocT((x*n+1) * y); 
    if(!filt) 
        return 0;
    line_buffer = tMallocT(x * n); 
    if(!line_buffer){ 
        tFreeT(filt); 
        return 0; 
    }
    for(int j = 0;j < y;++j){
        int filter_type;
        if(force_filter > -1){
            filter_type = force_filter;
            pngLineEncode((uint8*)(pixels),stride_bytes,x,y,j,n,force_filter,line_buffer);
        } 
        else{ // Estimate the best filter by running through all of them:
            int best_filter = 0, best_filter_val = 0x7fffffff;
            for(filter_type = 0;filter_type < 5;filter_type++){
            pngLineEncode((uint8*)(pixels),stride_bytes,x,y,j,n,filter_type,line_buffer);

            // Estimate the entropy of the line using this filter; the less, the better.
            int est = 0;
            for(int i = 0;i < x * n;++i)
                est += tAbs((int8) line_buffer[i]);
            
            if(est < best_filter_val){
                best_filter_val = est;
                best_filter = filter_type;
            }
            }
            if(filter_type != best_filter){  // If the last iteration already got us the best filter, don't redo it
            pngLineEncode((uint8*)(pixels),stride_bytes,x,y,j,n,best_filter,line_buffer);
            filter_type = best_filter;
            }
        }
        // when we get here, filter_type contains the filter type, and line_buffer contains the data
        filt[j*(x*n+1)] = (uint8) filter_type;
        STBIW_MEMMOVE(filt+j*(x*n+1) + 1, line_buffer, x*n);
    }
    tFreeT(line_buffer);
    zlib = zlibCompress(filt,y * (x * n + 1),&zlen,stbi_write_png_compression_level);
    tFreeT(filt);
    if(!zlib) 
        return 0;

    // each tag requires 12 bytes of overhead
    out = tMallocT(8 + 12 + 13 + 12 + zlen + 12);
    if(!out) 
        return 0;
    *out_len = 8 + 12 + 13 + 12 + zlen + 12;

    o = out;
    STBIW_MEMMOVE(o,sig,8); 
    o += 8;
    stbiw__wp32(o,13); // header length
    stbiw__wptag(o,"IHDR");
    stbiw__wp32(o,x);
    stbiw__wp32(o,y);
    *o++ = 8;
    *o++ = (uint8)(ctype[n]);
    *o++ = 0;
    *o++ = 0;
    *o++ = 0;
    wpcrc(&o,13);

    stbiw__wp32(o, zlen);
    stbiw__wptag(o, "IDAT");
    STBIW_MEMMOVE(o, zlib, zlen);
    o += zlen;
    tFreeT(zlib);
    wpcrc(&o, zlen);

    stbiw__wp32(o,0);
    stbiw__wptag(o, "IEND");
    wpcrc(&o,0);

    return out;
}

structure(CallbacksIO){
    int  (*read)(void *user,char *data,int size);   // fill 'data' with 'size' bytes.  return number of bytes actually read
    void (*skip)(void *user,int n);                 // skip the next 'n' bytes, or 'unget' the last -n bytes if negative
    int  (*eof)(void *user);                       // returns nonzero if we are at end of file/data
};

structure(Context){
    uint32 img_x, img_y;
    int img_n, img_out_n;

    CallbacksIO io;
    void *io_user_data;

    int read_from_callbacks;
    int buflen;
    uint8 buffer_start[128];
    int callback_already_read;

    uint8 *img_buffer, *img_buffer_end;
    uint8 *img_buffer_original, *img_buffer_original_end;
};

structure(PngChunk){
    uint32 length;
    uint32 type;
};

#define STBI__ZFAST_BITS  9 // accelerate all cases in default tables
#define STBI__ZNSYMS 288 // number of symbols in literal/length alphabet

structure(ZHuffman){
   uint16 fast[1 << STBI__ZFAST_BITS];
   uint16 firstcode[16];
   int maxcode[17];
   uint16 firstsymbol[16];
   uint8  size[STBI__ZNSYMS];
   uint16 value[STBI__ZNSYMS];
};

structure(StbiZbuf){
   uint8 *zbuffer, *zbuffer_end;
   int num_bits;
   int hit_zeof_once;
   uint32 code_buffer;

   char *zout;
   char *zout_start;
   char *zout_end;
   int   z_expandable;

   ZHuffman z_length, z_distance;
};

static void buffersRefill(Context *s){
    int n = (s->io.read)(s->io_user_data,(char*)s->buffer_start,s->buflen);
    s->callback_already_read += (int) (s->img_buffer - s->img_buffer_original);
    if(!n){
        // at end of file, treat same as if from memory, but need to handle case
        // where s->img_buffer isn't pointing to safe memory, e.g. 0-byte file
        s->read_from_callbacks = 0;
        s->img_buffer = s->buffer_start;
        s->img_buffer_end = s->buffer_start+1;
        *s->img_buffer = 0;
    } 
    else {
        s->img_buffer = s->buffer_start;
        s->img_buffer_end = s->buffer_start + n;
    }
}

static uint8 get8(Context *s){
    if(s->img_buffer < s->img_buffer_end)
        return *s->img_buffer++;
    if(s->read_from_callbacks){
        buffersRefill(s);
        return *s->img_buffer++;
    }
    return 0;
}

static int pngHeaderCheck(Context *s){
    static const uint8 png_sig[8] = { 137,80,78,71,13,10,26,10 };
    int i;
    for(i=0; i < 8; ++i)
        if(get8(s) != png_sig[i]) 
            return 0;
    return 1;
}

static int beGet16(Context *s){
    int z = get8(s);
    return (z << 8) + get8(s);
}

static uint32 beGet32(Context *s){
    uint32 z = beGet16(s);
    return (z << 16) + beGet16(s);
}

static PngChunk chunkHeaderGet(Context *s){
    PngChunk c;
    c.length = beGet32(s);
    c.type   = beGet32(s);
    return c;
}

structure(PNG){
    Context *s;
    uint8 *idata, *expanded, *out;
    int depth;
};

static const uint8 stbi__depth_scale_table[9] = { 0, 0xff, 0x55, 0, 0x11, 0,0,0, 0x01 };

#define STBI_MAX_DIMENSIONS (1 << 24)
#define STBI__PNG_TYPE(a,b,c,d)  (((unsigned) (a) << 24) + ((unsigned) (b) << 16) + ((unsigned) (c) << 8) + (unsigned) (d))

enum{
   STBI__SCAN_load,
   STBI__SCAN_type,
   STBI__SCAN_header
};

#ifdef _MSC_VER
#define STBI_NOTUSED(v)  (void)(v)
#else
#define STBI_NOTUSED(v)  (void)sizeof(v)
#endif

static uint8 zGet8(StbiZbuf *z){
    return zeof(z) ? 0 : *z->zbuffer++;
}

static void bitsFill(StbiZbuf *z){
    do{
        if(z->code_buffer >= (1U << z->num_bits)){
            z->zbuffer = z->zbuffer_end;  /* treat this as EOF so we fail. */
            return;
        }
        z->code_buffer |= (unsigned)zGet8(z) << z->num_bits;
        z->num_bits += 8;
    } while (z->num_bits <= 24);
}

static unsigned zReceive(StbiZbuf *z, int n){
    unsigned k;
    if (z->num_bits < n) 
        bitsFill(z);
    k = z->code_buffer & ((1 << n) - 1);
    z->code_buffer >>= n;
    z->num_bits -= n;
    return k;
}

static int zExpand(StbiZbuf *z,char *zout,int n){
    char *q;
    unsigned cur, limit, old_limit;
    z->zout = zout;
    if (!z->z_expandable) return 0;
    cur   = (unsigned) (z->zout - z->zout_start);
    limit = old_limit = (unsigned) (z->zout_end - z->zout_start);
    if (UINT_MAX - cur < (unsigned) n) return 0;
    while (cur + n > limit) {
        if(limit > UINT_MAX / 2) return 0;
        limit *= 2;
    }
    q = (char *) STBI_REALLOC_SIZED(z->zout_start, old_limit, limit);
    STBI_NOTUSED(old_limit);
    if (q == NULL) return 0;
    z->zout_start = q;
    z->zout       = q + cur;
    z->zout_end   = q + limit;
    return 1;
}

static int uncompressedBlockParse(StbiZbuf *a){
    uint8 header[4];
    int len,nlen,k;
    if(a->num_bits & 7)
        zReceive(a, a->num_bits & 7); // discard
    // drain the bit-packed data into header
    k = 0;
    while(a->num_bits > 0){
        header[k++] = (uint8) (a->code_buffer & 255); // suppress MSVC run-time check
        a->code_buffer >>= 8;
        a->num_bits -= 8;
    }
    if(a->num_bits < 0) return 0;
    // now fill header the normal way
    while (k < 4)
        header[k++] = zGet8(a);
    len  = header[1] * 256 + header[0];
    nlen = header[3] * 256 + header[2];
    if(nlen != (len ^ 0xffff)) 
        return 0;
    if(a->zbuffer + len > a->zbuffer_end) 
        return 0;
    if(a->zout + len > a->zout_end)
        if(!zExpand(a, a->zout, len)) 
            return 0;
    tMemcpy(a->zout, a->zbuffer, len);
    a->zbuffer += len;
    a->zout += len;
    return 1;
}

static const uint8 stbi__zdefault_length[STBI__ZNSYMS] = {
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, 9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, 7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8
};

static const uint8 stbi__zdefault_distance[32] = {
    5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5
};

static int bitReverse16(int n){
    n = ((n & 0xAAAA) >>  1) | ((n & 0x5555) << 1);
    n = ((n & 0xCCCC) >>  2) | ((n & 0x3333) << 2);
    n = ((n & 0xF0F0) >>  4) | ((n & 0x0F0F) << 4);
    n = ((n & 0xFF00) >>  8) | ((n & 0x00FF) << 8);
    return n;
}

static int bitReverse(int v, int bits){
    return bitReverse16(v) >> (16-bits);
}

static int huffmanZbuild(ZHuffman *z,const uint8 *sizelist,int num){
    int i,k=0;
    int code, next_code[16], sizes[17];

    // DEFLATE spec for generating codes
    tMemset(sizes, 0, sizeof(sizes));
    tMemset(z->fast, 0, sizeof(z->fast));
    for(i=0; i < num; ++i)
        ++sizes[sizelist[i]];
    sizes[0] = 0;
    for(i=1; i < 16; ++i)
        if(sizes[i] > (1 << i))
            return 0;
    code = 0;
    for(i=1; i < 16; ++i){
        next_code[i] = code;
        z->firstcode[i] = (uint16) code;
        z->firstsymbol[i] = (uint16) k;
        code = (code + sizes[i]);
        if (sizes[i])
            if (code-1 >= (1 << i)) return 0;
        z->maxcode[i] = code << (16-i); // preshift for inner loop
        code <<= 1;
        k += sizes[i];
    }
    z->maxcode[16] = 0x10000; // sentinel
    for(i=0;i < num;++i){
        int s = sizelist[i];
        if(s){
            int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
            uint16 fastv = (uint16) ((s << 9) | i);
            z->size [c] = (uint8) s;
            z->value[c] = (uint16) i;
            if (s <= STBI__ZFAST_BITS){
                int j = bitReverse(next_code[s],s);
                while (j < (1 << STBI__ZFAST_BITS)) {
                    z->fast[j] = fastv;
                    j += (1 << s);
                }
            }
            ++next_code[s];
        }
    }
    return 1;
}

static int zeof(StbiZbuf *z){
    return (z->zbuffer >= z->zbuffer_end);
}

static int zlibHeaderParse(StbiZbuf *a){
    int cmf = zGet8(a);
    int cm = cmf & 15;
    /* int cinfo = cmf >> 4; */
    int flg = zGet8(a);
    if(zeof(a)) 
        return 0; // zlib spec
    if((cmf*256+flg) % 31 != 0) 
        return 0; // zlib spec
    if(flg & 32) 
        return 0; // preset dictionary not allowed in png
    if(cm != 8) 
        return 0; // DEFLATE required for png
    // window = 1 << (8 + cinfo)... but who cares, we fully buffer output
    return 1;
}

static int huffmanCodesCompute(StbiZbuf *a){
    static const uint8 length_dezigzag[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
    ZHuffman z_codelength;
    uint8 lencodes[286+32+137];//padding for maximum single op
    uint8 codelength_sizes[19];
    int i,n;

    int hlit  = zReceive(a,5) + 257;
    int hdist = zReceive(a,5) + 1;
    int hclen = zReceive(a,4) + 4;
    int ntot  = hlit + hdist;

    tMemset(codelength_sizes, 0, sizeof(codelength_sizes));
    for(i=0;i < hclen;++i){
        int s = zReceive(a,3);
        codelength_sizes[length_dezigzag[i]] = (uint8) s;
    }
    if(!huffmanZbuild(&z_codelength, codelength_sizes, 19)) 
        return 0;

    n = 0;
    while(n < ntot){
        int c = zhuffmanDecode(a, &z_codelength);
        if(c < 0 || c >= 19) 
            return 0;
        if(c < 16)
            lencodes[n++] = (uint8)c;
        else {
            uint8 fill = 0;
            if(c == 16){
                c = zReceive(a,2)+3;
                if(n == 0) 
                    return 0;
                fill = lencodes[n-1];
            } 
            else if (c == 17){
                c = zReceive(a,3)+3;
            } 
            else if (c == 18){
                c = zReceive(a,7)+11;
            } 
            else{
                return 0;
            }
            if(ntot - n < c) 
                return 0;
            tMemset(lencodes+n, fill, c);
            n += c;
        }
    }
    if (n != ntot) return 0;
    if (!huffmanZbuild(&a->z_length, lencodes, hlit)) return 0;
    if (!huffmanZbuild(&a->z_distance, lencodes+hlit, hdist)) return 0;
    return 1;
}

static int zhuffmanSlowpathDecode(StbiZbuf *a,ZHuffman *z){
    int b,s,k;
    // not resolved by fast table, so compute it the slow way
    // use jpeg approach, which requires MSbits at top
    k = bitReverse(a->code_buffer, 16);
    for(s = STBI__ZFAST_BITS+1;;++s)
        if(k < z->maxcode[s])
            break;
    if(s >= 16) 
        return -1; // invalid code!
    // code size is s, so:
    b = (k >> (16-s)) - z->firstcode[s] + z->firstsymbol[s];
    if(b >= STBI__ZNSYMS) 
        return -1; // some data was corrupt somewhere!
    if(z->size[b] != s) 
        return -1;  // was originally an assert, but report failure instead.
    a->code_buffer >>= s;
    a->num_bits -= s;
    return z->value[b];
}

#define STBI__ZFAST_MASK  ((1 << STBI__ZFAST_BITS) - 1)

static int zhuffmanDecode(StbiZbuf *a,ZHuffman *z){
    int b,s;
    if(a->num_bits < 16){
        if(zeof(a)){
            if(!a->hit_zeof_once){
                // This is the first time we hit eof, insert 16 extra padding btis
                // to allow us to keep going; if we actually consume any of them
                // though, that is invalid data. This is caught later.
                a->hit_zeof_once = 1;
                a->num_bits += 16; // add 16 implicit zero bits
            } 
            else {
                // We already inserted our extra 16 padding bits and are again
                // out, this stream is actually prematurely terminated.
                return -1;
            }
        } 
        else {
            bitsFill(a);
        }
    }
    b = z->fast[a->code_buffer & STBI__ZFAST_MASK];
    if(b){
        s = b >> 9;
        a->code_buffer >>= s;
        a->num_bits -= s;
        return b & 511;
    }
    return zhuffmanSlowpathDecode(a, z);
}

static const int stbi__zlength_base[31] = {
   3,4,5,6,7,8,9,10,11,13,
   15,17,19,23,27,31,35,43,51,59,
   67,83,99,115,131,163,195,227,258,0,0 
};

static const int stbi__zlength_extra[31] ={ 
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,0,0 
};

static const int stbi__zdist_base[32] = { 
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,
    257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577,0,0
};

static const int stbi__zdist_extra[32] = { 
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

static int huffmanBlockParse(StbiZbuf *a){
    char *zout = a->zout;
    loop{
        int z = zhuffmanDecode(a, &a->z_length);
        if(z < 256){
            if(z < 0) 
                return 0; // error in huffman codes
            if(zout >= a->zout_end){
                if (!zExpand(a, zout, 1))
                    return 0;
                zout = a->zout;
            }
            *zout++ = (char) z;
        } 
        else{
            uint8 *p;
            int len,dist;
            if(z == 256){
                a->zout = zout;
                if(a->hit_zeof_once && a->num_bits < 16){
                    // The first time we hit zeof, we inserted 16 extra zero bits into our bit
                    // buffer so the decoder can just do its speculative decoding. But if we
                    // actually consumed any of those bits (which is the case when num_bits < 16),
                    // the stream actually read past the end so it is malformed.
                    return 0;
                }
                return 1;
            }
            if (z >= 286) 
                return 0; // per DEFLATE, length codes 286 and 287 must not appear in compressed data
            z -= 257;
            len = stbi__zlength_base[z];
            if (stbi__zlength_extra[z]) 
                len += zReceive(a, stbi__zlength_extra[z]);
            z = zhuffmanDecode(a, &a->z_distance);
            if(z < 0 || z >= 30) 
                return 0; // per DEFLATE, distance codes 30 and 31 must not appear in compressed data
            dist = stbi__zdist_base[z];
            if(stbi__zdist_extra[z]) 
                dist += zReceive(a, stbi__zdist_extra[z]);
            if(zout - a->zout_start < dist) 
                return 0;
            if(len > a->zout_end - zout){
                if(!zExpand(a, zout, len)) 
                    return 0;
                zout = a->zout;
            }
            p = (uint8 *)(zout - dist);
            if(dist == 1){ // run of one byte; common in images.
                uint8 v = *p;
                if(len){ 
                    do *zout++ = v; 
                    while(--len); 
                }
            } 
            else{
                if (len) { 
                    do *zout++ = *p++; 
                    while(--len); 
                }
            }
        }
    }
}

static int zlibParse(StbiZbuf *a,int parse_header){
    int final, type;
    if(parse_header)
        if(!zlibHeaderParse(a)) 
            return 0;
    a->num_bits = 0;
    a->code_buffer = 0;
    a->hit_zeof_once = 0;
    do{
        final = zReceive(a,1);
        type = zReceive(a,2);
        if(type == 0){
            if(!uncompressedBlockParse(a)) 
                return 0;
        } 
        else if(type == 3){
            return 0;
        } 
        else{
            if(type == 1){
                // use fixed code lengths
                if(!huffmanZbuild(&a->z_length  , stbi__zdefault_length  , STBI__ZNSYMS)) 
                    return 0;
                if(!huffmanZbuild(&a->z_distance, stbi__zdefault_distance,  32)) 
                    return 0;
            } 
            else{
                if(!huffmanCodesCompute(a)) 
                    return 0;
            }
            if(!huffmanBlockParse(a)) 
                return 0;
        }
    } while(!final);
    return 1;
}

static int zlibDo(StbiZbuf *a,char *obuf,int olen,int exp,int parse_header){
   a->zout_start = obuf;
   a->zout       = obuf;
   a->zout_end   = obuf + olen;
   a->z_expandable = exp;

   return zlibParse(a, parse_header);
}

static char *zlibMallocGuessSizeHeaderflagDecode(const char *buffer,int len,int initial_size,int *outlen,int parse_header){
    StbiZbuf a;
    char *p = tMallocT(initial_size);
    if(p == NULL) 
        return NULL;
    a.zbuffer = (uint8 *)buffer;
    a.zbuffer_end = (uint8 *)buffer + len;
    if(zlibDo(&a,p,initial_size,1,parse_header)){
        if(outlen) 
            *outlen = (int) (a.zout - a.zout_start);
        return a.zout_start;
    } 
    else{
        tFreeT(a.zout_start);
        return NULL;
    }
}

static int stbi__addsizes_valid(int a, int b){
    if(b < 0) 
        return 0;
    return a <= INT_MAX - b;
}

static int stbi__mul2sizes_valid(int a, int b){
    if(a < 0 || b < 0) 
        return 0;
    if(b == 0) 
        return 1; // mul-by-0 is always safe
    // portable way to check for no overflows in a*b
    return a <= INT_MAX/b;
}

static int stbi__mad3sizes_valid(int a, int b, int c, int add){
   return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a*b, c) && stbi__addsizes_valid(a*b*c, add);
}

static void *tMallocT_mad3(int a, int b, int c, int add){
   if(!stbi__mad3sizes_valid(a, b, c, add)) 
       return NULL;
   return tMallocT(a*b*c + add);
}

static int stbi__mad2sizes_valid(int a, int b, int add){
   return stbi__mul2sizes_valid(a, b) && stbi__addsizes_valid(a*b, add);
}

static void *tMallocT_mad2(int a,int b,int add){
   if(!stbi__mad2sizes_valid(a, b, add)) 
       return NULL;
   return tMallocT(a*b + add);
}

enum{
    STBI__F_none,
    STBI__F_sub,
    STBI__F_up,
    STBI__F_avg,
    STBI__F_paeth,
    STBI__F_avg_first
};

static uint8 first_row_filter[5] ={
    STBI__F_none,
    STBI__F_sub,
    STBI__F_none,
    STBI__F_avg_first,
    STBI__F_sub // Paeth with b=c=0 turns out to be equivalent to sub
};

static int paeth2(int a,int b,int c){
    int thresh = c*3 - (a + b);
    int lo = a < b ? a : b;
    int hi = a < b ? b : a;
    int t0 = (hi <= thresh) ? lo : c;
    int t1 = (thresh <= lo) ? hi : t0;
    return t1;
}

static void pngAlphaExpand8Create(uint8 *dest,uint8 *src,uint32 x,int img_n){
    // must process data backwards since we allow dest==src
    if(img_n == 1){
        for (int i = x - 1;i >= 0;--i){
            dest[i*2+1] = 255;
            dest[i*2+0] = src[i];
        }
    } 
    else{
        for (int i = x - 1;i >= 0;--i){
            dest[i * 4 + 3] = 255;
            dest[i * 4 + 2] = src[i * 3 + 2];
            dest[i * 4 + 1] = src[i * 3 + 1];
            dest[i * 4 + 0] = src[i * 3 + 0];
        }
    }
}

#define STBI__BYTECAST(x)  ((uint8) ((x) & 255)) 

static int pngImageCreateRaw(PNG *a,uint8 *raw,uint32 raw_len,int out_n,uint32 x,uint32 y,int depth,int color){
    int bytes = (depth == 16 ? 2 : 1);
    Context *s = a->s;
    uint32 i,stride = x*out_n*bytes;
    uint32 img_len, img_width_bytes;
    uint8 *filter_buf;
    int all_ok = 1;
    int k;
    int img_n = s->img_n; // copy it into a local for later

    int output_bytes = out_n*bytes;
    int filter_bytes = img_n*bytes;
    int width = x;

    a->out = tMallocT_mad3(x, y, output_bytes, 0); // extra bytes to write off the end into
    if (!a->out) 
        return 0;

    // note: error exits here don't need to clean up a->out individually,
    // stbi__do_png always does on error.
    if(!stbi__mad3sizes_valid(img_n, x, depth, 7))
        return 0;
    img_width_bytes = (((img_n * x * depth) + 7) >> 3);
    if(!stbi__mad2sizes_valid(img_width_bytes, y, img_width_bytes)) 
        return 0;
    img_len = (img_width_bytes + 1) * y;

    // we used to check for exact match between raw_len and img_len on non-interlaced PNGs,
    // but issue #276 reported a PNG in the wild that had extra data at the end (all zeros),
    // so just check for raw_len < img_len always.
    if(raw_len < img_len) 
        return 0;

    // Allocate two scan lines worth of filter workspace buffer.
    filter_buf = tMallocT_mad2(img_width_bytes, 2, 0);
    if(!filter_buf) 
        return 0;

    // Filtering for low-bit-depth images
    if (depth < 8) {
        filter_bytes = 1;
        width = img_width_bytes;
    }

    for(uint32 j = 0;j < y;++j){
        // cur/prior filter buffers alternate
        uint8 *cur = filter_buf + (j & 1)*img_width_bytes;
        uint8 *prior = filter_buf + (~j & 1)*img_width_bytes;
        uint8 *dest = a->out + stride*j;
        int nk = width * filter_bytes;
        int filter = *raw++;

        // check filter type
        if(filter > 4){
            all_ok = 0;
            break;
        }

        // if first row, use special filter that doesn't sample previous row
        if(!j) 
            filter = first_row_filter[filter];

        // perform actual filtering
        switch (filter){
            case STBI__F_none:
                tMemcpy(cur, raw, nk);
                break;
            case STBI__F_sub:
                tMemcpy(cur, raw, filter_bytes);
                for (k = filter_bytes; k < nk; ++k)
                cur[k] = STBI__BYTECAST(raw[k] + cur[k-filter_bytes]);
                break;
            case STBI__F_up:
                for (k = 0; k < nk; ++k)
                cur[k] = STBI__BYTECAST(raw[k] + prior[k]);
                break;
            case STBI__F_avg:
                for (k = 0; k < filter_bytes; ++k)
                cur[k] = STBI__BYTECAST(raw[k] + (prior[k]>>1));
                for (k = filter_bytes; k < nk; ++k)
                cur[k] = STBI__BYTECAST(raw[k] + ((prior[k] + cur[k-filter_bytes])>>1));
                break;
            case STBI__F_paeth:
                for (k = 0; k < filter_bytes; ++k)
                cur[k] = STBI__BYTECAST(raw[k] + prior[k]); // prior[k] == stbi__paeth(0,prior[k],0)
                for (k = filter_bytes; k < nk; ++k)
                cur[k] = STBI__BYTECAST(raw[k] + paeth2(cur[k-filter_bytes], prior[k], prior[k-filter_bytes]));
                break;
            case STBI__F_avg_first:
                tMemcpy(cur, raw, filter_bytes);
                for (k = filter_bytes; k < nk; ++k)
                cur[k] = STBI__BYTECAST(raw[k] + (cur[k-filter_bytes] >> 1));
                break;
        }

        raw += nk;

        // expand decoded bits in cur to dest, also adding an extra alpha channel if desired
        if(depth < 8){
            uint8 scale = (color == 0) ? stbi__depth_scale_table[depth] : 1; // scale grayscale values to 0..255 range
            uint8 *in = cur;
            uint8 *out = dest;
            uint8 inb = 0;
            uint32 nsmp = x*img_n;

            // expand bits to bytes first
            if(depth == 4){
                for (i=0; i < nsmp; ++i){
                    if((i & 1) == 0) 
                        inb = *in++;
                    *out++ = scale * (inb >> 4);
                    inb <<= 4;
                }
            } 
            else if (depth == 2) {
                for(i=0;i < nsmp;++i){
                    if((i & 3) == 0) 
                        inb = *in++;
                    *out++ = scale * (inb >> 6);
                    inb <<= 2;
                }
            } 
            else {
                for(i=0;i < nsmp;++i){
                    if((i & 7) == 0) 
                        inb = *in++;
                    *out++ = scale * (inb >> 7);
                    inb <<= 1;
                }
            }

            // insert alpha=255 values if desired
            if (img_n != out_n)
            pngAlphaExpand8Create(dest, dest, x, img_n);
        } 
        else if(depth == 8){
            if(img_n == out_n)
                tMemcpy(dest, cur, x*img_n);
            else
                pngAlphaExpand8Create(dest, cur, x, img_n);
        } 
        else if(depth == 16){
            // convert the image data from big-endian to platform-native
            uint16 *dest16 = (uint16*)dest;
            uint32 nsmp = x*img_n;

            if(img_n == out_n){
            for (i = 0; i < nsmp; ++i, ++dest16, cur += 2)
                *dest16 = (cur[0] << 8) | cur[1];
            } 
            else{
                if(img_n == 1){
                    for(i = 0; i < x; ++i, dest16 += 2, cur += 2){
                        dest16[0] = (cur[0] << 8) | cur[1];
                        dest16[1] = 0xffff;
                    }
                } 
                else{
                    for(i = 0; i < x; ++i, dest16 += 4, cur += 6){
                        dest16[0] = (cur[0] << 8) | cur[1];
                        dest16[1] = (cur[2] << 8) | cur[3];
                        dest16[2] = (cur[4] << 8) | cur[5];
                        dest16[3] = 0xffff;
                    }
                }
            }
        }
    }

    tFreeT(filter_buf);
    if(!all_ok) 
        return 0;

    return 1;
}

static int pngImageCreate(PNG *a,uint8 *image_data,uint32 image_data_len,int out_n,int depth,int color,int interlaced){
    int bytes = (depth == 16 ? 2 : 1);
    int out_bytes = out_n * bytes;
    uint8 *final;

    if(!interlaced)
        return pngImageCreateRaw(a, image_data, image_data_len, out_n, a->s->img_x, a->s->img_y, depth, color);

    // de-interlacing
    final = tMallocT_mad3(a->s->img_x,a->s->img_y,out_bytes,0);
    if(!final) 
        return 0;
    for(int p=0;p < 7;++p){
        int xorig[] = {0,4,0,2,0,1,0};
        int yorig[] = {0,0,4,0,2,0,1};
        int xspc[]  = {8,8,4,4,2,2,1};
        int yspc[]  = {8,8,8,4,4,2,2};
        // pass1_x[4] = 0, pass1_x[5] = 1, pass1_x[12] = 1
        int x = (a->s->img_x - xorig[p] + xspc[p]-1) / xspc[p];
        int y = (a->s->img_y - yorig[p] + yspc[p]-1) / yspc[p];
        if(x && y){
            uint32 img_len = ((((a->s->img_n * x * depth) + 7) >> 3) + 1) * y;
            if(!pngImageCreateRaw(a,image_data,image_data_len,out_n,x,y,depth,color)){
                tFreeT(final);
                return 0;
            }
            for(int j = 0; j < y;++j){
                for(int i = 0; i < x;++i){
                    int out_y = j*yspc[p]+yorig[p];
                    int out_x = i*xspc[p]+xorig[p];
                    tMemcpy(final + out_y*a->s->img_x*out_bytes + out_x*out_bytes,a->out + (j*x+i)*out_bytes,out_bytes);
                }
            }
            tFreeT(a->out);
            image_data += img_len;
            image_data_len -= img_len;
        }
    }
    a->out = final;

    return 1;
}

static void skip(Context *s,int n){
    if(n == 0) 
        return;  // already there!
    if(n < 0){
        s->img_buffer = s->img_buffer_end;
        return;
    }
    if(s->io.read){
        int blen = (int)(s->img_buffer_end - s->img_buffer);
        if(blen < n){
            s->img_buffer = s->img_buffer_end;
            (s->io.skip)(s->io_user_data, n - blen);
            return;
        }
    }
    s->img_buffer += n;
}

static int getn(Context *s,uint8 *buffer,int n){
    if(s->io.read){
        int blen = (int)(s->img_buffer_end - s->img_buffer);
        if (blen < n) {
            int res, count;

            tMemcpy(buffer, s->img_buffer, blen);

            count = (s->io.read)(s->io_user_data, (char*) buffer + blen, n - blen);
            res = (count == (n-blen));
            s->img_buffer = s->img_buffer_end;
            return res;
        }
    }
    if(s->img_buffer+n <= s->img_buffer_end){
        tMemcpy(buffer, s->img_buffer, n);
        s->img_buffer += n;
        return 1;
    } 
    else{
        return 0;
    }
}

static void *mallocMad2(int a,int b,int add){
   if(!stbi__mad2sizes_valid(a,b,add)) 
       return NULL;
   return tMallocT(a*b + add);
}

static int pngPaletteExpand(PNG *a,uint8 *palette,int len,int pal_img_n){
    uint32 i, pixel_count = a->s->img_x * a->s->img_y;
    uint8 *p, *temp_out, *orig = a->out;

    p = mallocMad2(pixel_count, pal_img_n, 0);
    if(p == NULL) 
        return 0;

    // between here and free(out) below, exitting would leak
    temp_out = p;

    if(pal_img_n == 3){
        for(i = 0;i < pixel_count;++i){
            int n = orig[i]*4;
            p[0] = palette[n  ];
            p[1] = palette[n+1];
            p[2] = palette[n+2];
            p += 3;
        }
    } 
    else{
        for(i = 0;i < pixel_count;++i){
            int n = orig[i]*4;
            p[0] = palette[n  ];
            p[1] = palette[n+1];
            p[2] = palette[n+2];
            p[3] = palette[n+3];
            p += 4;
        }
    }
    tFreeT(a->out);
    a->out = temp_out;

    STBI_NOTUSED(len);

    return 1;
}

static void deIphone(PNG *z){
    Context *s = z->s;
    uint32 pixel_count = s->img_x * s->img_y;
    uint8 *p = z->out;

    if(s->img_out_n == 3){  // convert bgr to rgb
        for(uint32_t i = 0;i < pixel_count;++i){
            uint8 t = p[0];
            p[0] = p[2];
            p[2] = t;
            p += 3;
        }
    } 
    else{
        for(uint32_t i = 0;i < pixel_count;++i){
            uint8 t = p[0];
            p[0] = p[2];
            p[2] = t;
            p += 4;
        }
    }
}

static int transparancyCompute(PNG *z,uint8 tc[3],int out_n){
    Context *s = z->s;
    uint32 pixel_count = s->img_x * s->img_y;
    uint8 *p = z->out;

    // compute color-based transparency, assuming we've
    // already got 255 as the alpha value in the output

    if(out_n == 2){
        for (uint32_t i = 0;i < pixel_count;++i){
            p[1] = (p[0] == tc[0] ? 0 : 255);
            p += 2;
        }
    } 
    else{
        for(uint32_t i = 0; i < pixel_count;++i){
            if (p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
            p += 4;
        }
    }
    return 1;
}

static int transparancy16Compute(PNG *z,uint16 tc[3],int out_n){
    Context *s = z->s;
    uint32 pixel_count = s->img_x * s->img_y;
    uint16 *p = (uint16*) z->out;

    // compute color-based transparency, assuming we've
    // already got 65535 as the alpha value in the output

    if(out_n == 2){
        for(uint32_t i = 0;i < pixel_count;++i){
            p[1] = (p[0] == tc[0] ? 0 : 65535);
            p += 2;
        }
    } 
    else{
        for(uint32_t i = 0;i < pixel_count;++i){
            if(p[0] == tc[0] && p[1] == tc[1] && p[2] == tc[2])
            p[3] = 0;
            p += 4;
        }
    }
    return 1;
}

static int pngFileParse(PNG *z, int scan, int req_comp){
    uint8 palette[1024], pal_img_n=0;
    uint8 has_trans=0, tc[3]={0};
    uint16 tc16[3];
    uint32 ioff=0, idata_limit=0, i, pal_len=0;
    int first=1,k,interlace=0, color=0, is_iphone=0;
    Context *s = z->s;

    z->expanded = NULL;
    z->idata = NULL;
    z->out = NULL;

    if(!pngHeaderCheck(s)) 
        return 0;

    if(scan == STBI__SCAN_type) 
        return 1;

    for (;;) {
        PngChunk c = chunkHeaderGet(s);
        switch (c.type) {
            case STBI__PNG_TYPE('C','g','B','I'):
                is_iphone = 1;
                skip(s, c.length);
                break;
            case STBI__PNG_TYPE('I','H','D','R'):{
                int comp,filter;
                if(!first) 
                    return 0;
                first = 0;
                if(c.length != 13) 
                    return 0;
                s->img_x = beGet32(s);
                s->img_y = beGet32(s);
                if (s->img_y > STBI_MAX_DIMENSIONS) 
                    return 0;
                if (s->img_x > STBI_MAX_DIMENSIONS) 
                    return 0;
                z->depth = get8(s);  
                if(z->depth != 1 && z->depth != 2 && z->depth != 4 && z->depth != 8 && z->depth != 16)  
                    return 0;
                color = get8(s);  
                if(color > 6)         
                    return 0;
                if(color == 3 && z->depth == 16)                  
                    return 0;
                if(color == 3) 
                    pal_img_n = 3; 
                else if(color & 1) 
                    return 0;
                comp = get8(s);  
                if (comp) 
                    return 0;
                filter = get8(s);  
                if(filter)
                    return 0;
                interlace = get8(s); 
                if(interlace > 1) 
                    return 0;
                if(!s->img_x || !s->img_y) 
                    return 0;
                if(!pal_img_n){
                    s->img_n = (color & 2 ? 3 : 1) + (color & 4 ? 1 : 0);
                    if((1 << 30) / s->img_x / s->img_n < s->img_y) 
                        return 0;
                } 
                else {
                    // if paletted, then pal_n is our final components, and
                    // img_n is # components to decompress/filter.
                    s->img_n = 1;
                    if((1 << 30) / s->img_x / 4 < s->img_y) 
                        return 0;
                }
                // even with SCAN_header, have to scan to see if we have a tRNS
            } break;
            case STBI__PNG_TYPE('P','L','T','E'):{
                if(first) 
                    return 0;
                if(c.length > 256*3) 
                    return 0;
                pal_len = c.length / 3;
                if(pal_len * 3 != c.length) 
                    return 0;
                for(i=0;i < pal_len;++i){
                    palette[i*4+0] = get8(s);
                    palette[i*4+1] = get8(s);
                    palette[i*4+2] = get8(s);
                    palette[i*4+3] = 255;
                }
            } break;
            case STBI__PNG_TYPE('t','R','N','S'):{
                if(first) 
                    return 0;
                if(z->idata) 
                    return 0;
                if (pal_img_n) {
                    if (scan == STBI__SCAN_header) { s->img_n = 4; return 1; }
                    if (pal_len == 0) return 0;
                    if (c.length > pal_len) return 0;
                    pal_img_n = 4;
                    for (i=0; i < c.length; ++i)
                        palette[i*4+3] = get8(s);
                } 
                else{
                    if(!(s->img_n & 1)) 
                        return 0;
                    if(c.length != (uint32) s->img_n*2) 
                        return 0;
                    has_trans = 1;
                    // non-paletted with tRNS = constant alpha. if header-scanning, we can stop now.
                    if(scan == STBI__SCAN_header){ 
                        ++s->img_n; 
                        return 1; 
                    }
                    if(z->depth == 16){
                        for (k = 0; k < s->img_n && k < 3; ++k) // extra loop test to suppress false GCC warning
                            tc16[k] = (uint16)beGet16(s); // copy the values as-is
                    } 
                    else{
                        for(k = 0; k < s->img_n && k < 3;++k)
                            tc[k] = (uint8)(beGet16(s) & 255) * stbi__depth_scale_table[z->depth]; // non 8-bit images will be larger
                    }
                }
            } break;
            case STBI__PNG_TYPE('I','D','A','T'):{
                if (first) 
                    return 0;
                if (pal_img_n && !pal_len) 
                    return 0;
                if(scan == STBI__SCAN_header){
                    // header scan definitely stops at first IDAT
                    if (pal_img_n)
                        s->img_n = pal_img_n;
                    return 1;
                }
                if(c.length > (1u << 30)) 
                    return 0;
                if((int)(ioff + c.length) < (int)ioff) 
                    return 0;
                if (ioff + c.length > idata_limit) {
                    uint32 idata_limit_old = idata_limit;
                    uint8 *p;
                    if(idata_limit == 0) 
                        idata_limit = c.length > 4096 ? c.length : 4096;
                    while(ioff + c.length > idata_limit)
                        idata_limit *= 2;
                    STBI_NOTUSED(idata_limit_old);
                    p = (uint8 *) STBI_REALLOC_SIZED(z->idata, idata_limit_old, idata_limit); 
                    if(p == NULL) 
                        return 0;
                    z->idata = p;
                }
                if(!getn(s, z->idata+ioff,c.length)) 
                    return 0;
                ioff += c.length;
            } break;
            case STBI__PNG_TYPE('I','E','N','D'):{
                uint32 raw_len, bpl;
                if(first) 
                    return 0;
                if(scan != STBI__SCAN_load) 
                    return 1;
                if(z->idata == NULL) 
                    return 0;
                // initial guess for decoded data size to avoid unnecessary reallocs
                bpl = (s->img_x * z->depth + 7) / 8; // bytes per line, per component
                raw_len = bpl * s->img_y * s->img_n /* pixels */ + s->img_y /* filter mode per row */;
                z->expanded = (uint8*)zlibMallocGuessSizeHeaderflagDecode((char *) z->idata, ioff, raw_len, (int *) &raw_len, !is_iphone);
                if(z->expanded == NULL) 
                    return 0; // zlib should set error
                tFreeT(z->idata); 
                z->idata = NULL;
                if((req_comp == s->img_n+1 && req_comp != 3 && !pal_img_n) || has_trans)
                    s->img_out_n = s->img_n+1;
                else
                    s->img_out_n = s->img_n;
                if(!pngImageCreate(z, z->expanded, raw_len, s->img_out_n, z->depth, color, interlace)) 
                    return 0;
                if(has_trans){
                    if(z->depth == 16){
                        if(!transparancy16Compute(z, tc16, s->img_out_n)) 
                            return 0;
                    } 
                    else{
                        if(!transparancyCompute(z, tc, s->img_out_n)) 
                            return 0;
                    }
                }
                if(is_iphone && s->img_out_n > 2)
                    deIphone(z);
                if(pal_img_n){
                    // pal_img_n == 3 or 4
                    s->img_n = pal_img_n; // record the actual colors we had
                    s->img_out_n = pal_img_n;
                    if(req_comp >= 3) 
                        s->img_out_n = req_comp;
                    if(!pngPaletteExpand(z,palette,pal_len,s->img_out_n))
                        return 0;
                } 
                else if(has_trans){
                    // non-paletted image with tRNS -> source image has (constant) alpha
                    ++s->img_n;
                }
                tFreeT(z->expanded); 
                z->expanded = NULL;
                // end of PNG chunk, read and skip CRC
                beGet32(s);
                return 1;
            }
            default:
                // if critical, fail
                if(first) 
                    return 0;
                if((c.type & (1 << 29)) == 0) 
                    return 0;

                skip(s,c.length);
            break;
        }
        // end of PNG chunk, read and skip CRC
        beGet32(s);
    }
}

structure(ResultInfo){
    int bits_per_channel;
    int num_channels;
    int channel_order;
};

static uint8 computeY(int r,int g,int b){
    return (uint8)(((r * 77) + (g * 150) + (29 * b)) >> 8);
}

static uint8 *formatConvert(uint8 *data,int img_n,int req_comp,unsigned x,unsigned y){
    uint8 *good;

    if(req_comp == img_n) 
        return data;

    good = (uint8 *) tMallocT_mad3(req_comp, x, y, 0);
    if(good == NULL) {
        tFreeT(data);
        return 0;
    }

    for (int j = 0;j < (int) y;++j) {
        uint8 *src  = data + j * x * img_n;
        uint8 *dest = good + j * x * req_comp;

    #define STBI__COMBO(a,b)  ((a)*8+(b))
    #define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(int i = x-1;i >= 0;--i,src += a,dest += b)
        // convert source image with img_n components to one with req_comp components;
        // avoid switch per pixel, so use switch per scanline and massive macros
        switch (STBI__COMBO(img_n, req_comp)) {
            STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=255;                                     } break;
            STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
            STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=255;                     } break;
            STBI__CASE(2,1) { dest[0]=src[0];                                                  } break;
            STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                  } break;
            STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                  } break;
            STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=255;        } break;
            STBI__CASE(3,1) { dest[0]=computeY(src[0],src[1],src[2]);                   } break;
            STBI__CASE(3,2) { dest[0]=computeY(src[0],src[1],src[2]); dest[1] = 255;    } break;
            STBI__CASE(4,1) { dest[0]=computeY(src[0],src[1],src[2]);                   } break;
            STBI__CASE(4,2) { dest[0]=computeY(src[0],src[1],src[2]); dest[1] = src[3]; } break;
            STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                    } break;
            default: tFreeT(data); tFreeT(good); return 0;
        }
    #undef STBI__CASE
    }

    tFreeT(data);
    return good;
}

static uint16 compute16Y(int r,int g,int b){
    return (uint16) (((r*77) + (g*150) +  (29*b)) >> 8);
}

static uint16* format16Convert(uint16 *data,int img_n,int req_comp,unsigned x,unsigned y){
    int i,j;
    uint16 *good;

    if (req_comp == img_n) 
        return data;

    good = tMallocT(req_comp * x * y * 2);
    if(!good){
        tFreeT(data);
        return 0;
    }

    for(j = 0;j < (int)y;++j){
        uint16 *src  = data + j * x * img_n;
        uint16 *dest = good + j * x * req_comp;

        #define STBI__COMBO(a,b)  ((a)*8+(b))
        #define STBI__CASE(a,b)   case STBI__COMBO(a,b): for(i=x-1; i >= 0; --i, src += a, dest += b)
        // convert source image with img_n components to one with req_comp components;
        // avoid switch per pixel, so use switch per scanline and massive macros
        switch (STBI__COMBO(img_n, req_comp)) {
            STBI__CASE(1,2) { dest[0]=src[0]; dest[1]=0xffff;                                     } break;
            STBI__CASE(1,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
            STBI__CASE(1,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=0xffff;                     } break;
            STBI__CASE(2,1) { dest[0]=src[0];                                                     } break;
            STBI__CASE(2,3) { dest[0]=dest[1]=dest[2]=src[0];                                     } break;
            STBI__CASE(2,4) { dest[0]=dest[1]=dest[2]=src[0]; dest[3]=src[1];                     } break;
            STBI__CASE(3,4) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];dest[3]=0xffff;        } break;
            STBI__CASE(3,1) { dest[0]=compute16Y(src[0],src[1],src[2]);                   } break;
            STBI__CASE(3,2) { dest[0]=compute16Y(src[0],src[1],src[2]); dest[1] = 0xffff; } break;
            STBI__CASE(4,1) { dest[0]=compute16Y(src[0],src[1],src[2]);                   } break;
            STBI__CASE(4,2) { dest[0]=compute16Y(src[0],src[1],src[2]); dest[1] = src[3]; } break;
            STBI__CASE(4,3) { dest[0]=src[0];dest[1]=src[1];dest[2]=src[2];                       } break;
            default: 
                tFreeT(data); 
                tFreeT(good);
                return (uint16*)0;
        }
        #undef STBI__CASE
    }

    tFreeT(data);
    return good;
}

static void* doPng(PNG* p,int* x,int* y,int* n,int req_comp,ResultInfo* ri){
    void* result = NULL;
    if(req_comp < 0 || req_comp > 4) 
        return 0;
    if(pngFileParse(p,STBI__SCAN_load,req_comp)){
        if(p->depth <= 8)
            ri->bits_per_channel = 8;
        else if(p->depth == 16)
            ri->bits_per_channel = 16;
        else
            return 0;
        result = p->out;
        p->out = NULL;
        if(req_comp && req_comp != p->s->img_out_n){
            if (ri->bits_per_channel == 8)
            result = formatConvert((uint8 *)result,p->s->img_out_n,req_comp,p->s->img_x,p->s->img_y);
            else
            result = format16Convert((uint16*) result,p->s->img_out_n,req_comp,p->s->img_x,p->s->img_y);
            p->s->img_out_n = req_comp;
            if(result == NULL) 
                return result;
        }
        *x = p->s->img_x;
        *y = p->s->img_y;
        if(n) 
            *n = p->s->img_n;
    }
    tFreeT(p->out);      p->out      = NULL;
    tFreeT(p->expanded); p->expanded = NULL;
    tFreeT(p->idata);    p->idata    = NULL;

    return result;
}

enum{
   STBI_ORDER_RGB,
   STBI_ORDER_BGR
};

static uint8 *convert16To8(uint16 *orig, int w, int h, int channels){
   int img_len = w * h * channels;
   uint8* reduced;

   reduced = tMallocT(img_len);
   if(reduced == NULL) 
       return 0;

   for(int i = 0;i < img_len;++i)
      reduced[i] = (uint8)((orig[i] >> 8) & 0xFF);

   tFreeT(orig);
   return reduced;
}

uint8* pngDecode(uint8 *buffer,int len,int *x,int *y,int *comp,int req_comp){
    Context context;
    context.io.read = NULL;
    context.read_from_callbacks = 0;
    context.callback_already_read = 0;
    context.img_buffer = context.img_buffer_original = (uint8*)buffer;
    context.img_buffer_end = context.img_buffer_original_end = (uint8*)buffer + len;
    ResultInfo ri;
    tMemset(&ri,0,sizeof(ri)); // make sure it's initialized if we add new fields
    ri.bits_per_channel = 8; // default is 8 so most paths don't have to be changed
    ri.channel_order = STBI_ORDER_RGB; // all current input & output are this, but this is here so we can add BGR order
    ri.num_channels = 0;
    PNG p;
    p.s = &context;
    void *result = doPng(&p, x,y,comp,req_comp,&ri);

    if(result == NULL)
        return NULL;

    if(ri.bits_per_channel != 8){
        result = convert16To8((uint16*) result, *x, *y, req_comp == 0 ? *comp : req_comp);
        ri.bits_per_channel = 8;
    }

    return result;
}