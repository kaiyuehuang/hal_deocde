#include <stdint.h>
#include <stdio.h>
#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#else
#include <sys/time.h>
#include <unistd.h>
#endif
#include <fstream>
#include <string>
int64_t getTimestamp(){
#if defined(_WIN32) || defined(_WIN64)
#define EPOCHFILETIME   (116444736000000000UL)
    FILETIME ft;
    LARGE_INTEGER li;
    int64_t tt = 0;
    GetSystemTimeAsFileTime(&ft);
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    tt = (li.QuadPart - EPOCHFILETIME) /10;
    return tt;
#else
    struct timeval time1;
    gettimeofday(&time1,NULL);
    long long startTime = (long long)time1.tv_sec*1000 + (long long )time1.tv_usec / 1000;
    return startTime;
#endif
}

void sleepMs(int ms) {
#if defined(_WIN32) || defined(_WIN64)
    Sleep(ms);
#else
    usleep(ms * 1000);
#endif    
}
int IsStringNumbers(const char *str){
    int len = strlen(str), i = 0;
    for (i = 0; i < len; i++){
        if (!(isdigit(str[i])))
            return 0;
    }
    return 1;
}
bool isFileExistsIfstream(std::string name) {
    std::ifstream f(name.c_str());
    return f.good();
}
