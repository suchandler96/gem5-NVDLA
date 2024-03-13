MULTI_THREAD = 0

nvdla:
	cd ext/rtl/model_nvdla && make clean && make library_vcd OPT=1 -j24 && cd ../../../
	CC=clang CXX=clang++ /usr/bin/python3 /usr/bin/scons build/ARM/gem5.fast PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24

nvdlapgo:
	cd ext/rtl/model_nvdla && make clean && make library_vcd OPT=1 MULTI_THREAD=$(MULTI_THREAD) -j24 && cd ../../../
	sed -i "s/ccflags = \(.*\)'fast' : \[/ccflags = \1'fast' : ['-fprofile-generate', /g" src/SConscript
	sed -i "s/ldflags = \(.*\)'fast' : \[/ldflags = \1'fast' : ['-fprofile-generate', /g" src/SConscript
	CC=clang CXX=clang++ /usr/bin/python3 /usr/bin/scons build/ARM/gem5.fast PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24
	cd ../nvdla/traces/lenet_readme_test/logs/pft/3/ && rm -f *.profraw && bash run.sh && \
	llvm-profdata-10 merge -output=../../../../../../gem5-nvdla/Le.profdata *.profraw && cd ../../../../../../gem5-nvdla
	sed -i "s/ccflags = \(.*\)'fast' : \['-fprofile-generate', /ccflags = \1'fast' : ['-fprofile-use=' + os.path.abspath('..\/..\/Le.profdata'), /g" src/SConscript
	sed -i "s/ldflags = \(.*\)'fast' : \['-fprofile-generate', /ldflags = \1'fast' : [/g" src/SConscript
	CC=clang CXX=clang++ /usr/bin/python3 /usr/bin/scons build/ARM/gem5.fast PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24
	sed -i "s/ccflags = \(.*\)'fast' : \['-fprofile-use=' + os.path.abspath('..\/..\/Le.profdata'), /ccflags = \1'fast' : [/g" src/SConscript

nvdladbg:
	cd ext/rtl/model_nvdla && make library_vcd && cd ../../../
	CC=clang CXX=clang++ /usr/bin/python3 /usr/bin/scons build/ARM/gem5.debug PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24
