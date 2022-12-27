/*
* Copyright (c) 2022 Barcelona Supercomputing Center
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
* Authors: Guillem Lopez Paradis
*/

#ifndef __RTL_PACKET_NVDLA_HH__
#define __RTL_PACKET_NVDLA_HH__

#include <cstdlib>
#include <iostream>
#include <queue>

struct read_resp_entry_t {
    bool        read_valid;
    uint32_t    read_addr;
    uint8_t*    read_data;
};
struct inputNVDLA {
    read_resp_entry_t read_dbbif;
    read_resp_entry_t read_sram;
};

struct write_req_entry_t {
    uint8_t     write_data;
    uint32_t    write_addr;
    bool        write_sram;
    bool        write_timing;
};

struct long_write_req_entry_t {
    uint8_t*    write_data;
    // we will malloc space to it when used,
    // and will pass it directly to gem5 pkt
    // to avoid extra data movement overhead

    uint32_t    write_addr;
    uint32_t    length;
    uint64_t    write_mask;
    bool        write_sram;
    bool        write_timing;
};

struct read_req_entry_t {
    uint32_t    read_addr;
    uint32_t    read_bytes;
    bool        read_sram;
    bool        read_timing;
};

struct outputNVDLA {
    bool                             read_valid;
    std::queue<read_req_entry_t>     read_buffer;
    bool                             write_valid;
    std::queue<write_req_entry_t>    write_buffer;
    std::queue<long_write_req_entry_t>    long_write_buffer;
    std::queue<std::pair<uint64_t, uint32_t>> dma_read_buffer;
    std::queue<std::pair<uint64_t, std::vector<uint8_t>>> dma_write_buffer;
};



#endif // __RTL_PACKET_NVDLA_HH__
