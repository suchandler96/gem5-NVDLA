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

    void *region_nvdla_0, *region_nvdla_1;
    int size = trace.size();
    region_nvdla_0 = aligned_alloc(sizeof(int)*16, REGION_NVDLA);
    region_nvdla_1 = aligned_alloc(sizeof(int)*16, REGION_NVDLA);
    char *ptr_0 = (char *)region_nvdla_0;
    char *ptr_1 = (char *)region_nvdla_1;



    for (int i = 0; i < trace.size(); i++)
        ptr_0[i] = trace[i];
    for (int i = 0; i < trace.size(); i++)
        ptr_1[i] = trace[i];

    //! nvdla 0 running start
    m5_start_accel_id((uint64_t)region_nvdla_0, size, (uint64_t)region_nvdla_0, 0);


    bool needToWaitForNVDLA = true;

    while (needToWaitForNVDLA) {
        needToWaitForNVDLA = m5_wait_accel_id(0);
    }

    needToWaitForNVDLA = true;
    //! nvdla 0 running complete

    //! nvdla 1 running start
    m5_start_accel_id((uint64_t)region_nvdla_1, size, (uint64_t)region_nvdla_1, 1);
    while (needToWaitForNVDLA) {
        needToWaitForNVDLA = m5_wait_accel_id(1);
    }
    //! nvdla 1 running complete



    return 0;
}
