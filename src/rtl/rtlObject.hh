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

#ifndef __RTLOBJECT_VERILATOR_HH__
#define __RTLOBJECT_VERILATOR_HH__

#include <string>
#include <vector>

#include "cpu/translation.hh"
#include "debug/rtlObject.hh"
#include "mem/packet.hh"
#include "mem/port.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

namespace gem5
{

struct rtlObjectParams;
/**
 * RTLObject class
 */
class rtlObject : public ClockedObject
{
  protected:

    /**
     * Handle the request from the CPU side
     *
     * @param requesting packet
     * @return true if we can handle the request this cycle, false if the
     *         requestor needs to retry later
     */
    virtual bool handleRequest(PacketPtr pkt) {return false;};

    /**
     * Handle the response from the memory side
     *
     * @param responding packet
     * @return true if we can handle the response this cycle, false if the
     *         responder needs to retry later
     */
    virtual bool handleResponse(PacketPtr pkt) {return false;};

    /**
     * Handle a packet functionally. Update the data on a write and get the
     * data on a read.
     *
     * @param packet to functionally handle
     */
    virtual void handleFunctional(PacketPtr pkt) =0;

    virtual AddrRangeList getAddrRanges() const = 0;

    /**
     * Tell the CPU side to ask for our memory ranges.
     */
    virtual void sendRangeChange() = 0;

public:
    /**
     * Port on the CPU-side that receives requests.
     * Mostly just forwards requests to the owner.
     * Part of a vector of ports. One for each CPU port (e.g., data, inst)
     */
    class CPUSidePort : public ResponsePort
    {
      private:
        /// The object that owns this object (rtlObject)
        rtlObject *owner;

        /// True if the port needs to send a retry req.
        bool needRetry;

        /// If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        CPUSidePort(const std::string& name, rtlObject *owner) :
            ResponsePort(name, owner), owner(owner), needRetry(false),
            blockedPacket(nullptr)
        { }

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         *
         * @param packet to send.
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
         * from the rtlObject whenever it is unblocked.
         */
        void trySendRetry();

      protected:
        /**
         * Receive an atomic request packet from the request port.
         * No need to implement in this simple memobj.
         */
        Tick recvAtomic(PacketPtr pkt) override
        { panic("recvAtomic unimpl."); }

        /**
         * Receive a functional request packet from the request port.
         * Performs a "debug" access updating/reading the data in place.
         *
         * @param packet the requestor sent.
         */
        void recvFunctional(PacketPtr pkt) override;

        /**
         * Receive a timing request from the request port.
         *
         * @param the packet that the requestor sent
         * @return whether this object can consume the packet. If false, we
         *         will call sendRetry() when we can try to receive this
         *         request again.
         */
        bool recvTimingReq(PacketPtr pkt) override;

        /**
         * Called by the request port if sendTimingResp was called on this
         * response port (causing recvTimingResp to be called on the request
         * port) and was unsuccesful.
         */
        void recvRespRetry() override;
    };

    /**
     * Port on the memory-side that receives responses.
     * Mostly just forwards requests to the owner
     */
    class MemSidePort : public RequestPort
    {
      private:
        // The object that owns this object (rtlObject)
        rtlObject *owner;

        // If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        MemSidePort(const std::string& name, rtlObject *owner) :
            RequestPort(name, owner), owner(owner), blockedPacket(nullptr)
        { }

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         *
         * @param packet to send.
         */
        void sendPacket(PacketPtr pkt);

        bool isBlocked() {
            return blockedPacket != nullptr;
        }

      protected:
        /**
         * Receive a timing response from the response port.
         */
        bool recvTimingResp(PacketPtr pkt) override;

        /**
         * Called by the response port if sendTimingReq was called on this
         * request port (causing recvTimingReq to be called on the responder
         * port) and was unsuccesful.
         */
        void recvReqRetry() override;

        /**
         * Called to receive an address range change from the peer responder
         * port. The default implementation ignores the change and does
         * nothing. Override this function in a derived class if the owner
         * needs to be aware of the address ranges, e.g. in an
         * interconnect component like a bus.
         */
        void recvRangeChange() override;
    };

protected:
    /*
    * Instiantiation of CPU-side port and MEM side ports
    */
    //CPUSidePort cpuPort;
    //MemSidePort memPort;

    // True if this is currently blocked waiting for a response.
    bool blocked;

    // System pointer
    System * system;

    // Enable RTL Object
    bool enableObject;

    // Enable RTL Object Trace
    bool enableWaveform;

    /** The tick event used for scheduling CPU ticks. */
    EventFunctionWrapper tickEvent;

    struct rtl_stats
    {
        statistics::Scalar rtl_cycles;
    };
    rtl_stats stats;

    uint64_t cyclesStat;

  public:

    /** constructor
     */
    rtlObject(const rtlObjectParams &params);

    virtual ~rtlObject() { delete system;};

    /* getPort needs to be implemented in the case
       ports are being used with the rtlObject derived class
      Port &getPort(const std::string &if_name,
                  PortID idx=InvalidPortID) override = 0; */

    /**
    * Functions to be used to make the init (reset and initialization)
    * tick and end functionalities of the rtl Model
    */

    // To be called when starting the rtl Object
    virtual void initRTLModel() = 0 ;
    // To be called when finishing the execution
    virtual void endRTLModel() = 0 ;
    // To be called every tick()
    virtual void tick() = 0 ;

    /*
    * Functions for TLB connection
    * finishTranslation needs to be override on derived class
    */
    bool isSquashed() const { return false; }
    void startTranslate(Addr vaddr, ContextID contextId);
    virtual void finishTranslation(WholeTranslationState *state);

    /*
    *  regStats functionalities,
    *  by default only implemented
    *  cycles statistic
    *  Should be override on derived class
    */
    void regStats() override;
};

} // End namespace gem5


#endif // __RTLOBJECT_VERILATOR_HH__
