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
#include "axiResponder.hh"
#include "csbMaster.hh"

namespace gem5
{

//class CSBMaster;
//class AXIResponder;

class TraceLoaderGem5
{
        enum axi_opc
        {
                AXI_LOADMEM,
                AXI_DUMPMEM
        };

        struct axi_op
        {
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

        int _test_passed;

public:
        enum stop_type
        {
                TRACE_CONTINUE = 0,
                TRACE_AXIEVENT,
                TRACE_WFI
        };

        TraceLoaderGem5(CSBMaster *_csb,
                        AXIResponder *_axi_dbb,
                        AXIResponder *_axi_cvsram);

        void read_local(int &last, char *buffer_trace,
                        void *buffer, unsigned int nbytes);

        void load(char *fname) ;

        void axievent();

        int test_passed();

    uint32_t getBaseAddr();
};

} // namespace gem5

#endif





