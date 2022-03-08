// example1.c
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#include <algorithm> // for std::copy
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <queue>
#include <vector>

#ifdef ENABLE_PARSEC_HOOKS
#include "hooks.h"

#endif

//#define ARRAYSIZE 16000000
#define CACHESIZE 64*1024*8 // (8192*4 = 32KB) anyways * 2 JIC
#define REGION_NVDLA 128*1024*1024*8 // 256MB

int main(int argc, char *argv[])
{
    std::ifstream stream(argv[1], std::ios::in | std::ios::binary);
    std::vector<char> trace((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());
    int size = 0;
    void *region_nvdla;
    size = trace.size();
    //a = malloc(sizeof(char)*size);
    region_nvdla = aligned_alloc(sizeof(int)*16,REGION_NVDLA);
    char *ptr = (char *)region_nvdla;
    //char *ptr2 = (char *)region_nvdla;

    // Create malloc of 1GB
    //region_nvdla = aligned_alloc(sizeof(int)*16,sizeof(int)*CACHESIZE*2);

    //printf("region_nvdla: %#x\n",region_nvdla);


    for (int i = 0; i < trace.size(); i++) {
        //printf("%02x \n", trace[i]);
        ptr[i] = trace[i];
        //printf("%02x \n", ptr[i]);
        //std::cout << buffer_trace[last+i] << std::endl;
    }
    //void *b;
    //b = aligned_alloc(sizeof(int)*16,sizeof(int)*CACHESIZE);
    //int * bb = (int *) b;
    //for (int i = 0; i < CACHESIZE; i++) {
    //    bb[i] = 123;
    //}
    //int *pointer = ;


#ifdef ENABLE_PARSEC_HOOKS
    __parsec_roi_begin();

    __start_accel((int*)region_nvdla,size,(int *)region_nvdla);


    bool needToWaitForNVDLA = true;

    while (needToWaitForNVDLA) {
        needToWaitForNVDLA = __wait_accel((int *)region_nvdla,size);
        // loose time?
        int sum = 0;
        for (int i=0;i<1000;i++) {
            sum+=i;
        }
    }

    __parsec_roi_end();
#endif



    return 0;
}
