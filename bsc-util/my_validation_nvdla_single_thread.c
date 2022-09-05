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

double wall_time() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1. * t.tv_sec + 1.e-9 * t.tv_nsec;
}


int main(int argc, char *argv[])
{
    std::ifstream stream;
    stream.open(argv[1], std::ios::in | std::ios::binary);

    if (!stream) {
        printf("Trace file open failed.\n");
        return 0;
    } else {
        printf("Trace file opened successfully.\n");
    }

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

    printf("start_time = %f\n", wall_time());
    m5_start_accel_id((uint64_t)region_nvdla, size, (uint64_t)region_nvdla, 0);


    bool needToWaitForNVDLA = true;

    while (needToWaitForNVDLA) {
        needToWaitForNVDLA = m5_wait_accel_id(0);
    }

    printf("end_time = %f\n", wall_time());



    return 0;
}
