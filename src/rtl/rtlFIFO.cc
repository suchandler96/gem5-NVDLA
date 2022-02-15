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

#include "rtl/rtlFIFO.hh"

#include "params/rtlFIFO.hh"

namespace gem5
{

rtlFIFO::rtlFIFO(const rtlFIFOParams &params) :
    rtlObject(params),
    cpuPort(params.name + ".cpu_side", this),
    memPort(params.name + ".mem_side", this)
{
    initRTLModel();
}


rtlFIFO::~rtlFIFO() {
    warn("calling destructor fifo");
    endRTLModel();
}

Port &
rtlFIFO::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem_side") {
        return memPort;
    } else if (if_name == "cpu_side") {
        return cpuPort;
    } else {
        panic_if(true, "Asking to rtlFIFO for a port different\
                        than cpu or mem");
        return ClockedObject::getPort(if_name, idx);
    }
    return cpuPort; // never reaching this point
}


bool
rtlFIFO::handleRequest(PacketPtr pkt)
{
    // Here we hace received the start rtlFIFO function
    // we check if there is an outstanding call
    if (blocked) {
        // There is currently an outstanding request. Stall.
        return false;
    }

    blocked = true;

    DPRINTF(rtlFIFO, "Got request for size: %d, addr: %#x\n",
                        pkt->getSize(),
                        pkt->req->getVaddr());

    // Just to test the fifo
    wr->reset();
    // schedule tick event
    schedule(tickEvent,nextCycle());

    // Do the translation with the TLB
    bytesToRead = pkt->getSize();

    ptrRequest = (char *) malloc(bytesToRead);

    startTranslate(pkt->req->getVaddr(), 0);

    return true;
}

void
rtlFIFO::initRTLModel() {
    // Init RTL Wrapper
    wr = new Wrapper_fifo(enableWaveform, "trace.vcd");
    // If we have a checkpoint this init can fail unless
    // we include the object in the checkpoint
    //tick();
    //schedule(tickEvent,nextCycle());
}

bool
rtlFIFO::handleResponse(PacketPtr pkt)
{
    if (pkt->hasData()){


    }
    else {
        // Strange situation, report!
        DPRINTF(rtlFIFO, "Got response for addr %#x no data\n",
                            pkt->getAddr());
    }


    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    blocked = false;

    return true;
}

void
rtlFIFO::handleFunctional(PacketPtr pkt)
{
    // Just pass this on to the memory side to handle for now.
    memPort.sendFunctional(pkt);
}

void
rtlFIFO::finishTranslation(WholeTranslationState *state) {

    DPRINTF(rtlFIFO, "Finishing translation\n");

    RequestPtr req = state->mainReq;

    if (req->hasPaddr()) {
        DPRINTF(rtlFIFO,
                "Finished translation step: Got request for addr %#x %#x\n",
        state->mainReq->getVaddr(),state->mainReq->getPaddr());

    } else {
        // we end up in a situation we shouldn't be
        DPRINTF(rtlFIFO,
                "Finished translation without physical addr\n");
    }

    PacketPtr new_pkt = new Packet(req, MemCmd::ReadReq, 64);
    if (memPort.isBlocked()) {
        DPRINTF(rtlFIFO, "Packet lost\n");
    }
    else {
        // send the request to memory
        new_pkt->allocate();
        memPort.sendPacket(new_pkt);
    }
}

AddrRangeList
rtlFIFO::getAddrRanges() const
{
    DPRINTF(rtlFIFO, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
rtlFIFO::sendRangeChange()
{
    cpuPort.sendRangeChange();
}


// To be called when finishing the execution
// destroy RTL Model
void
rtlFIFO::endRTLModel() {
    delete wr;
}

// To be called every tick
// advance RTL model simulation
void
rtlFIFO::tick() {
    wr->tick();
    cyclesStat++;
    stats.rtl_cycles++;
    schedule(tickEvent,nextCycle());
}

} // namespace gem5
