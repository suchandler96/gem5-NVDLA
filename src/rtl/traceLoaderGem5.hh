/* Obtained from nvdla.cpp
 * but Modified
 * Driver for Verilator testbench
 * NVDLA Open Source Project
 *
 * Copyright (c) 2017 NVIDIA Corporation.  Licensed under the NVDLA Open
 * Hardware License.  For more information, see the "LICENSE" file that came
 * with this distribution.
 */
#ifndef __TRACE_LOADER__GEM5__
#define __TRACE_LOADER__GEM5__

//#include "accelerator.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "axiResponder.hh"
#include "csbMaster.hh"

namespace gem5
{

class TraceLoaderGem5 {
    enum axi_opc {
        AXI_LOADMEM,
        AXI_DUMPMEM
    };

    struct axi_op {
        axi_opc opcode;
        uint32_t addr;
        uint32_t len;
        const uint8_t *buf;
        const char *fname;
    };
    std::queue<axi_op> opq;

    CSBMaster *csb;
    AXIResponder *axi_dbb, *axi_cvsram;

    uint32_t base_addr;
    uint32_t trace_size;    // this value will be valid after trace->load(), where we find 0xff as the end of trace

    int _test_passed;

public:
    uint32_t trace_and_rd_log_size; // this value will be valid right after receiving CPU launch accel pkt
    enum stop_type {
        TRACE_CONTINUE = 0,
        TRACE_AXIEVENT,
        TRACE_WFI
    };

    TraceLoaderGem5(CSBMaster *_csb,
                    AXIResponder *_axi_dbb,
                    AXIResponder *_axi_cvsram);

    void read_local(int &last, const char *buffer_trace,
                    void *buffer, unsigned int nbytes);

    void load(const char *fname) ;
    void load_read_var_log(const char* fname);

    void axievent(int* waiting_for_gem5_mem);

    int test_passed();

    uint32_t getBaseAddr();
};

} // namespace gem5

#endif





