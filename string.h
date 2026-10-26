#ifndef STRING_H
#define STRING_H

#include "langext.h"

structure(MemoryArena);

structure(String){
    int size;
    char* data;
};

bool stringCompare(String string,String compare);
bool stringCompareSizeInsensitive(String string,String compare);
bool stringCompareSizeCaseInsensitive(String string,String compare);

String stringMake(char* cstring);
String numberToString(char* buffer,int number);
String stringCopy(MemoryArena* arena,String string);
int stringToNumber(String string);
String stringInString(String string_1,String string_2);
String stringConcat(MemoryArena* arena,String string,String append);
String stringConcatChar(MemoryArena* arena,String string,char c);
String stringInsertChar(MemoryArena* arena,String string,char c,int index);
String stringWordSlice(String string);
void stringToUpper(String string);
void stringToLower(String string);

#define STRING_LITERAL(STRING) {.size = sizeof(STRING) - 1,.data = (char*)(STRING)}

static String stringForwardSlice(String string,int amount){
    if(string.size <= amount)
        return (String){0};
    return (String){.data = string.data + amount,.size = string.size - amount};
}

#endif
