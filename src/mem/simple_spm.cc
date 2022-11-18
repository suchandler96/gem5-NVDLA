/*
 * Copyright (c) 2017 Jason Lowe-Power
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

#include "mem/simple_spm.hh"

#include "base/compiler.hh"
#include "base/random.hh"
#include "debug/SimpleSPM.hh"
#include "sim/system.hh"

namespace gem5
{

SimpleSPM::SimpleSPM(const SimpleSPMParams &params) :
    ClockedObject(params),
    latency(params.latency),
    blockSize(params.spm_line_size),
    systemCacheLineSize(params.system->cacheLineSize()),
    capacity(params.size / blockSize),
    readOnly(params.read_only),
    needWriteBack(params.need_write_back),
    memPort(params.name + ".mem_side", this),
    dmaPort(this, params.system)
    // blocked(false),
    // originalPacket(nullptr),
    // stats(this)
{
    if (readOnly) {
        // check legality
        assert(!needWriteBack);
    }
    // Since the CPU side ports are a vector of ports, create an instance of
    // the CPUSidePort for each connection. This member of params is
    // automatically created depending on the name of the vector port and
    // holds the number of connections to this port name
    for (int i = 0; i < params.port_cpu_side_connection_count; ++i) {
        cpuPorts.emplace_back(name() + csprintf(".cpu_side[%d]", i), i, this);
    }



    dma_rd_engine = new Dma4SPM(this,                      // owner
                                 false,                     // proactive_get
                                 dmaPort,
                                 false,                     // is_write
                                 capacity * blockSize,      // total size
                                 blockSize,                 // DMA granularity
                                 capacity,                  // max concurrent req
                                 Request::UNCACHEABLE
                                 );
    dma_wr_engine = new Dma4SPM(this,
                                 false,
                                 dmaPort,
                                 true,
                                 capacity * blockSize,
                                 blockSize,
                                 capacity,
                                 Request::UNCACHEABLE);
}

Port &
SimpleSPM::getPort(const std::string &if_name, PortID idx)
{
    // This is the name from the Python SimObject declaration in SimpleSPM.py
    if (if_name == "mem_side") {
        panic_if(idx != InvalidPortID,
                 "Mem side of simple SPM not a vector port");
        return memPort;
    } else if (if_name == "cpu_side" && idx < cpuPorts.size()) {
        // We should have already created all the ports in the constructor
        return cpuPorts[idx];
    } else if (if_name == "dma_port") {
        return dmaPort;
    } else {
        // pass it along to our super class
        return ClockedObject::getPort(if_name, idx);
    }
}

void
SimpleSPM::CPUSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the SPM is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    DPRINTF(SimpleSPM, "Sending %s to CPU\n", pkt->print());
    if (!sendTimingResp(pkt)) {
        DPRINTF(SimpleSPM, "failed!\n");
        blockedPacket = pkt;
    }
}

AddrRangeList
SimpleSPM::CPUSidePort::getAddrRanges() const
{
    return owner->getAddrRanges();
}

void
SimpleSPM::CPUSidePort::trySendRetry()
{
    if (needRetry && owner->dma_rd_engine->atEndOfBlock()) {
        //! currently only dma_rd_engine is causing a block.
        // Only send a retry if the port is now completely free
        needRetry = false;
        DPRINTF(SimpleSPM, "Sending retry req.\n");
        sendRetryReq();
    }
}

void
SimpleSPM::CPUSidePort::recvFunctional(PacketPtr pkt)
{
    // Just forward to the SPM.
    return owner->handleFunctional(pkt);
}

bool
SimpleSPM::CPUSidePort::recvTimingReq(PacketPtr pkt)
{
    if (pkt->isCleanEviction()) {
        owner->allFlush();
        delete pkt;
        return true;
    } else {
        DPRINTF(SimpleSPM, "Got request %s\n", pkt->print());

        if (blockedPacket || needRetry) {
            // The SPM may not be able to send a reply if this is blocked
            DPRINTF(SimpleSPM, "Request blocked\n");
            needRetry = true;
            return false;
        }
        // Just forward to the SPM.
        if (!owner->handleRequest(pkt, id)) {
            DPRINTF(SimpleSPM, "Request failed\n");
            // stalling
            needRetry = true;
            return false;
        } else {
            DPRINTF(SimpleSPM, "Request succeeded\n");
            return true;
        }
    }
}

void
SimpleSPM::CPUSidePort::recvRespRetry()
{
    // We should have a blocked packet if this function is called.
    assert(blockedPacket != nullptr);

    // Grab the blocked packet.
    PacketPtr pkt = blockedPacket;
    blockedPacket = nullptr;

    DPRINTF(SimpleSPM, "Retrying response pkt %s\n", pkt->print());
    // Try to resend it. It's possible that it fails again.
    sendPacket(pkt);

    // We may now be able to accept new packets
    trySendRetry();
}

void
SimpleSPM::MemSidePort::sendPacket(PacketPtr pkt)
{
    // Note: This flow control is very simple since the SPM is blocking.

    panic_if(blockedPacket != nullptr, "Should never try to send if blocked!");

    // If we can't send the packet across the port, store it for later.
    if (!sendTimingReq(pkt)) {
        blockedPacket = pkt;
    }
}

bool
SimpleSPM::MemSidePort::recvTimingResp(PacketPtr pkt)
{
    // Just forward to the cache.
    return owner->handleResponse(pkt);
}

void
SimpleSPM::MemSidePort::recvReqRetry()
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
SimpleSPM::MemSidePort::recvRangeChange()
{
    owner->sendRangeChange();
}

SimpleSPM::Dma4SPM::Dma4SPM(SimpleSPM* _owner,
                   bool proactive_get,
                   DmaPort &_port, bool _is_write, size_t size,
                   unsigned max_req_size,
                   unsigned max_pending,
                   Request::Flags flags
                   )
    : maxReqSize(max_req_size), fifoSize(size),
      reqFlags(flags), port(_port), cacheLineSize(port.sys->cacheLineSize()),
      buffer(size), is_write(_is_write),
      proactive_get(proactive_get),
      owner(_owner)
{
    freeRequests.resize(max_pending);
    for (auto &e : freeRequests)
        e.reset(new DmaDoneEvent(this, max_req_size));
}

SimpleSPM::Dma4SPM::~Dma4SPM()
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
SimpleSPM::Dma4SPM::serialize(CheckpointOut &cp) const
{
    assert(pendingRequests.empty());

    SERIALIZE_CONTAINER(buffer);
    SERIALIZE_SCALAR(endAddr);
    SERIALIZE_SCALAR(nextAddr);
}

void
SimpleSPM::Dma4SPM::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_CONTAINER(buffer);
    UNSERIALIZE_SCALAR(endAddr);
    UNSERIALIZE_SCALAR(nextAddr);
}

bool
SimpleSPM::Dma4SPM::tryGet(uint8_t *dst, size_t len)
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
SimpleSPM::Dma4SPM::get(uint8_t *dst, size_t len)
{
    panic_if(!tryGet(dst, len), "Buffer underrun in DmaNvdla::get()");
}

void
SimpleSPM::Dma4SPM::startFill(Addr start, size_t size, uint8_t* d)
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
SimpleSPM::Dma4SPM::stopFill()
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
SimpleSPM::Dma4SPM::resumeFill()
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
SimpleSPM::Dma4SPM::resumeFillBypass()
{
    const size_t fifo_space = buffer.capacity() - buffer.size();
    if (fifo_space >= cacheLineSize || buffer.capacity() < cacheLineSize) {
        const size_t block_remaining = endAddr - nextAddr;
        const size_t xfer_size = std::min(fifo_space, block_remaining);
        std::vector<uint8_t> tmp_buffer(xfer_size);

        assert(pendingRequests.empty());

        port.dmaAction(MemCmd::ReadReq, nextAddr, xfer_size, nullptr,
                tmp_buffer.data(), 0, reqFlags);

        buffer.write(tmp_buffer.begin(), xfer_size);
        nextAddr += xfer_size;
    }
}

void
SimpleSPM::Dma4SPM::resumeFillTiming()
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

        event->reset(req_size, nextAddr);

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
SimpleSPM::Dma4SPM::dmaDone()
{
    const bool old_active(isActive());

    handlePending();
    resumeFill();

    if (old_active && !isActive())
        onIdle();
}

void
SimpleSPM::Dma4SPM::handlePending()
{
    while (!pendingRequests.empty() && pendingRequests.front()->done()) {
        // Get the first finished pending request
        DmaDoneEventUPtr event(std::move(pendingRequests.front()));
        pendingRequests.pop_front();

        if (!event->canceled() && !is_write) {
            if(proactive_get) {
                buffer.write(event->data(), event->requestSize());
            } else {
                // so currently allow DMA read data to be acquired only upon finish
                // todo: try to use std::move to improve performance
                size_t dma_size = event->requestSize();
                std::vector<uint8_t> temp(dma_size);
                for (int i = 0; i < dma_size; i++) temp[i] = event->_data[i];
                owner_fetch_buffer.emplace_back(event->_addr, std::move(temp));
                owner->accessDMAData();
            }
        }

        // Move the event to the list of free requests
        freeRequests.emplace_back(std::move(event));
    }

    if (pendingRequests.empty())
        signalDrainDone();
}

DrainState
SimpleSPM::Dma4SPM::drain()
{
    return pendingRequests.empty() ?
        DrainState::Drained : DrainState::Draining;
}


SimpleSPM::Dma4SPM::DmaDoneEvent::DmaDoneEvent
(Dma4SPM *_parent, size_t max_size)
    : parent(_parent), _data(max_size, 0), _addr(0)
{
}

void
SimpleSPM::Dma4SPM::DmaDoneEvent::kill()
{
    parent = nullptr;
    setFlags(AutoDelete);
}

void
SimpleSPM::Dma4SPM::DmaDoneEvent::cancel()
{
    _canceled = true;
}

void
SimpleSPM::Dma4SPM::DmaDoneEvent::reset(size_t size, Addr addr)
{
    assert(size <= _data.size());
    _done = false;
    _canceled = false;
    _requestSize = size;
    _addr = addr;
}

void
SimpleSPM::Dma4SPM::DmaDoneEvent::process()
{
    if (!parent)
        return;

    assert(!_done);
    _done = true;
    parent->dmaDone();
}

bool
SimpleSPM::handleRequest(PacketPtr pkt, int port_id)
{
    DPRINTF(SimpleSPM, "Got request for addr %#x\n", pkt->getAddr());

    // need to look for this piece of data at once.
    // If missed, need to immediately send a request to downstream
    // memory to imitate software scheduling.
    // Else, schedule an event after cache access latency to actually access
    // All writes will be write-back, and will be subject to the access latency
    bool hit = checkHit(pkt);
    if ((!hit) && pkt->isRead()) {
        Addr block_addr = pkt->getBlockAddr(blockSize);

        bool access_suc = ((inflight_dma_reads.find(block_addr)
                            != inflight_dma_reads.end())
                            || dma_rd_engine->atEndOfBlock());
        if (!access_suc) {
            // filter out accesses that are going to fail due to busy DMA engine
            // so that they won't be counted into stats
            //! another read miss, and DMA engine blocked.
            //! So SPM is also going to stall
            return false;
        }

        accessTiming(pkt, port_id);
    } else {
        schedule(new EventFunctionWrapper([this, pkt, port_id]
                                          {accessTiming(pkt, port_id);},
                                          name() + ".accessEvent", true),
                 clockEdge(latency));
    }
    return true;
}

bool
SimpleSPM::handleResponse(PacketPtr pkt)
{
    DPRINTF(SimpleSPM, "Got response for addr %#x\n", pkt->getAddr());
    insert(pkt);
    delete pkt;

    checkMissQueueOnResponse();
    return true;
}

void
SimpleSPM::sendResponse(PacketPtr pkt, int port_id) {
    // assert(blocked);
    DPRINTF(SimpleSPM, "Sending resp for addr %#x\n", pkt->getAddr());

    // The packet is now done. We're about to put it in the port, no need for
    // this object to continue to stall.
    // We need to free the resource before sending the packet in case the CPU
    // tries to send another request immediately (e.g., in the same callchain).
    // blocked = false;

    // Simply forward to the memory port
    cpuPorts[port_id].sendPacket(pkt);

    // For each of the cpu ports, if it needs to send a retry, it should do it
    // now since this memory object may be unblocked now.
    for (auto& port : cpuPorts) {
        port.trySendRetry();
    }
}

void
SimpleSPM::handleFunctional(PacketPtr pkt)
{
    // this should not be called, however
    assert(false);
    if (accessFunctional(pkt)) {
        pkt->makeResponse();
    } else {
        memPort.sendFunctional(pkt);
    }
}

void
SimpleSPM::accessTiming(PacketPtr pkt, int port_id)
{
    assert(pkt->isWrite() || pkt->isRead());

    bool hit = accessFunctional(pkt);

    DPRINTF(SimpleSPM, "%s for packet: %s\n", hit ? "Hit" : "Miss",
            pkt->print());

    if (hit) {
        if (pkt->isRead()) {
            stats.read_hits++;  // update stats
        } else if(pkt->isWrite()) {
            stats.write_hits++; // update stats
        }
        // DDUMP(SimpleSPM, pkt->getConstPtr<uint8_t>(), pkt->getSize());
        // Respond to the CPU side
        pkt->makeResponse();
        sendResponse(pkt, port_id);
    } else {
        if (pkt->isWrite()) {
            stats.write_misses++; // update stats
            insert(pkt);
        } else {
            //! distinguish write miss & read miss
            //! this is different from SimpleCache,
            //! where on an unaligned miss, a read cmd is issued to lower memory.
            //! Here we assume the software has already been designed to avoid
            //! this kind of conflict
            miss_pkt_with_time_port_list.emplace_back(pkt, curTick(), port_id);

            // Forward to the memory side.
            // Data transfer to SPM is by default handled by DMA engine
            // And we need to check whether this has been covered by
            // a previous DMA read
            Addr block_addr = pkt->getBlockAddr(blockSize);

            if (inflight_dma_reads.find(block_addr) == inflight_dma_reads.end()) {
                // issue a new DMA request
                if (dma_rd_engine->atEndOfBlock()) {
                    stats.read_new_misses++;
                    // DMA engine is available
                    DPRINTF(SimpleSPM, "simple spm issue dma rd 0x%08x\n",
                    block_addr);
                    dma_rd_engine->startFill(block_addr, blockSize);

                    inflight_dma_reads[block_addr] = 1;
                    // '1' currently has no meaning
                } else {
                    // else DMA engine is busy,
                    // but it should have been filtered out in handleRequest()
                    assert(false);
                }
            } else {    // else just wait for the issued DMA to return
                stats.read_inflight_misses++;   // update stats
            }
        }
    }
}

bool
SimpleSPM::accessFunctional(PacketPtr pkt)
{
    Addr block_addr = pkt->getBlockAddr(blockSize);
    auto it = SPMStore.find(block_addr);
    if (it != SPMStore.end()) {
        //! first interact with the pkt
        if (pkt->isWrite()) {
            // Write the data into the block in the SPM
            pkt->writeDataToBlock(&it->second.first[0], blockSize);
        } else if (pkt->isRead()) {
            // Read the data out of the SPM line into the packet
            pkt->setDataFromBlock(&it->second.first[0], blockSize);
        } else {
            panic("Unknown packet type!");
        }

        //! then maintain lru_order
        auto& lru_order_it = it->second.second;
        lru_order.splice(lru_order.end(), lru_order, lru_order_it);
        // see https://cplusplus.com/reference/list/list/splice/ version 2.
        // the iterator should keep valid

        return true;
    }
    return false;
}

void
SimpleSPM::insert(PacketPtr pkt)
{
    // The packet could be unaligned.
    assert(!readOnly);

    Addr pkt_addr = pkt->getAddr();
    Addr pkt_aligned_addr = pkt->getBlockAddr(blockSize);

    // The address should not be in the cache
    assert(SPMStore.find(pkt_aligned_addr) == SPMStore.end());
    //  should be a response
    //! The pkt is not necessarily a response in SPM.
    //! If we always read the whole spm_line from main memory
    //! before writing this pkt to SPM (which happens in
    //! unaligned access in a cache), then it should be a response.
    //! But if we are not reading main memory before writing
    //! pkt into SPM, insert() will also be called in accessTiming,
    //! where the pkt could be a write request.
    // assert(pkt->isResponse());

    if (SPMStore.size() >= capacity) {
        if (needWriteBack) {
            // because not read-only, assume write-back
            //! Select a line in SPM to write back and
            //! remove this dict item from SPM
            writeBackLine();
        } else {
            Addr to_del_addr = lru_order.front();
            lru_order.pop_front();
            auto to_del_it = SPMStore.find(to_del_addr);
            //! Delete this entry
            DPRINTF(SimpleSPM, "SPM full. Erase SPM line of addr %#x\n",
                    to_del_addr);
            SPMStore.erase(to_del_it);
        }
    }

    //! insert and assign space for entire spm line
    // since insert() is only called when a MISS happens
    // (either write or, possibly, read, in the future),
    // there is certainly no record of this spm line in lru_order
    lru_order.push_back(pkt_aligned_addr);

    DPRINTF(SimpleSPM, "Inserting pkt addr %#x, SPM line addr%#x\n",
            pkt_addr, pkt_aligned_addr);
    // DDUMP(SimpleSPM, pkt->getConstPtr<uint8_t>(), blockSize);

    // Allocate space for the SPM line data
    auto& entry = SPMStore[pkt_aligned_addr];
    auto& line_data = entry.first;
    line_data.resize(blockSize, 0);
    pkt->writeDataToBlock(&line_data[0], blockSize);

    // and also log the iterator of this spm_line in lru_order
    entry.second = std::prev(lru_order.end());
}

void
SimpleSPM::insert(Addr addr, std::vector<uint8_t>& to_insert_line) {
    // The address should not be in the SPM
    printf("SPM trying to insert addr 0x%08x\n", addr);
    assert(SPMStore.find(addr) == SPMStore.end());

    DPRINTF(SimpleSPM, "Inserting line addr %#x\n", addr);
    if (SPMStore.size() >= capacity) {
        if (readOnly || !needWriteBack) {
            Addr to_del_addr = lru_order.front();
            lru_order.pop_front();
            auto to_del_it = SPMStore.find(to_del_addr);
            //! Delete this entry
            DPRINTF(SimpleSPM, "SPM full. Erase SPM line of addr %#x\n",
                    to_del_addr);
            SPMStore.erase(to_del_it);
        } else {
            //! Select a line in SPM to write back and
            //! remove this dict item from SPM
            writeBackLine();
        }
    }

    //! insert and assign space for entire spm line
    // since insert() is only called when a MISS happens
    // (either write or, possibly, read, in the future),
    // there is certainly no record of this spm line in lru_order
    lru_order.push_back(addr);
    // DDUMP(SimpleSPM, pkt->getConstPtr<uint8_t>(), blockSize);

    // Allocate space for the SPM line data
    auto& entry = SPMStore[addr];
    entry.first = std::move(to_insert_line);

    // and also log the iterator of this spm_line in lru_order
    entry.second = std::prev(lru_order.end());
}

void
SimpleSPM::writeBackLine() {
    //! Evict a SPM line
    // Select random thing to evict.
    // int id = random_mt.random(0, (int)SPMStore.size() - 1);
    // auto to_del_it = std::next(SPMStore.begin(), id);

    // or one can use a LRU logic to select
    Addr to_del_addr = lru_order.front();
    lru_order.pop_front();
    auto to_del_it = SPMStore.find(to_del_addr);
    assert(to_del_it != SPMStore.end());

    //! Write back the data with DMA write engine (dma_nvdla)
    printf("SPM full. Try to write back SPM line of addr %#x "
                       "via DMA\n", to_del_addr);
    DPRINTF(SimpleSPM, "SPM full. Try to write back SPM line of addr %#x "
                       "via DMA\n", to_del_addr);
    if (dma_wr_engine->atEndOfBlock()) {
        dma_wr_engine->startFill(to_del_addr, blockSize,
                                 &to_del_it->second.first[0]);
        // startFill() will copy data to its internal buffer

        DPRINTF(SimpleSPM, "DMA write for addr %#x is issued", to_del_addr);
    } else {
        DPRINTF(SimpleSPM, "DMA write addr %#x attempt fail, scheduled later",
                to_del_addr);
        to_flush_list.emplace_back(std::move(*to_del_it));

        schedule(new EventFunctionWrapper([this] { tryFlush(); }, name() +
                                          ".tryFlushEvent", true), nextCycle());
    }

    //! Delete this entry
    SPMStore.erase(to_del_it);
}

bool
SimpleSPM::checkHit(PacketPtr pkt) const
{
    Addr block_addr = pkt->getBlockAddr(blockSize);
    auto it = SPMStore.find(block_addr);
    if (it == SPMStore.end()) {
        return false;
    }
    return true;
}

void
SimpleSPM::tryFlush() {
    while (!to_flush_list.empty()) {

        if (dma_wr_engine->atEndOfBlock()) {
            auto &to_flush_item = to_flush_list.front();
            Addr to_flush_addr = to_flush_item.first;
            std::vector <uint8_t> &to_flush_data = to_flush_item.second.first;

            dma_wr_engine->startFill(to_flush_addr, blockSize, &to_flush_data[0]);
            // startFill() will copy data into DMA engine's internal buffer

            DPRINTF(SimpleSPM, "DMA write addr %#x is issued",
                    to_flush_item.first);

            to_flush_list.pop_front();
        } else {
            break;
        }
    }

    if (!to_flush_list.empty()) {
        schedule(new EventFunctionWrapper([this] { tryFlush(); },
                                          name() + ".tryFlushEvent", true),
                 nextCycle());
    }
}

void
SimpleSPM::accessDMAData() {
    std::list<std::pair<Addr, std::vector<uint8_t>>>& src_buffer =
            dma_rd_engine->owner_fetch_buffer;
    for (auto it = src_buffer.begin(); it != src_buffer.end(); it++) {
        insert(it->first, it->second);  // insert addr, std::vector<uint8_t>
        inflight_dma_reads.erase(it->first);    // remove the inflight record
    }
    src_buffer.clear();

    checkMissQueueOnResponse();
}

void
SimpleSPM::checkMissQueueOnResponse() {
    // check the missed request packets from upstream
    bool resent_back = false;
    auto it = miss_pkt_with_time_port_list.begin();
    while (it != miss_pkt_with_time_port_list.end()) {
        PacketPtr to_return_pkt = std::get<0>(*it);
        bool hit_now = accessFunctional(to_return_pkt);
        if (hit_now) {
            stats.miss_latency.sample(curTick() - std::get<1>(*it));
            // DDUMP(SimpleSPM, to_return_pkt->getConstPtr<uint8_t>(),
            //       to_return_pkt->getSize());
            to_return_pkt->makeResponse();

            sendResponse(to_return_pkt, std::get<2>(*it));
            miss_pkt_with_time_port_list.erase(it++);
            resent_back = true;
            continue;
            // this should work because rtlNVDLA::handleResponseNVDLA()
            // always returns true, so it just won't get blocked
        }

        it++;
    }

    for (auto& port : cpuPorts) {
        port.trySendRetry();
    }
}

void
SimpleSPM::allFlush() {
    if (!SPMStore.empty()) {
        for (auto it = SPMStore.begin(); it != SPMStore.end(); it++) {
            to_flush_list.emplace_back(std::move(*it));
        }
        SPMStore.clear();

        tryFlush();
    }
}

AddrRangeList
SimpleSPM::getAddrRanges() const
{
    DPRINTF(SimpleSPM, "Sending new ranges\n");
    // Just use the same ranges as whatever is on the memory side.
    return memPort.getAddrRanges();
}

void
SimpleSPM::sendRangeChange() const
{
    for (auto& port : cpuPorts) {
        port.sendRangeChange();
    }
}

void
SimpleSPM::regStats() {
    ClockedObject::regStats();

    using namespace statistics;

    stats.read_hits
        .name(name() + ".read_hits")
        .desc("Number of read hits");

    stats.read_new_misses
        .name(name() + ".read_new_misses")
        .desc("Number of read misses that need to issue new DMA requests");

    stats.read_inflight_misses
        .name(name() + ".read_inflight_misses")
        .desc("Number of read misses that are covered by an inflight DMA request");

    stats.write_hits
        .name(name() + ".write_hits")
        .desc("Number of write hits");

    stats.write_misses
        .name(name() + ".write_misses")
        .desc("Number of write_misses");

    stats.miss_latency
        .name(name() + ".miss_latency")
        .desc("Ticks for misses to the cache")
        .init(512);

    stats.read_hit_ratio
        .name(name() + ".read_hit_ratio")
        .desc("The ratio of hits to the total accesses to the SPM");
    stats.read_hit_ratio = stats.read_hits / (stats.read_hits +
            stats.read_new_misses + stats.read_inflight_misses);

    stats.write_hit_ratio
        .name(name() + ".write_hit_ratio")
        .desc("The ratio of hits to the total writes to the SPM");
    stats.write_hit_ratio = stats.write_hits /
            (stats.write_hits + stats.write_misses);
}
/*
SimpleSPM::SimpleSPMStats::SimpleSPMStats(statistics::Group *parent)
    : statistics::Group(parent),
    ADD_STAT(read_hits, statistics::units::Count::get(),
             "Number of read hits"),
    ADD_STAT(read_new_misses, statistics::units::Count::get(),
             "Number of read misses that need to issue new DMA requests"),
    ADD_STAT(read_inflight_misses, statistics::units::Count::get(),
             "Number of read misses that are covered by an inflight DMA request"),
    ADD_STAT(write_hits, statistics::units::Count::get(),
             "Number of write hits"),
    ADD_STAT(write_misses, statistics::units::Count::get(),
             "Number of write misses"),
    ADD_STAT(miss_latency, statistics::units::Tick::get(),
             "Ticks for misses to the cache"),
    ADD_STAT(read_hit_ratio, statistics::units::Ratio::get(),
             "The ratio of hits to the total accesses to the SPM",
             read_hits / (read_hits + read_new_misses + read_inflight_misses)),
    ADD_STAT(write_hit_ratio, statistics::units::Ratio::get(),
             "The ratio of hits to the total writes to the SPM",
             write_hits / (write_hits + write_misses))
{
    miss_latency.init(12); // number of buckets
}
*/
} // namespace gem5
