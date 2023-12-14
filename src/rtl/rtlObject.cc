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

#include "rtl/rtlObject.hh"

#include "params/rtlObject.hh"

namespace gem5
{

rtlObject::rtlObject(const rtlObjectParams &params) :
    ClockedObject(params),
    blocked(false),
    system(params.system),
    enableObject(params.enableRTLObject),
    enableWaveform(params.enableWaveform),
    to_retry_vaddr(0),
    tickEvent([this]{ tick(); }, params.name + " tick"),
    retryTranslateEvent([this]{ retryTranslate(); }, params.name + " retryTranslate"),
    cyclesStat(0)
{

}


void
rtlObject::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.
    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingResp(pkt)) {
        blockedPacket = pkt;
    }
}

AddrRangeList
rtlObject::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
rtlObject::CPUSidePort::trySendRetry()
{
    if (needRetry && blockedPacket == nullptr) {
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(rtlObject, "Sending retry req for %d\n", id);
        sendRetryReq();
    }
}

void
rtlObject::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleFunctional(pkt);
}

bool
rtlObject::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    // Just forward to the memobj.
    if (pkt->req->hasPaddr()) {
        DPRINTF(rtlObject, "Got request for size: %d,  addr: %#x %#x\n",
            pkt->getSize(),
            pkt->req->getVaddr(),
            pkt->req->getPaddr());

    } else {
        owner->handleRequest(pkt);
    }
    // Try to handle the request by calling to
    // handleRequest() function to be implemented in
    // the rtlObject derived class
    if (!owner->handleRequest(pkt)) {
        needRetry = true;
        return false;
    } else {
        return true;
    }
    return true;
}

void
rtlObject::CPUSidePort::recvRespRetry()
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
rtlObject::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the memobj is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    DPRINTF(rtlObject, "Send Mem Req to L2 %#x %d \n",
            pkt->getAddr(), pkt->getSize());

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
rtlObject::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the memobj.
    return owner->handleResponse(pkt);
}

void
rtlObject::MemSidePort::recvReqRetry()
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
rtlObject::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

void
rtlObject::startTranslate(Addr vaddr, ContextID contextId) {

    DPRINTF(rtlObject, "Started translation\n");

    BaseMMU * mmu =
        system->threads[contextId]->getMMUPtr();
    assert(mmu);

    Fault fault;
    BaseMMU::Mode mode = BaseMMU::Write;
    RequestPtr req = std::make_shared<Request>(
                        vaddr, 64, 0x40, 0, 0, contextId);

    WholeTranslationState *state =
        new WholeTranslationState(req, new uint8_t[64], NULL, mode);
    DataTranslation<rtlObject *> *translation
        = new DataTranslation<rtlObject *>(this, state);

    mmu->translateTiming(req, system->threads[contextId],
                             translation, mode);

}

void
rtlObject::finishTranslation(WholeTranslationState *state) {

    DPRINTF(rtlObject, "Finishing translation\n");

    RequestPtr req = state->mainReq;

    if (req->hasPaddr()) {
        DPRINTF(rtlObject,
                "Finished translation step: Got request for addr %#x %#x\n",
        state->mainReq->getVaddr(),state->mainReq->getPaddr());

    } else {
        // we end up in a situation we shouldn't be
        DPRINTF(rtlObject,
                "Finished translation without physical addr\n");
    }

    warn("finishTranslation called from rtlObject");
    exit(-1);
}

void
rtlObject::retryTranslate() {
    printf("retryTranslate at tick = %lu\n", curTick());
    startTranslate(to_retry_vaddr, 0);
}

void
rtlObject::regStats()
{
    // If you don't do this you get errors about uninitialized stats.
    ClockedObject::regStats();

    using namespace statistics;

    stats.rtl_cycles
        .name(name() + ".rtl_cycles")
        .desc("Number of Cycles");

}

} // End gem5 namespace
