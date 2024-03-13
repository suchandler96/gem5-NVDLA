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
DIR = verilator_nvdla
CC = clang
CXX = clang++

PARAMS = -fPIC -I$(DIR) -I$(VERILATOR_ROOT)/include -std=c++14
INTERFACES = csbMaster axiResponder embeddedBuffer wrapper_nvdla
VERILATOR_LIBS = verilated # verilated_vcd_c

ifeq ($(OPT), 1)
    PARAMS += -DNDEBUG -O3 -Ofast
else
    PARAMS += -g
endif

ifeq ($(MULTI_THREAD), 1)
    PARAMS += -DVL_THREADED
    VERILATOR_LIBS += verilated_threads
endif

sconscript_edit:
ifeq ($(MULTI_THREAD), 1)
	sed -i "s/'VL_THREADED=0'/'VL_THREADED'/g" ../SConscript
else
	sed -i "s/'VL_THREADED'/'VL_THREADED=0'/g" ../SConscript
	sed -i "s/'VL_THREADED=1'/'VL_THREADED=0'/g" ../SConscript
endif

define interface_rules
$(1).o: sconscript_edit $(1).hh $(1).cc
	$(CXX) $(PARAMS) -c -o $(1).o $(1).cc
endef

define verilator_lib_rules
$(1).o:
	$(CXX) -I$(DIR) -I$(VERILATOR_ROOT)/include/vltstd $(PARAMS) -c -o $(1).o $(VERILATOR_ROOT)/include/$(1).cpp
endef

$(foreach interface,$(INTERFACES),$(eval $(call interface_rules,$(interface))))
$(foreach verilator_lib,$(VERILATOR_LIBS),$(eval $(call verilator_lib_rules,$(verilator_lib))))

library_vcd: sconscript_edit $(foreach file_name,$(VERILATOR_LIBS) $(INTERFACES),$(file_name).o)
	ar rvs libVerilatorNVDLA.a $(foreach file_name,$(VERILATOR_LIBS) $(INTERFACES),$(file_name).o)

VNV_nvdla: $(foreach file_name,$(INTERFACES),$(file_name).o)
	$(CXX) nvdla.cpp $(foreach file_name,$(INTERFACES),$(file_name).o) $(PARAMS) \
	-I$(VERILATOR_ROOT)/include/vltstd -L $(DIR) -l VNV_nvdla__ALL -o VNV_nvdla

.PHONY: clean

clean:
	rm -f *.o *.d *.so *.a

