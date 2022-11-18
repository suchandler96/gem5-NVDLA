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

#ifndef __MEM_SIMPLE_SPM_HH__
#define __MEM_SIMPLE_SPM_HH__

#include <map>
#include <list>

#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/SimpleSPM.hh"
#include "sim/clocked_object.hh"
#include "dev/dma_device.hh"

namespace gem5
{

/**
 * A very simple scratchpad memory object. Use Cache-like (addr, spm_line)
 * mapping for indexing items (because the up-stream accelerator
 * issues requests like this)
 * This scratchpad is non-blocking, as long as there is free space
 * in dma_engine.
 * //This cache is fully blocking (not non-blocking). Only a single request can
 * //be outstanding at a time.
 * This scratchpad can be configured either read-only (e.g., for weights
 * in Neural Networks) and writeable (e.g., for intermediate variables)
 */
class SimpleSPM : public ClockedObject
{
  private:

    /**
     * Port on the CPU-side that receives requests.
     * Mostly just forwards requests to the SPM (owner)
     */
    class CPUSidePort : public ResponsePort
    {
      private:
        /// Since this is a vector port, need to know what number this one is
        int id;

        /// The object that owns this object (SimpleSPM)
        SimpleSPM *owner;

        /// True if the port needs to send a retry req.
        bool needRetry;

        /// If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        CPUSidePort(const std::string& name, int id, SimpleSPM *owner) :
            ResponsePort(name, owner), id(id), owner(owner), needRetry(false),
            blockedPacket(nullptr)
        { }

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is handled in this function.
         * This is a convenience function for the SimpleSPM to send pkts.
         *
         * @param pkt packet to send.
         */
        void sendPacket(PacketPtr pkt);

        /**
         * Get a list of the non-overlapping address ranges the owner is
         * responsible for. All response ports must override this function
         * and return a populated list with at least one item.
         *
         * @return a list of ranges responded to
         */
        AddrRangeList getAddrRanges() const override;

        /**
         * Send a retry to the peer port only if it is needed. This is called
         * from the SimpleSPM whenever it is unblocked.
         */
        void trySendRetry();

      protected:
        /**
         * Receive an atomic request packet from the request port.
         * No need to implement in this simple SPM.
         */
        Tick recvAtomic(PacketPtr pkt) override
        { return owner->memPort.sendAtomic(pkt); }

        /**
         * Receive a functional request packet from the request port.
         * Performs a "debug" access updating/reading the data in place.
         *
         * @param pkt the requestor sent.
         */
        void recvFunctional(PacketPtr pkt) override;

        /**
         * Receive a timing request from the request port.
         *
         * @param pkt the packet that the requestor sent
         * @return whether this object can consume to packet. If false, we
         *         will call sendRetry() when we can try to receive this
         *         request again.
         */
        bool recvTimingReq(PacketPtr pkt) override;

        /**
         * Called by the request port if sendTimingResp was called on this
         * response port (causing recvTimingResp to be called on the request
         * port) and was unsuccessful.
         */
        void recvRespRetry() override;
    };

    /**
     * Port on the memory-side that receives responses.
     * Mostly just forwards requests to the SPM (owner)
     */
    class MemSidePort : public RequestPort
    {
      private:
        /// The object that owns this object (SimpleSPM)
        SimpleSPM *owner;

        /// If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        MemSidePort(const std::string& name, SimpleSPM *owner) :
            RequestPort(name, owner), owner(owner), blockedPacket(nullptr)
        { }

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is handled in this function.
         * This is a convenience function for the SimpleSPM to send pkts.
         *
         * @param packet to send.
         */
        void sendPacket(PacketPtr pkt);

      protected:
        /**
         * Receive a timing response from the response port.
         */
        bool recvTimingResp(PacketPtr pkt) override;

        /**
         * Called by the response port if sendTimingReq was called on this
         * request port (causing recvTimingReq to be called on the response
         * port) and was unsuccessful.
         */
        void recvReqRetry() override;

        /**
         * Called to receive an address range change from the peer response
         * port. The default implementation ignores the change and does
         * nothing. Override this function in a derived class if the owner
         * needs to be aware of the address ranges, e.g. in an
         * interconnect component like a bus.
         */
        void recvRangeChange() override;
    };

    class Dma4SPM : public Drainable, public Serializable
    {
    public:
        Dma4SPM(SimpleSPM* owner,
                 bool proactive_get,
                 DmaPort &port, bool _is_write, size_t size,
                 unsigned max_req_size,
                 unsigned max_pending,
                 Request::Flags flags=0
        );

        ~Dma4SPM();

    public: // Serializable
        void serialize(CheckpointOut &cp) const override;
        void unserialize(CheckpointIn &cp) override;

    public: // Drainable
        DrainState drain() override;

    public: // FIFO access
        /**
         * @{
         * @name FIFO access
         */
        /**
         * Try to read data from the FIFO.
         *
         * This method reads len bytes of data from the FIFO and stores
         * them in the memory location pointed to by dst. The method
         * fails, and no data is written to the buffer, if the FIFO
         * doesn't contain enough data to satisfy the request.
         *
         * @param dst Pointer to a destination buffer
         * @param len Amount of data to read.
         * @return true on success, false otherwise.
         */
        bool tryGet(uint8_t *dst, size_t len);

        template<typename T>
        bool
        tryGet(T &value)
        {
            return tryGet(static_cast<T *>(&value), sizeof(T));
        };

        /**
         * Read data from the FIFO and panic on failure.
         *
         * @see tryGet()
         *
         * @param dst Pointer to a destination buffer
         * @param len Amount of data to read.
         */
        void get(uint8_t *dst, size_t len);

        template<typename T>
        T
        get()
        {
            T value;
            get(static_cast<uint8_t *>(&value), sizeof(T));
            return value;
        };

        /** Get the amount of data stored in the FIFO */
        size_t size() const { return buffer.size(); }

        /** @} */
    public: // FIFO fill control
        /**
         * @{
         * @name FIFO fill control
         */
        /**
         * Start filling the FIFO.
         *
         * @warn It's considered an error to call start on an active DMA
         * engine unless the last request from the active block has been
         * sent (i.e., atEndOfBlock() is true).
         *
         * @param start Physical address to copy from.
         * @param size Size of the block to copy.
         */
        void startFill(Addr start, size_t size, uint8_t* d = nullptr);

        /**
         * Stop the DMA engine.
         *
         * Stop filling the FIFO and ignore incoming responses for pending
         * requests. The onEndOfBlock() callback will not be called after
         * this method has been invoked. However, once the last response
         * has been received, the onIdle() callback will still be called.
         */
        void stopFill();

        /**
         * Has the DMA engine sent out the last request for the active
         * block?
         */
        bool atEndOfBlock() const { return nextAddr == endAddr; }

        /**
         * Is the DMA engine active (i.e., are there still in-flight
         * accesses)?
         */
        bool
        isActive() const
        {
            return !(pendingRequests.empty() && atEndOfBlock());
        }

        /** @} */
    protected: // Callbacks
        /**
         * @{
         * @name Callbacks
         */
        /**
         * End of block callback
         *
         * This callback is called <i>once</i> after the last access in a
         * block has been sent. It is legal for a derived class to call
         * startFill() from this method to initiate a transfer.
         */
        virtual void onEndOfBlock() {};

        /**
         * Last response received callback
         *
         * This callback is called when the DMA engine becomes idle (i.e.,
         * there are no pending requests).
         *
         * It is possible for a DMA engine to reach the end of block and
         * become idle at the same tick. In such a case, the
         * onEndOfBlock() callback will be called first. This callback
         * will <i>NOT</i> be called if that callback initiates a new DMA transfer.
         */
        virtual void onIdle() {};

        /** @} */
    private: // Configuration
        /** Maximum request size in bytes */
        const Addr maxReqSize;
        /** Maximum FIFO size in bytes */
        const size_t fifoSize;
        /** Request flags */
        const Request::Flags reqFlags;

        DmaPort &port;

        const int cacheLineSize;

    private:
        class DmaDoneEvent : public Event
        {
        public:
            DmaDoneEvent(Dma4SPM *_parent, size_t max_size);

            void kill();
            void cancel();
            bool canceled() const { return _canceled; }
            void reset(size_t size, Addr addr);
            void process();

            bool done() const { return _done; }
            size_t requestSize() const { return _requestSize; }
            const uint8_t *data() const { return _data.data(); }
            uint8_t *data() { return _data.data(); }

        private:
            Dma4SPM *parent;
            bool _done = false;
            bool _canceled = false;
            size_t _requestSize;
        public:
            std::vector<uint8_t> _data;
            Addr _addr;
        };

        typedef std::unique_ptr<DmaDoneEvent> DmaDoneEventUPtr;

        /**
         * DMA request done, handle incoming data and issue new
         * request.
         */
        void dmaDone();

        /** Handle pending requests that have been flagged as done. */
        void handlePending();

        /** Try to issue new DMA requests or bypass DMA requests*/
        void resumeFill();

        /** Try to issue new DMA requests during normal execution*/
        void resumeFillTiming();

        /** Try to bypass DMA requests in non-caching mode */
        void resumeFillBypass();

    private: // Internal state
        Fifo<uint8_t> buffer;

        Addr nextAddr = 0;
        Addr endAddr = 0;

        std::deque<DmaDoneEventUPtr> pendingRequests;
        std::deque<DmaDoneEventUPtr> freeRequests;
        bool is_write;
        bool proactive_get;     // whether owner will treGet() proactively

    public:
        std::list<std::pair<Addr, std::vector<uint8_t>>> owner_fetch_buffer;
        SimpleSPM* owner;
    };



    /**
     * Handle the request from the CPU side. Called from the CPU port
     * on a timing request.
     *
     * @param pkt requesting packet
     * @param port_id id of the port to send the response
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    bool handleRequest(PacketPtr pkt, int port_id);

    /**
     * Handle the response from the memory side. Called from the memory port
     * on a timing response.
     *
     * @param responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    bool handleResponse(PacketPtr pkt);

    /**
     * Send the packet to the CPU side.
     * This function assumes the pkt is already a response packet and forwards
     * it to the correct port. This function also unblocks this object and
     * cleans up the whole request.
     *
     * @param the packet to send to the cpu side
     */
    void sendResponse(PacketPtr pkt, int port_id);

    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read. Called from CPU port on a recv functional.
     *
     * @param packet to functionally handle
     */
    void handleFunctional(PacketPtr pkt);

    /**
     * Access the SPM for a timing access. This is called after the SPM
     * access latency has already elapsed.
     */
    void accessTiming(PacketPtr pkt, int port_id);

    /**
     * This is where we actually update / read from the SPM. This function
     * is executed on both timing and functional accesses.
     *
     * @return true if a hit, false otherwise
     */
    bool accessFunctional(PacketPtr pkt);

    /**
     * Insert a block into the SPM. If there is no room left in the SPM,
     * then this function evicts an entry to make room for the new block.
     * The entry evicted is determined by implementation in writeBackLine().
     *
     * @param packet with the data (and address) to insert into the SPM
     */
    void insert(PacketPtr pkt);

    /**
     * Insert a SPM line got by DMA into SPM. If there is no room left in SPM,
     * then this function evicts an entry to make room for the new SPM line.
     * The entry evicted is determined by implementation in writeBackLine().
     *
     * @param addr address of the SPM line to insert
     * @param to_insert_line a vector of uint8_t (data to insert)
     */
    void insert(Addr addr, std::vector<uint8_t>& to_insert_line);

    /**
     * Write back a SPM line to main memory via DMA. In read-only setting,
     * this function should not be called since no data are dirty.
     * Note that using this function is just an approximation for software
     * scheduling of using an SPM.
     * The line to be written back is selected according to a strategy
     * defined inside this function
     */
     void writeBackLine();
    /**
     * Return the address ranges this SPM is responsible for. Just use the
     * same as the next upper level of the hierarchy.
     *
     * @return the address ranges this SPM is responsible for
     */
    AddrRangeList getAddrRanges() const;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    void sendRangeChange() const;

    /**
     * check whether data for a request pkt is in SPM or not
     */
    bool checkHit(PacketPtr pkt) const;

    /**
     * try to flush entries in to_flush_list to main memory via DMA
     */
    void tryFlush();

    /**
     * check miss queue from upstream and resend packets whose data request
     * has been returned. Called when there is some response from lower
     * memory hierarchy arrives.
     */
    void checkMissQueueOnResponse();

    /**
     * If this this SPM needWriteBack, this function can be called to flush
     * everything inside back to main memory via DMA.
     */
    void allFlush();

    /// Latency to check the SPM. Number of cycles for both hit and miss
    const Cycles latency;

    /// The block size for the SPM (SPM line_size)
    const unsigned blockSize;

    /// System cache line size
    const unsigned systemCacheLineSize;

    /// Number of blocks in the SPM (size of SPM / block size)
    const unsigned capacity;

    /// whether this SPM is read-only (e.g. for Neural Network weights)
    const bool readOnly;

    /// whether this SPM has to write back its contents when evicting
    const bool needWriteBack;

    /// Instantiation of the CPU-side port
    std::vector<CPUSidePort> cpuPorts;

    /// Instantiation of the memory-side port
    MemSidePort memPort;

    /// Instantiation of dma port (towards the memory system)
    DmaPort dmaPort;

    /// True if this SPM is currently blocked waiting for DMA engine to be free.
    // bool blocked;

    /// Packet that we are currently handling. Used for upgrading to larger
    /// SPM line sizes
    // PacketPtr originalPacket;
    // todo: we can also record the request port ID in each miss
    std::list<std::tuple<PacketPtr, Tick, int>> miss_pkt_with_time_port_list;

    /// To keep track of inflight DMA read transfers from main memory
    /// The address is aligned to blockSize (spm_line_size)
    std::map<Addr, uint32_t> inflight_dma_reads;

    Dma4SPM* dma_rd_engine;
    Dma4SPM* dma_wr_engine;

    std::list<std::pair<Addr,
                        std::pair<std::vector<uint8_t>,
                                  std::list<Addr>::iterator>>> to_flush_list;

    /// An incredibly simple SPM storage. Maps block addresses to data
    std::map<Addr,
             std::pair<std::vector<uint8_t>,
                       std::list<Addr>::iterator>> SPMStore;
    std::list<Addr> lru_order;

    /// SPM statistics
  protected:
    struct SimpleSPMStats
    {
        statistics::Scalar read_hits;
        statistics::Scalar read_new_misses;
        statistics::Scalar read_inflight_misses;
        statistics::Scalar write_hits;
        statistics::Scalar write_misses;
        statistics::Histogram miss_latency;
        statistics::Formula read_hit_ratio;
        statistics::Formula write_hit_ratio;
    } stats;

  public:

    /** constructor
     */
    SimpleSPM(const SimpleSPMParams &params);

    /**
     * Register the stats
     */
    void regStats() override;

    /**
     * Get a port with a given name and index. This is used at
     * binding time and returns a reference to a protocol-agnostic
     * port.
     *
     * @param if_name Port name
     * @param idx Index in the case of a VectorPort
     *
     * @return A reference to the given port
     */
    Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override;

    /**
     * get data from dma_rd_engine->owner_fetch_buffer
     */
    void accessDMAData();
};

} // namespace gem5

#endif // __MEM_SIMPLE_SPM_HH__
