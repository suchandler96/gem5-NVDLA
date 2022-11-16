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
    dmaPort(this, params.system),
    // blocked(false),
    // originalPacket(nullptr),
    stats(this)
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



    dma_rd_engine = new DmaNvdla(this,                      // owner
                                 false,                     // proactive_get
                                 dmaPort,
                                 false,                     // is_write
                                 capacity * blockSize,      // total size
                                 blockSize,                 // DMA granularity
                                 capacity,                  // max concurrent req
                                 Request::UNCACHEABLE
                                 EventFunctionWrapper(
                                 [this]{ accessDMAData(); },
                                 params.name + ".accessDMAData"));
    dma_wr_engine = new DmaNvdla(this,
                                 false,
                                 dmaPort,
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
    if (needRetry && dma_rd_engine->atEndOfBlock()) {
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
        allFlush();
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
                            == inflight_dma_reads.end())
                            && !dma_rd_engine->atEndofBlock());
        if (!access_suc) {
            // filter out accesses that are going to fail due to busy DMA engine
            // so that they won't be counted into stats
            //! another read miss, and DMA engine blocked.
            //! So SPM is also going to stall
            return false;
        }

        accessTiming(pkt, port_id);
    } else {
        schedule(new EventFunctionWrapper([this, pkt]
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
// todo: need a flush function
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
        return true;
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
            stats.read_misses++; // update stats
            miss_pkt_with_time_port_list.emplace_back(pkt, curTick(), port_id);

            // Forward to the memory side.
            // Data transfer to SPM is by default handled by DMA engine
            // And we need to check whether this has been covered by
            // a previous DMA read
            Addr addr = pkt->getAddr();
            Addr block_addr = pkt->getBlockAddr(blockSize);
            unsigned size = pkt->getSize();

            if (inflight_dma_reads.find(block_addr) == inflight_dma_reads.end()) {
                // issue a new DMA request
                if (dma_rd_engine->atEndofBlock()) {
                    // DMA engine is available
                    dma_rd_engine->startFill(block_addr, blockSize);

                    inflight_dma_reads[Addr] = 1; // '1' currently has no meaning
                } else {
                    // else DMA engine is busy,
                    // but it should have been filtered out in handleRequest()
                    assert(false);
                }
            }   // else just wait for the issued DMA to return
            // todo: stats for miss but already inflight
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
            pkt->writeDataToBlock(&it->second->first[0], blockSize);
        } else if (pkt->isRead()) {
            // Read the data out of the SPM line into the packet
            pkt->setDataFromBlock(&it->second->first[0], blockSize);
        } else {
            panic("Unknown packet type!");
        }

        //! then maintain lru_list
        auto& lru_list_it = it->second.second;
        lru_list.splice(lru_list.end(), lru_list, lru_list_it);
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

    // and also log the iterator of this spm_line in lru_list
    entry.second = std::prev(lru_order.end());
}

void
SimpleSPM::insert(Addr addr, std::vector<uint8_t>& to_insert_line) {
    // The address should not be in the cache
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

    // and also log the iterator of this spm_line in lru_list
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
    DPRINTF(SimpleSPM, "SPM full. Try to write back SPM line of addr %#x "
                       "via DMA\n", to_del_addr);
    if (dma_wr_engine->atEndOfBlock()) {
        dma_wr_engine->startFill(to_del_addr, blockSize,
                                 &to_del_it->second->first[0]);
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
                    to_flush_item->first);

            to_flush_list.pop();
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
    std::vector<std::pair<Addr, std::vector<uint8_t>>>& src_buffer =
            dma_rd_engine->owner_fetch_buffer;
    for (auto it = src_buffer.begin(); it != src_buffer.end(); it++) {
        insert(it->first, it->second);  // insert addr, std::vector<uint8_t>
        inflight_dma_reads.erase(it->first);    // remove the inflight record
    }

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
    for (auto it = SPMStore.begin(); it != SPMStore.end(); it++) {
        to_flush_list.emplace_back(std::move(*it));
    }
    SPMStore.clear();

    tryFlush();
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

SimpleSPM::SimpleSPMStats::SimpleSPMStats(statistics::Group *parent)
    : statistics::Group(parent),
    ADD_STAT(read_hits, statistics::units::Count::get(),
             "Number of read hits"),
    ADD_STAT(read_misses, statistics::units::Count::get(),
             "Number of read misses"),
    ADD_STAT(write_hits, statistics::units::Count::get(),
             "Number of write hits"),
    ADD_STAT(write_misses, statistics::units::Count::get(),
             "Number of write misses"),
    ADD_STAT(miss_latency, statistics::units::Tick::get(),
             "Ticks for misses to the cache"),
    ADD_STAT(read_hit_ratio, statistics::units::Ratio::get(),
             "The ratio of hits to the total accesses to the SPM",
             read_hits / (read_hits + read_misses)),
    ADD_STAT(read_hit_ratio, statistics::units::Ratio::get(),
             "The ratio of hits to the total reads to the SPM",
             read_hits / (read_hits + read_misses)),
    ADD_STAT(write_hit_ratio, statistics::units::Ratio::get(),
             "The ratio of hits to the total writes to the SPM",
             write_hits / (write_hits + write_misses))
{
    miss_latency.init(12); // number of buckets
}

} // namespace gem5
