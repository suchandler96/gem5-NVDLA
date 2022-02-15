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
