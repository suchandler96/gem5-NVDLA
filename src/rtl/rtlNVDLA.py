# -*- coding: utf-8 -*-
# Copyright (c) 2022 Guillem Lopez Paradis
# All rights reserved.
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

from m5.params import *
from m5.proxy import *
from m5.objects.rtlObject import rtlObject

class rtlNVDLA(rtlObject):
    type = 'rtlNVDLA'
    cxx_header = "rtl/rtlNVDLA.hh"
    cxx_class = 'gem5::rtlNVDLA'

    cpu_side  = ResponsePort("CPU side port, receives requests")
    mem_side  = RequestPort("Memory side port, sends requests")
    sram_port = RequestPort("High Speed port to SRAM, sends requests")
    dram_port = RequestPort("Regular Speed to DRAM, sends requests")

    id_nvdla = Param.UInt64(0,"id of the NVDLA")

    maxReq = Param.UInt64(4,"Max Request inglight for NVDLA")

    base_addr_dram = Param.UInt64(0xA0000000,"Max Request inglight for NVDLA")

    base_addr_sram = Param.UInt64(0xB0000000,"Max Request inglight for NVDLA")

    enableTimingAXI = Param.Bool(False,"Enable Timing mode in AXI")
