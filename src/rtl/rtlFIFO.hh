/*
 * Copyright (c) 2017 Jason Lowe-Power
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
 * Authors: Jason Lowe-Power Guillem Lopez Paradis
 */

#ifndef __RTL_FIFO_VERILATOR_HH__
#define __RTL_FIFO_VERILATOR_HH__

#include <queue>
#include <string>
#include <vector>

#include "cpu/base.hh"
#include "cpu/translation.hh"
#include "debug/rtlFIFO.hh"
#include "debug/rtlFIFODebug.hh"
#include "rtl/rtlObject.hh"
#include "sim/system.hh"
#include "wrapper_fifo.hh"

namespace gem5
{

struct rtlFIFOParams;
/**
 * A very simple memory object. Current implementation doesn't even cache
 * anything it just forwards requests and responses.
 * This memobj is fully blocking (not non-blocking). Only a single request can
 * be outstanding at a time.
 */
class rtlFIFO : public rtlObject
{
  private:

    /** The tick event used for scheduling CPU ticks. */
    //EventFunctionWrapper tickEvent;

    /**
     * Handle the request from the CPU side
     *
     * @param requesting packet
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PacketPtr pkt) override;

    /**
     * Handle the response from the memory side
     *
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
    bool handleResponseFIFO(PacketPtr pkt, bool sram);

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
    AddrRangeList getAddrRanges() const override ;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange() override;

    /// Instantiation of the CPU-side ports
    CPUSidePort cpuPort;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

    int bytesToRead;

    char *ptrRequest;


public:

    // wrapper pointer
    Wrapper_fifo *wr;

    // Constructor
    rtlFIFO(const rtlFIFOParams &params);

    // Destructor
    ~rtlFIFO();

    gem5::Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;

    // TLB Functions
    //bool isSquashed() const { return false; }
    //void startTranslate(Addr vaddr, ContextID contextId);
    void finishTranslation(WholeTranslationState *state) override;

    // To be called when starting the rtl Object
    void initRTLModel() override;
    // To be called when finishing the execution
    void endRTLModel() override;
    // To be called every tick()
    void tick() override;

};

} //End namespace gem5


#endif // __ACCELERATOR_VERILATOR_HH__
