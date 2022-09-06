/* nvdla.cpp
 * Driver for Verilator testbench
 * NVDLA Open Source Project
 *
 * Copyright (c) 2017 NVIDIA Corporation.  Licensed under the NVDLA Open
 * Hardware License.  For more information, see the "LICENSE" file that came
 * with this distribution.
 * 
 * Slightly changed to be adapted for gem5 requirements
 * 
 * Guillem Lopez Paradis
 */


#include <assert.h>
#include "axiResponder.hh"

AXIResponder::AXIResponder(struct connections _dla,
                                   Wrapper_nvdla *_wrapper,
                                   const char *_name,
                                   bool sram_,
                                   const unsigned int maxReq) {
    dla = _dla;

    wrapper = _wrapper;

    *dla.aw_awready = 1;
    *dla.w_wready = 1;
    *dla.b_bvalid = 0;
    *dla.ar_arready = 1;
    *dla.r_rvalid = 0;

    read_resp_ready = 1;

    name = _name;

    sram = sram_;

    max_req_inflight = (maxReq<240) ? maxReq:240;

    // add some latency...
    for (int i = 0; i < AXI_R_LATENCY; i++) {
        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
                txn.rdata[i] = 0xAAAAAAAA;
        }

        r0_fifo.push(txn);
    }
}

uint8_t
AXIResponder::read_ram(uint32_t addr) {
    ram[addr / AXI_BLOCK_SIZE].resize(AXI_BLOCK_SIZE, 0);
    return ram[addr / AXI_BLOCK_SIZE][addr % AXI_BLOCK_SIZE];
}

void
AXIResponder::write_ram(uint32_t addr, uint8_t data) {
    ram[addr / AXI_BLOCK_SIZE].resize(AXI_BLOCK_SIZE, 0);
    ram[addr / AXI_BLOCK_SIZE][addr % AXI_BLOCK_SIZE] = data;
}

void
AXIResponder::write(uint32_t addr, uint8_t data, bool timing) {
    // always write to fake ram
    // write_ram(addr,data);
    // we access gem5 memory
    wrapper->addWriteReq(sram,timing,addr,data);
}

void
AXIResponder::eval_ram() {
    /* write request */
    if (*dla.aw_awvalid && *dla.aw_awready) {
        #ifdef PRINT_DEBUG
            printf("(%lu) %s: write request from dla, addr %08lx id %d\n",
                    wrapper->tickcount, name, *dla.aw_awaddr, *dla.aw_awid);
        #endif
        axi_aw_txn txn;

        txn.awid = *dla.aw_awid;
        txn.awaddr = *dla.aw_awaddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        txn.awlen = *dla.aw_awlen;
        aw_fifo.push(txn);

        *dla.aw_awready = 0;
    } else
        *dla.aw_awready = 1;

    /* write data */
    if (*dla.w_wvalid) {
        #ifdef PRINT_DEBUG
            printf("(%lu) %s: write data from dla (%08x %08x...)\n",
                wrapper->tickcount, name, dla.w_wdata[0], dla.w_wdata[1]);
        #endif
        axi_w_txn txn;

        for (int i = 0; i < AXI_WIDTH / 32; i++)
            txn.wdata[i] = dla.w_wdata[i];
        txn.wstrb = *dla.w_wstrb;
        txn.wlast = *dla.w_wlast;
        w_fifo.push(txn);
    }

    /* read request */
    if (*dla.ar_arvalid && *dla.ar_arready) {
        uint64_t addr = *dla.ar_araddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        uint8_t len = *dla.ar_arlen;

        #ifdef PRINT_DEBUG
            printf("(%lu) %s: %s %08lx burst %d id %d\n",
                    wrapper->tickcount, name,
                   "read request from dla, addr",
                   *dla.ar_araddr,
                   *dla.ar_arlen, *dla.ar_arid);
        #endif

        do {
            axi_r_txn txn;

            txn.rvalid = 1;
            txn.rlast = len == 0;
            txn.rid = *dla.ar_arid;

            for (int i = 0; i < AXI_WIDTH / 32; i++) {
                uint32_t da = read_ram(addr + i * 4) +
                                (read_ram(addr + i * 4 + 1) << 8) +
                                (read_ram(addr + i * 4 + 2) << 16) +
                                (read_ram(addr + i * 4 + 3) << 24);
                txn.rdata[i] = da;
            }

            r_fifo.push(txn);

            addr += AXI_WIDTH / 8;
        } while (len--);

        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            txn.rdata[i] = 0x55555555;
        }

        for (int i = 0; i < AXI_R_DELAY; i++)
            r_fifo.push(txn);

        *dla.ar_arready = 0;
    } else
        *dla.ar_arready = 1;

    /* now handle the write FIFOs ... */
    if (!aw_fifo.empty() && !w_fifo.empty()) {
        axi_aw_txn &awtxn = aw_fifo.front();
        axi_w_txn &wtxn = w_fifo.front();

        if (wtxn.wlast != (awtxn.awlen == 0)) {
            printf("(%lu) %s: wlast / awlen mismatch\n",
                   wrapper->tickcount, name);
            abort();
        }

        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            if (!((wtxn.wstrb >> i) & 1))
                continue;

            write_ram(awtxn.awaddr + i,
                      (wtxn.wdata[i / 4] >> ((i % 4) * 8)) & 0xFF);
        }


        if (wtxn.wlast) {
            #ifdef PRINT_DEBUG
                printf("(%lu) %s: write, last tick\n", wrapper->tickcount, name);
            #endif
            aw_fifo.pop();

            axi_b_txn btxn;
            btxn.bid = awtxn.awid;
            b_fifo.push(btxn);
        } else {
            #ifdef PRINT_DEBUG
                printf("(%lu) %s: write, ticks remaining\n",
                    wrapper->tickcount, name);
            #endif
            awtxn.awlen--;
            awtxn.awaddr += AXI_WIDTH / 8;
        }

        w_fifo.pop();
    }

    /* read response */
    if (!r_fifo.empty()) {
        axi_r_txn &txn = r_fifo.front();

        r0_fifo.push(txn);
        r_fifo.pop();
    } else {
        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            txn.rdata[i] = 0xAAAAAAAA;
        }

        r0_fifo.push(txn);
    }

    *dla.r_rvalid = 0;
    if (*dla.r_rready && !r0_fifo.empty()) {
        axi_r_txn &txn = r0_fifo.front();

        *dla.r_rvalid = txn.rvalid;
        *dla.r_rid = txn.rid;
        *dla.r_rlast = txn.rlast;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            dla.r_rdata[i] = txn.rdata[i];
        }
        #ifdef PRINT_DEBUG
            if (txn.rvalid) {
                printf("(%lu) %s: read push: id %d, da %08x %08x %08x %08x\n",
                        wrapper->tickcount, name,
                        txn.rid, txn.rdata[0], txn.rdata[1],
                        txn.rdata[2], txn.rdata[3]);
            }
        #endif

        r0_fifo.pop();
    }

    /* write response */
    *dla.b_bvalid = 0;
    if (*dla.b_bready && !b_fifo.empty()) {
        *dla.b_bvalid = 1;

        axi_b_txn &txn = b_fifo.front();
        *dla.b_bid = txn.bid;
        b_fifo.pop();
    }
}

void
AXIResponder::eval_atomic() {
    /* write request */
    if (*dla.aw_awvalid && *dla.aw_awready) {
        #ifdef PRINT_DEBUG
            printf("(%lu) %s: write request from dla, addr %08lx id %d\n",
                wrapper->tickcount,
                name,
                *dla.aw_awaddr,
                *dla.aw_awid);
        #endif

        axi_aw_txn txn;

        txn.awid = *dla.aw_awid;
        txn.awaddr = *dla.aw_awaddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        txn.awlen = *dla.aw_awlen;
        aw_fifo.push(txn);

        *dla.aw_awready = 0;
    } else
        *dla.aw_awready = 1;

    /* write data */
    if (*dla.w_wvalid) {
        #ifdef PRINT_DEBUG
            printf("(%lu) %s: write data from dla (%08x %08x...)\n",
                wrapper->tickcount,
                name,
                dla.w_wdata[0],
                dla.w_wdata[1]);
        #endif

        axi_w_txn txn;

        for (int i = 0; i < AXI_WIDTH / 32; i++)
            txn.wdata[i] = dla.w_wdata[i];
        txn.wstrb = *dla.w_wstrb;
        txn.wlast = *dla.w_wlast;
        w_fifo.push(txn);
    }

    /* read request */
    if (*dla.ar_arvalid && *dla.ar_arready) {
        uint64_t addr = *dla.ar_araddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        uint8_t len = *dla.ar_arlen;
        #ifdef PRINT_DEBUG
            printf("(%lu) %s: read request from dla, addr %08lx burst %d id %d\n",
                wrapper->tickcount,
                name,
                *dla.ar_araddr,
                *dla.ar_arlen,
                *dla.ar_arid);
        #endif

        do {
            axi_r_txn txn;

            txn.rvalid = 1;
            txn.rlast = len == 0;
            txn.rid = *dla.ar_arid;

            /*for (int i = 0; i < AXI_WIDTH / 32; i++) {
                uint32_t da = read_ram(addr + i * 4) +
                             (read_ram(addr + i * 4 + 1) << 8) +
                             (read_ram(addr + i * 4 + 2) << 16) +
                             (read_ram(addr + i * 4 + 3) << 24);
                txn.rdata[i] = da;
            }*/
            const uint8_t* dataPtr = read_variable(addr, false, 512/8);
            inflight_resp_atomic(addr,dataPtr,&txn);

            r_fifo.push(txn);

            addr += AXI_WIDTH / 8;
        } while (len--);

        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            txn.rdata[i] = 0x55555555;
        }

        for (int i = 0; i < AXI_R_DELAY; i++)
            r_fifo.push(txn);

        *dla.ar_arready = 0;
    } else
        *dla.ar_arready = 1;

    /* now handle the write FIFOs ... */
    if (!aw_fifo.empty() && !w_fifo.empty()) {
        axi_aw_txn &awtxn = aw_fifo.front();
        axi_w_txn &wtxn = w_fifo.front();

        if (wtxn.wlast != (awtxn.awlen == 0)) {
            printf("(%lu) %s: wlast / awlen mismatch\n",
                   wrapper->tickcount, name);
            abort();
        }

        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            if (!((wtxn.wstrb >> i) & 1))
                continue;

            write(awtxn.awaddr + i,
                 (wtxn.wdata[i / 4] >> ((i % 4) * 8)) & 0xFF,
                 false);
        }


        if (wtxn.wlast) {
            #ifdef PRINT_DEBUG
                printf("(%lu) %s: write, last tick\n", wrapper->tickcount, name);
            #endif
            aw_fifo.pop();

            axi_b_txn btxn;
            btxn.bid = awtxn.awid;
            b_fifo.push(btxn);
        } else {
            #ifdef PRINT_DEBUG
                printf("(%lu) %s: write, ticks remaining\n",
                       wrapper->tickcount, name);
            #endif

            awtxn.awlen--;
            awtxn.awaddr += AXI_WIDTH / 8;
        }

        w_fifo.pop();
    }

    /* read response */
    if (!r_fifo.empty()) {
        axi_r_txn &txn = r_fifo.front();

        r0_fifo.push(txn);
        r_fifo.pop();
    } else {
        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            txn.rdata[i] = 0xAAAAAAAA;
        }

        r0_fifo.push(txn);
    }

    *dla.r_rvalid = 0;
    if (*dla.r_rready && !r0_fifo.empty()) {
        axi_r_txn &txn = r0_fifo.front();

        *dla.r_rvalid = txn.rvalid;
        *dla.r_rid = txn.rid;
        *dla.r_rlast = txn.rlast;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            dla.r_rdata[i] = txn.rdata[i];
        }
        #ifdef PRINT_DEBUG
            if (txn.rvalid) {
                printf("(%lu) %s: read push: id %d, da %08x %08x %08x %08x\n",
                    wrapper->tickcount, name, txn.rid, txn.rdata[0],
                    txn.rdata[1], txn.rdata[2], txn.rdata[3]);
            }
        #endif

        r0_fifo.pop();
    }

    /* write response */
    *dla.b_bvalid = 0;
    if (*dla.b_bready && !b_fifo.empty()) {
        *dla.b_bvalid = 1;

        axi_b_txn &txn = b_fifo.front();
        *dla.b_bid = txn.bid;
        b_fifo.pop();
    }
}

void
AXIResponder::eval_timing() {
    /* write request */
    if (*dla.aw_awvalid && *dla.aw_awready) {
            printf("(%lu) %s: write request from dla, addr %08lx id %d\n",
                wrapper->tickcount,
                name,
                *dla.aw_awaddr,
                *dla.aw_awid);

        axi_aw_txn txn;

        txn.awid = *dla.aw_awid;
        txn.awaddr = *dla.aw_awaddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        txn.awlen = *dla.aw_awlen;
        aw_fifo.push(txn);

        *dla.aw_awready = 0;
    } else
        *dla.aw_awready = 1;

    /* write data */
    if (*dla.w_wvalid) {
        #ifdef PRINT_DEBUG
            printf("(%lu) %s: write data from dla (%08x %08x...)\n",
                    wrapper->tickcount,
                    name,
                    dla.w_wdata[0],
                    dla.w_wdata[1]);
        #endif

        axi_w_txn txn;

        for (int i = 0; i < AXI_WIDTH / 32; i++)
            txn.wdata[i] = dla.w_wdata[i];
        txn.wstrb = *dla.w_wstrb;
        txn.wlast = *dla.w_wlast;
        w_fifo.push(txn);
    }

    /* read request */
    if (*dla.ar_arvalid && *dla.ar_arready) {
        uint64_t addr = *dla.ar_araddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        uint8_t len = *dla.ar_arlen;
        uint8_t i = 0;
        

            printf("(%lu) %s: read request from dla, addr %08lx burst %d id %d\n",
                wrapper->tickcount,
                name,
                *dla.ar_araddr,
                *dla.ar_arlen,
                *dla.ar_arid);


        if (wrapper->dma_enable) {
            // check whether the requested address ranges are already in spm
            for (int j = 0; j < len + 1; j++) {
                uint64_t start_addr = addr + j * (AXI_WIDTH / 8);
                uint64_t spm_line_addr = start_addr & ~((uint64_t)(wrapper->spm_line_size - 1));

                // generate the txn descriptor of this (AXI_WIDTH/8)-long txn
                axi_r_txn txn;

                txn.rlast = (j == len);
                txn.rid = *dla.ar_arid;

                // if yes, get them in spm (and previously intend to put it in countdown queue (spm_access_txn_fifo))
                //! only when waiting_for_dma_txn_addr_order is empty, we can get spm data directly to r_fifo
                if (wrapper->spm.find(spm_line_addr) != wrapper->spm.end()) {  // this key exists in spm

                    if(!waiting_for_dma_txn_addr_order.empty()) {
                        // we must follow the order of read requests issued by nvdla
                        txn.rvalid = 0;
                        waiting_for_dma_txn_addr_order.push_back(start_addr);
                        waiting_for_dma_txn[start_addr].push_back(txn);
                    } else {
                        // directly transfer from spm to nvdla
                        txn.rvalid = 1;

                        for (int k = 0; k < AXI_WIDTH / 32; k++) {
                            uint32_t da = wrapper->read_spm(start_addr + k * 4) +
                                          (wrapper->read_spm(start_addr + k * 4 + 1) << 8) +
                                          (wrapper->read_spm(start_addr + k * 4 + 2) << 16) +
                                          (wrapper->read_spm(start_addr + k * 4 + 3) << 24);
                            txn.rdata[k] = da;
                        }
                        r_fifo.push(txn);   // this txn will be delayed by AXI_R_LATENCY cycles (r_fifo & r0_fifo)
                    }
                } else { // else, start dma fill
                    txn.rvalid = 0;
                    // first check whether this (AXI_WIDTH/8)-long txn is covered by a previous DMA
                    if(dma_addr_record.find(spm_line_addr) != dma_addr_record.end()) {  // it has been covered
                        // only append it to the waiting_for_dma queue
                        waiting_for_dma_txn_addr_order.push_back(start_addr);
                        waiting_for_dma_txn[start_addr].push_back(txn);
                    } else {    // need to initiate a new DMA
                        dma_addr_fifo.push(spm_line_addr);
                        dma_addr_record[spm_line_addr] = wrapper->spm_line_size;
                        waiting_for_dma_txn_addr_order.push_back(start_addr);
                        waiting_for_dma_txn[start_addr].push_back(txn);
                        wrapper->addDMAReadReq(spm_line_addr, wrapper->spm_line_size);
                    }

                }
            }
        } else {
            do {
                axi_r_txn txn;

                // write to the txn
                txn.rvalid = 0;
                txn.rlast = len == 0;
                txn.rid = *dla.ar_arid;
                // put txn to the map
                inflight_req[addr].push_back(txn);
                // put in req the txn
                inflight_req_order.push(addr);

                read_variable(addr, true, AXI_WIDTH / 8);

                addr += AXI_WIDTH / 8;
                i++;
            } while (len--);
        }
        // next cycle we are not ready
        *dla.ar_arready = 0;
    } else {
        *dla.ar_arready = wrapper->dma_enable ?
            (waiting_for_dma_txn_addr_order.size() <= max_req_inflight) : \
            (inflight_req_order.size() <= max_req_inflight);
    }


    // handle read response from gem5 memory
    if (read_resp_ready) {
        if (wrapper->dma_enable) {
            uint64_t addr_front = waiting_for_dma_txn_addr_order.front();
            if (waiting_for_dma_txn[addr_front].front().rvalid) {
                // push the front one
                r_fifo.push(waiting_for_dma_txn[addr_front].front());
                // remove the front
                waiting_for_dma_txn[addr_front].pop_front();
                // check if empty to remove entry from map
                if(waiting_for_dma_txn[addr_front].empty())
                    waiting_for_dma_txn.erase(addr_front);
                // delete in the queue order
                waiting_for_dma_txn_addr_order.pop_front();
            }

        } else {
#ifdef PRINT_DEBUG
            if (inflight_req_order.size() > 0) {
                printf("(%lu) %s: Remaining %d\n",
                        wrapper->tickcount, name,
                        inflight_req_order.size());
            }
#endif

            unsigned int addr_front = inflight_req_order.front();
            // if burst
            if (inflight_req[addr_front].front().rvalid) {
                printf("(%lu) read data has returned by gem5 and got in nvdla, addr %08x\n", wrapper->tickcount, addr_front);
                // push the front one
                r_fifo.push(inflight_req[addr_front].front());
                // remove the front
                inflight_req[addr_front].pop_front();
                // check if empty to remove entry from map
                if (inflight_req[addr_front].empty()) {
                    inflight_req.erase(addr_front);
                }
                // delete in the queue order
                inflight_req_order.pop();
            }
        }

        read_resp_ready = 0;
    } else {
        read_resp_ready = 1;
    }

    /* now handle the write FIFOs ... */
    if (!aw_fifo.empty() && !w_fifo.empty()) {
        axi_aw_txn &awtxn = aw_fifo.front();
        axi_w_txn &wtxn = w_fifo.front();

        if (wtxn.wlast != (awtxn.awlen == 0)) {
            printf("(%lu) %s: wlast / awlen mismatch\n",
                   wrapper->tickcount, name);
            abort();
        }

        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            if (!((wtxn.wstrb >> i) & 1))
                continue;

            write(awtxn.awaddr + i,
                 (wtxn.wdata[i / 4] >> ((i % 4) * 8)) & 0xFF,
                 false);
        }


        if (wtxn.wlast) {
            #ifdef PRINT_DEBUG
                printf("(%lu) %s: write, last tick\n", wrapper->tickcount, name);
            #endif
            aw_fifo.pop();

            axi_b_txn btxn;
            btxn.bid = awtxn.awid;
            b_fifo.push(btxn);
        } else {
            #ifdef PRINT_DEBUG
                printf("(%lu) %s: write, ticks remaining\n",
                   wrapper->tickcount, name);
            #endif

            awtxn.awlen--;
            awtxn.awaddr += AXI_WIDTH / 8;
        }

        w_fifo.pop();
    }

    /* read response */
    if (!r_fifo.empty()) {
        axi_r_txn &txn = r_fifo.front();

        r0_fifo.push(txn);
        r_fifo.pop();
    } else {
        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            txn.rdata[i] = 0xAAAAAAAA;
        }

        r0_fifo.push(txn);
    }

    *dla.r_rvalid = 0;
    if (*dla.r_rready && !r0_fifo.empty()) {
        axi_r_txn &txn = r0_fifo.front();

        *dla.r_rvalid = txn.rvalid;
        *dla.r_rid = txn.rid;
        *dla.r_rlast = txn.rlast;
        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            dla.r_rdata[i] = txn.rdata[i];
        }
        #ifdef PRINT_DEBUG
            if (txn.rvalid) {
                printf("(%lu) %s: read push: id %d, da %08x %08x %08x %08x\n",
                    wrapper->tickcount, name, txn.rid, txn.rdata[0],
                    txn.rdata[1], txn.rdata[2], txn.rdata[3]);
            }
        #endif

        r0_fifo.pop();
    }

    /* write response */
    *dla.b_bvalid = 0;
    if (*dla.b_bready && !b_fifo.empty()) {
        *dla.b_bvalid = 1;

        axi_b_txn &txn = b_fifo.front();
        *dla.b_bid = txn.bid;
        b_fifo.pop();
    }
}

void
AXIResponder::inflight_resp(uint32_t addr, const uint8_t* data) {
    #ifdef PRINT_DEBUG
        printf("(%lu) %s: Inflight Resp Timing: addr %08x \n",
                wrapper->tickcount, name, addr);
    #endif
    uint32_t * ptr = (uint32_t*) data;
    std::list<axi_r_txn>::iterator it = inflight_req[addr].begin();
    // Get the correct ptr
    while (it != inflight_req[addr].end() and it->rvalid) {
               it++;
    }
    assert(it != inflight_req[addr].end());
    for (int i = 0; i < AXI_WIDTH / 32; i++) {
        it->rdata[i] = ptr[i];
        /*printf("Expecting: %0x Got %0x%0x%0x%0x", 
                ptr[i],
                read_ram(addr+i+3),
                read_ram(addr+i+2),
                read_ram(addr+i+1),
                read_ram(addr+i+0));*/
    }
    it->rvalid = 1;
    #ifdef PRINT_DEBUG
        printf("Remaining %d\n", inflight_req_order.size());
        printf("(%lu) %s: Inflight Resp Timing Finished: addr %08lx \n",
                wrapper->tickcount, name, addr);
    #endif
}

void
AXIResponder::emptyInflight() {
    printf("(%lu) %s: Empty Inflight\n",
            wrapper->tickcount, name);

    /*unsigned int addr_order = inflight_req_order.front();
    if (inflight_req[addr_order].rvalid) {
        while (inflight_req[addr_order].rvalid) {
            r_fifo.push(inflight_req[addr_order]);
            inflight_req.erase(addr_order);
            // delete in the queue order
            inflight_req_order.pop();
            // get new order
            addr_order = inflight_req_order.front();
        }
        *dla.ar_arready = 0;
    }*/
}

void
AXIResponder::inflight_resp_atomic(uint32_t addr,
                                       const uint8_t* data,
                                       axi_r_txn *txn) {
    //printf("(%lu) %s: Inflight Resp Atomic: addr %08lx \n",
    //        wrapper->tickcount, name, addr);
    uint32_t * ptr = (uint32_t*) data;
    for (int i = 0; i < AXI_WIDTH / 32; i++) {
        txn->rdata[i] = ptr[i];
    }
    txn->rvalid = 1;
}

void
AXIResponder::inflight_dma_resp(uint64_t addr, const uint8_t* data, uint32_t len) {
    printf("AXIResponder handling DMA return data @0x%08lx with length %d.\n", addr, len);

    uint64_t addr_recorded = dma_addr_fifo.front();
    // printf("addr_recorded = 0x%08lx, addr = 0x%08lx\n", addr_recorded, addr);
    assert(addr_recorded == addr);      //! DMA transfer should be in-order

    uint32_t old_size_remaining = dma_addr_record[addr];
    uint32_t size_remaining = old_size_remaining - len;
    dma_addr_record[addr] = size_remaining;

    std::vector<uint8_t>& entry_vector = wrapper->spm[addr];
    entry_vector.resize(wrapper->spm_line_size - size_remaining, 0);
    for (int i = 0; i < len; i++)
        entry_vector[wrapper->spm_line_size - old_size_remaining + i] = data[i];


    //! assume a (AXI_WIDTH/8)-long txn will not be split into two DMA transfers
    for (auto addr_it = waiting_for_dma_txn_addr_order.begin(); addr_it != waiting_for_dma_txn_addr_order.end(); addr_it++) {
        // judge whether the data for this (AXI_WIDTH/8)-long txn has arrived
        uint64_t dma_transfer_addr = (*addr_it) & ~(uint64_t)(wrapper->spm_line_size - 1);
        if (wrapper->spm.find(dma_transfer_addr) == wrapper->spm.end() || *addr_it - dma_transfer_addr >= entry_vector.size()) {
            // the first condition says the entire DMA line has not arrived
            // the second says part of this DMA line has arrived, but data for this txn hasn't
            printf("data for 0x%08lx has not arrived in spm, so stop updating spm, and go on to do DMA.\n", *addr_it);
            break;
        }

        printf("data for 0x%08lx is now in spm.\n", *addr_it);
        std::list<axi_r_txn>::iterator it = waiting_for_dma_txn[*addr_it].begin();
        // Get the correct ptr
        while (it != waiting_for_dma_txn[*addr_it].end() and it->rvalid)
            it++;
        if(it == waiting_for_dma_txn[*addr_it].end())   // this txn has been covered by a previous partial get of this DMA
            continue;                                   // continue checking other transactions that may be covered by this partial get

        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            uint32_t da = wrapper->read_spm(*addr_it + i * 4) +
                          (wrapper->read_spm(*addr_it + i * 4 + 1) << 8) +
                          (wrapper->read_spm(*addr_it + i * 4 + 2) << 16) +
                          (wrapper->read_spm(*addr_it + i * 4 + 3) << 24);
            it->rdata[i] = da;
            /*printf("Expecting: %0x Got %0x%0x%0x%0x",
                    ptr[i],
                    read_ram(addr+i+3),
                    read_ram(addr+i+2),
                    read_ram(addr+i+1),
                    read_ram(addr+i+0));*/
        }
        it->rvalid = 1;
    }

    if (size_remaining == 0) {  // all data for this DMA transfer has been got
        dma_addr_record.erase(addr);
        dma_addr_fifo.pop();
    }
}

const uint8_t*
AXIResponder::read_variable(uint32_t addr, bool timing,
                                unsigned int bytes) {
    //printf("name: %s %d\n", name, strcmp(name,"DBB"));
    wrapper->addReadReq(sram,timing,addr,bytes);
    return 0;//readGem5;
}

// Reads 1 value
uint8_t
AXIResponder::read(uint32_t addr) {
    ram[addr / AXI_BLOCK_SIZE].resize(AXI_BLOCK_SIZE, 0);

    wrapper->addReadReq(sram,false,addr,1);

    //printf("Compare reading gem5ram: %08lx, ram: %08lx \n",
    //        readGem5, readRam);
    return 0;//readGem5;
}

void
AXIResponder::read_for_traceLoaderGem5(uint32_t start_addr, uint32_t length) {
    uint64_t txn_start_addr = (uint64_t)start_addr & ~(uint64_t)(AXI_WIDTH / 8 - 1);

    for (uint32_t delta_addr = 0; txn_start_addr + delta_addr < start_addr + length; delta_addr += (AXI_WIDTH / 8)) {
        uint32_t txn_addr = txn_start_addr + delta_addr;
        axi_r_txn txn;
        txn.rvalid = 0;
        txn.burst = true;
        txn.rlast = (txn_start_addr + delta_addr + (AXI_WIDTH / 8) >= start_addr + length);
        // put txn to the map
        inflight_req[txn_addr].push_back(txn);
        // put in req the txn
        inflight_req_order.push(txn_addr);

        read_variable(txn_addr, true, AXI_WIDTH / 8);
    }
}

// this function assumes start_addr is aligned to (AXI_WIDTH / 8)
uint32_t
AXIResponder::read_response_for_traceLoaderGem5(uint32_t start_addr, uint8_t* data_buffer) {
    // check status of memory reading request
    assert(!inflight_req_order.empty());
    uint32_t addr_front = inflight_req_order.front();

    if (addr_front != start_addr) {
        // this should not happen
        printf("addr_front(0x%08x) does not match start_addr (0x%08x)", addr_front, start_addr);
        abort();
    }

    assert(!inflight_req[addr_front].empty());
    axi_r_txn txn = inflight_req[addr_front].front();
    if (txn.rvalid) {
        printf("memory request at 0x%08x has arrived.\n", start_addr);
        inflight_req[addr_front].pop_front();
        if (inflight_req[addr_front].empty())
            inflight_req.erase(addr_front);

        inflight_req_order.pop();

        // get the value
        for (uint32_t byte_id = 0; byte_id < AXI_WIDTH / 8; byte_id++)
            data_buffer[byte_id] = (txn.rdata[byte_id / 4] >> (8 * (byte_id % 4))) & (uint32_t)0xFF;

        return 1;
    } else {
        printf("still waiting for memory request at 0x%08x\n", start_addr);
        return 0;
    }
}

uint32_t
AXIResponder::getRequestsOnFlight() {
    return inflight_req_order.size();
}
