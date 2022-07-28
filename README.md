# gem5-RTL-NVDLA
gem5-RTL-NVDLA is a specialized version of gem5-RTL that is designed to be used with the NVDLA verilog model. For gem5+RTL framework that this project is based on, see its original repo [gem5-RTL](https://gitlab.bsc.es/glopez/gem5-rtl.git). Here we show most of the original readme file for gem5+RTL, but a lot more details are added regarding the installation process to make it more newcomer-friendly.

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
This explanation focuses around the NVDLA use-case. To check out its code do:
```
# Go to the gem5+RTL folder
git checkout use-case-nvdla
```
​
### Step 1: (For NVDLA) Build the RTL C++ model using Verilator

1. Download repo and follow instructions at [NVDLA](http://nvdla.org/hw/v1/integration_guide.html) and switch to nvdlav1 branch to obtain the model and compiling. Make sure you use verilator v3.912 and clang 3.4. RAM requirements>24GB
2. Before building NVDLA for verilator verification, modify the Makefile at `nvdla/hw/verif/verilator/Makefile`:
```
   VERILATOR_PARAMS ?= --compiler clang --output-split 250000000 -CFLAGS **-fPIC**

   $(DEPTH)/outdir/nv_full/verilator/VNV_nvdla.mk: verilator.f ../../outdir/nv_full/vmod # and a lot of RTL...
   $(VERILATOR) --cc --exe -f verilator.f --Mdir ../../outdir/nv_full/verilator/ nvdla.cpp **$(VERILATOR_PARAMS)**
```
3. Run some sanity tests provided in `nvdla/hw/verif/traces` as introduced at their website.
4. Get also the traces (automatically generated after running a sanity test in `nvdla/hw/outdir/nv_full/verilator/test/YOUR_SANITY_TEST/trace.bin`) and move them to the disk image that will be used in gem5 full system simulation.
5. Move the contents in the NVDLA verilator output folder (`nvdla/hw/outdir/nv_full/verilator/*`) to `ext/rtl/model_nvdla/verilator_nvdla/`
6. To create the library you need to use the same clang v3.4 with stdlib of gcc v6: `clang++ -stdlib=libc++ --gcc-toolchain=/path/to/gccv6`

Finally:

```
# Obtain libVerilatorNVDLA.so
# Every time any file in /ext/rtl/model_nvdla is modified, these commands should be executed to rebuild the library.
# When files in /src is modified, only recompiling gem5 is ok.
cd ext/rtl/model_nvdla
make create_library_vcd 
make install
```

​

### Step 2: Compile gem5
FOR NVDLA: please use clang v6.0.0
```
# Compile
CC=CC=clang-6.0 CXX=clang++-6.0 scons -j2 build/ARM/gem5.opt

# An alternative command in case the above one doesn't work (specify explicitly where these prerequisites are):
CC=clang-6.0 CXX=clang++-6.0 /usr/bin/python3 $(which scons) build/ARM/gem5.opt PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j2

```
​
### Step 3: Usage
Before executing you need to set up the LIBRARY_PATH to the place where the shared library of the RTL Object resides:
​
``` 
export LD_LIBRARY_PATH="gem5+RTL_FOLDER/ext/rtl"
```
​
Since this framework runs in full system mode, you need to prepare a linux kernel and disk image. One choice is use those provided in [gem5 doc ARM fs binaries](https://www.gem5.org/documentation/general_docs/fullsystem/guest_binaries). Once prepared, update the paths to them in the first few lines in `configs/example/arm/fs_bigLITTLE_RTL.py`. Files can be moved to the disk image by first mounting it using the `gem5-rtl-nvdla/util/gem5img.py` script. Remember checkpoints have to be regenerated after adding new files to the disk image.

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
	--accelerators \
	--restore-from YOUR_CPT_PATH \
	--bootscript bsc-util/bootscript_validation_rtl.rcS
​
# To use debug flags you can add the following options
--debug-file=trace.out --debug-flags=PseudoInst,rtlObject,rtlNVDLA
# You may also refer to Makefile in this directory to see similar running commands
```
​
The application used in the example `bootscript_validation_rtl.rcS` can be found in the folder `bsc-util/validation_rtl/`
​

### Step 4: Design the memory subsystem architecture as you want
Currently classic memory models (no ruby memory system) are used in the simulation. The major codes describing the memory connection are in `configs/example/arm/devices` which will be imported by the `fs_bigLITTLE_RTL.py` script. The codes of interest are in `CpuCluster.addPrivateAccelerator` function. Connecting NVDLA to main memory directly through membus is the default setting. This configuration can be changed with two options: `--dma-enable` and `--add-accel-private-cache`. These two options are parsed in `devices.py` to connect the memory system.

​
## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.
​
​
## License
[MIT](https://choosealicense.com/licenses/mit/)
