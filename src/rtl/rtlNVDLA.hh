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

#ifndef __RTL_NVDLA_VERILATOR_HH__
#define __RTL_NVDLA_VERILATOR_HH__

#include <string>
#include <vector>
#include <utility>

#include "dev/dma_device.hh"
#include "dev/dma_nvdla.hh"

#include "cpu/base.hh"
#include "cpu/translation.hh"
#include "debug/rtlNVDLA.hh"
#include "debug/rtlNVDLADebug.hh"
#include "params/rtlNVDLA.hh"
#include "rtl/rtlObject.hh"
#include "rtl/traceLoaderGem5.hh"
#include "sim/system.hh"
#include "wrapper_nvdla.hh"

namespace gem5
{

class TraceLoaderGem5;

/**
 * rtlNVDLA class
 */
class rtlNVDLA : public rtlObject
{
  private:

    /**
     * Port on the memory-side that receives responses.
     * Mostly just forwards requests to the owner
     */
    class MemNVDLAPort : public RequestPort
    {
      private:
        /// The object that owns this object (rtlNVDLA)
        rtlNVDLA *owner;

        /// If we tried to send a packet and it was blocked, store it here
        //PacketPtr blockedPacket;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        MemNVDLAPort(const std::string& name, rtlNVDLA *owner, bool sram_) :
            RequestPort(name, owner),
            owner(owner),
            sram(sram_),
            blockedRetry(false)
        { }

        uint8_t recentData;

        uint32_t recentData32;

        const uint8_t *recentDataptr;

        std::queue<PacketPtr> pending_req;

        bool sram;
        // if we are blocked due to a req retry
        bool blockedRetry;

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         *
         * @param packet to send.
         */
        void sendPacket(PacketPtr pkt, bool timing);

        /**
         * We check if we have any pending request and we try to send it
         *
         */
        void tick();

      protected:
        /**
         * Receive a timing response from the slave port.
         */
        bool recvTimingResp(PacketPtr pkt) override;

        /**
         * Called by the slave port if sendTimingReq was called on this
         * master port (causing recvTimingReq to be called on the slave
         * port) and was unsuccesful.
         */
        void recvReqRetry() override;

        /**
         * Called to receive an address range change from the peer slave
         * port. The default implementation ignores the change and does
         * nothing. Override this function in a derived class if the owner
         * needs to be aware of the address ranges, e.g. in an
         * interconnect component like a bus.
         */
        void recvRangeChange() override;
    };

    /**
     * Handle the request from the CPU side
     *
     * @param requesting packet
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PacketPtr pkt) override;


    /*
     * @param responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponse(PacketPtr pkt) override;

    /**
     * Handle the response from the memory side for NVDLA
     *
     * @param responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponseNVDLA(PacketPtr pkt, bool sram);

    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read.
     *
     * @param packet to functionally handle
     */
    void handleFunctional(PacketPtr pkt) override;

    /**
     * Return the address ranges this memobj is responsible for. Just use the
     * same as the next upper level of the hierarchy.
     *
     * @return the address ranges this memobj is responsible for
     */
    AddrRangeList getAddrRanges() const override;

    // function that is called at every cycle
    void tick() override;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange() override;

    /// Instantiation of the CPU-side ports
    CPUSidePort cpuPort;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

    MemNVDLAPort sramPort;

    MemNVDLAPort dramPort;

    int bytesToRead;    // it works as a counter
    unsigned int bytesReaded;

    // True if this is currently blocked waiting for a response.
    bool blocked;

    const unsigned int max_req_inflight;

    bool timingMode;

    uint64_t id_nvdla;

    uint64_t baseAddrDRAM;
    uint64_t baseAddrSRAM;

    uint32_t startMemRegion;
    uint32_t startBaseTrace;
    char *ptrTrace;
    bool traceEnable;

    struct nvdla_stats
    {
        statistics::Scalar nvdla_cycles;
        statistics::Scalar nvdla_reads;
        statistics::Scalar nvdla_writes;
        statistics::Histogram nvdla_avgReqCVSRAM;
        statistics::Histogram nvdla_avgReqDBBIF;

        // statistics::Scalar num_dma_rd;
        // statistics::Scalar num_dma_wr;

        // statistics::Scalar num_spm_hit;
        // statistics::Scalar num_spm_miss;
        // statistics::Scalar num_spm_use;
    };
    nvdla_stats stats;
    void processOutput(outputNVDLA& out);

public:

    // NVDLA pointers
    Wrapper_nvdla *wr;
    TraceLoaderGem5 *trace;

    // rtl packet
    inputNVDLA input;

    ~rtlNVDLA();
    void runIterationNVDLA();
    void initNVDLA();
    void initRTLModel() override;
    void endRTLModel() override;
    void loadTraceNVDLA(char *ptr);

    // variables for the NVDLA
    int quiesc_timer;
    int waiting;
    int waiting_for_gem5_mem;   // an indicator only used for AXI_DUMPMEM
    int flushing_spm;           // we need to flush output data in spm to main memory after csb->done()
    uint32_t cyclesNVDLA;

    /** constructor
     */
    rtlNVDLA(const rtlNVDLAParams &params);

    gem5::Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;

    void finishTranslation(WholeTranslationState *state) override;

    uint8_t readAXI(uint32_t addr, bool sram, bool timing);
    uint32_t readAXI32(uint32_t addr, bool sram, bool timing);
    const uint8_t * readAXIVariable(uint32_t addr, bool sram,
                                    bool timing, unsigned int size);
    void writeAXI(uint32_t addr, uint8_t data, bool sram, bool timing);
    void writeAXILong(uint32_t addr, uint32_t length, uint8_t* data, uint64_t mask, bool sram, bool timing);

    uint32_t getRealAddr(uint32_t addr, bool sram);
    uint32_t getAddrNVDLA(uint32_t addr, bool sram);
    /**
     * Register the stats
     */
    void regStats() override;

    int prefetch_enable;

    uint32_t spm_latency;
    uint32_t spm_line_size;
    uint32_t spm_line_num;

    int dma_enable;
    DmaPort dmaPort;
    DmaReadFifo* dma_rd_engine;
    DmaNvdla* dma_wr_engine;

    uint32_t pft_threshold;
    bool use_fake_mem;

    void try_get_dma_read_data(uint32_t size);
};

} //End namespace gem5

#endif // __RTL_NVDLA_VERILATOR_HH__
