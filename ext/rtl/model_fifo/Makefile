# Copyright (c) 2022 Barcelona Supercomputing Center
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Guillem Lopez Paradis

# output dirs from Verilator
#DIR_FST =verilator_fifo_fst
DIR_VCD =verilator_fifo_vcd
CC=gcc
CXX=g++
OBJS_VERILATE='*.sv' # Modify with the rtl object files

## In the case you want to verilate with fst trace
#verilate_fst: $(OBJS)
#    $(VERILATOR_ROOT)/bin/verilator -Wall -cc --trace-fst top.sv --Mdir $(DIR_FST)

verilate_vcd: $(OBJS)
	$(VERILATOR_ROOT)/bin/verilator -Wall -cc --trace top.sv --Mdir $(DIR_VCD)

#library_fst: verilate_fst wrapper_fifo.cc wrapper_fifo.hh
#    g++ -I $(DIR_FST) -I$(VERILATOR_ROOT)/include wrapper_fifo.cc $(DIR)/*.cpp \
$(VERILATOR_ROOT)/include/verilated.cpp $(VERILATOR_ROOT)/include/verilated_fst_c.cpp \
-lz -fpic  -shared -o libVerilatorFIFO.so

library_vcd: verilate_vcd wrapper_fifo.cc wrapper_fifo.hh
	g++ -I $(DIR_VCD) -I$(VERILATOR_ROOT)/include wrapper_fifo.cc $(DIR_VCD)/*.cpp \
	$(VERILATOR_ROOT)/include/verilated.cpp $(VERILATOR_ROOT)/include/verilated_vcd_c.cpp \
	-fpic  -shared -o libVerilatorFIFO.so

install: libVerilatorFIFO.so
	cp libVerilatorFIFO.so ../
	cp wrapper_fifo.hh ../
	cp rtl_packet_fifo.hh ../

.PHONY: clean ultra-clean verilate_vcd library_vcd install

clean:
	rm -rf $(DIR_VCD) $(DIR_FST) libVerilatorFIFO.so 

ultra-clean:
	rm -rf  ../libVerilatorFIFO.so ../wrapper_fifo.hh ../rtl_packet_fifo.hh
