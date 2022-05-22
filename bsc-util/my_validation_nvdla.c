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

#include "gem5/m5ops.h"

//#define ARRAYSIZE 16000000
#define CACHESIZE 64*1024*8 // (8192*4 = 32KB) anyways * 2 JIC
#define REGION_NVDLA 128*1024*1024*8 // 256MB

int main(int argc, char *argv[])
{
    std::ifstream stream;
    stream.open(argv[1], std::ios::in | std::ios::binary);

    if (!stream) {
        std::cout << "Trace file open failed.\n";
        return 0;
    } else {
        std::cout << "Trace file opened successfully.\n";
    }

    std::vector<char> trace((std::istreambuf_iterator<char>(stream)),
                                std::istreambuf_iterator<char>());

    std::cout << "Check trace contents from validation_nvdla:\n\n";

    for (int i = 0; i < 30; i++)
        printf("trace[%d] = 0x%02x, char = '%c'\n", \
        i, (uint8_t)(trace[i]), trace[i]);

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



    m5_start_accel((uint64_t)region_nvdla,size,(uint64_t)region_nvdla);


    bool needToWaitForNVDLA = true;

    while (needToWaitForNVDLA) {
        needToWaitForNVDLA = m5_wait_accel((uint64_t)region_nvdla,size);
        // loose time?
        int sum = 0;
        for (int i=0;i<1000;i++) {
            sum+=i;
        }
    }




    return 0;
}
