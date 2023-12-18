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


# here there is the code of nvdla
DIR=verilator_nvdla

CC=clang-6.0
CXX=clang++-6.0 -fPIC

create_csbMaster_o: csbMaster.cc csbMaster.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include -std=c++11 \
	-c -o csbMaster.o csbMaster.cc

create_axiResponder_o: axiResponder.cc axiResponder.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include -std=c++11 \
	-c -o axiResponder.o axiResponder.cc

create_embeddedBuffer_o: embeddedBuffer.cc embeddedBuffer.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include -std=c++11 \
	-c -o embeddedBuffer.o embeddedBuffer.cc

create_wrapper_vcd_o: create_axiResponder_o create_csbMaster_o create_embeddedBuffer_o wrapper_nvdla.cc wrapper_nvdla.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include -std=c++11 \
	-c -o wrapper_nvdla.o wrapper_nvdla.cc

verilated_o:
	$(CXX) -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	-I$(VERILATOR_ROOT)/include/vltstd \
	$(VERILATOR_ROOT)/include/verilated.cpp -fPIC \
	-c -o verilated.o

verilated_vcd_o:
	$(CXX) -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	-I$(VERILATOR_ROOT)/include/vltstd \
	$(VERILATOR_ROOT)/include/verilated_vcd_c.cpp -fPIC \
	-c -o verilated_vcd.o

create_library_vcd: create_wrapper_vcd_o verilated_o verilated_vcd_o
	ar rvs libVerilatorNVDLA.a csbMaster.o axiResponder.o embeddedBuffer.o wrapper_nvdla.o verilated.o verilated_vcd.o

.PHONY: clean create_csbMaster_o create_axiResponder_o create_embeddedBuffer_o create_wrapper_vcd_o \
	verilated_o verilated_vcd_o create_library_vcd create_nvdla_o

clean:
	rm -f *.o *.d *.so *.a

