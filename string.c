#include "string.h"
#include "memory.h"
#include "tmath.h"
#include "libc.h"

static int numberStringLength(int number){
    int length = 0;
    if(!number)
        return 1;
    if(number < 0)
        length += 1;
    while(number){
        number /= 10;
        length += 1;
    }
    return length;
}

int stringToNumber(String string){
    int number = 0;

    if(string.size <= 0)
        return number;
    
    bool negative = *string.data == '-'; 
    if(negative)
        string.data += 1;
    
    while(
          *string.data >= '0' &&
          *string.data <= '9' &&
          string.size > 0
    ){
        number *= 10;
        number += *string.data++ - '0';
        string.size -= 1;
    }
    return negative ? -number : number;
}

bool stringCompare(String string,String compare){
    if(string.size != compare.size)
        return false;
    for(int i = compare.size;i--;){
        if(*string.data++ != *compare.data++)
            return false;
    }
    return true;
}

bool stringCompareSizeInsensitive(String string,String compare){
    if(string.size < compare.size)
        return false;
    for(int i = compare.size;i--;){
        if(*string.data++ != *compare.data++)
            return false;
    }
    return true;
}

bool stringCompareSizeCaseInsensitive(String string,String compare){
    if(string.size < compare.size)
        return false;
    for(int i = compare.size;i--;){
        char c1 = string.data[i] >= 'a' && string.data[i] <= 'z' ? string.data[i] & ~0x20 : string.data[i];
        char c2 = compare.data[i] >= 'a' && compare.data[i] <= 'z' ? compare.data[i] & ~0x20 : compare.data[i];
        if(c1 != c2)
            return false;
    }
    return true;
}

String stringWordSlice(String string){
    String word = {.data = string.data};
    for(int i = 0;i < string.size;i++){
        if(*string.data > ' ')
            string.data += 1;
        else
            goto succes;
    }
    return string;
 succes:
    word.size = string.data - word.data;
    return word;
}

void stringToUpper(String string){
    for(int i = string.size;i--;)
        string.data[i] = string.data[i] >= 'a' && string.data[i] <= 'z' ? string.data[i] & ~0x20 : string.data[i];
}

void stringToLower(String string){
    for(int i = string.size;i--;)
        string.data[i] = string.data[i] >= 'A' && string.data[i] <= 'Z' ? string.data[i] | 0x20 : string.data[i];
}

String stringConcat(MemoryArena* arena,String string,String append){
    int alloc_size = string.size + append.size;
    String string_new = {
        .data = arena ? memoryArenaAllocate(arena,alloc_size) : tMalloc(alloc_size),
        .size = alloc_size,
    };
    tMemcpy(string_new.data,string.data,string.size);
    tMemcpy(string_new.data + string.size,append.data,append.size);
    return string_new;
}

String stringConcatChar(MemoryArena* arena,String string,char append){
    int alloc_size = string.size + 1;
    String string_new = {
        .data = arena ? memoryArenaAllocate(arena,alloc_size) : tMalloc(alloc_size),
        .size = alloc_size,
    };
    tMemcpy(string_new.data,string.data,string.size);
    string_new.data[string.size] = append;
    return string_new;
}

String stringInsertChar(MemoryArena* arena,String string,char c,int index){
    int alloc_size = string.size + 1;
    String string_new = {
        .data = arena ? memoryArenaAllocate(arena,alloc_size) : tMalloc(alloc_size),
        .size = alloc_size,
    };
    tMemcpy(string_new.data,string.data,index);
    string_new.data[index] = c;
    tMemcpy(string_new.data + index + 1,string.data + index,string.size - index);
    return string_new;
}

String stringInString(String string_1,String string_2){
    if(string_1.size < string_2.size) 
        return (String){.data = nullptr,.size = 0};
    for(int i = 0;i <= string_1.size - string_2.size;i++){
        if(stringCompareSizeInsensitive((String){.data = string_1.data + i,.size = string_1.size - i},string_2))
            return (String){.data = string_1.data + i,.size = string_1.size - i};
    }
    return (String){.data = nullptr,.size = 0};
}

String stringMake(char* cstring){
    char* copy = cstring;
    while(*cstring++);
    size_t len = cstring - copy - 1;
    return (String){.data = copy,.size = len};
}

String numberToString(char* buffer,int number){
	int length = numberStringLength(number);
	int length_copy;
	bool negative = false;
	int number_copy;

    if(number < 0){
        negative = true;
        number = -number;
    }
	number_copy = number;
	length_copy = length;
 
    while(length--){
        buffer[length] = number % 10 + '0';
        number /= 10;
    }
    if(negative)
        buffer[0] = '-';
    buffer[length_copy] = 0;
    return (String){.data = buffer,.size = length_copy};
}

String stringCopy(MemoryArena* arena,String string){
    String string_new = {
        .data = arena ? memoryArenaAllocate(arena,string.size) : tMalloc(string.size),
        .size = string.size,
    };
    tMemcpy(string_new.data,string.data,string.size);
    return string_new;
}
