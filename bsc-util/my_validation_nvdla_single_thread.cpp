#include <fcntl.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>

#include "gem5/m5ops.h"

//#define ARRAYSIZE 16000000
#define CACHESIZE 64*1024*8 // (8192*4 = 32KB) anyways * 2 JIC
#define REGION_NVDLA 128*1024*1024*8 // 256MB

double wall_time() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1. * t.tv_sec + 1.e-9 * t.tv_nsec;
}


int main(int argc, char *argv[]) {
    // load trace.bin
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        printf("Trace file open failed.\n");
        return 0;
    } else
        printf("Trace file opened successfully.\n");

    int size = 0;
    void* region_nvdla = aligned_alloc(sizeof(int) * 16,REGION_NVDLA);
    char* ptr = (char*)region_nvdla;

    do {
        int temp_char = fgetc(fp);
        if(feof(fp))
            break;

        ptr[size++] = temp_char;
    } while(1);
    fclose(fp);

    if (argc > 2) {
        // load rd_only_var_log
        fp = fopen(argv[2], "rb");
        if (!fp) {
            printf("rd_only_var_log open failed.\n");
            return 0;
        } else
            printf("rd_only_var_log opened successfully.\n");

        do {
            int temp_char = fgetc(fp);
            if (feof(fp))
                break;

            ptr[size++] = temp_char;
        } while(1);
        fclose(fp);
    }

    printf("start_time = %f\n", wall_time());
    m5_start_accel_id((uint64_t)region_nvdla, size, (uint64_t)region_nvdla, 0);


    bool needToWaitForNVDLA = true;
    while (needToWaitForNVDLA)
        needToWaitForNVDLA = m5_wait_accel_id(0);

    printf("end_time = %f\n", wall_time());
    return 0;
}
