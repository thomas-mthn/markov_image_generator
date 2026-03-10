#ifndef TMATH_H
#define TMATH_H

#ifndef __wasm__
#include "x86.h"
#endif
#include "fixed.h"

#define M_PI 0x000325A1

static unsigned tHash(unsigned x){
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
	return x;
}

static unsigned tRnd(void){
    static unsigned v = 2463534242u;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return v;
}

static bool tRndChance(int change){
    return tRnd() % change == 0;
}

static int tAbs(int v){
    return v < 0 ? -v : v;
}

static int tMax(int v1,int v2){
    return v1 > v2 ? v1 : v2;
}

static int tMin(int v1,int v2){
    return v1 < v2 ? v1 : v2;
}

static int tClamp(int v,int max,int min){
    return tMin(tMax(v,max),min);
}

static int tMix(int v1,int v2,int mix){
    return fixedMulR(v1,FIXED_ONE - mix) + fixedMulR(v2,mix);
}

static int tSqrtInt(int value){
#ifndef __wasm__
    return x86Sqrt((float)value);
#else
    return __builtin_sqrtf((float)value);
#endif
}

static int tSqrt(int value){
#ifndef __wasm__
    return x86Sqrt((float)value / FIXED_ONE) * FIXED_ONE;
#else
    return __builtin_sqrtf((float)value / FIXED_ONE) * FIXED_ONE;
#endif
}

static int tInverseSqrt(int value){
#ifndef __wasm__
    return x86InverseSqrt((float)value / FIXED_ONE) * FIXED_ONE;
#else
    return (1.0f / __builtin_sqrtf((float)value / FIXED_ONE)) * FIXED_ONE;
#endif
}

int tCos(int value);

static int tSin(int value){
    return tCos(value - 0x4000);
}

#endif
