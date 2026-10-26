#include "print.h"
#include "syscall.h"

void print(String str){
    systemWrite(1,str.data,str.size);
}

void printNumber(int number){
    char buffer[0x10];
    String string = numberToString(buffer,number);

    print(string);
}

void printNumberNL(int number){
    printNumber(number);
    print(stringMake("\n"));
}
