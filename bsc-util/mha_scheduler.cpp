#include <fcntl.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>

#include "gem5/m5ops.h"

#define REGION_NVDLA 1024*1024


size_t get_trace_from_file(const std::string& dir, int head_id, char* dst) {    // input head_id starts from 0, but in file names start from '1'
    // open trace file
    char subscript_char = '1' + head_id;
    std::string trace_file_name = dir + std::string("trace.bin_") + subscript_char;
    FILE* fp = fopen(trace_file_name.c_str(), "rb");

    if (!fp) {
        printf("Trace file open failed.\n");
        exit(0);
    }

    int size = 0;
    do {
        int temp_char = fgetc(fp);
        if(feof(fp))
            break;

        dst[size++] = temp_char;
    } while(1);
    fclose(fp);

    // open rd_only_var_log
    std::string var_log_file_name = dir + std::string("rd_only_var_log_") + subscript_char;
    fp = fopen(var_log_file_name.c_str(), "rb");

    if(fp) {    // some tests may not have this log file, that's ok
        do {
            int temp_char = fgetc(fp);
            if(feof(fp))
                break;

            dst[size++] = temp_char;
        } while(1);
        fclose(fp);
    }

    // write the following 8 bytes (2 x uint32_t with 0xffffffff) to mark the end of rd_only_var_log
    dst[size++] = 0xff;
    dst[size++] = 0xff;
    dst[size++] = 0xff;
    dst[size++] = 0xff;

    dst[size++] = 0xff;
    dst[size++] = 0xff;
    dst[size++] = 0xff;
    dst[size++] = 0xff;

    return size;
}

int main(int argc, char *argv[]) {
    std::string dir = std::string(argv[1]);

    uint32_t worker_num = 4;
    void* region_nvdlas[worker_num];
    for(int i = 0; i < worker_num; i++) {
        region_nvdlas[i] = aligned_alloc(sizeof(int)*16,REGION_NVDLA);
    }

    size_t trace_sizes[worker_num];
    for(int i = 0; i < worker_num; i++) {
        trace_sizes[i] = get_trace_from_file(dir, i, (char*)region_nvdlas[i]);
    }

    bool need_waiting[4] = {false, false, false, false};

    for(int i = 0; i < worker_num; i++) {
        m5_start_accel_id((uint64_t)region_nvdlas[i], trace_sizes[i], (uint64_t)region_nvdlas[i], i);
        need_waiting[i] = true;
    }

    while(need_waiting[0] || need_waiting[1] || need_waiting[2] || need_waiting[3]) {
        for(int i = 0; i < worker_num; i++) {
            need_waiting[i] = m5_wait_accel_id(i);
        }
    }

    return 0;
}
