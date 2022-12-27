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

Wrapper_nvdla::Wrapper_nvdla(int id_nvdla, bool traceOn, std::string name,const unsigned int maxReq,
                             int _dma_enable, int _spm_latency, int _spm_line_size, int _spm_line_num, int pft_enable) :
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
    // dma rd and wr buffer should be kept because dma_engine cannot be issued with multiple tasks at once
    //memset(&output,0,sizeof(outputNVDLA));
    //output.write_buffer = new std::queue<write_req_entry_t>();
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

    auto spm_line_vec_it = spm.find(addr_base);

    assert(spm_line_vec_it != spm.end());

    uint64_t offset = addr & (uint64_t)(spm_line_size - 1);
    return spm_line_vec_it->second[offset];
}

void Wrapper_nvdla::read_spm_line(uint64_t aligned_addr, uint8_t* data_out) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);

    auto spm_line_vec_it = spm.find(aligned_addr);

    assert(spm_line_vec_it != spm.end());

    for (int i = 0; i < (AXI_WIDTH / 8); i++) {
        data_out[i] = spm_line_vec_it->second[i];
    }
}

void Wrapper_nvdla::read_spm_axi_line(uint64_t axi_addr, uint8_t* data_out) {
    assert((axi_addr & (uint64_t)(AXI_WIDTH / 8 - 1)) == 0);

    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);

    auto spm_line_vec_it = spm.find(addr_base);
    assert(spm_line_vec_it != spm.end());
    std::vector<uint8_t>& entry_vector = spm_line_vec_it->second;
    for (int i = 0; i < AXI_WIDTH / 8; i++) {
        data_out[i] = entry_vector[offset + i];
    }
}

void Wrapper_nvdla::write_spm_byte(uint64_t addr, uint8_t data) {
    uint64_t addr_base = addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = addr & (uint64_t)(spm_line_size - 1);

    auto spm_line_vec_it = spm.find(addr_base);

    if (spm_line_vec_it == spm.end()) {      // if the element to write is not in spm
        // find an SPM line to erase if size exceeds
        // temporarily write back everything

        // assign space to this spm line
        std::vector<uint8_t>& entry_vector = spm[addr_base];
        entry_vector.resize(spm_line_size, 0);
        entry_vector[offset] = data;
    } else {
        spm_line_vec_it->second[offset] = data;
    }
}

void Wrapper_nvdla::write_spm_line(uint64_t aligned_addr, const uint8_t* const data) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);

    auto spm_line_vec_it = spm.find(aligned_addr);
    if (spm_line_vec_it == spm.end()) {
        // todo: find an SPM line to erase if size exceeds
        // temporarily write back everything

        // assign space to this spm line
        spm[aligned_addr].assign(data, data + (AXI_WIDTH / 8));
    } else {
        spm_line_vec_it->second.assign(data, data + (AXI_WIDTH / 8));
    }    
}

void Wrapper_nvdla::write_spm_axi_line(uint64_t axi_addr, const uint8_t* const data) {
    assert((axi_addr & (uint64_t)(AXI_WIDTH / 8 - 1)) == 0);

    uint64_t addr_base = axi_addr & ~(uint64_t)(spm_line_size - 1);
    uint64_t offset = axi_addr & (uint64_t)(spm_line_size - 1);

    auto spm_line_vec_it = spm.find(addr_base);
    std::vector<uint8_t>* entry_vector_ptr;
    if (spm_line_vec_it == spm.end()) {
        // find an SPM line to erase if size exceeds
        // temporarily write back everything

        // assign space to this spm line
        entry_vector_ptr = &(spm[addr_base]);
        entry_vector_ptr->resize(spm_line_size, 0);
    } else {
        entry_vector_ptr = &(spm_line_vec_it->second);
    }
    for (int i = 0; i < AXI_WIDTH / 8; i++) {
        (*entry_vector_ptr)[offset + i] = data[i];
    }
}

void Wrapper_nvdla::write_spm_line(uint64_t aligned_addr, const std::vector<uint8_t>& data) {
    assert((aligned_addr & (uint64_t)(spm_line_size - 1)) == 0);

    auto spm_line_vec_it = spm.find(aligned_addr);
    if (spm_line_vec_it == spm.end()) {
        // todo: find an SPM line to erase if size exceeds
        // temporarily write back everything

        // assign space to this spm line
        spm.emplace(std::make_pair(aligned_addr, data));
    } else {
        spm_line_vec_it->second.assign(data.begin(), data.end());
    }
}

bool Wrapper_nvdla::check_txn_data_in_spm_and_wr_queue(uint64_t addr) {
    // search spm_write_queue first
    for (auto it = spm_write_queue.begin(); it != spm_write_queue.end(); it++) {
        if ((it->addr) == addr)
            return true;
    }

    // then search spm
    uint64_t spm_line_addr = addr & ~((uint64_t)(spm_line_size - 1));
    if (spm.find(spm_line_addr) == spm.end()) {
        return false;
    }
    return true;
}

bool Wrapper_nvdla::get_txn_data_from_spm_and_wr_queue(uint64_t addr, uint8_t* to_be_filled_data) {
    uint8_t* spm_write_queue_data = nullptr;
    // search spm_write_queue first
    for (auto it = spm_write_queue.begin(); it != spm_write_queue.end(); it++) {
        if (it->addr == addr) {
            spm_write_queue_data = it->data;
            break;
        }
    }
    if (spm_write_queue_data != nullptr) {
        for (int k = 0; k < AXI_WIDTH / 8; k++) {
            to_be_filled_data[k] = spm_write_queue_data[k];
        }
        return true;
    }
    // then search spm
    uint64_t spm_line_addr = addr & ~((uint64_t)(spm_line_size - 1));
    if (spm.find(spm_line_addr) == spm.end())
        return false;

    read_spm_axi_line(addr, to_be_filled_data);
    return true;
}

void Wrapper_nvdla::countdown_spm_write_queue() {
    if (dma_enable && !spm_write_queue.empty()) {
        while (spm_write_queue.front().countdown == 0) {
            // write to spm
            auto& spm_txn = spm_write_queue.front();
            for (int i = 0; i < AXI_WIDTH / 8; i++) {
                if (!((spm_txn.mask >> i) & 1))
                    continue;
                write_spm_byte(spm_txn.addr + i, spm_txn.data[i]);
            }

            spm_write_queue.pop_front();
            if (spm_write_queue.empty())
                break;
        }
        for (auto it = spm_write_queue.begin(); it != spm_write_queue.end(); it++)
            it->countdown--;
    }
}

void Wrapper_nvdla::flush_spm() {
    // first write all items in spm write queue into spm
    while (!spm_write_queue.empty()) {
        auto &txn = spm_write_queue.front();
        if (txn.mask == 0xffffffffffffffff) {
            write_spm_axi_line(txn.addr, txn.data);
            spm_write_queue.pop_front();
        } else {
            for (int i = 0; i < AXI_WIDTH / 8; i++) {
                if (!((txn.mask >> i) & 1))
                    continue;
                write_spm_byte(txn.addr + i, txn.data[i]);
                spm_write_queue.pop_front();
            }
        }
    }

    auto it = spm.begin();
    while (it != spm.end()) {
        if ((it->first & 0xF0000000) == 0x90000000) {   // this is a read-and-write variable
            output.dma_write_buffer.push(std::make_pair(it->first, std::move(it->second)));
            spm.erase(it++);
        } else
            it++;
    }
}

void Wrapper_nvdla::addDMAReadReq(uint64_t read_addr, uint32_t read_bytes) {
    output.dma_read_buffer.push(std::make_pair(read_addr, read_bytes));
}
