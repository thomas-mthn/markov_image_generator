#ifndef FIXED_H
#define FIXED_H

#include "langext.h"

#define FIXED_PRECISION 16
#define FIXED_ONE (1 << FIXED_PRECISION)

static int fixedDivR(int value_1,int value_2){
    if(!value_2)
        return INT_MAX;
    int result;
#if !defined(_WIN64) && defined(_MSC_VER) && !defined(__wasm__)
    if(((value_1 / value_2) < 0 ? -(value_1 / value_2) : (value_1 / value_2)) >= FIXED_ONE / 2)
        return INT_MAX;
    _asm {
        mov     eax, value_1
        cdq 
        shld    edx, eax, FIXED_PRECISION
        sal     eax, FIXED_PRECISION
        idiv    value_2
        mov     result, eax
    }
#elif !defined(_WIN64) && defined(__GNUC__) && !defined(__wasm__)
    if(value_1 / value_2 >= FIXED_ONE)
        return INT_MAX;
    asm (
        "movl %1, %%eax\n\t"
        "cdq\n\t"
        "shldl $16, %%eax, %%edx\n\t"
        "sall $16, %%eax\n\t"
        "idivl %2\n\t"
        "movl %%eax, %0\n\t"
        : "=m" (result)
        : "r" (value_1),
          "r" (value_2)
        : "%eax", "%ecx", "%edx"
    );
#else
    result = ((int64)value_1 << FIXED_PRECISION) / value_2;
#endif
    return result;
}

static int fixedMulR(int value_1,int value_2){
    int result;
#if !defined(_WIN64) && defined(_MSC_VER) && !defined(__wasm__)
    _asm {
        mov     eax,value_1
        imul    value_2
        shrd    eax,edx,FIXED_PRECISION
        mov     result, eax
    }
#else
    result = ((int64)value_1 * value_2) >> FIXED_PRECISION;
#endif
    return result;
}

static void fixedMul(int* value,int a){
    *value = fixedMulR(*value,a);
}

static void fixedDiv(int* value,int a){
    *value = fixedDivR(*value,a);
}

static int fixedDivUnsafe(int value_1,int value_2){
    return (value_1 << FIXED_PRECISION) / value_2;
}

static int fixedFract(int value){
    return value & FIXED_ONE - 1;
}

static int fixedToInt(int value){
    return value >> FIXED_PRECISION;
}

static int intToFixed(int value){
    return value << FIXED_PRECISION;
}

#endif
