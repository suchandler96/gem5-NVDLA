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
                           const unsigned int maxReq,
                           bool _dma_enable):
                               dla(_dla), wrapper(_wrapper), name(_name), sram(sram_), dma_enable(_dma_enable) {
    *dla.aw_awready = 1;
    *dla.w_wready = 1;
    *dla.b_bvalid = 0;
    *dla.ar_arready = 1;
    *dla.r_rvalid = 0;

    max_req_inflight = (maxReq < 240) ? maxReq : 240;

    AXI_R_LATENCY = dma_enable ? wrapper->spm->spm_latency : 0;
    // for non-spm configuration, this fixed latency is modeled by gem5 memory system

    pft_threshold = 16;
    dma_pft_threshold = 32;

    // add some latency...
    for (int i = 0; i < AXI_R_LATENCY; i++) {
        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int j = 0; j < AXI_WIDTH / 8; j++) {
            txn.rdata[i] = 0xAA;
        }

        r0_fifo.push(txn);
    }
}

uint8_t
AXIResponder::read_ram(uint64_t addr) {
    ram[addr / AXI_BLOCK_SIZE].resize(AXI_BLOCK_SIZE, 0);
    return ram[addr / AXI_BLOCK_SIZE][addr % AXI_BLOCK_SIZE];
}

void
AXIResponder::write_ram(uint64_t addr, uint8_t data) {
    ram[addr / AXI_BLOCK_SIZE].resize(AXI_BLOCK_SIZE, 0);
    ram[addr / AXI_BLOCK_SIZE][addr % AXI_BLOCK_SIZE] = data;
}

void
AXIResponder::write(uint64_t addr, uint8_t data, bool timing) {
    // we access gem5 memory
    wrapper->addWriteReq(sram, timing, addr, data);
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

        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            txn.wdata[4 * i    ] =  dla.w_wdata[i]        & 0xFF;
            txn.wdata[4 * i + 1] = (dla.w_wdata[i] >>  8) & 0xFF;
            txn.wdata[4 * i + 2] = (dla.w_wdata[i] >> 16) & 0xFF;
            txn.wdata[4 * i + 3] = (dla.w_wdata[i] >> 24) & 0xFF;
        }
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

            for (int i = 0; i < AXI_WIDTH / 8; i++) {
                txn.rdata[i] = read_ram(addr + 4 * i);
            }

            r_fifo.push(txn);

            addr += AXI_WIDTH / 8;
        } while (len--);

        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            txn.rdata[i] = 0x55;
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
                      (wtxn.wdata[i]));
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
        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            txn.rdata[i] = 0xAA;
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
            dla.r_rdata[i] = txn.rdata[4 * i    ]         +
                (((uint32_t) txn.rdata[4 * i + 1]) <<  8) +
                (((uint32_t) txn.rdata[4 * i + 2]) << 16) +
                (((uint32_t) txn.rdata[4 * i + 3]) << 24);
        }
        #ifdef PRINT_DEBUG
            if (txn.rvalid) {
                printf("(%lu) %s: read push: id %d, da %02x %02x %02x %02x %02x %02x %02x %02x\n",
                        wrapper->tickcount, name,
                    txn.rid, txn.rdata[0], txn.rdata[1], txn.rdata[2], txn.rdata[3], txn.rdata[4], txn.rdata[5], txn.rdata[6], txn.rdata[7]);
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
            printf("(%lu) nvdla#%d %s: write request from dla, addr 0x%08lx id %d\n",
                wrapper->tickcount,
                wrapper->id_nvdla,
                name,
                *dla.aw_awaddr,
                *dla.aw_awid);

        axi_aw_txn txn;

        txn.awid = *dla.aw_awid;
        txn.awaddr = *dla.aw_awaddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        txn.awlen = *dla.aw_awlen;
        aw_fifo.push(std::move(txn));

        *dla.aw_awready = 0;
    } else
        *dla.aw_awready = 1;

    /* write data */
    if (*dla.w_wvalid) {
        #ifdef PRINT_DEBUG
            printf("(%lu) nvdla#%d %s: write data from dla (%08x %08x...)\n",
                    wrapper->tickcount,
                    wrapper->id_nvdla,
                    name,
                    dla.w_wdata[0],
                    dla.w_wdata[1]);
        #endif

        axi_w_txn txn;

        for (int i = 0; i < AXI_WIDTH / 32; i++) {
            txn.wdata[4 * i    ] = (dla.w_wdata[i]      ) & 0xFF;
            txn.wdata[4 * i + 1] = (dla.w_wdata[i] >>  8) & 0xFF;
            txn.wdata[4 * i + 2] = (dla.w_wdata[i] >> 16) & 0xFF;
            txn.wdata[4 * i + 3] = (dla.w_wdata[i] >> 24) & 0xFF;
        }
        txn.wstrb = *dla.w_wstrb;
        txn.wlast = *dla.w_wlast;
        w_fifo.push(txn);
    }

    /* read request */
    bool issued_req_this_cycle = process_read_req();

#ifdef PRINT_DEBUG
    if (inflight_req_order.size() > 0) {
        printf("(%lu) nvdla#%d %s: Remaining %d\n",
                wrapper->tickcount, wrapper->id_nvdla, name,
                inflight_req_order.size());
    }
#endif

    //! generate prefetch request
    if ((wrapper->prefetch_enable && !sram) && inflight_req_order.size() < pft_threshold &&
        inflight_dma_addr_queue.size() < dma_pft_threshold && !issued_req_this_cycle) {
        generate_prefetch_request();
    }

    //! handle read return
    process_read_resp();


    /* now handle the write FIFOs ... */
    if (!aw_fifo.empty() && !w_fifo.empty()) {
        axi_aw_txn &awtxn = aw_fifo.front();
        axi_w_txn &wtxn = w_fifo.front();

        if (wtxn.wlast != (awtxn.awlen == 0)) {
            printf("(%lu) nvdla#%d %s: wlast / awlen mismatch\n",
                   wrapper->tickcount, wrapper->id_nvdla, name);
            abort();
        }

        if (dma_enable) {
            // intermediate variables and outputs should be written to spm (actually, all writes belong to this type)
            wrapper->spm->write_spm_axi_line_with_mask(awtxn.awaddr, wtxn.wdata, wtxn.wstrb);
        } else {
            bool cache_write = (wrapper->buf_mode == BUF_MODE_ALL);
            wrapper->addLongWriteReq(sram, true, cache_write, awtxn.awaddr, AXI_WIDTH / 8, wtxn.wdata, wtxn.wstrb);
        }


        if (wtxn.wlast) {
            #ifdef PRINT_DEBUG
                printf("(%lu) nvdla#%d %s: write, last tick\n", wrapper->tickcount, wrapper->id_nvdla, name);
            #endif
            aw_fifo.pop();

            axi_b_txn btxn;
            btxn.bid = awtxn.awid;
            b_fifo.push(std::move(btxn));
        } else {
            #ifdef PRINT_DEBUG
                printf("(%lu) nvdla#%d %s: write, ticks remaining\n", wrapper->tickcount, wrapper->id_nvdla, name);
            #endif

            awtxn.awlen--;
            awtxn.awaddr += AXI_WIDTH / 8;
        }

        w_fifo.pop();
    }

    /* read response */
    if (!r_fifo.empty()) {
        axi_r_txn &txn = r_fifo.front();

        r0_fifo.push(std::move(txn));
        r_fifo.pop();
    } else {
        axi_r_txn txn;

        txn.rvalid = 0;
        txn.rid = 0;
        txn.rlast = 0;
        // this assignment is removed to improve performance
        // for (int i = 0; i < AXI_WIDTH / 8; i++) {
        //     txn.rdata[i] = 0xAA;
        // }

        r0_fifo.push(std::move(txn));
    }

    *dla.r_rvalid = 0;
    if (*dla.r_rready && !r0_fifo.empty()) {
        axi_r_txn &txn = r0_fifo.front();

        *dla.r_rvalid = txn.rvalid;
        if (txn.rvalid) {
            *dla.r_rid = txn.rid;
            *dla.r_rlast = txn.rlast;
            for (int i = 0; i < AXI_WIDTH / 32; i++) {
                dla.r_rdata[i] = (txn.rdata[4 * i]) +
                    (((uint32_t)txn.rdata[4 * i + 1]) << 8) +
                    (((uint32_t)txn.rdata[4 * i + 2]) << 16) +
                    (((uint32_t)txn.rdata[4 * i + 3]) << 24);
            }
            #ifdef PRINT_DEBUG
            printf("(%lu) nvdla#%d %s: read push: id %d, da %08x %08x %08x %08x\n",
                wrapper->tickcount, wrapper->id_nvdla, name, txn.rid, txn.rdata[0],
                txn.rdata[1], txn.rdata[2], txn.rdata[3]);
            #endif
        }        

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


bool
AXIResponder::process_read_req() {
    bool issued_req_this_cycle = false;
    if (*dla.ar_arvalid && *dla.ar_arready) {
        uint64_t addr = *dla.ar_araddr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
        uint8_t len = *dla.ar_arlen;
        uint8_t i = 0;


        printf("(%lu) nvdla#%d %s: read request from dla, addr 0x%08lx burst %d id %d\n",
               wrapper->tickcount,
               wrapper->id_nvdla,
               name,
               *dla.ar_araddr,
               *dla.ar_arlen,
               *dla.ar_arid);


        if (dma_enable) {
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

                if (wrapper->prefetch_enable && !sram) log_req_issue(start_addr);
                bool to_bypass = (wrapper->buf_mode == BUF_MODE_PFT);

                // check spm and write queue
                bool data_get_in_spm = wrapper->spm->read_spm_axi_line(start_addr, txn.rdata);
                inflight_req_order.push_back(start_addr);

                std::map<uint64_t, std::list<axi_r_txn>>::iterator req_it;
                bool req_alloc_succ;
                std::tie(req_it, req_alloc_succ) = inflight_req.emplace(start_addr, std::list<axi_r_txn>());
                req_it->second.push_back(txn);

                if (data_get_in_spm) {
                    // need to maintain the order of issuing requests
                    printf("(%lu) this spm_line_addr exists in spm, addr 0x%08lx\n", wrapper->tickcount, start_addr);
                    req_it->second.back().rvalid = 1;
                } else {
                    // first check whether this addr has been covered by an inflight DMA request or not
                    uint64_t spm_line_addr = start_addr & ~((uint64_t)(wrapper->spm->spm_line_size - 1));
                    std::map<uint64_t, DMAAttr>::iterator map_it;
                    bool alloc_succ;
                    std::tie(map_it, alloc_succ) = inflight_dma_attr.emplace(spm_line_addr, DMAAttr());

                    if (alloc_succ) {
                        // not covered, need to initiate a new DMA
                        printf("(%lu) DMA read req issued from NVDLA side, addr 0x%08lx\n", wrapper->tickcount, spm_line_addr);
                        map_it->second.is_bypass = to_bypass;
                        inflight_dma_addr_queue.push(spm_line_addr);
                        wrapper->addDMAReadReq(spm_line_addr, wrapper->spm->spm_line_size);
                        issued_req_this_cycle = true;
                    }
                    map_it->second.deps.emplace_back(start_addr, --req_it->second.end());
                    req_it->second.back().rvalid = 0;
                }
            }
        } else {
            issued_req_this_cycle = true;
            bool cache_read = (wrapper->buf_mode == BUF_MODE_ALL);
            do {
                axi_r_txn txn;

                // write to the txn
                txn.rvalid = 0;
                txn.rlast = len == 0;
                txn.rid = *dla.ar_arid;
                txn.is_prefetch = 0;

                if(wrapper->prefetch_enable && !sram) log_req_issue(addr);

                // put txn to the map
                inflight_req[addr].push_back(txn);
                // put in req the txn
                inflight_req_order.push_back(addr);

                wrapper->addReadReq(sram, true, cache_read, addr, AXI_WIDTH / 8);

                addr += AXI_WIDTH / 8;
                i++;
            } while (len--);
        }
        // next cycle we are not ready
        *dla.ar_arready = 0;
    } else {
        *dla.ar_arready = (inflight_req_order.size() <= max_req_inflight);
    }

    return issued_req_this_cycle;
}


void
AXIResponder::process_read_resp() {
    auto it_addr = inflight_req_order.begin();
    std::map<uint64_t, std::list<axi_r_txn>>::iterator req_it;
    std::list<axi_r_txn>* req_list_ptr;

    while (1) {
        if (it_addr == inflight_req_order.end()) {
            // this automatically filters out empty inflight_req_order
            return;
        }
        req_it = inflight_req.find(*it_addr);
        req_list_ptr = &(req_it->second);
        if (req_list_ptr->front().is_prefetch) {
            // find the first non-prefetch inflight_req addr;
            // assume all entries in inflight_req & inflight_req_order for arrived prefetches
            // have been removed in inflight_resp() and inflight_resp_dma()
            it_addr++;
        } else {
            break;
        }
    }

    // that's a non-prefetch txn. we'll check valid or not below
    uint64_t addr_front = *it_addr;

    axi_r_txn &txn = req_list_ptr->front();
    // data just arrived in spm via DMA will not update its corresponding txn.rvalid
    if (dma_enable && txn.rvalid == 0) {
        bool got = wrapper->spm->read_spm_axi_line(addr_front, txn.rdata);
        if (got) txn.rvalid = 1;
    }
    if (txn.rvalid) {  // ensures the order of response
        printf("(%lu) read data used by nvdla#%d (returned by gem5 or already in spm), addr 0x%08lx\n",
                wrapper->tickcount, wrapper->id_nvdla, addr_front);

        // push the front one
        r_fifo.push(txn);
        // todo: add some AXI_R_DELAY txns. currently we are setting AXI_R_DELAY = 0 so it is also correct

        // remove the front
        req_list_ptr->pop_front();
        // check if empty to remove entry from map
        if (req_list_ptr->empty()) inflight_req.erase(req_it);
        // delete in the queue order
        inflight_req_order.erase(it_addr);
    }
}


void
AXIResponder::inflight_resp(uint64_t addr, const uint8_t* data) {
    #ifdef PRINT_DEBUG
        printf("(%lu) nvdla#%d %s: Inflight Resp Timing: addr 0x%08x \n",
                wrapper->tickcount, wrapper->id_nvdla, name, addr);
    #endif

    auto addr_it = inflight_req.find(addr);
    std::list<axi_r_txn>& req_list = addr_it->second;

    std::list<axi_r_txn>::iterator it = req_list.begin();
    // Get the correct ptr
    int count_pos = 0;
    while (it != req_list.end() && it->rvalid) {
       it++;
       count_pos++;
    }
    assert(it != req_list.end());
    for (int i = 0; i < AXI_WIDTH / 8; i++) {
        it->rdata[i] = data[i];
    }
    it->rvalid = 1;
    if (it->is_prefetch) {
        printf("(%lu) nvdla#%d read data returned by gem5 PREFETCH, addr 0x%08lx\n", wrapper->tickcount, wrapper->id_nvdla, addr);
        //! delete this txn in inflight_req_order and inflight_req
        // first delete in inflight_req_order
        bool deleted = false;
        for (auto addr_it = inflight_req_order.begin(); addr_it != inflight_req_order.end(); addr_it++) {
            if ((*addr_it) == addr) {
                if (count_pos == 0) {
                    // that's the exact addr_it that should be deleted
                    inflight_req_order.erase(addr_it);
                    deleted = true;
                    break;
                } else {
                    count_pos--;
                }
            }
        }
        assert(deleted);

        // then delete in inflight_req
        req_list.erase(it);
        if (req_list.empty()) {
            inflight_req.erase(addr_it);
        }

    } else {
        printf("(%lu) nvdla#%d read data returned by gem5, addr 0x%08lx\n", wrapper->tickcount, wrapper->id_nvdla, addr);
    }
    #ifdef PRINT_DEBUG
    printf("Remaining %d\n", inflight_req_order.size());
    printf("(%lu) nvdla#%d %s: Inflight Resp Timing Finished: addr %08lx \n",
            wrapper->tickcount, wrapper->id_nvdla, name, addr);
    #endif
}

void
AXIResponder::inflight_dma_resp(const uint8_t* data, uint32_t len) {
    //! we HAVE TO assume dma is in-order. If it is out-of-order due to bus arbitration, even DMAFifo can't handle it
    uint64_t addr = inflight_dma_addr_queue.front();
    printf("(%lu) nvdla#%d AXIResponder handling DMA return data addr 0x%08lx with length %d.\n",
           wrapper->tickcount, wrapper->id_nvdla, addr, len);

    auto addr_it = inflight_dma_attr.find(addr);
    assert(addr_it != inflight_dma_attr.end());
    if (!addr_it->second.is_bypass)     // if not bypass, then write this data
        wrapper->spm->write_spm_line(addr, data, 0);

    for (auto it : inflight_dma_attr[addr].deps) {
        auto txn_addr = it.first;
        auto txn_it = it.second;
        for (uint64_t pos = 0; pos < AXI_WIDTH / 8; pos++)
            txn_it->rdata[pos] = data[txn_addr - addr + pos];
        txn_it->rvalid = 1;
    }

    inflight_dma_attr.erase(addr);
    inflight_dma_addr_queue.pop();
}

void
AXIResponder::read_for_traceLoaderGem5(uint64_t start_addr, uint32_t length) {
    uint64_t txn_start_addr = (uint64_t)start_addr & ~(uint64_t)(AXI_WIDTH / 8 - 1);
    if (txn_start_addr != start_addr)
        printf("this dump_mem is not aligned to AXI_WIDTH / 8, op.addr = 0x%08lx\n", start_addr);

    for (uint64_t delta_addr = 0; txn_start_addr + delta_addr < start_addr + length; delta_addr += (AXI_WIDTH / 8)) {
        uint64_t txn_addr = txn_start_addr + delta_addr;
        axi_r_txn txn;
        txn.rvalid = 0;
        txn.burst = true;
        txn.rlast = (txn_start_addr + delta_addr + (AXI_WIDTH / 8) >= start_addr + length);
        txn.is_prefetch = 0;
        if (dma_enable) {
            bool got = wrapper->spm->read_spm_axi_line(txn_addr, txn.rdata);
            if(got) {
                txn.rvalid = 1;
            } else {    // when verifying results, no need to use dma...
                wrapper->addReadReq(sram, true, false, txn_addr, AXI_WIDTH / 8);
            }
        } else {
            wrapper->addReadReq(sram, true, true, txn_addr, AXI_WIDTH / 8);
        }

        // put txn to the map
        inflight_req[txn_addr].push_back(txn);
        // put in req the txn
        inflight_req_order.push_back(txn_addr);
    }
}

// this function assumes start_addr is aligned to (AXI_WIDTH / 8)
uint32_t
AXIResponder::read_response_for_traceLoaderGem5(uint64_t start_addr, uint8_t* data_buffer) {
    // check status of memory reading request
    assert(!inflight_req_order.empty());
    uint64_t addr_front = inflight_req_order.front();

    if (addr_front != start_addr) {
        // this should not happen
        printf("(%lu) nvdla#%d addr_front(0x%08lx) does not match start_addr (0x%08lx)",
               wrapper->tickcount, wrapper->id_nvdla, addr_front, start_addr);
        abort();
    }

    auto& addr_front_list = inflight_req[addr_front];

    assert(!addr_front_list.empty());
    axi_r_txn& txn = addr_front_list.front();
    if (txn.rvalid) {
        printf("(%lu) nvdla#%d memory request at addr 0x%08lx has arrived.\n", wrapper->tickcount, wrapper->id_nvdla, start_addr);

        // get the value
        for (uint32_t i = 0; i < AXI_WIDTH / 8; i++) {
            data_buffer[i] = txn.rdata[i];
        }

        addr_front_list.pop_front();
        if (addr_front_list.empty()) {
            inflight_req.erase(addr_front);
        }

        inflight_req_order.pop_front();

        return 1;
    } else {
        printf("(%lu) nvdla#%d still waiting for memory request at addr 0x%08lx\n", wrapper->tickcount, wrapper->id_nvdla, start_addr);
        return 0;
    }
}

uint32_t
AXIResponder::getRequestsOnFlight() {
    return inflight_req_order.size();
}

void
AXIResponder::add_rd_var_log_entry(uint64_t addr, uint32_t size) {
    read_var_log.emplace_back(addr, size, 0);
}

void
AXIResponder::log_req_issue(uint64_t addr) {
    for (auto it = read_var_log.begin(); it != read_var_log.end(); it++) {
        uint32_t& log_entry_length = std::get<1>(*it);
        uint64_t& log_entry_addr = std::get<0>(*it);

        if (log_entry_addr <= addr && addr < log_entry_addr + log_entry_length) {
            uint32_t& log_entry_issued_len = std::get<2>(*it);
            if (addr > log_entry_addr + log_entry_issued_len) {
                printf("nvdla#%d addr issued is beyond the log.\n", wrapper->id_nvdla);
                abort();
            } else if (addr == log_entry_addr + log_entry_issued_len) {
                // that's the normal case
                log_entry_issued_len += (AXI_WIDTH / 8);

                if (log_entry_issued_len >= log_entry_length)
                    read_var_log.erase(it);
            } else {
                // printf("read req for %#x has been covered by a previous pft / fetch.\n", addr);
            }
            // since requests are either solely DMA or solely cache-line-sized,
            // it is not possible that log_entry_issued_len is not aligned with spm_line_size
            break;
        }
    }
    // a mem req might also be covered by a popped entry, so no assert for logged
}

void
AXIResponder::generate_prefetch_request() {
    if (read_var_log.empty())
        return;
    // assume the front of the list is a legal entry to prefetch
    uint64_t& log_entry_addr = std::get<0>(read_var_log.front());
    uint32_t& log_entry_length = std::get<1>(read_var_log.front());
    uint32_t& log_entry_issued_len = std::get<2>(read_var_log.front());

    uint64_t to_issue_addr = log_entry_addr + log_entry_issued_len;

    // generate the corresponding txn
    axi_r_txn txn;

    // write to the txn
    txn.rvalid = 0;     // since read addresses are streaming, we don't think this is present in spm (if dma_enable)
    txn.is_prefetch = 1;
    // if to_issue_addr is covered by a previous dma, it's useless to issue such prefetch request

    if (dma_enable) {
        // assert(!wrapper->check_txn_data_in_spm(to_issue_addr)); // weights are unique, shouldn't have been prefetched
        // todo: whether to assert should be consistent with rd_only_var_log: if the script generating it allows `input` in rd_only_var_log, this should not be asserted
        // if not, this could be asserted

        // first check whether this addr has been covered by an inflight DMA request or not
        uint64_t spm_line_addr = to_issue_addr & ~((uint64_t)(wrapper->spm->spm_line_size - 1));
        std::map<uint64_t, DMAAttr>::iterator map_it;
        bool alloc_succ;
        std::tie(map_it, alloc_succ) = inflight_dma_attr.emplace(spm_line_addr, DMAAttr());
        if (alloc_succ) {
            // not covered, need to initiate a new DMA
            printf("(%lu) nvdla#%d PREFETCH (DMA) request addr 0x%08lx issued.\n", wrapper->tickcount, wrapper->id_nvdla, spm_line_addr);
            map_it->second.is_bypass = false;

            inflight_dma_addr_queue.push(spm_line_addr);
            wrapper->addDMAReadReq(spm_line_addr, wrapper->spm->spm_line_size);

            log_entry_issued_len += spm_line_addr + wrapper->spm->spm_line_size - to_issue_addr;
            // here we don't add dma prefetch to inflight_req and inflight_order
            // because as long as spm can get the prefetched data, we don't bother axi responder to check it

        }   // else we don't need a new dma request. it is covered by a previous one
    } else {
        printf("(%lu) nvdla#%d PREFETCH request addr 0x%08lx issued.\n", wrapper->tickcount, wrapper->id_nvdla, to_issue_addr);
        inflight_req[to_issue_addr].push_back(txn);
        inflight_req_order.push_back(to_issue_addr);

        // cacheable for buf_mode = BUF_MODE_ALL and BUF_MODE_PFT
        wrapper->addReadReq(sram, true, true, to_issue_addr, AXI_WIDTH / 8);
        log_entry_issued_len += AXI_WIDTH / 8;  // todo: here the gap of issuing should be cache line size. currently cache line size == AXI_WIDTH/8
    }

    if (log_entry_issued_len >= log_entry_length)       // this prefetch is the end of the variable
        read_var_log.pop_front();
}
