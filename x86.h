#ifndef X86_H
#define X86_H

#include <immintrin.h>

static unsigned x86Rdtsc(){
    return __rdtsc();
}

static float x86Sqrt(float value){
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(value)));
}

//12 bits of precision
static float x86InverseSqrt(float value){
    return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(value)));
}

#endif
