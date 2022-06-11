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
DIR_FST =verilator_fst
DIR_VCD =verilator_nvdla
DIR_TH =verilator_threads_fst
DIR=${DIR_VCD}
#verilator_gcd_opt
#make -f VNV_nvdla.mk CC=clang CXX=clang++ CXXFLAGS+="-fPIC"  VM_PARALLEL_BUILDS=1 OPT_FAST="-Os -fno-stack-protector"
#GCC=/usr/bin/gcc-4.9
#CPP=/usr/bin/cpp-4.9
VERILATOR_ROOT=/usr/local/verilator_3.912
CC=clang
CXX=clang++ -std=c++0x -fPIC
# --gcc-toolchain=/usr/bin/gcc-6
# CLANG=/home/glopez/BSC/MasterThesis/src/sw/clang-3.4/bin/clang
CLANG=/usr/local/clang_llvm/bin/clang

create_sc_time_o: sc_time.cc sc_time.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	-MMD -I$(VERILATOR_ROOT)/include/vltstd \
	-DVL_PRINTF=printf -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 \
	-Wno-sign-compare -Wno-uninitialized \
	-Wno-unused-parameter -Wno-unused-variable -Wno-shadow \
	-c -o sc_time.o sc_time.cc

create_csbMaster_o: csbMaster.cc csbMaster.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	-MMD -I$(VERILATOR_ROOT)/include/vltstd \
	-DVL_PRINTF=printf -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 \
	-Wno-sign-compare -Wno-uninitialized \
	-Wno-unused-parameter -Wno-unused-variable -Wno-shadow \
	-c -o csbMaster.o csbMaster.cc

create_axiResponder_o: axiResponder.cc axiResponder.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	-MMD -I$(VERILATOR_ROOT)/include/vltstd \
	-DVL_PRINTF=printf -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 \
	-Wno-sign-compare -Wno-uninitialized \
	-Wno-unused-parameter -Wno-unused-variable -Wno-shadow \
	-c -o axiResponder.o axiResponder.cc

create_wrapper_vcd_o: create_axiResponder_o create_csbMaster_o wrapper_nvdla.cc wrapper_nvdla.hh
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	-MMD -I$(VERILATOR_ROOT)/include/vltstd \
	-DVL_PRINTF=printf -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 \
	-Wno-sign-compare -Wno-uninitialized \
	-Wno-unused-parameter -Wno-unused-variable -Wno-shadow \
	-c -o wrapper_nvdla.o wrapper_nvdla.cc

create_wrapper_fst_o: wrapper_nvdla.cc wrapper_nvdla.hh
	$(CXX) -fpic -I$(DIR_FST) -g -I$(VERILATOR_ROOT)/include \
	-MMD -I$(VERILATOR_ROOT)/include/vltstd \
	-DVL_PRINTF=printf -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 \
	-Wno-sign-compare -Wno-uninitialized \
	-Wno-unused-parameter -Wno-unused-variable -Wno-shadow  \
	-c -o wrapper_nvdla_fst.o wrapper_nvdla.cc

create_wrapper_th_o:
	$(CXX) -fPIC -I$(DIR)  -MMD -I$(VERILATOR_ROOT)/include \
	 -I$(VERILATOR_ROOT)/include/vltstd \
	 -DVL_PRINTF=printf -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 \
	 -faligned-new -Wno-bool-operation -Wno-sign-compare \
	 -Wno-uninitialized \
	 -Wno-unused-parameter -Wno-unused-variable -Wno-shadow  \
	 -DVL_THREADED -std=gnu++14 -Os -fno-stack-protector \
	 -c -o wrapper_nvdla.o wrapper_nvdla.cc

create_nvdla_o: nvdla.cpp
	$(CXX) -fpic -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	-MMD -I$(VERILATOR_ROOT)/include/vltstd \
	-DVL_PRINTF=printf -DVM_COVERAGE=0 -DVM_SC=0 -DVM_TRACE=1 \
	-Wno-sign-compare -Wno-uninitialized \
	-Wno-unused-parameter -Wno-unused-variable -Wno-shadow  \
	-c -o nvdla.o nvdla.cpp

create_library_vcd: create_wrapper_vcd_o
	$(CXX) -I$(DIR) -g -I$(VERILATOR_ROOT)/include \
	wrapper_nvdla.o csbMaster.o axiResponder.o \
	 -I$(VERILATOR_ROOT)/include/vltstd \
	 $(VERILATOR_ROOT)/include/verilated.cpp \
	 $(VERILATOR_ROOT)/include/verilated_vcd_c.cpp \
	 -fPIC -shared -o libVerilatorNVDLA.so \
	 -Wl,--whole-archive $(DIR)/VNV_nvdla__ALL.a -Wl,--no-whole-archive

create_library_fst: create_wrapper_fst_o
	$(CXX) -I$(DIR_FST) -I$(VERILATOR_ROOT)/include wrapper_nvdla_fst.o \
	 -I$(VERILATOR_ROOT)/include/vltstd \
	 $(VERILATOR_ROOT)/include/verilated.cpp \
	 $(VERILATOR_ROOT)/include/verilated_fst_c.cpp \
	 -fPIC -shared -lz -o libVerilatorNVDLAFST.so \
	 -Wl,--whole-archive $(DIR_FST)/VNV_nvdla__ALL.a -Wl,--no-whole-archive


create_library_th: create_wrapper_th_o
	$(CXX) -I$(DIR) -g -I$(VERILATOR_ROOT)/include wrapper_nvdla.o \
	 -I$(VERILATOR_ROOT)/include/vltstd \
	 $(VERILATOR_ROOT)/include/verilated.cpp \
	 $(VERILATOR_ROOT)/include/verilated_fst_c.cpp \
	 -fPIC  -shared -o libVerilatorNVDLATHFSTOPT.so \
	 -pthread -lpthread -latomic -lm -lstdc++ -lz \
	 -Wl,--whole-archive $(DIR)/VNV_nvdla__ALL.a -Wl,--no-whole-archive




main-fst: main.cpp 
	$(CXX) -lstdc++ -g -I$(DIR_FST) -I$(VERILATOR_ROOT)/include main.cpp -L . -lz -o main-fst -lVerilatorNVDLAFSTOPT

main-vcd: main.cpp 
	$(CXX) -lstdc++ -g -I$(DIR_VCD) -I$(VERILATOR_ROOT)/include main.cpp -L .  -o main-vcd -lVerilatorNVDLAVCD

main-th: main.cpp 
	$(CXX) -lstdc++ -g -I$(DIR_TH) -I$(VERILATOR_ROOT)/include main.cpp -L .  -o main-th -lVerilatorNVDLATHFSTOPT

main-fst-opt: main.cpp 
	$(CXX) -lstdc++ -g -I$(DIR_FST) -I$(VERILATOR_ROOT)/include main.cpp -L . -lz -o main-fst -lVerilatorNVDLAFSTOPT

main-vcd-opt: main.cpp 
	$(CXX) -lstdc++ -g -I$(DIR_VCD) -I$(VERILATOR_ROOT)/include main.cpp -L .  -o main-vcd -lVerilatorNVDLAGCDOPT

main-th-opt: main.cpp 
	$(CXX) -lstdc++ -g -I$(DIR_TH) -I$(VERILATOR_ROOT)/include main.cpp -L .  -o main-th -lVerilatorNVDLATHFSTOPT

#g++ -g -I$(DIR) -I$(VERILATOR_ROOT)/include main.cpp -L . -o main -lVerilator

main-g++: main.cpp 
	#clang++ -lstdc++ -g -I$(DIR) -I$(VERILATOR_ROOT)/include main.cpp -L . -o main -lVerilator
	g++ -v -I$(DIR) -I$(VERILATOR_ROOT)/include main.cpp -L . -o main_g++ -libVerilatorNVDLA

main-static: main.cpp 
	$(CXX) -g -I$(DIR) -I$(VERILATOR_ROOT)/include main.cpp\
	 $(DIR)/verilated.o $(DIR)/verilated_vcd_c.o\
	 $(DIR)/VNV_nvdla__ALL.a -fpic -o main-static

main-nvdla: nvdla.cpp 
	$(CXX) -g -I$(DIR) -I$(VERILATOR_ROOT)/include nvdla.cpp\
	 $(DIR)/verilated.o $(DIR)/verilated_vcd_c.o\
	 $(DIR)/VNV_nvdla__ALL.a -fpic -o main-nvdla

hellomake: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

install:
	cp wrapper_nvdla.hh ..
	cp libVerilatorNVDLA.so ..
	cp rtl_packet_nvdla.hh ..

# this is to make it visible outsite command line
.PHONY: clean create_library_wrapper main create_nvdla_o main-nvdla 

#clean:
	#rm -f $(DIR)/*.o $(DIR)/*.d $(DIR)/*.o*

