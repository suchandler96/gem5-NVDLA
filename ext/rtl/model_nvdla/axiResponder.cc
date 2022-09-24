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

    name = _name;

    sram = sram_;

    max_req_inflight = (maxReq<240) ? maxReq:240;

    AXI_R_LATENCY = wrapper->dma_enable ? wrapper->spm_latency : 0;
    // for non-spm configuration, this fixed latency is modeled by gem5 memory system

    pft_threshold = 16;

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
    bool issued_req_this_cycle = false;
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

        issued_req_this_cycle = true;

        if (wrapper->dma_enable) {
            // check whether the requested address ranges are already in spm_write_queue or spm
            // for now since all variables are aligned to 0x1000,
            // we think a mem request will not involve one part valid in spm, while another part valid in main mem
            for (int j = 0; j < len + 1; j++) {
                uint64_t start_addr = addr + j * (AXI_WIDTH / 8);

                // generate the txn descriptor of this (AXI_WIDTH/8)-long txn
                axi_r_txn txn;

                txn.rlast = (j == len);
                txn.rid = *dla.ar_arid;
                txn.is_prefetch = 0;

                if(wrapper->prefetch_enable)
                    log_req_issue(start_addr);

                // check spm and write queue
                bool data_get_in_spm_or_queue = get_txn_data_from_spm_and_wr_queue(start_addr, txn.rdata);
                if (data_get_in_spm_or_queue) {
                    // need to maintain the order of issuing requests
                    // printf("this spm_line_addr exists in spm, 0x%08lx\n", spm_line_addr);
                    txn.rvalid = 1;
                } else {
                    // first check whether this addr has been covered by an inflight DMA request or not
                    uint64_t spm_line_addr = start_addr & ~((uint64_t)(wrapper->spm_line_size - 1));
                    if (inflight_dma_addr_size.find(spm_line_addr) == inflight_dma_addr_size.end()) {
                        // not covered, need to initiate a new DMA
                        inflight_dma_addr_size[spm_line_addr] = wrapper->spm_line_size;
                        inflight_dma_addr_queue.push(spm_line_addr);
                        wrapper->addDMAReadReq(spm_line_addr, wrapper->spm_line_size);
                    }
                    txn.rvalid = 0;
                }
                inflight_req_order.push(start_addr);
                inflight_req[start_addr].push_back(txn);
            }
        } else {
            do {
                axi_r_txn txn;

                // write to the txn
                txn.rvalid = 0;
                txn.rlast = len == 0;
                txn.rid = *dla.ar_arid;
                txn.is_prefetch = 0;

                if(wrapper->prefetch_enable)
                    log_req_issue(addr);

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
        *dla.ar_arready = (inflight_req_order.size() <= max_req_inflight);
    }

#ifdef PRINT_DEBUG
    if (inflight_req_order.size() > 0) {
        printf("(%lu) %s: Remaining %d\n",
                wrapper->tickcount, name,
                inflight_req_order.size());
    }
#endif

    //! generate prefetch request
    if(wrapper->prefetch_enable && inflight_req_order.size() < pft_threshold && !issued_req_this_cycle) {
        generate_prefetch_request();
    }

    //! handle read return
    if (!inflight_req_order.empty()) {
        unsigned int addr_front = inflight_req_order.front();

        axi_r_txn& txn = inflight_req[addr_front].front();
        // data just arrived in spm via DMA will not update its corresponding txn.rvalid
        if (wrapper->dma_enable && txn.rvalid == 0) {
            bool got = get_txn_data_from_spm_and_wr_queue(addr_front, txn.rdata);
            if(got)
                txn.rvalid = 1;
        }
        if (txn.rvalid) {  // ensures the order of response
            // push the front one
            if (txn.is_prefetch) {
                printf("(%lu) read data returned by gem5 (or already in spm) PREFETCH, addr 0x%08x\n", wrapper->tickcount, addr_front);
            } else {
                printf("(%lu) read data returned by gem5 (or already in spm), addr 0x%08x\n", wrapper->tickcount, addr_front);
                r_fifo.push(inflight_req[addr_front].front());
                // todo: add some AXI_R_DELAY txns. currently we are setting AXI_R_DELAY = 0 so it is also correct
            }
            // remove the front
            inflight_req[addr_front].pop_front();
            // check if empty to remove entry from map
            if (inflight_req[addr_front].empty())
                inflight_req.erase(addr_front);
            // delete in the queue order
            inflight_req_order.pop();
        }
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

        if(wrapper->dma_enable) {
            // intermediate variables and outputs should be written to spm (actually, all writes belong to this type)
            Wrapper_nvdla::spm_wr_txn spm_txn;
            spm_txn.addr = awtxn.awaddr;
            spm_txn.mask = wtxn.wstrb;
            spm_txn.countdown = wrapper->spm_latency;
            for (int i = 0; i < AXI_WIDTH / 8; i++)
                spm_txn.data[i] = (wtxn.wdata[i / 4] >> ((i % 4) * 8)) & 0xFF;
            wrapper->spm_write_queue.push_back(spm_txn);
        } else {
            uint8_t tmp_buf[AXI_WIDTH / 8];
            for (int ii = 0; ii < AXI_WIDTH / 8; ii++)
                tmp_buf[ii] = ((wtxn.wdata[ii / 4] >> ((ii % 4) * 8)) & 0xFF);

            wrapper->addLongWriteReq(sram, true, awtxn.awaddr, AXI_WIDTH / 8, tmp_buf, wtxn.wstrb);
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

    // process spm write queue countdown
    if (wrapper->dma_enable && !wrapper->spm_write_queue.empty()) {
        while (wrapper->spm_write_queue.front().countdown == 0) {
            // write to spm
            auto& spm_txn = wrapper->spm_write_queue.front();
            for (int i = 0; i < AXI_WIDTH / 8; i++) {
                if (!((spm_txn.mask >> i) & 1))
                    continue;
                wrapper->write_spm(spm_txn.addr + i, spm_txn.data[i]);
            }

            wrapper->spm_write_queue.pop_front();
            if(wrapper->spm_write_queue.empty())
                break;
        }
        for (auto it = wrapper->spm_write_queue.begin(); it != wrapper->spm_write_queue.end(); it++)
            it->countdown--;
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
    printf("(%lu) read data returned by gem5, addr 0x%08x\n", wrapper->tickcount, addr);
    #ifdef PRINT_DEBUG
        printf("Remaining %d\n", inflight_req_order.size());
        printf("(%lu) %s: Inflight Resp Timing Finished: addr %08lx \n",
                wrapper->tickcount, name, addr);
    #endif
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
AXIResponder::inflight_dma_resp(const uint8_t* data, uint32_t len) {
    //! we HAVE TO assume dma is in-order. If it is out-of-order due to bus arbitration, even DMAFifo can't handle it
    uint32_t addr = inflight_dma_addr_queue.front();
    printf("(%lu) AXIResponder handling DMA return data @0x%08x with length %d.\n", wrapper->tickcount, addr, len);

    uint32_t old_size_remaining = inflight_dma_addr_size[addr];
    uint32_t size_remaining = old_size_remaining - len;
    inflight_dma_addr_size[addr] = size_remaining;

    std::vector<uint8_t>& entry_vector = wrapper->spm[addr];
    entry_vector.resize(wrapper->spm_line_size - size_remaining, 0);
    for (int i = 0; i < len; i++)
        entry_vector[wrapper->spm_line_size - old_size_remaining + i] = data[i];

    if (size_remaining == 0) {  // all data for this DMA transfer has been got
        inflight_dma_addr_size.erase(addr);
        inflight_dma_addr_queue.pop();
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
    if(txn_start_addr != start_addr)
        printf("this dump_mem is not aligned to AXI_WIDTH / 8, op.addr = 0x%08x\n", start_addr);

    for (uint32_t delta_addr = 0; txn_start_addr + delta_addr < start_addr + length; delta_addr += (AXI_WIDTH / 8)) {
        uint32_t txn_addr = txn_start_addr + delta_addr;
        axi_r_txn txn;
        txn.rvalid = 0;
        txn.burst = true;
        txn.rlast = (txn_start_addr + delta_addr + (AXI_WIDTH / 8) >= start_addr + length);
        if(wrapper->dma_enable) {
            bool got = get_txn_data_from_spm_and_wr_queue(txn_addr, txn.rdata);
            if(got) {
                txn.rvalid = 1;
            } else      // when verifying results, no need to use dma...
                read_variable(txn_addr, true, AXI_WIDTH / 8);
        } else
            read_variable(txn_addr, true, AXI_WIDTH / 8);

        // put txn to the map
        inflight_req[txn_addr].push_back(txn);
        // put in req the txn
        inflight_req_order.push(txn_addr);
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
        printf("(%lu) addr_front(0x%08x) does not match start_addr (0x%08x)", wrapper->tickcount, addr_front, start_addr);
        abort();
    }

    assert(!inflight_req[addr_front].empty());
    axi_r_txn txn = inflight_req[addr_front].front();
    if (txn.rvalid) {
        printf("(%lu) memory request at 0x%08x has arrived.\n", wrapper->tickcount, start_addr);
        inflight_req[addr_front].pop_front();
        if (inflight_req[addr_front].empty())
            inflight_req.erase(addr_front);

        inflight_req_order.pop();

        // get the value
        for (uint32_t byte_id = 0; byte_id < AXI_WIDTH / 8; byte_id++)
            data_buffer[byte_id] = (txn.rdata[byte_id / 4] >> (8 * (byte_id % 4))) & (uint32_t)0xFF;

        return 1;
    } else {
        printf("(%lu) still waiting for memory request at 0x%08x\n", wrapper->tickcount, start_addr);
        return 0;
    }
}

bool
AXIResponder::check_txn_data_in_spm_and_wr_queue(uint32_t addr) {
    // search spm_write_queue first
    for (auto it = wrapper->spm_write_queue.begin(); it != wrapper->spm_write_queue.end(); it++) {
        if ((uint32_t)(it->addr) == addr)
            return true;
    }

    // then search spm
    uint64_t spm_line_addr = addr & ~((uint64_t)(wrapper->spm_line_size - 1));
    if (wrapper->spm.find(spm_line_addr) == wrapper->spm.end()) {
        return false;
    }
    return true;
}

bool
AXIResponder::get_txn_data_from_spm_and_wr_queue(uint32_t addr, uint32_t* to_be_filled_data) {
    uint8_t* spm_write_queue_data = nullptr;
    // search spm_write_queue first
    for (auto it = wrapper->spm_write_queue.begin(); it != wrapper->spm_write_queue.end(); it++) {
        if ((uint32_t)it->addr == addr) {
            spm_write_queue_data = it->data;
            break;
        }
    }
    if (spm_write_queue_data != nullptr) {
        for (int k = 0; k < AXI_WIDTH / 8; k += 4) {
            uint32_t da = spm_write_queue_data[k] + (spm_write_queue_data[k + 1] << 8) +
                          (spm_write_queue_data[k + 2] << 16) + (spm_write_queue_data[k + 3] << 24);
            to_be_filled_data[k / 4] = da;
        }
        return true;
    }
    // then search spm
    uint64_t spm_line_addr = addr & ~((uint64_t)(wrapper->spm_line_size - 1));
    if (wrapper->spm.find(spm_line_addr) == wrapper->spm.end())
        return false;

    for (int k = 0; k < AXI_WIDTH / 32; k++) {
        uint32_t da = wrapper->read_spm(addr + k * 4) +
                (wrapper->read_spm(addr + k * 4 + 1) << 8) +
                (wrapper->read_spm(addr + k * 4 + 2) << 16) +
                (wrapper->read_spm(addr + k * 4 + 3) << 24);
        to_be_filled_data[k] = da;
    }
    return true;
}

uint32_t
AXIResponder::getRequestsOnFlight() {
    return inflight_req_order.size();
}

void
AXIResponder::add_rd_var_log_entry(uint32_t addr, uint32_t size) {
    read_var_log.push_back(std::make_tuple(addr, size, 0));
}

void
AXIResponder::log_req_issue(uint32_t addr) {
    for (auto it = read_var_log.begin(); it != read_var_log.end(); it++) {
        uint32_t& log_entry_length = std::get<1>(*it);
        uint32_t& log_entry_addr = std::get<0>(*it);

        if (log_entry_addr <= addr && addr < log_entry_addr + log_entry_length) {
            uint32_t& log_entry_issued_len = std::get<2>(*it);
            if (addr > log_entry_addr + log_entry_issued_len) {
                printf("addr issued is beyond the log.\n");
                abort();
            } else if (addr == log_entry_addr + log_entry_issued_len) {
                // that's the normal case
                log_entry_issued_len += 0x40;

                if (log_entry_issued_len == log_entry_length)
                    read_var_log.erase(it);
            } else {
                // printf("read req for %d has been covered by a previous pft / fetch.\n", addr);
            }
            break;
        }
    }
    // a mem req might also be covered by a popped entry, so no assert for logged
}

void
AXIResponder::generate_prefetch_request() {
    // assume the front of the list is a legal entry to prefetch
    uint32_t& log_entry_addr = std::get<0>(read_var_log.front());
    uint32_t& log_entry_length = std::get<1>(read_var_log.front());
    uint32_t& log_entry_issued_len = std::get<2>(read_var_log.front());

    uint32_t to_issue_addr = log_entry_addr + log_entry_issued_len;

    // generate the corresponding txn
    axi_r_txn txn;

    // write to the txn
    txn.rvalid = 0;     // since read addresses are streaming, we don't think this is present in spm (if dma_enable)
    txn.is_prefetch = 1;
    // if to_issue_addr is covered by a previous dma, it's useless to issue such prefetch request

    printf("(%lu)prefetch request addr %08x issued.\n", wrapper->tickcount, to_issue_addr);

    if (wrapper->dma_enable) {
        assert(!check_txn_data_in_spm_and_wr_queue(to_issue_addr));
        // first check whether this addr has been covered by an inflight DMA request or not
        uint64_t spm_line_addr = to_issue_addr & ~((uint64_t)(wrapper->spm_line_size - 1));
        if (inflight_dma_addr_size.find(spm_line_addr) == inflight_dma_addr_size.end()) {
            // not covered, need to initiate a new DMA
            inflight_dma_addr_size[spm_line_addr] = wrapper->spm_line_size;
            inflight_dma_addr_queue.push(spm_line_addr);
            wrapper->addDMAReadReq(spm_line_addr, wrapper->spm_line_size);

            inflight_req[to_issue_addr].push_back(txn);
            inflight_req_order.push(to_issue_addr);

        }   // or we don't need a new dma request. just wait for a previous one
    } else {
        inflight_req[to_issue_addr].push_back(txn);
        inflight_req_order.push(to_issue_addr);
        read_variable(to_issue_addr, true, AXI_WIDTH / 8);
    }

    log_entry_issued_len += AXI_WIDTH / 8;
    if (log_entry_issued_len == log_entry_length)       // this prefetch is the end of the variable
        read_var_log.pop_front();
}
