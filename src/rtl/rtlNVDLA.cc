/*
 * Copyright (c) 2022 Guillem Lopez Paradis
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Guillem Lopez Paradis
 */

#include "rtl/rtlNVDLA.hh"

namespace gem5
{


rtlNVDLA::rtlNVDLA(const rtlNVDLAParams &params) :
    rtlObject(params),
    cpuPort(params.name + ".cpu_side", this),
    memPort(params.name + ".mem_side", this),
    sramPort(params.name + ".sram_port", this, true),
    dramPort(params.name + ".dram_port", this, false),
    bytesToRead(0),
    bytesReaded(0),
    blocked(false),
    max_req_inflight(params.maxReq),
    freq_ratio(params.freq_ratio),
    id_nvdla(params.id_nvdla),
    baseAddrDRAM(params.base_addr_dram),
    baseAddrSRAM(params.base_addr_sram),
    waiting_for_gem5_mem(0),
    flushing_spm(0),
    prefetch_enable(params.prefetch_enable),
    spm_latency(params.spm_latency),
    spm_line_size(params.spm_line_size),
    spm_line_num(params.spm_size / params.spm_line_size),
    dma_enable(params.dma_enable),
    dmaPort(this, params.system),
    pft_threshold(params.pft_threshold),
    use_fake_mem(params.use_fake_mem) {

    initNVDLA(params.use_shared_spm);
    startMemRegion = 0xC0000000;
    cyclesNVDLA = 0;
    std::cout << std::hex << "NVDLA " << id_nvdla
              << " Base Addr DRAM: " << baseAddrDRAM
              << " Base Addr SRAM: " << baseAddrSRAM << std::endl;
    // Clear input
    memset(&input, 0, sizeof(inputNVDLA));

    if (dma_enable) {
        dma_rd_engine = new DmaReadFifo(dmaPort, spm_line_size * spm_line_num,
                                    spm_line_size, spm_line_num, Request::UNCACHEABLE);
        dma_wr_engine = new DmaNvdla(dmaPort, true, spm_line_size * spm_line_num,
                                     spm_line_size, spm_line_num, Request::UNCACHEABLE);
    } else {
        dma_rd_engine = nullptr;
        dma_wr_engine = nullptr;
    }

    pft_buf_size = params.pft_buf_size;

    switch (params.buffer_mode) {
        case 0:
            buffer_mode = BUF_MODE_ALL;
            break;
        case 1:
            buffer_mode = BUF_MODE_PFT;
            break;
        default:
            assert(false);
    }

    // todo: add another parameter controlling the mode of using embedded SPM / cache, whether as an all-in-one buffer or simply a prefetch buffer
    // this involves changing the encoding of rd_only_var_log, caching attributes of rd_wr packets, SPM handling the non-caching DMA txns
    // and also parameter meaningful logic, as prefetch buffer makes no sense when it is not prefetching

}


rtlNVDLA::~rtlNVDLA() {
    delete wr;
    if (dma_rd_engine != nullptr)
        delete dma_rd_engine;
    if (dma_wr_engine != nullptr)
        delete dma_wr_engine;
}

Port &
rtlNVDLA::getPort(const std::string &if_name, PortID idx) {
    if (if_name == "mem_side") {
        return memPort;
    } else if (if_name == "cpu_side") {
        return cpuPort;
    } else if (if_name == "sram_port") {
        return sramPort;
    } else if (if_name == "dram_port") {
        return dramPort;
    } else if (if_name == "dma_port") {
        return dmaPort;
    } else {
        panic_if(true, "Asking to rtlNVDLA for a port other "
                       "than cpu or mem");
        return ClockedObject::getPort(if_name, idx);
    }
}

bool
rtlNVDLA::handleRequest(PacketPtr pkt) {
    // Here we have just received the start rtlNVDLA function
    // we check if there is an outstanding call
    // otherwise start getting the whole trace
    if (blocked) {
        // There is currently an outstanding request. Stall.
        return false;
    }

    blocked = true;

    DPRINTF(rtlNVDLA, "Got request for size: %d, addr: %#x\n",
                        pkt->getSize(),
                        pkt->req->getVaddr());

    bytesToRead = pkt->getSize();   // it works as a counter
    trace->trace_and_rd_log_size = bytesToRead;

    ptrTrace = (char *) malloc(bytesToRead);

    startTranslate(pkt->req->getVaddr(), 0);

    return true;
}

void
rtlNVDLA::initNVDLA(bool use_shared_spm) {
    // Wrapper
    wr = new Wrapper_nvdla(id_nvdla, max_req_inflight,
    dma_enable, spm_latency, spm_line_size, spm_line_num, prefetch_enable, use_shared_spm, buffer_mode, pft_buf_size);
    // wrapper trace from nvidia
    trace = new TraceLoaderGem5(wr->csb, wr->axi_dbb, wr->axi_cvsram);
}

void
rtlNVDLA::initRTLModel() {

}
void
rtlNVDLA::endRTLModel() {

}

void
rtlNVDLA::loadTraceNVDLA(char *ptr) {
    // load the trace into the queues
    trace->load(ptr);
    trace->load_read_var_log(ptr);  // call load_read_var_log no matter prefetch enabled or not.
    // If not, we will directly see 8 bytes of 0xff indicating the end of rd_var_log.

    startBaseTrace = trace->getBaseAddr();

    DPRINTF(rtlNVDLA,
            "Base Addr: %#x \n",
            trace->getBaseAddr());
    // reset NVDLA
    wr->init();
    // init some variable before exec of trace
    quiesc_timer = 200;
    waiting = 0;

    schedule(tickEvent, nextCycle() + (freq_ratio - 1) * clockPeriod());
}

void
rtlNVDLA::processOutput(outputNVDLA& out) {
    if (out.read_valid) {
        while (!out.read_buffer.empty()) {
            read_req_entry_t aux = out.read_buffer.front();
            // printf("read req addr: %08x, size %d\n", aux.read_addr, aux.read_bytes);
            // std::cout << std::hex << "read req: " \
            // << aux.read_addr << std::endl;
            readAXIVariable(aux.read_addr,
                            aux.read_sram,
                            aux.read_timing,
                            aux.cacheable,
                            aux.read_bytes);
            out.read_buffer.pop();
        }
    }

    if (out.write_valid) {
        while (!out.write_buffer.empty()) {     // this buffer outputs in 1-byte granularity
            write_req_entry_t aux = out.write_buffer.front();
            writeAXI(aux.write_addr,
                     aux.write_data,
                     aux.write_sram,
                     aux.write_timing);
            out.write_buffer.pop();
        }

        while (!out.long_write_buffer.empty()) { // this buffer outputs in 1-64 bytes granularity
            auto& aux = out.long_write_buffer.front();
            writeAXILong(aux.write_addr, aux.length, aux.write_data, aux.write_mask, aux.write_sram, aux.write_timing, aux.cacheable);
            out.long_write_buffer.pop();
        }
    }

    //! use dma_rd_engine to process reading requests
    // memory requests already in spm is dealt with in wrapper_nvdla
    // only one DMA request can be tackled at once
    if (!out.dma_read_buffer.empty()) {
        auto& aux = out.dma_read_buffer.front();

        uint64_t real_addr = getRealAddr(aux.first, false);         // only DRAM has DMA fetch
        if (dma_rd_engine->atEndOfBlock()) {
            dma_rd_engine->startFill(real_addr, aux.second);
            printf("(%lu) nvdla#%d DMA read req is issued: addr 0x%08lx, len %d\n", wr->tickcount, id_nvdla, aux.first, aux.second);
            // stats.num_dma_rd++;
            // after successfully calling DMA, pop aux
            out.dma_read_buffer.pop();
        }   // if previous DMA request not sent, have to stop here
    }


    //! use dma_wr_engine to process writing requests
    if (!out.dma_write_buffer.empty()) {
        auto& aux = out.dma_write_buffer.front();
        if (dma_wr_engine->atEndOfBlock()) {                    // previous DMA write has been sent
            uint64_t real_addr = getRealAddr(aux.first, false);     // only DRAM has DMA write
            dma_wr_engine->startFill(real_addr, aux.second.size(), aux.second.data());
            printf("(%lu) nvdla#%d DMA write req is issued: addr 0x%08lx, len %ld\n", wr->tickcount, id_nvdla, aux.first, aux.second.size());
            // stats.num_dma_wr++;
            out.dma_write_buffer.pop();
        }
    }
}

void
rtlNVDLA::runIterationNVDLA() {
    wr->clearOutput();

    int extevent;

    if (!waiting_for_gem5_mem)
        extevent = wr->csb->eval(waiting);
    else
        extevent = 0;

    if (extevent == TraceLoaderGem5::TRACE_AXIEVENT || waiting_for_gem5_mem) {
        trace->axievent(&waiting_for_gem5_mem);
    } else if (extevent == TraceLoaderGem5::TRACE_WFI) {
        waiting = 1;
        printf("(%lu) waiting for interrupt...\n", wr->tickcount);
    } else if (extevent == TraceLoaderGem5::TRACE_RESET) {
        wr->init();
        printf("nvdla#%d reset\n", id_nvdla);
    }

    if (waiting && wr->dla->dla_intr) {
        printf("(%lu) nvdla#%d interrupt!\n", wr->tickcount, id_nvdla);
        waiting = 0;
    }

    if (!waiting_for_gem5_mem) {
        if (!use_fake_mem) {
            wr->axi_dbb->eval_timing();
            wr->axi_cvsram->eval_timing();
        } else {
            wr->axi_dbb->eval_ram();
            wr->axi_cvsram->eval_ram();
        }
    }

    outputNVDLA& output = wr->tick(input);

    if (dma_enable) {
        try_get_dma_read_data(spm_line_size);
        if (wr->csb->done()) {
            // write back dirty data in spm to main memory
            if (!flushing_spm) {
                wr->spm->write_back_dirty();
                flushing_spm = 1;
            }
            if (flushing_spm && output.dma_write_buffer.empty()) {   // all items have been flushed to dma write engine
                flushing_spm = 0;
                // printf("nvdla#%d spm flush complete!\n", id_nvdla);
            }
        }
    }
    processOutput(output);
}

void
rtlNVDLA::tick() {
    DPRINTF(rtlNVDLADebug, "Tick NVDLA \n");
    // if we are still running trace
    // runIteration
    // schedule new iteration
    if (!wr->csb->done() || (quiesc_timer-- > 0) || waiting_for_gem5_mem || flushing_spm) {
        // Update stats
        // stats.nvdla_avgReqCVSRAM.sample(wr->axi_cvsram->getRequestsOnFlight());
        stats.nvdla_avgReqDBBIF.sample(wr->axi_dbb->getRequestsOnFlight());
        stats.nvdla_cycles++;
        cyclesNVDLA++;
        runIterationNVDLA();
        schedule(tickEvent, nextCycle() + (freq_ratio - 1) * clockPeriod());
    } else {
        // we have finished running the trace
        printf("done at %lu ticks\n", wr->tickcount);

        if (!trace->test_passed()) {
            printf("*** FAIL: test failed due to output mismatch\n");

        } else if (!wr->csb->test_passed()) {
            printf("*** FAIL: test failed due to CSB read mismatch\n");
        } else {
            printf("NVDLA %d *** PASS\n", id_nvdla);
        }

        // we send a null packet telling we have finished
        RequestPtr req = std::make_shared<Request>(id_nvdla, 1,
                                               Request::UNCACHEABLE, 0);
        PacketPtr packet = nullptr;
        // we create the real packet, write request
        packet = Packet::createRead(req);
        packet->allocate();
        packet->makeResponse();
        cpuPort.sendPacket(packet);
    }
    // check DRAM Ports
    dramPort.tick();
    sramPort.tick();
}


bool
rtlNVDLA::handleResponse(PacketPtr pkt) {
    if (pkt->hasData()) {
        char *data_ptr = pkt->getPtr<char>();

        int maxRead = ((bytesToRead - bytesReaded) > 64) ?
                         64 : (bytesToRead - bytesReaded);

        for (int i = 0;i < maxRead; i++) {
            ptrTrace[bytesReaded + i] = data_ptr[i];
        }

        bytesReaded += 64;

        if (bytesReaded < bytesToRead) {
            startTranslate(pkt->req->getVaddr() + 64, 0);
        } else {
            bytesReaded = 0;
            bytesToRead = 0;

            // Load the trace and reset NVDLA
            loadTraceNVDLA(ptrTrace);
        }
    }
    else {
        // Strange situation, report!
        DPRINTF(rtlNVDLA, "Got response for addr %#x no data\n",
                            pkt->getAddr());
    }


    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    blocked = false;

    return true;
}

bool
rtlNVDLA::handleResponseNVDLA(PacketPtr pkt, bool sram) {
    if (pkt->hasData()) {
        if (pkt->isRead()) {
            // Get data from gem5 memory system
            // and set it to AXI
            DPRINTF(rtlNVDLADebug,
                    "Handling response for data read Timing\n");
            // Get the data ptr and sent it
            const uint8_t* dataPtr = pkt->getConstPtr<uint8_t>();
            uint64_t addr_nvdla = getAddrNVDLA(pkt->getAddr(), sram);
            if (sram) {
                // SRAM
                wr->axi_cvsram->inflight_resp(addr_nvdla, dataPtr);
            } else {
                // DBBIF
                wr->axi_dbb->inflight_resp(addr_nvdla, dataPtr);
            }
        } else {
            // this is somehow odd, report!
            DPRINTF(rtlNVDLA, "Got response for addr %#x no read\n",
                    pkt->getAddr());
        }
    } else {
         DPRINTF(rtlNVDLA, "Got response for addr %#x no data\n",
         pkt->getAddr());
    }

    return true;
}

void
rtlNVDLA::handleFunctional(PacketPtr pkt) {
    // Just pass this on to the memory side to handle for now.
    memPort.sendFunctional(pkt);
}

AddrRangeList
rtlNVDLA::getAddrRanges() const {
    DPRINTF(rtlNVDLA, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
rtlNVDLA::sendRangeChange() {
    cpuPort.sendRangeChange();
}

void
rtlNVDLA::finishTranslation(WholeTranslationState *state) {
    DPRINTF(rtlNVDLA, "Finishing translation\n");

    RequestPtr req = state->mainReq;

    if (req->hasPaddr()) {
        DPRINTF(rtlNVDLA,
                "Finished translation step: Got request for addr %#x %#x\n",
        state->mainReq->getVaddr(),state->mainReq->getPaddr());

    } else {
        // we end up in a situation we shouldn't be
        DPRINTF(rtlNVDLA,
                "Finished translation without physical addr\n");
    }

    PacketPtr new_pkt = new Packet(req, MemCmd::ReadReq, 64);

    if (memPort.blockedPacket != nullptr) {
        DPRINTF(rtlNVDLA, "Packet lost\n");
    } else {
        new_pkt->allocate();
        memPort.sendPacket(new_pkt);
    }
}

// DRAM PORT
void
rtlNVDLA::MemNVDLAPort::sendPacket(PacketPtr pkt, bool timing) {
    if (timing) {
        DPRINTF(rtlNVDLA, "Add Mem Req pending %#x size: %d timing s: %d\n",
            pkt->getAddr(), pkt->getSize(), pending_req.size());
        // we add as a pending request, we deal later
        pending_req.push(pkt);
    } else {
        DPRINTF(rtlNVDLA, "Send Mem Req to DRAM %#x size: %d functional\n",
            pkt->getAddr(), pkt->getSize());
        // send Atomic
        sendAtomic(pkt);
        // Update all the pointers
        recentData32 = *pkt->getConstPtr<uint32_t>();
        recentData = *pkt->getConstPtr<uint8_t>();
        recentDataptr = pkt->getConstPtr<uint8_t>();
    }
}

void
rtlNVDLA::MemNVDLAPort::recvRangeChange() {
    owner->sendRangeChange();
}

bool
rtlNVDLA::MemNVDLAPort::recvTimingResp(PacketPtr pkt) {
    DPRINTF(rtlNVDLA, "Got response SRAM?: %d\n", sram);
    return owner->handleResponseNVDLA(pkt, sram);
}

void
rtlNVDLA::MemNVDLAPort::recvReqRetry() {
    // we check we have pending packets
    assert(blockedRetry);
    // Grab the blocked packet.
    PacketPtr pkt = pending_req.front();
    bool sent = sendTimingReq(pkt);
    // if not sent put it in the queue
    if (sent) {
        pending_req.pop();
        blockedRetry = false;
    }
    // else
    // we do nothing
    // we failed sending the packet we wait
}

// In this function we send the packets
void
rtlNVDLA::MemNVDLAPort::tick() {
    // we check we have pending packets
    if (!blockedRetry and !pending_req.empty()) {
        PacketPtr pkt = pending_req.front();
        bool sent = sendTimingReq(pkt);
        // if not sent, put it in the queue
        if (sent) {
            pending_req.pop();
        } else {
            blockedRetry = true;
        }
    }
}

uint64_t
rtlNVDLA::getRealAddr(uint64_t addr, bool sram) {
    uint64_t real_addr;

    if (sram) {
        // Base addr is 0x5000_0000
        real_addr = (addr - 0x50000000) + baseAddrSRAM;
    } else {
        // Base addr is 0x8000_0000
        real_addr = (addr - 0x80000000) + baseAddrDRAM;
    }
    return real_addr;
}

uint64_t
rtlNVDLA::getAddrNVDLA(uint64_t addr, bool sram) {
    uint64_t real_addr;

    if (sram) {
        // Base addr is 0x5000_0000
        //baseAddrDRAM
        real_addr = (addr - baseAddrSRAM) + 0x50000000;
        //real_addr = (addr - 0xC0000000) + 0x50000000;
    } else {
        // Base addr is 0x8000_0000
        // 0xA0000000
        real_addr = (addr - baseAddrDRAM) + 0x80000000;
    }
    return real_addr;
}

const uint8_t *
rtlNVDLA::readAXIVariable(uint64_t addr, bool sram, bool timing, bool cacheable, unsigned int size) {
    // Update stats
    stats.nvdla_reads++;

    uint64_t real_addr = getRealAddr(addr, sram);

    DPRINTF(rtlNVDLA,
            "Read AXI Variable addr: %#x, real_addr %#x, size %d\n",
            addr, real_addr, size);

    RequestPtr req = std::make_shared<Request>(real_addr, size, cacheable ? 0: Request::UNCACHEABLE, 0);
    PacketPtr packet = nullptr;
    // we create the real packet, write request
    packet = Packet::createRead(req);
    packet->allocate();
    // send the packet in timing?
    if (sram) {
        sramPort.sendPacket(packet, timing);
        return sramPort.recentDataptr;
    } else {
        dramPort.sendPacket(packet, timing);
        return dramPort.recentDataptr;
    }
    return nullptr;
}

void
rtlNVDLA::writeAXI(uint64_t addr, uint8_t data, bool sram, bool timing) {
    // Update stats
    stats.nvdla_writes++;

    uint64_t real_addr = getRealAddr(addr, sram);

    DPRINTF(rtlNVDLA,
            "Write AXI Variable addr: %#x, real_addr %#x, data_to_write 0x%02x\n",
            addr, real_addr, data);
    //Request(Addr paddr, unsigned size, Flags flags, MasterID mid)
    // addr is the physical addr
    // size is one byte
    // flags is physical (vaddr is also the physical one)
    RequestPtr req = std::make_shared<Request>(real_addr, 1,
                                               0, 0);
    PacketPtr packet = nullptr;
    // we create the real packet, write request
    packet = Packet::createWrite(req);
    // always in Little Endian
    PacketDataPtr dataAux = new uint8_t[1];
    dataAux[0] = data;

    packet->dataDynamic(dataAux);
    // send the packet in timing?
    if (sram) {
        sramPort.sendPacket(packet, timing);
    } else {
        dramPort.sendPacket(packet, timing);
    }
}

void
rtlNVDLA::writeAXILong(uint64_t addr, uint32_t length, uint8_t* data, uint64_t mask, bool sram, bool timing, bool cacheable) {
    stats.nvdla_writes++;

    uint64_t real_addr = getRealAddr(addr,sram);
    RequestPtr req = std::make_shared<Request>(real_addr, length, cacheable ? 0: Request::UNCACHEABLE, 0);
    std::vector<bool> byte_enable_vec(length);

    for (int i = 0; i < length; i++)
        byte_enable_vec[i] = ((mask >> i) & 1);

    req->setByteEnable(byte_enable_vec);

    PacketPtr packet = nullptr;
    packet = Packet::createWrite(req);
    // always in Little Endian
    // PacketDataPtr dataAux = new uint8_t[length];
    // for (int i = 0; i < length; i++)
    //     dataAux[i] = data[i];

    // here we directly give the 'data' ptr to pkt.
    // This is supported by the fact that 'data' is malloced by ourselves
    packet->dataDynamic(data);
    // send the packet in timing?
    if (sram) {
        sramPort.sendPacket(packet, timing);
    } else {
        dramPort.sendPacket(packet, timing);
    }
}

void
rtlNVDLA::try_get_dma_read_data(uint32_t size) {
    uint8_t dma_temp_buffer[size];
    bool get_success = dma_rd_engine->tryGet(dma_temp_buffer, size);
    if (get_success) {
        // we assume only DBB involves DMA. SRAM should not be accessed with DMA
        wr->axi_dbb->inflight_dma_resp(dma_temp_buffer, size);
    }
}

void
rtlNVDLA::regStats() {
    // If you don't do this you get errors about uninitialized stats.
    ClockedObject::regStats();

    using namespace statistics;

    stats.nvdla_cycles
        .name(name() + ".nvdla_cycles")
        .desc("Number of Cycles to run the trace");

    stats.nvdla_reads
        .name(name() + ".nvdla_reads")
        .desc("Number of reads performed");

    stats.nvdla_writes
        .name(name() + ".nvdla_writes")
        .desc("Number of writes performed");
    stats.nvdla_avgReqCVSRAM
        .init(256)
        .name(name() + ".nvdla_avgReqCVSRAM")
        .desc("Histogram Requests onflight CVSRAM")
        .flags(pdf);

    stats.nvdla_avgReqDBBIF
        .init(256)
        .name(name() + ".nvdla_avgReqDBBIF")
        .desc("Histogram Requests onflight DBBIF")
        .flags(pdf);


    // stats.num_dma_rd
    //     .name(name() + ".num_dma_rd")
    //     .desc("Number of DMA read issued by this NVDLA");
    // stats.num_dma_wr
    //     .name(name() + ".num_dma_wr")
    //     .desc("Number of DMA write issued by this NVDLA");
}

} //End namespace gem5
