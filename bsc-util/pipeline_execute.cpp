#include <fcntl.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
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


size_t get_trace_from_file(const std::string& file_name_prefix, char* dst) {
    // open trace file
    std::string trace_file_name = file_name_prefix + "_trace.bin";
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
    std::string var_log_file_name = file_name_prefix + "_rd_only_var_log";
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
    return size;
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

    uint32_t trace_sizes[worker_num * batch_num];
    for(int i = 0; i < worker_num * batch_num; i++) {
        std::string file_name_prefix = trace_file_prefix + std::to_string(i / worker_num + 1) + '_' + std::to_string(i % worker_num + 1);
        trace_sizes[i] = get_trace_from_file(file_name_prefix, (char*)(region_nvdlas[i % worker_num]) + (i / worker_num) * REGION_NVDLA);
    }

    while(1) {
        for(int i = 0; i < worker_num; i++) {
            if(!accel_busy[i] && task_map[(i + 1) * map_time_size + wave_front[i]] == NOT_LAUNCHED &&
               task_map[(i + 1 - 1) * map_time_size + (wave_front[i] - 1)] == FINISHED &&
               task_map[(i + 1) * map_time_size + wave_front[i] - 1] == FINISHED) {

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
