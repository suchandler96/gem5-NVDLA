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
