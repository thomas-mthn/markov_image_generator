#ifndef LANGEXT_H
#define LANGEXT_H

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <stddef.h>

typedef int8_t   int8;
typedef uint8_t  uint8;
typedef int16_t  int16;
typedef uint16_t uint16;
typedef int32_t  int32;
typedef uint32_t uint32;
typedef int64_t  int64;
typedef uint64_t uint64;

typedef void (*funcptr_t)(void);

#define atomic _Atomic

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#define nullptr NULL
#endif

#ifdef _MSC_VER
#define noinline _declspec(noinline)
#elif __GNUC__
#define noinline __attribute__((noinline))
#else
#define noinline
#endif

#define structure(NAME) typedef struct NAME NAME;struct NAME
#define enumeration(NAME) typedef enum NAME NAME;enum NAME

#define countof(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#define repeat(AMOUNT) for(size_t I##__LINE__ = AMOUNT;I##__LINE__--;)

#if _MSC_VER
#define stdcall _stdcall
#else
#define stdcall
#endif

#define alignas(AMOUNT) _declspec(align(AMOUNT))

#endif
