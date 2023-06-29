# gem5-RTL-NVDLA
gem5-RTL-NVDLA is a specialized version of gem5-RTL that is designed to be used with the NVDLA verilog model. For gem5+RTL framework that this project is based on, see its original repo [gem5-RTL](https://gitlab.bsc.es/glopez/gem5-rtl.git). Here we are showing citation for gem5+RTL, but we have rewritten the installation process to make it more newcomer-friendly.

Apart from specialized adaption of gem5 memory system for NVDLA, this project also enables the conversion from any NVDLA-supported caffe NN model to NVDLA register transaction traces (i.e., something like input.txn [here](https://github.com/nvdla/hw/tree/nvdlav1/verif/traces/traceplayer)). The code and detailed usage of this conversion utility can be found in `bsc-util/nvdla_utilities`.

In short, this repo makes the following key contributions:
1. Enable the compilation of arbitrary caffe NN models to NVDLA register transaction traces.
2. Enable data transfer between gem5 memory system and NVDLA cores via DMA (while in gem5-RTL, only direct connection with memory bus is possible), and use scratchpad memories to buffer read / write data.
3. Enable scheduling multiple NVDLA cores at a sub-NN level to run a single NN. For example, a pipeline-parallel scheduler is implemented.

# gem5+RTL (copied from [gem5-RTL](https://gitlab.bsc.es/glopez/gem5-rtl.git))
gem5+RTL is a flexible framework that enables the simulation of RTL models inside the gem5 full-system software simulator. The framework allows easy integration of RTL models on a simulated system-on-chip (SoC) that is able to boot Linux and run complex multi-threaded and multi-programmed workloads.

If you use gem5+RTL in your research please cite the gem5+RTL article: 
```
gem5 + rtl: A Framework to Enable RTL Models Inside a Full-System Simulator. Guillem López-Paradís, Adrià Armejach, Miquel Moretó. ICPP 2021: 29:1-29:11
```

# Installation
## Step 0: Install dependencies
Verilator v3.912, clang v3.4, clang v6.0.0, aarch64-linux-gnu-gcc-7 (and g++), and other dependencies that this repo refers the user to other websites. The following installation process is verified on Ubuntu 18.04.6.

## Step 1: clone gem5-rtl-NVDLA repo
```
# Download repo and make sure you are on branch `DMAEnable`
git@github.com:suchandler96/gem5-rtl-nvdla.git
```
## Step 2: Install Verilator
Go to [Verilator webpage](https://verilator.org/guide/latest/install.html) and follow the instructions. Verilator is required to build the model from SV to C++.
```
# Installation example
git clone https://github.com/verilator/verilator

unset VERILATOR_ROOT     # For bash
git checkout stable      # Use most recent stable release
git checkout v{version}  # Switch to specified release version

autoconf 
./configure --prefix /Specific/location ---> FOR NVDLA 3.912

make -j (e.g. 2|4|8) 
[sudo] make install

# This must be set when compiling gem5+RTL
export VERILATOR_ROOT=/Specific/location
```
## Step 3: Prepare gem5 disk image and kernel
Since this framework runs in full system mode of gem5, one needs to prepare a linux kernel and disk image. One choice is to use those provided in [gem5 doc ARM fs binaries](https://www.gem5.org/documentation/general_docs/fullsystem/guest_binaries). Once prepared, update the paths to them in the first few lines in `configs/example/arm/fs_bigLITTLE_RTL.py`. Files can be moved to the disk image by first mounting it using the `util/gem5img.py` script, as will be shown in step 4.4. Remember checkpoints have to be regenerated after adding new files to the disk image.


## Step 4: Build the RTL C++ model using Verilator

1. Download repo and follow instructions at [NVDLA](http://nvdla.org/hw/v1/integration_guide.html) and switch to `nvdlav1` branch to obtain the model and compiling. Make sure you use verilator v3.912 and clang 3.4. RAM requirements > 24GB
2. Before building NVDLA for verilator verification, modify the Makefile at `nvdla/hw/verif/verilator/Makefile` (See also [this pull request](https://github.com/nvdla/hw/pull/312/files)):
    ```
    # add the '-fPIC' flag
    VERILATOR_PARAMS ?= --compiler clang --output-split 250000000 -CFLAGS -fPIC

    # use the VERILATOR_PARAMS variable defined above:
    $(DEPTH)/outdir/nv_full/verilator/VNV_nvdla.mk: verilator.f ../../outdir/nv_full/vmod # and a lot of RTL...
    $(VERILATOR) --cc --exe -f verilator.f --Mdir ../../outdir/nv_full/verilator/ nvdla.cpp $(VERILATOR_PARAMS)
    ```
3. Run some sanity tests provided in `nvdla/hw/verif/traces` as introduced at their website.
4. Make sure the NVDLA register transaction traces have been automatically generated after running a sanity test (like `nvdla/hw/outdir/nv_full/verilator/test/YOUR_SANITY_TEST/trace.bin`) and move them to the disk image that will be used in gem5 full system simulation.
    ```
    # cd to the root directory of this repo
    mkdir mnt
    python3 util/gem5img.py mount /path/to/gem5_linux_images/ubuntu-18.04-arm64-docker.img ./mnt

    # copy the NVDLA register transaction traces to the disk image
    mkdir ./mnt/home/YOUR_SANITY_TEST/
    cp nvdla/hw/outdir/nv_full/verilator/test/YOUR_SANITY_TEST/trace.bin ./mnt/home/YOUR_SANITY_TEST/
   ```
5. Move the contents in the NVDLA verilator output folder (`nvdla/hw/outdir/nv_full/verilator/*`) to `ext/rtl/model_nvdla/verilator_nvdla/`
    ```
    cp /path/to/nvdla/hw/outdir/nv_full/verilator/* ext/rtl/model_nvdla/verilator_nvdla/
    ```
6. To create the library one needs to use the same clang v3.4 with stdlib of gcc v6: `clang++ -stdlib=libc++ --gcc-toolchain=/path/to/gccv6`
    ```
    # Obtain libVerilatorNVDLA.so
    # make sure the output of command `clang++ -v` is v3.4
    cd ext/rtl/model_nvdla
    make create_library_vcd 
    make install
    ```

## Step 5: Compile gem5
Please use clang v6.0.0 to compile gem5.
```
# Compile
CC=clang-6.0 CXX=clang++-6.0 scons -j2 build/ARM/gem5.opt

# An alternative command in case the above one doesn't work (specify explicitly where these prerequisites are):
CC=clang-6.0 CXX=clang++-6.0 /usr/bin/python3 $(which scons) build/ARM/gem5.opt PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j2
```
One can modify the above commands to build gem5 according to the configuration. Once any source code under `src/` or `ext/` is modified, one can run `make nvdla` to combine section 4.6 and section 5 to recompile.

Before executing, one needs to set up the LIBRARY_PATH to the place where the shared library of the RTL Object resides:
``` 
export LD_LIBRARY_PATH="/path/to/this/repo/ext/rtl"
```
## Step 6: Run a workload invoking NVDLA
The NVDLA workload should be defined in the full system simulation. This is done through cross-compiling an ARM CPU binary that will invoke the NVDLA accelerator. The binary, whose source code is in `bsc-util/*.c`, is cross-compiled with `aarch64-linux-gnu-g++-7`. In addition, an assembly file (`util/m5/src/abi/arm64/m5op.S`) defining the gem5-specific operations should also be cross-compiled. So one may compile them separately into *.o's and link them together. An example compilation process would be:
```
cd bsc-util/
aarch64-linux-gnu-g++-7 my_validation_nvdla_single_thread.cpp -c -o my_validation_nvdla_single_thread.o -fPIC -O3 --static -std=c++11
aarch64-linux-gnu=g++-7 util/m5/src/abi/arm64/m5op.S -c -o m5op.o -fPIC -O3 --static -std=c++11
aarch64-linux-gnu-g++-7 my_validation_nvdla_single_thread.o m5op.o -o my_validation_nvdla_single_thread -fPIC -O3 --static -std=c++11
mv my_validation_nvdla_single_thread ../mnt/home/
# must move to ../mnt/home/ because bsc-util/nvdla_utilities/sweep/main.py will look for the binary there
```
To run a simulation one can use the already prepared `configs/example/arm/fs_bigLITTLE_RTL.py` configuration script to generate checkpoints and execute simulations:
```
# To create a checkpoint
build/ARM/gem5.opt configs/example/arm/fs_bigLITTLE_RTL.py \
 	--big-cpus 0 --little-cpus 1 \
  	--cpu-type atomic --bootscript=configs/boot/hack_back_ckpt.rcS

# To simulate from checkpoint
# Note that one will need to provide a valid disk and kernel images to the configuration script
build/ARM/gem5.opt configs/example/arm/fs_bigLITTLE_RTL.py \
	--little-cpus 1 --last-cache-level 3 --caches \
	--accelerators \
	--restore-from YOUR_CPT_PATH \
	--bootscript bsc-util/bootscript_validation_rtl.rcS

# To use debug flags one can add the following options
--debug-file=trace.out --debug-flags=PseudoInst,rtlObject,rtlNVDLA
# May also refer to Makefile in this directory to see similar running commands
```
The application used in the example `bootscript_validation_rtl.rcS` can be found in `bsc-util/`.

## Step 7: Choose the memory subsystem architecture
Currently classic memory models (no ruby memory system) are used in the simulation. The major codes describing the memory connection are in `configs/example/arm/devices.py` which will be imported by the `fs_bigLITTLE_RTL.py` script. The codes of interest are in `CpuCluster.addPrivateAccelerator` function, where connecting NVDLA to main memory directly through membus is the default setting. This configuration can be changed with options like `--dma-enable`, `--add-accel-private-cache` and `--add-accel-shared-cache`. These options are parsed in `devices.py` to connect the memory system.

## Step 8: Run more test cases on NVDLA with this framework
In this repo, we have made it feasible to run more tests on NVDLA software simulation flow OTHER THAN the sanity tests provided with NVDLA. For more details, please refer to `bsc-util/nvdla_utilities/README.md`.

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

## License
[MIT](https://choosealicense.com/licenses/mit/)
