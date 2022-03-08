# gem5+RTL
​
gem5+RTL is a flexible framework that enables the simulation of RTL models inside the gem5 full-system software simulator. The framework allows easy integration of RTL models on a simulated system-on-chip (SoC) that is able to boot Linux and run complex multi-threaded and multi-programmed workloads. 
​
This repository includes multiple relevant use-cases that integrate a multi-core SoC with a Performance Monitoring Unit (PMU) and the NVIDIA Deep Learning Accelerator (NVDLA), showcasing how the framework enables testing RTL model features and how it can enable co-design taking into account the entire SoC.
​
If you use gem5+RTL in your research please cite the gem5+RTL article: 
```
gem5 + rtl: A Framework to Enable RTL Models Inside a Full-System Simulator. Guillem López-Paradís, Adrià Armejach, Miquel Moretó. ICPP 2021: 29:1-29:11
```
​
## Installation
​
### gem5+RTL repository
```
# Download repo
git clone https://gitlab.bsc.es/glopez/gem5-rtl.git
​
# Select use-case
git checkout use-case-fifo
git checkout use-case-pmu
git checkout use-case-ghdl
git checkout use-case-nvdla
```
### Install Verilator
Go to [Verilator webpage](https://verilator.org/guide/latest/install.html) and follow the instructions. Verilator is required to build the model from SV to C++.
​
```
# Installation example
git clone https://github.com/verilator/verilator
​
unset VERILATOR_ROOT      # For bash
#git checkout stable      # Use most recent stable release
#git checkout v{version}  # Switch to specified release version
​
autoconf 
./configure --prefix /Specific/location ---> FOR NVDLA 3.912
​
make -j (e.g. 2|4|8) 
[sudo] make install
​
# This must be set when compiling gem5+RTL
export VERILATOR_ROOT=/Specific/location
```
​
## Using gem5+RTL
​
This explanation focuses around the fifo use-case. To check out its code do:
```
# Go to the gem5+RTL folder
git checkout use-case-fifo
```
​
### Step 1: Build the RTL C++ model using Verilator
We provide some examples on how to build RTL Verilator models: e.g. the fifo example, in `ext/rtl/model_fifo` there is a makefile that describes the simple flow. To compile do:
​
```
make
make library_vdc
make install
```
## FOR NVDLA
1. Download repo and follow instructions at [NVDLA](http://nvdla.org/hw/v1/integration_guide.html) to obtain the model and compiling. Make sure you use verilator v3.912 and clang 3.4. Get also the traces and move them to the gem5 image.
2. Move the verilator output folder to ext/rtl/model_nvdla/verilator_nvdla
3. To create the library you need to use the same clang v3.4 with stdlib of gcc v6: clang++ -stdlib=libc++ --gcc-toolchain=/path/to/gccv6
make create_library_vcd *
​
### Step 2: Adapt script in ext/rtl/SConscript
​
Check out the `gem5+rtl_FOLDER/ext/rtl/SConscript` and add the folder of the RTL model created by Verilator. For the fifo use-case this is already done.
​
### Step 3: Create the rtlObject of the desired RTL model
This model will be in charge of interfacing gem5 with the verilator model created in step 1.
We recommend using the FIFO example (rtlFIFO.cc|hh|py on src/rtl) as the base for the new rtlObject model.   
​
Don't forget to add this new object in the SConscript (src/rtl/SConscript)
​
### Step 4: Compile gem5 
Just compile with the regular scons infrastructure:
​
```
# Compile
scons -j2 build/ARM/gem5.opt
```
FOR NVDLA --> clang v6.0.0
```
# Compile
CC=clang CXX=clang++ scons -j2 build/ARM/gem5.opt
```
​
### Step 5: Usage
Before executing you need to set up the LIBRARY_PATH to the place where the shared library of the RTL Object resides:
​
``` 
export LD_LIBRARY_PATH="gem5+RTL_FOLDER/ext/rtl"
```
​
To run a simulation you can use the already prepared `configs/example/arm/fs_bigLITTLE_RTL.py` configuration script to generate checkpoints and execute simulations:
​
```
# To create a checkpoint
build/ARM/gem5.opt configs/example/arm/fs_bigLITTLE_RTL.py \
 	--big-cpus 0 --little-cpus 1 \
  	--cpu-type atomic --bootscript=configs/boot/hack_back_ckpt.rcS
​
# To simulate from checkpoint
# Note that you will need to provide a valid disk and kernel images to the configuration script
build/ARM/gem5.opt configs/example/arm/fs_bigLITTLE_RTL.py \
	--little-cpus 1 --last-cache-level 3 --caches \
	--accelerators --enableWaveform \
	--restore-from YOUR_CPT_PATH \
	--bootscript bsc-util/bootscript_validation_rtl.rcS
​
# To use debug flags you can add the following options
--debug-file=trace.out --debug-flags=PseudoInst,rtlFIFO
```
​
The application used in the example `bootscript_validation_rtl.rcS` can be found in the folder `bsc-util/validation_rtl/`
​
## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.
​
​
## License
[MIT](https://choosealicense.com/licenses/mit/)