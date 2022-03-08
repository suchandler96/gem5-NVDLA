
/* Obtained from nvdla.cpp
 * but Modified
 * Driver for Verilator testbench
 * NVDLA Open Source Project
 *
 * Copyright (c) 2017 NVIDIA Corporation.  Licensed under the NVDLA Open
 * Hardware License.  For more information, see the "LICENSE" file that came
 * with this distribution.
 */
#ifndef __CSB_MASTER__
#define __CSB_MASTER__

#include "wrapper_nvdla.hh"

class Wrapper_nvdla;

class CSBMaster {
    struct csb_op {
        int is_ext;
        int write;
        int tries;
        int reading;
        uint32_t addr;
        uint32_t mask;
        uint32_t data;
    };

    std::queue<csb_op> opq;

    VNV_nvdla *dla;

    Wrapper_nvdla *wrapper;

    int _test_passed;

public:
    CSBMaster(VNV_nvdla *_dla, Wrapper_nvdla *_wrapper);

    void read(uint32_t addr, uint32_t mask, uint32_t data);

    void write(uint32_t addr, uint32_t data);

    void ext_event(int ext);

    int eval(int noop);

    bool done();

    int test_passed(); 
};
#endif // __CSB_MASTER__
