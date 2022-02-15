

#ifndef __RTL_PACKET_FIFO_HH__
#define __RTL_PACKET_FIFO_HH__

#include <cstdlib>
#include <iostream>

struct inputFIFO {
    uint64_t    write_enable;
    uint64_t    data_input;
    uint64_t    read_enable;      
};

struct outputFIFO {
    uint64_t    full;
    uint64_t    empty;
    uint64_t    data_output;
};



#endif // __RTL_PACKET_FIFO_HH__
