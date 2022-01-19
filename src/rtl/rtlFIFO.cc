/*
 * Copyright (c) 2017 Jason Lowe-Power
 * Copyright (c) 2019 Guillem Lopez Paradis
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
    ClockedObject(params),
    cpuPort(params.name + ".cpu_side", this),
    memPort(params.name + ".mem_side", this),
    traceEnable(params.enableWaveform),
    system(params.system),
    blocked(false)
{
    initRTLModel();
}


/*rtlFIFO::~rtlFIFO() {
    delete wr;
}*/

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
}

void
rtlFIFO::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

AddrRangeList
rtlFIFO::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
rtlFIFO::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(rtlFIFO, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

void
rtlFIFO::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleFunctional(pkt);
}

bool
rtlFIFO::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    // Just forward to the memobj.
    //if (Debug::ROI.status()) {

        if (pkt->req->hasPaddr()) {
            DPRINTF(rtlFIFO, "Got request for size: %d,  addr: %#x %#x\n",
                pkt->getSize(),
                pkt->req->getVaddr(),
                pkt->req->getPaddr());

        } else {
            owner->handleRequest(pkt);
        }
    //}
    if (!owner->handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
    return true;
}

void
rtlFIFO::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
rtlFIFO::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    DPRINTF(rtlFIFO, "Send Mem Req to L2 %#x %d \n",
            pkt->getAddr(), pkt->getSize());

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
rtlFIFO::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleResponse(pkt);
}

void
rtlFIFO::MemSidePort::recvReqRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);
}

void
rtlFIFO::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
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

    warn("inside handle Request");
    DPRINTF(rtlFIFO, "Got request for size: %d, addr: %#x\n",
                        pkt->getSize(),
                        pkt->req->getVaddr());

    wr->reset();
    wr->addInput(1,1,0);
    wr->tick();
    wr->addInput(1,2,0);
    wr->tick();
    wr->addInput(1,3,0);
    wr->tick();
    wr->addInput(1,4,0);
    wr->tick();
    wr->addInput(1,5,0);
    wr->tick();
    wr->addInput(0,5,0);
    for (int a=0;a<1000;a++) {
        wr->tick();
    }
    wr->disableTracing();

    bytesToRead = pkt->getSize();

    ptrRequest = (char *) malloc(bytesToRead);

    startTranslate(pkt->req->getVaddr(), 0);

    return true;
}

void
rtlFIFO::initRTLModel() {
    // Wrapper
    wr = new Wrapper_fifo(traceEnable, "trace.vcd");
}

/*void
rtlFIFO::tick() {
    DPRINTF(rtlFIFODebug, "Tick FIFO \n");
    // if we are still running trace
}*/

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
    //blocked = false;

    return true;
}

/*bool
rtlFIFO::handleResponseRTLModel(PacketPtr pkt, bool sram)
{
    if (pkt->hasData()){
        if (pkt->isRead()){
            // Get data from gem5 memory system
            // and set it to AXI
            DPRINTF(rtlFIFODebug,
                    "Handling response for data read Timing\n");
            // Get the data ptr and sent it
            const uint8_t* dataPtr = pkt->getConstPtr<uint8_t>();
           // uint32_t addr_nvdla = getAddrNVDLA(pkt->getAddr(),sram);
        } else {
            // this is somehow odd, report!
            DPRINTF(rtlFIFO, "Got response for addr %#x no read\n",
                    pkt->getAddr());
        }
    }
    else {
         DPRINTF(rtlFIFO, "Got response for addr %#x no data\n",
         pkt->getAddr());
    }

    return true;
}*/

void
rtlFIFO::handleFunctional(PacketPtr pkt)
{
    // Just pass this on to the memory side to handle for now.
    //memPort.sendFunctional(pkt);
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

/*rtlFIFO*
rtlFIFOParams::create()
{
    return new rtlFIFO(this);
}*/

void
rtlFIFO::startTranslate(Addr vaddr, ContextID contextId) {

    DPRINTF(rtlFIFO, "Started translation\n");

    BaseMMU * mmu = //(TheISA::TLB*)
        system->threads[contextId]->getMMUPtr();
    assert(mmu);

    Fault fault;
    BaseMMU::Mode mode = BaseMMU::Write;
    RequestPtr req = std::make_shared<Request>(
                        vaddr, 64, 0x40, 0, 0, contextId);
    //RequestPtr req = new Request(0, vaddr, 64, 0x40,
    //                                           0, 0, contextId);

    WholeTranslationState *state =
        new WholeTranslationState(req, new uint8_t[64], NULL, mode);
    DataTranslation<rtlFIFO *> *translation
        = new DataTranslation<rtlFIFO *>(this, state);

    mmu->translateTiming(req, system->threads[contextId],
                             translation, mode);

    //delete req;
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

    //PacketPtr new_pkt = new Packet(req, MemCmd::ReadReq, 64);

    /*if (memPort.blockedPacket != nullptr) {
        DPRINTF(rtlFIFO, "Packet lost\n");
    }
    else {
        new_pkt->allocate();
        memPort.sendPacket(new_pkt);
    }*/
}


} // namespace gem5
