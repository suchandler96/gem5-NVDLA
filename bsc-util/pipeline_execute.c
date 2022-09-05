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
#include <string>

#include "gem5/m5ops.h"

#define REGION_NVDLA 1024*1024


enum TASK_STATUS {
    NOT_LAUNCHED,
    LAUNCHED,
    FINISHED
};


double wall_time() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return 1. * t.tv_sec + 1.e-9 * t.tv_nsec;
}


size_t get_trace_from_file(const char* file_name, char* dst) {
    std::ifstream stream;
    stream.open(file_name, std::ios::in | std::ios::binary);

    if (!stream) {
        printf("Trace file %s open failed.\n", file_name);
        exit(0);
    }

    std::vector<char> trace((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    for(int i = 0, size = trace.size(); i < size; i++) dst[i] = trace[i];

    return trace.size();
}


int main(int argc, char *argv[]) {
    std::string trace_file_prefix = std::string(argv[1]);

    int batch_num = atoi(argv[2]);      // for lenet testcase, batch_num = 4
    int worker_num = atoi(argv[3]);     // for lenet testcase, worker_num = 2

    int map_time_size = batch_num + worker_num;
    int map_space_size = worker_num + 1;

    uint8_t task_map[map_time_size * map_space_size]; //= {2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 0, 0, 0, 0, 2};
    for(int i = 0; i < map_time_size * map_space_size; i++) task_map[i] = FINISHED;
    for(int i = 1; i <= worker_num; i++) for(int j = 0; j < batch_num; j++) task_map[i * (map_time_size + 1) + j] = NOT_LAUNCHED;

    uint32_t wave_front[worker_num];
    for(int i = 0; i < worker_num; i++) wave_front[i] = i + 1;

    uint8_t accel_busy[worker_num] = {0};

    void* region_nvdlas[worker_num];
    for(int i = 0; i < worker_num; i++) {
        region_nvdlas[i] = aligned_alloc(sizeof(int)*16,REGION_NVDLA * batch_num);
    }

    // void *region_nvdla_1, *region_nvdla_2;
    // region_nvdla_1 = aligned_alloc(sizeof(int)*16,REGION_NVDLA * 4);
    // region_nvdla_2 = aligned_alloc(sizeof(int)*16,REGION_NVDLA * 4);
    //
    // void* region_nvdlas[2] = {region_nvdla_1, region_nvdla_2};

    uint32_t trace_sizes[worker_num * batch_num];
    for(int i = 0; i < worker_num * batch_num; i++) {
        std::string file_name = trace_file_prefix + std::to_string(i / worker_num + 1) + '_' + std::to_string(i % worker_num + 1) + "_trace.bin";
        trace_sizes[i] = get_trace_from_file(file_name.c_str(), (char*)(region_nvdlas[i % worker_num]) + (i / worker_num) * REGION_NVDLA);
    }

    while(1) {
        for(int i = 0; i < worker_num; i++) {
            if(!accel_busy[i] && task_map[(i + 1) * map_time_size + wave_front[i]] == NOT_LAUNCHED &&
               task_map[(i + 1 - 1) * map_time_size + (wave_front[i] - 1)] == FINISHED &&
               task_map[(i + 1) * map_time_size + wave_front[i] - 1] == FINISHED) {

                // std::string file_name = trace_file_prefix + std::to_string(wave_front[i] - i) + '_' + std::to_string((i + 1)) + "_trace.bin";
                // file name: first number is batch id (starting from 1), second is accelerator id (starting from 1)

                // size_t size = get_trace_from_file(file_name.c_str(), (char*)region_nvdlas[i]);
                uint64_t trace_addr = (uint64_t)(region_nvdlas[i]) + (wave_front[i] - i - 1) * REGION_NVDLA;

                // accel id and batch id start from 1
                printf("NVDLA %d launched batch %d at t = %f\n", i + 1, wave_front[i] - i, wall_time());

                m5_start_accel_id(trace_addr, trace_sizes[worker_num * (wave_front[i] - i - 1) + i], trace_addr, i);
                accel_busy[i] = 1;
                task_map[(i + 1) * map_time_size + wave_front[i]] = LAUNCHED;
            }
        }

        for(int i = 0; i < worker_num; i++) {
            if(accel_busy[i]) {
                accel_busy[i] = m5_wait_accel_id(i);
                if(!accel_busy[i]) {
                    printf("NVDLA %d finished batch %d at t = %f\n", i + 1, wave_front[i] - i, wall_time());
                    task_map[(i + 1) * map_time_size + wave_front[i]] = FINISHED;
                    wave_front[i]++;
                }
            }
        }

        if(task_map[map_time_size * map_space_size - 1] == FINISHED) {
            printf("finished all ops\n");
            break;
        }
    }

    return 0;
}
