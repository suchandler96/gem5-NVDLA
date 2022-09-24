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
TraceLoaderGem5::read_local(int &last, const char *buffer_trace,
                            void *buffer, unsigned int nbytes) {
    char *buffer_char = (char*) buffer;

    for (int i = 0; i < nbytes; i++) {
        buffer_char[i] = buffer_trace[last+i];
    }

    last += nbytes;
}

void
TraceLoaderGem5::load(const char *trace) {
    int last = 0;
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
        case 6: {
            uint32_t addr;
            uint32_t data;

            VERILY_READ(&addr, 4);
            VERILY_READ(&data, 4);
            printf("CMD: until %08x %08x\n", addr, data);
            csb->wait_until(addr, uint32_t(0xffffffff), data);
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

    trace_size = last;      // update reg trace size
}

void
TraceLoaderGem5::load_read_var_log(const char* trace) {
    // assume we start from the end of reg txn trace
    printf("trace size = %d bytes, trace&log_size = %d bytes\n", trace_size, trace_and_rd_log_size);
    if (trace_size == trace_and_rd_log_size) {
        printf("specified to prefetch, but no rd_log file is provided.\n");
        abort();
    }

    int rd_log_idx = trace_size;
    while (rd_log_idx < trace_and_rd_log_size) {
        uint32_t addr;
        uint32_t size;
        read_local(rd_log_idx, trace, &addr, 4);
        read_local(rd_log_idx, trace, &size, 4);
        printf("model var addr = 0x%08x, size = 0x%08x\n", addr, size);
        // assume we only use dram port
        axi_dbb->add_rd_var_log_entry(addr, size);
    }
}

void
TraceLoaderGem5::axievent(int* waiting_for_gem5_mem) {
    if (opq.empty()) {
        printf("extevent with nothing in the queue?\n");
        abort();
    }

    axi_op &op = opq.front();

    AXIResponder *axi;                              // todo: because nvdla compiler has addr reuse, 0x9: all data
    if ((op.addr & 0xF0000000) == 0x50000000)       // in nvdla.cpp, offset can only be 0x5 or 0x8.
        axi = axi_cvsram;                           // but here use 0x8 to denote read-only variables, 0x9: write-only
    else if ((op.addr & 0xF0000000) >= 0x80000000)  // and 0xa to denote read-and-write variables
        axi = axi_dbb;                              // so that different caching policies can be adopted
    else {
        printf("AXI event to bad offset\n");
        abort();
    }

    switch(op.opcode) {
    // In this case we are actually writing to memory, hence
    // we can modify this function to write to the DRAM
    case AXI_LOADMEM: {
        // here we are at the beginning of the trace, and hence
        // we don't care much about timing
        const uint8_t *buf = op.buf;
        printf("AXI: loading (TRACE) memory at 0x%08x, length = %d\n",\
        op.addr, op.len);
        while (op.len) {
            // this op we can do it atomicly
            // don't access in timing here
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

        if (!*waiting_for_gem5_mem) {
            // create the file to dump to
            printf("AXI: dumping memory to %s, length = %d\n", op.fname, op.len);
            fd = creat(op.fname, 0666);
            if (!fd) {
                perror("creat(dumpmem)");
                break;
            }
            close(fd);

            // issue memory reading request
            axi->read_for_traceLoaderGem5(op.addr, op.len);
            *waiting_for_gem5_mem = 1;

        } else {    // waiting for gem5 memory
            uint32_t txn_start_addr = op.addr & ~(uint32_t)(AXI_WIDTH / 8 - 1);
            uint32_t bytes_to_write = AXI_WIDTH / 8;
            bool aligned = true;
            if (op.addr != txn_start_addr) {
                bytes_to_write = AXI_WIDTH / 8 - (op.addr - txn_start_addr);
                if(op.len < bytes_to_write)
                    bytes_to_write = op.len;
                aligned = false;
            } else if (op.len < AXI_WIDTH / 8) {
                // this txn is aligned but of length < AXI_WIDTH / 8
                bytes_to_write = op.len;
            }

            uint8_t read_response_buffer[AXI_WIDTH / 8];

            uint32_t got_response = axi->read_response_for_traceLoaderGem5(txn_start_addr, read_response_buffer);

            if (got_response) {
                uint32_t old_op_addr = op.addr;
                // first we update the op info, which is related to the next txn (if there is)
                if (!aligned) {
                    op.addr += bytes_to_write;
                    op.len -= bytes_to_write;
                } else if (op.len < AXI_WIDTH / 8) {
                    op.addr += op.len;
                    op.len = 0;
                } else {
                    op.addr += (AXI_WIDTH / 8);
                    op.len -= (AXI_WIDTH / 8);
                }

                // continue writing the file created above
                fd = open(op.fname, O_WRONLY | O_APPEND);
                if (!fd) {
                    perror("Open file failed.\n");
                    abort();
                }

                write(fd, read_response_buffer + (old_op_addr - txn_start_addr), bytes_to_write);
                close(fd);

                if (op.len <= 0) {
                    if (axi->getRequestsOnFlight() != 0) {
                        printf("op.len == 0 is not synchronous with inflight_req_order.empty().\n");
                        abort();
                    }
                    *waiting_for_gem5_mem = 0;

                    // check answer
                    uint8_t check_byte, bytes_got = 0;
                    int byte_cnt = 0;
                    fd = open(op.fname, O_RDONLY);
                    while (1) {
                        bytes_got = read(fd, &check_byte, 1);
                        if (bytes_got == 0)
                            break;
                        if (check_byte != *buf && matched) {
                            printf("Memory dump does not match golden answer at byte %d:exp 0x%02x, got 0x%02x, and maybe others too.\n", byte_cnt, *buf, check_byte);
                            matched = 0;
                            _test_passed = 0;
                        }
                        buf++;
                        byte_cnt++;
                    }
                    close(fd);
                    if (matched)
                        printf("AXI: memory dump matched reference.\n");
                }
            }
        }
        break;
    }
    default:
        abort();
    }

    if (!*waiting_for_gem5_mem)
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



