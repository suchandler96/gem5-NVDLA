/*
 * Copyright (c) 2017 Jason Lowe-Power
 * Copyright (c) 2021 Guillem Lopez Paradis
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

//#include "cpu/base.hh"
#include "cpu/translation.hh"
#include "debug/rtlObject.hh"

// #include "params/rtlObject.hh"
#include "sim/clocked_object.hh"
#include "sim/system.hh"

//class rtlObject;
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
    virtual void handleFunctional(PacketPtr pkt) {};

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
         * Send a retry to the peer port only if it is needed. This is called
         * from the SimpleCache whenever it is unblocked.
         */
        void trySendRetry();

      protected:
        /**
         * Receive an atomic request packet from the master port.
         * No need to implement in this simple memobj.
         */
        Tick recvAtomic(PacketPtr pkt) override
        { panic("recvAtomic unimpl."); }

        /**
         * Receive a functional request packet from the master port.
         * Performs a "debug" access updating/reading the data in place.
         *
         * @param packet the requestor sent.
         */
        void recvFunctional(PacketPtr pkt) override;

        /**
         * Receive a timing request from the master port.
         *
         * @param the packet that the requestor sent
         * @return whether this object can consume to packet. If false, we
         *         will call sendRetry() when we can try to receive this
         *         request again.
         */
        bool recvTimingReq(PacketPtr pkt) override;

        /**
         * Called by the master port if sendTimingResp was called on this
         * slave port (causing recvTimingResp to be called on the master
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
        /// The object that owns this object (rtlObject)
        rtlObject *owner;

        /// If we tried to send a packet and it was blocked, store it here
        PacketPtr blockedPacket;

      public:
        /**
         * Constructor. Just calls the superclass constructor.
         */
        MemSidePort(const std::string& name, rtlObject *owner) :
            RequestPort(name, owner), owner(owner), blockedPacket(nullptr)
        { }

        PacketPtr blockedPacket;

        /**
         * Send a packet across this port. This is called by the owner and
         * all of the flow control is hanled in this function.
         *
         * @param packet to send.
         */
        void sendPacket(PacketPtr pkt);

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

    };

protected:
    // Instantiation of the CPU-side port
    //CPUSidePort cpuPort;

    // Instantiation of the memory-side port
    //MemSidePort memPort;

    // True if this is currently blocked waiting for a response.
    bool blocked;

    // System pointer
    //System * system;

    // Enable RTL Object
    bool enableObject;

    // Enable RTL Object Trace
    bool traceEnable;

    /** The tick event used for scheduling CPU ticks. */
    //EventFunctionWrapper tickEvent;

  public:

    /** constructor
     */
    rtlObject(rtlObjectParams *params);

    //virtual Port &getPort(const std::string &if_name,
    //              PortID idx=InvalidPortID) override = 0;

    // Functions to be used to make the starting
    // tick and end functionalities

    // To be called when starting the rtl Object
    virtual void start() = 0 ;
    // To be called when finishing the execution
    virtual void end() = 0 ;
    // To be called every tick()
    virtual void tick() = 0 ;

    // Functions for TLB connection
    //bool isSquashed() const;// { return false; }
    //void startTranslate(Addr vaddr, ContextID contextId);
    //void finishTranslation(WholeTranslationState *state);

    //virtual void regStats() override = 0;
};


#endif // __RTLOBJECT_VERILATOR_HH__
