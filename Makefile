nvdla:
	cd ext/rtl/model_nvdla && make clean && make library_vcd_opt -j24 && cd ../../../
	CC=clang-10 CXX=clang++-10 /usr/bin/python3 /usr/bin/scons build/ARM/gem5.fast PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24

nvdlapgo:
	cd ext/rtl/model_nvdla && make clean && make library_vcd_opt -j24 && cd ../../../
	sed -i "s/ccflags = \(.*\)'fast' : \[/ccflags = \1'fast' : ['-fprofile-generate', /g" src/SConscript
	sed -i "s/ldflags = \(.*\)'fast' : \[/ldflags = \1'fast' : ['-fprofile-generate', /g" src/SConscript
	CC=clang-10 CXX=clang++-10 /usr/bin/python3 /usr/bin/scons build/ARM/gem5.fast PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24
	cd ../nvdla/traces/lenet_readme_test/logs/pft/3/ && rm -f *.profraw && bash run.sh && \
	llvm-profdata-10 merge -output=../../../../../../gem5-nvdla/Le.profdata *.profraw && cd ../../../../../../gem5-nvdla
	sed -i "s/ccflags = \(.*\)'fast' : \['-fprofile-generate', /ccflags = \1'fast' : ['-fprofile-use=' + os.path.abspath('..\/..\/Le.profdata'), /g" src/SConscript
	sed -i "s/ldflags = \(.*\)'fast' : \['-fprofile-generate', /ldflags = \1'fast' : [/g" src/SConscript
	CC=clang-10 CXX=clang++-10 /usr/bin/python3 /usr/bin/scons build/ARM/gem5.fast PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24
	sed -i "s/ccflags = \(.*\)'fast' : \['-fprofile-use=' + os.path.abspath('..\/..\/Le.profdata'), /ccflags = \1'fast' : [/g" src/SConscript

nvdladbg:
	cd ext/rtl/model_nvdla && make library_vcd && cd ../../../
	CC=clang-6.0 CXX=clang++-6.0 /usr/bin/python3 /usr/bin/scons build/ARM/gem5.debug PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24
