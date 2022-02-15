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

#include "wrapper_fifo.hh"

Wrapper_fifo::Wrapper_fifo(bool traceOn, std::string name) :
        tickcount(0),
        fst(NULL),
        fstname(name),
        traceOn(traceOn) {

    top = new Vtop;

    Verilated::traceEverOn(traceOn);
    fst = new VerilatedVcdC;
    if (!fst) {
        return;
    }
    
    // fst, levels, options
    top->trace(fst,99);

    std::cout << fstname << std::endl;
    fst->open(fstname.c_str());
}

Wrapper_fifo::~Wrapper_fifo() {
    if (fst) {
        fst->dump(tickcount);
        fst->close();
        delete fst;
    }
    top->final();
    delete top;
    exit(EXIT_SUCCESS);
}

void Wrapper_fifo::enableTracing() {
    traceOn = true;
}

void Wrapper_fifo::disableTracing() {
    traceOn = false;
    fst->dump(tickcount);
    fst->close();
}

void Wrapper_fifo::tick() {

    top->clk = 1;
    top->eval();

    advanceTickCount();

    top->clk = 0;
    top->eval();

    advanceTickCount();
}

outputFIFO Wrapper_fifo::tick(inputFIFO in) {
    // Part of the code to adapt to RTL Model
    processInput(in);

    top->clk = 1;
    top->eval();

    advanceTickCount();

    top->clk = 0;
    top->eval();

    advanceTickCount();

    return processOutput();

}

void Wrapper_fifo::processInput(inputFIFO in) {
    top->write_enable = in.write_enable;
    top->data_input   = in.data_input;
    top->read_enable  = in.read_enable;
}

outputFIFO Wrapper_fifo::processOutput() {
    outputFIFO out;
    
    out.data_output = top->full;
    out.empty       = top->empty;
    out.data_output = top->data_output;
    
    return out;
}

void Wrapper_fifo::advanceTickCount() {
    if (fst and traceOn) {
        fst->dump(tickcount);
    }
    tickcount++;
}

uint64_t Wrapper_fifo::getTickCount() {
    return tickcount;
}

void Wrapper_fifo::reset() {
    top->rst = 1;
    top->clk = 1;
    top->eval();

    advanceTickCount();

    top->clk = 0;
    top->eval();

    advanceTickCount();
    top->rst = 0;
}

void Wrapper_fifo::addInput(int write_enable,
                      int data_input,
                      int read_enable)
{
    top->write_enable = write_enable;
    top->data_input = data_input;
    top->read_enable = read_enable;
}
int Wrapper_fifo::outputFull() {
    return top->full;    
}
int Wrapper_fifo::outputEmpty() {
    return top->empty;
}
int Wrapper_fifo::outputDataOutput() {
    return top->data_output;
}
