#include <fcntl.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>

#include "gem5/m5ops.h"

//#define ARRAYSIZE 16000000
#define CACHESIZE 64*1024*8 // (8192*4 = 32KB) anyways * 2 JIC
#define REGION_NVDLA 128*1024*1024*8 // 256MB

int main(int argc, char *argv[]) {
    FILE* fp = fopen(argv[1], "rb");

    if (!fp) {
        printf("Trace file open failed.\n");
        return 0;
    } else
        printf("Trace file opened successfully.\n");

    void *region_nvdla_0, *region_nvdla_1;
    int size = 0;
    region_nvdla_0 = aligned_alloc(sizeof(int)*16, REGION_NVDLA);
    region_nvdla_1 = aligned_alloc(sizeof(int)*16, REGION_NVDLA);
    char *ptr_0 = (char *)region_nvdla_0, *ptr_1 = (char *)region_nvdla_1;

    do {
        int temp_char = fgetc(fp);
        if(feof(fp))
            break;

        ptr_0[size] = temp_char;
        ptr_1[size] = temp_char;
        size++;
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

            ptr_0[size] = temp_char;
            ptr_1[size] = temp_char;
            size++;
        } while(1);
        fclose(fp);
    }

    //! nvdla 0 running start
    m5_start_accel_id((uint64_t)region_nvdla_0, size, (uint64_t)region_nvdla_0, 0);

    bool needToWaitForNVDLA = true;
    while (needToWaitForNVDLA)
        needToWaitForNVDLA = m5_wait_accel_id(0);

    needToWaitForNVDLA = true;
    //! nvdla 0 running complete

    //! nvdla 1 running start
    m5_start_accel_id((uint64_t)region_nvdla_1, size, (uint64_t)region_nvdla_1, 1);
    while (needToWaitForNVDLA)
        needToWaitForNVDLA = m5_wait_accel_id(1);
    //! nvdla 1 running complete

    return 0;
}
