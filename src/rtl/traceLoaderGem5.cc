/* Obtained from nvdla.cpp
 * but Modified
 * Driver for Verilator testbench
 * NVDLA Open Source Project
 *
 * Copyright (c) 2017 NVIDIA Corporation.  Licensed under the NVDLA Open
 * Hardware License.  For more information, see the "LICENSE" file that came
 * with this distribution.
 */

#include "rtl/traceLoaderGem5.hh"

namespace gem5
{

TraceLoaderGem5::TraceLoaderGem5(CSBMaster *_csb,
                                 AXIResponder *_axi_dbb,
                                 AXIResponder *_axi_cvsram) {
        csb = _csb;
        axi_dbb = _axi_dbb;
        axi_cvsram = _axi_cvsram;
        _test_passed = 1;
        base_addr = -1;
    }

void
TraceLoaderGem5::read_local(int &last, char *buffer_trace,
                            void *buffer, unsigned int nbytes) {
    char *buffer_char = (char*) buffer;

    for (int i = 0; i < nbytes; i++) {
        buffer_char[i] = buffer_trace[last+i];
    }

    last += nbytes;
}

void
TraceLoaderGem5::load(char *trace) {

    int last=0;


    unsigned char cmd;

// original
//#define VERILY_READ(p, n) do { read_local(last, (trace), (p), (n));
//} while (0)
#define VERILY_READ(p, n) {\
  read_local(last, (trace), (p), (n));\
}
    do {
        VERILY_READ(&cmd, 1);

        switch (cmd) {
        case 1:
            printf("CMD: wait\n");
            csb->ext_event(TRACE_WFI);
            break;
        case 2: {
            uint32_t addr;
            uint32_t data;
            VERILY_READ(&addr, 4);
            VERILY_READ(&data, 4);
            printf("CMD: write_reg %08x %08x\n", addr, data);
            csb->write(addr, data);
            break;
        }
        case 3: {
            uint32_t addr;
            uint32_t mask;
            uint32_t data;
            VERILY_READ(&addr, 4);
            VERILY_READ(&mask, 4);
            VERILY_READ(&data, 4);
            printf("CMD: read_reg %08x %08x %08x\n", addr, mask, data);
            csb->read(addr, mask, data);
            break;
        }
        case 4: {
            uint32_t addr;
            uint32_t len;
            uint8_t *buf;
            uint32_t namelen;
            char *fname;
            axi_op op;

            VERILY_READ(&addr, 4);
            VERILY_READ(&len, 4);
            buf = (uint8_t *)malloc(len);
            VERILY_READ(buf, len);

            VERILY_READ(&namelen, 4);
            fname = (char *) malloc(namelen+1);
            VERILY_READ(fname, namelen);
            fname[namelen] = 0;

            op.opcode = AXI_DUMPMEM;
            op.addr = addr;
            op.len = len;
            op.buf = buf;
            op.fname = fname;
            opq.push(op);
            csb->ext_event(TRACE_AXIEVENT);

            printf("CMD: dump_mem %08x bytes from %08x -> %s\n",
                    len, addr, fname);
            break;
        }
        case 5: {
            uint32_t addr;
            uint32_t len;
            uint8_t *buf;
            axi_op op;

            VERILY_READ(&addr, 4);
            VERILY_READ(&len, 4);
            buf = (uint8_t *)malloc(len);
            VERILY_READ(buf, len);

            op.opcode = AXI_LOADMEM;
            op.addr = addr;
            op.len = len;
            op.buf = buf;
            opq.push(op);
            csb->ext_event(TRACE_AXIEVENT);
            base_addr = addr&0xF0000000;

            printf("CMD: load_mem %08x bytes to %08x\n", len, addr);
            break;
        }
        case 0xFF:
            printf("CMD: done\n");
            break;
        default:
            printf("unknown command %c\n", cmd);
            abort();
        }
    } while (cmd != 0xFF);

}

void
TraceLoaderGem5::axievent() {
    if (opq.empty()) {
        printf("extevent with nothing in the queue?\n");
        abort();
    }

    axi_op &op = opq.front();

    AXIResponder *axi;
    if ((op.addr & 0xF0000000) == 0x50000000)
        axi = axi_cvsram;
    else if ((op.addr & 0xF0000000) == 0x80000000)
        axi = axi_dbb;
    else {
        printf("AXI event to bad offset\n");
        abort();
    }

    switch(op.opcode) {
    // In this case we are actually writing to memory, hence
    // we can modify this function to write to the DRAM
    case AXI_LOADMEM: {
        // here we are at the beggining of the trace, and hence
        // we don't care much about timing
        const uint8_t *buf = op.buf;
        printf("AXI: loading (TRACE) memory at 0x%08x\n", op.addr);
        while (op.len) {
            // this op we can do it atomicly
            // don't acces in timing here
            axi->write(op.addr, *buf, false);
            buf++;
            op.addr++;
            op.len--;
        }
        break;
    }
    case AXI_DUMPMEM: {
        int fd;
        const uint8_t *buf = op.buf;
        int matched = 1;

        printf("AXI: dumping memory to %s\n", op.fname);
        fd = creat(op.fname, 0666);
        if (!fd) {
            perror("creat(dumpmem)");
            break;
        }
        while (op.len) {
            uint8_t da = axi->read(op.addr);
            write(fd, &da, 1);
            if (da != *buf && matched) {
                printf("AXI: FAIL: mismatch at memory address %08x\
                 (exp 0x%02x, got 0x%02x), and maybe others too\n",
                  op.addr, *buf, da);

                matched = 0;
                _test_passed = 0;
            }
            buf++;
            op.addr++;
            op.len--;
        }
        close(fd);

        if (matched)
            printf("AXI: memory dump matched reference\n");
        break;
    }
    default:
        abort();
    }

    opq.pop();
}

int
TraceLoaderGem5::test_passed() {
    return _test_passed;
}

uint32_t
TraceLoaderGem5::getBaseAddr() {
    return base_addr;
}

} // namespace gem5



