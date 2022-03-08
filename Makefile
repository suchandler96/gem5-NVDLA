
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
	