
CPT_LITTLE=cpt_little_25_01



DEBUG_FLAGS='ROI,Accelerator,PseudoInst'


do_cpt:
	build/ARM/gem5.opt configs/example/arm/fs_bigLITTLE_RTL.py \
	--dtb armv8_gem5_v1_big_little_0_1.dtb \
 	--big-cpus 0 --little-cpus 1 \
  	--cpu-type atomic --bootscript=configs/boot/hack_back_ckpt.rcS

#	--mem-type DDR4_2400_8x8 --mem-channels 4 \

run_little:
	build/ARM/gem5.opt --debug-file=trace.out --debug-flags=PseudoInst,rtlObject,rtlNVDLA \
	configs/example/arm/fs_bigLITTLE_RTL.py \
	--dtb armv8_gem5_v1_big_little_0_1.dtb --big-cpus 0  \
	--little-cpus 1 --last-cache-level 3 --caches \
	--accelerators --numNVDLA 1 --maxReqNVDLA 240 \
    --enableTimingAXI \
	--restore-from cpts/${CPT_LITTLE} \
	--bootscript bsc-util/bootscript_validation_rtl.rcS

nvdla:
	cd ext/rtl/model_nvdla && make clean && make library_vcd_opt -j24 && cd ../../../
	CC=clang-10 CXX=clang++-10 /usr/bin/python3 /usr/bin/scons build/ARM/gem5.fast PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24


nvdladbg:
	cd ext/rtl/model_nvdla && make library_vcd && cd ../../../
	CC=clang-6.0 CXX=clang++-6.0 /usr/bin/python3 /usr/bin/scons build/ARM/gem5.debug PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j 24
