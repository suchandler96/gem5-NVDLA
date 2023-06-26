/* Some code is based on nvdla.cpp which has:
* nvdla.cpp
* Driver for Verilator testbench
* NVDLA Open Source Project
*
* Copyright (c) 2017 NVIDIA Corporation.  Licensed under the NVDLA Open
* Hardware License.  For more information, see the "LICENSE" file that came
* with this distribution.
*
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

#include "wrapper_nvdla.hh"
#include <iostream>

double sc_time_stamp(){
  return double_t(0);
}

Wrapper_nvdla::Wrapper_nvdla(int id_nvdla, bool traceOn, std::string name, const unsigned int maxReq,
                             bool _dma_enable, int _spm_latency, int _spm_line_size, int _spm_line_num,
                             bool pft_enable) :
        id_nvdla(id_nvdla),
        tickcount(0),
        tfp(NULL),
        tfpname(name),
        traceOn(traceOn),
        dma_enable(_dma_enable),
        spm_latency(_spm_latency),
        spm_line_size(_spm_line_size),
        spm_line_num(_spm_line_num),
        prefetch_enable(pft_enable) {

    int argcc = 1;
    char* buf[] = {(char*)"aaa",(char*)"bbb"};
    Verilated::commandArgs(argcc, buf);

    dla = new VNV_nvdla();

    // we always enable the trace
    // but we will use it depending on traceOn
    // otherwise this function launch an error
    Verilated::traceEverOn(true);
    tfp = new VerilatedVcdC;
    // dla->trace(tfp, 99);
    tfp->open(name.c_str());
    if (!tfp) {
        return;
    }

    std::cout << tfpname << std::endl;
    //tfp->open(tfpname.c_str());

    // CSB Wrapper
    csb = new CSBMaster(dla, this);

    // AXI DBBIF
    AXIResponder::connections dbbconn = {
        .aw_awvalid = &dla->nvdla_core2dbb_aw_awvalid,
        .aw_awready = &dla->nvdla_core2dbb_aw_awready,
        .aw_awid = &dla->nvdla_core2dbb_aw_awid,
        .aw_awlen = &dla->nvdla_core2dbb_aw_awlen,
        .aw_awaddr = &dla->nvdla_core2dbb_aw_awaddr,

        .w_wvalid = &dla->nvdla_core2dbb_w_wvalid,
        .w_wready = &dla->nvdla_core2dbb_w_wready,
        .w_wdata = dla->nvdla_core2dbb_w_wdata,
        .w_wstrb = &dla->nvdla_core2dbb_w_wstrb,
        .w_wlast = &dla->nvdla_core2dbb_w_wlast,

        .b_bvalid = &dla->nvdla_core2dbb_b_bvalid,
        .b_bready = &dla->nvdla_core2dbb_b_bready,
        .b_bid = &dla->nvdla_core2dbb_b_bid,

        .ar_arvalid = &dla->nvdla_core2dbb_ar_arvalid,
        .ar_arready = &dla->nvdla_core2dbb_ar_arready,
        .ar_arid = &dla->nvdla_core2dbb_ar_arid,
        .ar_arlen = &dla->nvdla_core2dbb_ar_arlen,
        .ar_araddr = &dla->nvdla_core2dbb_ar_araddr,

        .r_rvalid = &dla->nvdla_core2dbb_r_rvalid,
        .r_rready = &dla->nvdla_core2dbb_r_rready,
        .r_rid = &dla->nvdla_core2dbb_r_rid,
        .r_rlast = &dla->nvdla_core2dbb_r_rlast,
        .r_rdata = dla->nvdla_core2dbb_r_rdata,
    };
    axi_dbb = new AXIResponder(dbbconn, this, "DBB",
              false, maxReq);

    // AXI CVSRAM
    AXIResponder::connections cvsramconn = {
        .aw_awvalid = &dla->nvdla_core2cvsram_aw_awvalid,
        .aw_awready = &dla->nvdla_core2cvsram_aw_awready,
        .aw_awid = &dla->nvdla_core2cvsram_aw_awid,
        .aw_awlen = &dla->nvdla_core2cvsram_aw_awlen,
        .aw_awaddr = &dla->nvdla_core2cvsram_aw_awaddr,

        .w_wvalid = &dla->nvdla_core2cvsram_w_wvalid,
        .w_wready = &dla->nvdla_core2cvsram_w_wready,
        .w_wdata = dla->nvdla_core2cvsram_w_wdata,
        .w_wstrb = &dla->nvdla_core2cvsram_w_wstrb,
        .w_wlast = &dla->nvdla_core2cvsram_w_wlast,

        .b_bvalid = &dla->nvdla_core2cvsram_b_bvalid,
        .b_bready = &dla->nvdla_core2cvsram_b_bready,
        .b_bid = &dla->nvdla_core2cvsram_b_bid,

        .ar_arvalid = &dla->nvdla_core2cvsram_ar_arvalid,
        .ar_arready = &dla->nvdla_core2cvsram_ar_arready,
        .ar_arid = &dla->nvdla_core2cvsram_ar_arid,
        .ar_arlen = &dla->nvdla_core2cvsram_ar_arlen,
        .ar_araddr = &dla->nvdla_core2cvsram_ar_araddr,

        .r_rvalid = &dla->nvdla_core2cvsram_r_rvalid,
        .r_rready = &dla->nvdla_core2cvsram_r_rready,
        .r_rid = &dla->nvdla_core2cvsram_r_rid,
        .r_rlast = &dla->nvdla_core2cvsram_r_rlast,
        .r_rdata = dla->nvdla_core2cvsram_r_rdata,
    };
    axi_cvsram = new AXIResponder(cvsramconn, this, "CVSRAM",
                                      true, maxReq);
    
    // Clear output
    // memset(&output,0,sizeof(outputNVDLA));
}


Wrapper_nvdla::~Wrapper_nvdla() {
    if (tfp) {
        tfp->dump(tickcount);
        tfp->close();
            delete tfp;
    }
    // TODO: this was causing a segfault not sure
    // TO CHECK
    //dla->final(); 
    delete dla;
    exit(EXIT_SUCCESS);
}

void Wrapper_nvdla::enableTracing() {
    traceOn = true;
}

void Wrapper_nvdla::disableTracing() {
    traceOn = false;
}

void Wrapper_nvdla::tick() {
    
    dla->dla_core_clk = 1;
    dla->dla_csb_clk = 1;
    dla->eval();

    advanceTickCount();

    dla->dla_core_clk = 0;
    dla->dla_csb_clk = 0;
    dla->eval();

    advanceTickCount();
}

void Wrapper_nvdla::advanceTickCount() {
    tickcount++;
    if (tfp and traceOn) {
        tfp->dump(tickcount);
    }
    
}

uint64_t Wrapper_nvdla::getTickCount() {
    return tickcount;
}

void Wrapper_nvdla::reset() {
    //dla->rst = 1;
    dla->dla_core_clk = 1;
    dla->dla_csb_clk = 1;
    dla->eval();

    advanceTickCount();

    dla->dla_core_clk = 0;
    dla->dla_csb_clk = 0;
    dla->eval();

    advanceTickCount();
    //top->rst = 0;
}

void Wrapper_nvdla::init() {
    dla->global_clk_ovr_on = 0;
    dla->tmc2slcg_disable_clock_gating = 0;
    dla->test_mode = 0;
    dla->nvdla_pwrbus_ram_c_pd = 0;
    dla->nvdla_pwrbus_ram_ma_pd = 0;
    dla->nvdla_pwrbus_ram_mb_pd = 0;
    dla->nvdla_pwrbus_ram_p_pd = 0;
    dla->nvdla_pwrbus_ram_o_pd = 0;
    dla->nvdla_pwrbus_ram_a_pd = 0;
    
    printf("reset...\n");
    dla->dla_reset_rstn = 1;
    dla->direct_reset_ = 1;
    dla->eval();
    for (int i = 0; i < 20; i++) {
        dla->dla_core_clk = 1;
        dla->dla_csb_clk = 1;
        dla->eval();
        tickcount++;
        tfp->dump(tickcount);
        
        dla->dla_core_clk = 0;
        dla->dla_csb_clk = 0;
        dla->eval();
        tickcount++;
        tfp->dump(tickcount);
    }

    dla->dla_reset_rstn = 0;
    dla->direct_reset_ = 0;
    dla->eval();
    
    for (int i = 0; i < 20; i++) {
        dla->dla_core_clk = 1;
        dla->dla_csb_clk = 1;
        dla->eval();
        tickcount++;
        tfp->dump(tickcount);
        
        dla->dla_core_clk = 0;
        dla->dla_csb_clk = 0;
        dla->eval();
        tickcount++;
        tfp->dump(tickcount);
    }
    
    dla->dla_reset_rstn = 1;
    dla->direct_reset_ = 1;
    
    printf("letting buffers clear after reset...\n");
    for (int i = 0; i < 4096; i++) {
        dla->dla_core_clk = 1;
        dla->dla_csb_clk = 1;
        dla->eval();
        tickcount++;
        tfp->dump(tickcount);
        
        dla->dla_core_clk = 0;
        dla->dla_csb_clk = 0;
        dla->eval();
        tickcount++;
        tfp->dump(tickcount);
    }
}

void Wrapper_nvdla::addReadReq(bool read_sram, bool read_timing,
                uint32_t read_addr, uint32_t read_bytes) {

    output.read_valid = true;
    read_req_entry_t rd;
    rd.read_sram      = read_sram;
    rd.read_timing    = read_timing;
    rd.read_addr      = read_addr;
    rd.read_bytes     = read_bytes;
    output.read_buffer.push(rd);
}

void Wrapper_nvdla::addWriteReq(bool write_sram, bool write_timing,
                 uint32_t write_addr, uint8_t write_data) {
    output.write_valid = true;
    write_req_entry_t wr;
    wr.write_sram   = write_sram;
    wr.write_timing = write_timing;
    wr.write_data   = write_data;
    wr.write_addr   = write_addr;
    output.write_buffer.push(wr);
}

void Wrapper_nvdla::addLongWriteReq(bool write_sram, bool write_timing,
                uint32_t write_addr, uint32_t length, const uint8_t* const write_data, uint64_t mask) {
    output.write_valid = true;
    long_write_req_entry_t wr;
    wr.write_sram = write_sram;
    wr.write_timing = write_timing;
    wr.write_addr = write_addr;
    wr.length = length;
    wr.write_mask = mask;
    wr.write_data = new uint8_t[length];
    for (int i = 0; i < length; i++) {
        wr.write_data[i] = write_data[i];
    }
    output.long_write_buffer.push(std::move(wr));
}

void Wrapper_nvdla::clearOutput() {
    output.read_valid  = false;
    output.write_valid = false;
    while (!output.read_buffer.empty()) {
        output.read_buffer.pop();
    }
    while (!output.write_buffer.empty()) {
        output.write_buffer.pop();
    }
    // dma rd and wr buffer should be kept because
    // dma_engine cannot be issued with multiple tasks at once
}

outputNVDLA& Wrapper_nvdla::tick(inputNVDLA in) {
    
    dla->dla_core_clk = 1;
    dla->dla_csb_clk = 1;
    dla->eval();

    advanceTickCount();

    dla->dla_core_clk = 0;
    dla->dla_csb_clk = 0;
    dla->eval();

    advanceTickCount();

    return output;    
}

uint8_t Wrapper_nvdla::read_spm_byte(uint64_t addr) {
    uint64_t addr_base = addr & ~(uint64_t)(spm_line_size - 1);

    auto spm_line_it = spm.find(addr_base);

    assert(spm_line_it != spm.end());

    uint64_t offset = addr & (uint64_t)(spm_line_size - 1);
    
    lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);

    return spm_line_it->second.spm_line[offset];
}

void Wrapper_nvdla::read_spm_line(uint64_t aligned_addr, uint8_t* data_out) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);

    auto spm_line_it = spm.find(aligned_addr);

    assert(spm_line_it != spm.end());

    for (int i = 0; i < spm_line_size; i++) {
        data_out[i] = spm_line_it->second.spm_line[i];
    }

    lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);
}

bool Wrapper_nvdla::read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out) {
    assert((axi_addr & (uint64_t)(AXI_WIDTH / 8 - 1)) == 0);

    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);

    auto spm_line_it = spm.find(addr_base);
    if (spm_line_it == spm.end()) {
        return false;
    }
    std::vector<uint8_t>& entry_vector = spm_line_it->second.spm_line;
    for (int i = 0; i < AXI_WIDTH / 8; i++) {
        data_out[i] = entry_vector[offset + i];
    }
    lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);

    return true;
}

void Wrapper_nvdla::write_spm_byte(uint64_t addr, uint8_t data) {
    uint64_t addr_base = addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = addr & (uint64_t)(spm_line_size - 1);

    auto spm_line_it = spm.find(addr_base);

    if (spm_line_it == spm.end()) {      // if the element to write is not in spm
        if (spm.size() >= spm_line_num) {
            erase_spm_line();   // lru_order maintanence of erasing is done inside
        }

        // assign space to this spm line
        std::map<uint64_t, SPMLineWithTag>::iterator entry_it;
        bool alloc_succ;
        std::tie(entry_it, alloc_succ) = spm.insert(std::move(std::make_pair(addr_base, SPMLineWithTag())));
        lru_order.push_back(entry_it);

        std::vector<uint8_t>& entry_vector = entry_it->second.spm_line;
        entry_vector.resize(spm_line_size, 0);
        entry_vector[offset] = data;
        entry_it->second.dirty = 1;    // write bytes are always coming from NVDLA
        entry_it->second.lru_it = std::prev(lru_order.end());
    } else {
        spm_line_it->second.spm_line[offset] = data;
        spm_line_it->second.dirty = 1;

        lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);
    }
}

void Wrapper_nvdla::write_spm_line(uint64_t aligned_addr, const uint8_t* const data, uint8_t dirty) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);

    auto spm_line_it = spm.find(aligned_addr);
    if (spm_line_it == spm.end()) {
        if (spm.size() >= spm_line_num) {
            erase_spm_line();   // lru_order maintenance of erasing is done inside
        }

        // assign space to this spm line
        std::map<uint64_t, SPMLineWithTag>::iterator entry_it;
        bool alloc_succ;
        std::tie(entry_it, alloc_succ) = spm.insert(std::move(std::make_pair(aligned_addr, SPMLineWithTag())));
        lru_order.push_back(entry_it);

        auto& entry = entry_it->second;
        entry.spm_line.assign(data, data + spm_line_size);
        entry.dirty = dirty;
        entry.lru_it = std::prev(lru_order.end());
    } else {
        spm_line_it->second.spm_line.assign(data, data + spm_line_size);
        spm_line_it->second.dirty = dirty;

        lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);
    }    
}

void Wrapper_nvdla::write_spm_line(uint64_t aligned_addr, const std::vector<uint8_t>& data, uint8_t dirty) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);

    auto spm_line_it = spm.find(aligned_addr);
    if (spm_line_it == spm.end()) {
        if (spm.size() >= spm_line_num) {
            erase_spm_line();   // lru_order maintenance of erasing is done inside
        }

        // assign space to this spm line
        std::map<uint64_t, SPMLineWithTag>::iterator entry_it;
        bool alloc_succ;
        std::tie(entry_it, alloc_succ) = spm.insert(std::move(std::make_pair(aligned_addr,
            SPMLineWithTag{ data, std::list<std::map<uint64_t, SPMLineWithTag>::iterator>::iterator(), dirty })));
        lru_order.push_back(entry_it);

        entry_it->second.lru_it = std::prev(lru_order.end());
    } else {
        spm_line_it->second.spm_line.assign(data.begin(), data.end());
        spm_line_it->second.dirty = dirty;

        lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);
    }
}

void Wrapper_nvdla::write_spm_axi_line(uint64_t axi_addr, const uint8_t* const data) {
    assert((axi_addr & (uint64_t)(AXI_WIDTH / 8 - 1)) == 0);

    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);

    auto spm_line_it = spm.find(addr_base);
    if (spm_line_it == spm.end()) {
        if (spm.size() >= spm_line_num) {
            erase_spm_line();   // lru_order maintenance of erasing is done inside
        }

        // assign space to this spm line
        bool alloc_succ;
        std::tie(spm_line_it, alloc_succ) = spm.insert(std::move(std::make_pair(addr_base, SPMLineWithTag())));
        lru_order.push_back(spm_line_it);
        spm_line_it->second.lru_it = std::prev(lru_order.end());
        spm_line_it->second.spm_line.resize(spm_line_size, 0);
    } else {
        lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);
    }
    // writes at AXI_WIDTH/8-granularity is always coming from NVDLA, so we write dirty bit
    spm_line_it->second.dirty = 1;
    for (int i = 0; i < AXI_WIDTH / 8; i++) {
        spm_line_it->second.spm_line[offset + i] = data[i];
    }
}

void Wrapper_nvdla::write_spm_axi_line_with_mask(uint64_t axi_addr, const uint8_t* const data, const uint64_t mask) {
    assert((axi_addr & (uint64_t)(AXI_WIDTH / 8 - 1)) == 0);

    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);

    auto spm_line_it = spm.find(addr_base);
    if (spm_line_it == spm.end()) {
        if (spm.size() >= spm_line_num) {
            erase_spm_line();   // lru_order maintenance of erasing is done inside
        }

        // assign space to this spm line
        bool alloc_succ;
        std::tie(spm_line_it, alloc_succ) = spm.insert(std::move(std::make_pair(addr_base, SPMLineWithTag())));
        lru_order.push_back(spm_line_it);
        spm_line_it->second.lru_it = std::prev(lru_order.end());
        spm_line_it->second.spm_line.resize(spm_line_size, 0);
    } else {
        lru_order.splice(lru_order.end(), lru_order, spm_line_it->second.lru_it);
    }
    // writes at AXI_WIDTH/8-granularity is always coming from NVDLA, so we write dirty bit
    spm_line_it->second.dirty = 1;
    if (mask == 0xFFFFFFFFFFFFFFFF) {
        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            spm_line_it->second.spm_line[offset + i] = data[i];
        }
    } else {
        for (int i = 0; i < AXI_WIDTH / 8; i++) {
            if (!((mask >> i) & 1))
                continue;
            spm_line_it->second.spm_line[offset + i] = data[i];
        }
    }
}

bool Wrapper_nvdla::check_txn_data_in_spm(uint64_t addr) {
    uint64_t spm_line_addr = addr & ~((uint64_t)(spm_line_size - 1));
    if (spm.find(spm_line_addr) == spm.end()) {
        return false;
    }
    return true;
}

std::map<uint64_t, Wrapper_nvdla::SPMLineWithTag>::iterator Wrapper_nvdla::get_it_to_erase() {
    // naive: spm.begin()
    // return spm.begin();

    return lru_order.front();
}

void Wrapper_nvdla::erase_spm_line() {
    assert(!spm.empty());

    auto to_erase_it = get_it_to_erase();

    if (to_erase_it->second.dirty) {
        output.dma_write_buffer.push(std::make_pair(to_erase_it->first, std::move(to_erase_it->second.spm_line)));
    }
    lru_order.pop_front();
    spm.erase(to_erase_it);
    printf("(%lu) SPM line addr 0x%08lx is erased, dirty: %d\n", tickcount, to_erase_it->first, to_erase_it->second.dirty);
}

void Wrapper_nvdla::flush_spm() {
    auto it = spm.begin();
    while (it != spm.end()) {
        if (it->second.dirty) {   // this is a read-and-write variable
            output.dma_write_buffer.push(std::make_pair(it->first, std::move(it->second.spm_line)));
            spm.erase(it++);
        } else {
            it++;
        }
    }
    lru_order.clear();
}

void Wrapper_nvdla::addDMAReadReq(uint64_t read_addr, uint32_t read_bytes) {
    output.dma_read_buffer.push(std::make_pair(read_addr, read_bytes));
}
