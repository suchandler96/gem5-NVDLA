/*
 * Copyright (c) 2012, 2015, 2017, 2019 ARM Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2006 The Regents of The University of Michigan
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
 */

#include "dev/dma_nvdla.hh"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/DMA.hh"
#include "debug/Drain.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

namespace gem5
{

DmaNvdla::DmaNvdla(DmaPort &_port, bool _is_write, size_t size,
                         unsigned max_req_size,
                         unsigned max_pending,
                         Request::Flags flags)
    : maxReqSize(max_req_size), fifoSize(size),
      reqFlags(flags), port(_port), cacheLineSize(port.sys->cacheLineSize()),
      buffer(size), is_write(_is_write)
{
    freeRequests.resize(max_pending);
    for (auto &e : freeRequests)
        e.reset(new DmaDoneEvent(this, max_req_size));

}

DmaNvdla::~DmaNvdla()
{
    for (auto &p : pendingRequests) {
        DmaDoneEvent *e(p.release());

        if (e->done()) {
            delete e;
        } else {
            // We can't kill in-flight DMAs, so we'll just transfer
            // ownership to the event queue so that they get freed
            // when they are done.
            e->kill();
        }
    }
}

void
DmaNvdla::serialize(CheckpointOut &cp) const
{
    assert(pendingRequests.empty());

    SERIALIZE_CONTAINER(buffer);
    SERIALIZE_SCALAR(endAddr);
    SERIALIZE_SCALAR(nextAddr);
}

void
DmaNvdla::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_CONTAINER(buffer);
    UNSERIALIZE_SCALAR(endAddr);
    UNSERIALIZE_SCALAR(nextAddr);
}

bool
DmaNvdla::tryGet(uint8_t *dst, size_t len)
{
    if (!is_write && buffer.size() >= len) {
        buffer.read(dst, len);
        resumeFill();
        return true;
    } else {
        return false;
    }
}

void
DmaNvdla::get(uint8_t *dst, size_t len)
{
    panic_if(!tryGet(dst, len), "Buffer underrun in DmaNvdla::get()");
}

void
DmaNvdla::startFill(Addr start, size_t size, uint8_t* d)
{
    assert(atEndOfBlock());

    nextAddr = start;
    endAddr = start + size;
    if (is_write) {
        buffer.write(d, size);
    }
    resumeFill();
}

void
DmaNvdla::stopFill()
{
    // Prevent new DMA requests by setting the next address to the end
    // address. Pending requests will still complete.
    nextAddr = endAddr;

    // Flag in-flight accesses as canceled. This prevents their data
    // from being written to the FIFO.
    for (auto &p : pendingRequests)
        p->cancel();
}

void
DmaNvdla::resumeFill()
{
    // Don't try to fetch more data if we are draining. This ensures
    // that the DMA engine settles down before we checkpoint it.
    if (drainState() == DrainState::Draining)
        return;

    const bool old_eob(atEndOfBlock());

    if (port.sys->bypassCaches())
        resumeFillBypass();
    else
        resumeFillTiming();

    if (!old_eob && atEndOfBlock())
        onEndOfBlock();
}

void
DmaNvdla::resumeFillBypass()
{
    const size_t fifo_space = buffer.capacity() - buffer.size();
    if (fifo_space >= cacheLineSize || buffer.capacity() < cacheLineSize) {
        const size_t block_remaining = endAddr - nextAddr;
        const size_t xfer_size = std::min(fifo_space, block_remaining);
        std::vector<uint8_t> tmp_buffer(xfer_size);

        assert(pendingRequests.empty());
        DPRINTF(DMA, "Direct bypass startAddr=%#x xfer_size=%#x " \
                "fifo_space=%#x block_remaining=%#x\n",
                nextAddr, xfer_size, fifo_space, block_remaining);

        port.dmaAction(MemCmd::ReadReq, nextAddr, xfer_size, nullptr,
                tmp_buffer.data(), 0, reqFlags);

        buffer.write(tmp_buffer.begin(), xfer_size);
        nextAddr += xfer_size;
    }
}

void
DmaNvdla::resumeFillTiming()
{
    size_t size_pending(0);
    for (auto &e : pendingRequests)
        size_pending += e->requestSize();

    while (!freeRequests.empty() && !atEndOfBlock()) {
        const size_t req_size(std::min(maxReqSize, endAddr - nextAddr));
        if (buffer.size() + size_pending + req_size > fifoSize)
            break;

        DmaDoneEventUPtr event(std::move(freeRequests.front()));
        freeRequests.pop_front();
        assert(event);

        event->reset(req_size);

        if (is_write)
            buffer.read(event->data(), req_size);

        port.dmaAction(is_write ? MemCmd::WriteReq : MemCmd::ReadReq, nextAddr, req_size, event.get(),
                       event->data(), 0, reqFlags);
        nextAddr += req_size;
        size_pending += req_size;

        pendingRequests.emplace_back(std::move(event));
    }
}

void
DmaNvdla::dmaDone()
{
    const bool old_active(isActive());

    handlePending();
    resumeFill();

    if (old_active && !isActive())
        onIdle();
}

void
DmaNvdla::handlePending()
{
    while (!pendingRequests.empty() && pendingRequests.front()->done()) {
        // Get the first finished pending request
        DmaDoneEventUPtr event(std::move(pendingRequests.front()));
        pendingRequests.pop_front();

        if (!event->canceled() && !is_write)
            buffer.write(event->data(), event->requestSize());

        // Move the event to the list of free requests
        freeRequests.emplace_back(std::move(event));
    }

    if (pendingRequests.empty())
        signalDrainDone();
}

DrainState
DmaNvdla::drain()
{
    return pendingRequests.empty() ?
        DrainState::Drained : DrainState::Draining;
}


DmaNvdla::DmaDoneEvent::DmaDoneEvent(DmaNvdla *_parent, size_t max_size)
    : parent(_parent), _data(max_size, 0)
{
}

void
DmaNvdla::DmaDoneEvent::kill()
{
    parent = nullptr;
    setFlags(AutoDelete);
}

void
DmaNvdla::DmaDoneEvent::cancel()
{
    _canceled = true;
}

void
DmaNvdla::DmaDoneEvent::reset(size_t size)
{
    assert(size <= _data.size());
    _done = false;
    _canceled = false;
    _requestSize = size;
}

void
DmaNvdla::DmaDoneEvent::process()
{
    if (!parent)
        return;

    assert(!_done);
    _done = true;
    parent->dmaDone();
}

} // namespace gem5
