# gem5-NVDLA
gem5-NVDLA is a specialized version of gem5-RTL that is designed to be used with the NVDLA verilog model. For gem5+RTL framework that this project is based on, see its original repo [gem5-RTL](https://gitlab.bsc.es/glopez/gem5-rtl.git). Here we are showing citation for gem5+RTL, but we have rewritten the installation process to make it more newcomer-friendly.

Apart from specialized adaption of gem5 memory system for NVDLA, this project also enables the conversion from any NVDLA-supported caffe NN model to NVDLA register transaction traces (i.e., something like input.txn [here](https://github.com/nvdla/hw/tree/nvdlav1/verif/traces/traceplayer)). The code and detailed usage of this conversion utility can be found in `bsc-util/nvdla_utilities`.

The NVDLA (NVIDIA Deep Learning Accelerator) is a full-stack utility demonstrating how a industry-level accelerator works. With its maintenance stopped in 2018, the public are on their own to make it work from compilation to runtime. Previously the only way to run NVDLA is to instantiate on an FPGA and run NNs physically on board, using the compiler and runtime provided with NVDLA. This, however, leaves memory architects unable to explore memory subsystem in AI accelerator systems, since they require **a simulator** to get statistics with different memory configuration. The aim of this repo is thus to bridge this gap. On one hand, it integrates solutions to all the undocumented bugs and usages required to run NVDLA compiler, runtime, and virtual platform. On the other hand, it provides new capabilities to run NVDLA in a simulator, i.e., to apply scheduling algorithms and hardware prefetching mechanisms, which enables further exploration with NVDLA.

Currently there are only a handful of works available on system integration between accelerators and cycle accurate simulators. [gem5-RTL](https://gitlab.bsc.es/glopez/gem5-rtl.git), the skeleton that this repo is based on, focuses on integrating arbitrary RTL models with gem5 simulator. It only provides simple connection between gem5 and NVDLA, i.e., through memory bus, lacking various connections like through cache hierarchy and DMA with private scratchpad memories, let alone hardware prefetching mechanisms that this repo features. Most importantly, its experiments are only conducted against the sanity tests provided with NVDLA repo, which are far from real world neural networks in terms of sizes and structures. [SMAUG](https://github.com/harvard-acc/smaug) is a scheduling framework to map NN workloads to gem5-aladdin systems. The accelerator model they employ is a set of parameters based on Aladdin, which is an HLS-like interpretation mechanism. Compared with NVDLA RTL model used in this repo, Aladdin-based accelerator description lacks accuracy and interpretability. Furthermore, SMAUG employs a redundant and repetitive "tiling - computation - untiling" procedure for every operator, instead of generating addresses from the accelerator model, introducing tremendous excessive memory accesses.

In short, this repo makes the following key contributions:
1. Enable the compilation of arbitrary caffe NN models to NVDLA register transaction traces, especially handling interrupts correctly, so as to run NVDLA in simulators.
2. Enable data transfer between gem5 memory system and NVDLA cores via DMA (while in gem5-RTL, only direct connection with memory bus is possible), and use scratchpad memories to buffer read / write data.
3. Enable hardware prefetching mechanism (instead of being controlled by software) in the simulator.
4. Enable scheduling multiple NVDLA cores at a sub-NN level to run a single NN. For example, a pipeline-parallel scheduler is implemented.

# [gem5+RTL](https://gitlab.bsc.es/glopez/gem5-rtl.git)
gem5+RTL is a flexible framework that enables the simulation of RTL models inside the gem5 full-system software simulator. The framework allows easy integration of RTL models on a simulated system-on-chip (SoC) that is able to boot Linux and run complex multi-threaded and multi-programmed workloads.

If you use gem5+RTL in your research please cite the gem5+RTL article:
```
gem5 + rtl: A Framework to Enable RTL Models Inside a Full-System Simulator. Guillem López-Paradís, Adrià Armejach, Miquel Moretó. ICPP 2021: 29:1-29:11
```
# Code Structure
Here we list the structure of codes apart from the gem5 skeleton:
1. `ext/rtl/model_nvdla/` includes the wrapper class of NVDLA and embedded SPM (`wrapper_nvdla.cc`) and the logic of converting between gem5 packets and NVDLA AXI requests (`axiResponder.cc`);
2. `src/rtl/rtlNVDLA.cc` puts the behavior of NVDLA as a gem5 object, e.g., sending and receiving memory requests;
3. `src/dev/dma_nvdla.cc` describes the behavior of DMA engine;
4. `bsc-util/` puts all the schedulers for invoking NVDLA in simulation;
5. `bsc-util/nvdla_utilities/` puts all the compilation-related stuffs for NVDLA.

# Installation
## Step 0: Install dependencies
Verilator v3.912, clang v3.4 (install from pre-built binary), clang v6.0.0 (through apt-get), aarch64-linux-gnu-gcc-7 (and g++) (apt-get), SystemC 2.3.0, perl (v5.10 required on NVDLA website, but v5.26.1 was tested ok, with IO-Tee, yaml packages installed), jdk 1.7 (tested with jdk 1.7.0_80) and other dependencies that this repo refers the user to other websites. The following installation process is verified on Ubuntu 18.04.6.

## Step 1: Clone gem5-NVDLA repo
```
# Download repo and make sure you are on branch `DMAEnable`
git clone https://github.com/suchandler96/gem5-NVDLA.git
```
## Step 2: Install Verilator
Go to [Verilator webpage](https://verilator.org/guide/latest/install.html) and follow the instructions. Verilator is required to build the model from SV to C++. Note verilator should be built with clang and clang++ v3.4 to keep consistent with compilation of NVDLA verilog model to verilated-C model.
```
# Installation example
git clone https://github.com/verilator/verilator

unset VERILATOR_ROOT     # For bash
git checkout stable      # Use most recent stable release
git checkout v{version}  # Switch to specified release version

# ensure the clang and clang++ are v3.4 just installed
export CC=clang
export CXX=clang++

autoconf 
./configure --prefix /usr/local/verilator_3.912 ---> FOR NVDLA 3.912

make -j (e.g. 2|4|8) 
[sudo] make install

# create symbolic link for include directory
sudo ln -s /usr/local/verilator_3.912/share/verilator/include/ /usr/local/verilator_3.912/include

# Write environment variables to ~/.bashrc, change path to verilator accordingly
echo >> ~/.bashrc
echo "# for verilator 3.912 (gem5-NVDLA uses verilator 3.912)" >> ~/.bashrc
echo "export PATH=/usr/local/verilator_3.912/bin:$PATH" >> ~/.bashrc
echo "export VERILATOR_ROOT=/usr/local/verilator_3.912" >> ~/.bashrc
echo "export C_INCLUDE_PATH=/usr/local/verilator_3.912/include/:$C_INCLUDE_PATH" >> ~/.bashrc
echo "export CPLUS_INCLUDE_PATH=/usr/local/verilator_3.912/include/:$CPLUS_INCLUDE_PATH" >> ~/.bashrc
echo >> ~/.bashrc
source ~/.bashrc
```
## Step 3: Prepare gem5 disk image and kernel
Since this framework runs in full system mode of gem5, one needs to prepare a linux kernel and disk image. One choice is to use those provided in [gem5 doc ARM fs binaries](https://www.gem5.org/documentation/general_docs/fullsystem/guest_binaries). Using a browser, one needs to download *Latest Linux Kernel Image / Bootloader* and *Latest Linux Disk Images* by right-clicking on the links and clicking "Save link as". Once prepared, update the paths to them in the first few lines in `configs/example/arm/fs_bigLITTLE_RTL.py`. Files can be moved to the disk image by first mounting it using the `util/gem5img.py` script, as will be shown in step 4.4. Remember checkpoints have to be regenerated after adding new files to the disk image. Organize the files downloaded in a hierarchy below:
```
gem5_linux_images/
|-- ubuntu-18.04-arm64-docker.img
|-- aarch-system-20220707/
    |-- binaries/
    |-- disks/
    ......
```
And add them to `M5_PATH`:
```
echo "export M5_PATH=/path/to/gem5_linux_images/:/path/to/gem5_linux_images/aarch-system-20220707:/path/to/gem5_linux_images/aarch-system-20220707/binaries/:$M5_PATH" >> ~/.bashrc
```
## Step 4: Build the RTL C++ model using Verilator

1. Download repo and follow instructions at [NVDLA](http://nvdla.org/hw/v1/integration_guide.html) and switch to `nvdlav1` branch to obtain the model and compiling. Make sure you use verilator v3.912 and clang 3.4. RAM requirements > 24GB.
2. Before building NVDLA for verilator verification, apply some bugfixes (for reasons see [this pull request](https://github.com/nvdla/hw/pull/312/files) and [this issue](https://github.com/nvdla/hw/issues/219)):
```
    git clone git@github.com:nvdla/hw.git
    cd hw/
    git apply /path/to/gem5-NVDLA/bsc-util/nvdla_utilities/nvdla_hw_bugfixes.patch
```
3. Run some sanity tests provided in `nvdla/hw/verif/traces` as introduced at their website to ensure correct compilation.

4. Make sure the NVDLA register transaction traces have been automatically generated after running a sanity test (like `nvdla/hw/outdir/nv_full/verilator/test/YOUR_SANITY_TEST/trace.bin`) and move them to the disk image that will be used in gem5 full system simulation.
```
    # cd to the root directory of this repo
    mkdir mnt
    python3 util/gem5img.py mount /path/to/gem5_linux_images/ubuntu-18.04-arm64-docker.img ./mnt

    # copy the NVDLA register transaction traces to the disk image
    mkdir ./mnt/home/YOUR_SANITY_TEST/
    sudo cp nvdla/hw/outdir/nv_full/verilator/test/YOUR_SANITY_TEST/trace.bin ./mnt/home/YOUR_SANITY_TEST/
```
5. Move the contents in the NVDLA verilator output folder (`nvdla/hw/outdir/nv_full/verilator/*`) to `ext/rtl/model_nvdla/verilator_nvdla/`
```
    cp /path/to/nvdla/hw/outdir/nv_full/verilator/* ext/rtl/model_nvdla/verilator_nvdla/
```
6. To create the library one needs to use the same clang v3.4 with stdlib of gcc v6:
```
    # Obtain libVerilatorNVDLA.so
    # make sure the output of command `clang++ -v` is v3.4
    cd ext/rtl/model_nvdla
    make create_library_vcd
```
## Step 5: Compile gem5
Please use clang v6.0.0 to compile gem5. Note sometimes gem5 could not correctly detect Anaconda environments, so please make sure to use system python3.
```
# Compile
CC=clang-6.0 CXX=clang++-6.0 scons -j2 build/ARM/gem5.opt

# An alternative command in case the above one doesn't work (specify explicitly where these prerequisites are):
CC=clang-6.0 CXX=clang++-6.0 /usr/bin/python3 $(which scons) build/ARM/gem5.opt PYTHON_CONFIG=/usr/bin/python3-config PROTOC=/usr/local/bin/protoc -j2
```
One can modify the above commands to build gem5 according to the configuration. Once any source code under `src/` or `ext/` is modified, one can run `make nvdla` to combine section 4.6 and section 5 to recompile.

Before executing, one needs to set up the LIBRARY_PATH to the place where the shared library of the RTL Object resides:
``` 
echo >> ~/.bashrc
echo "# for gem5-NVDLA" >> ~/.bashrc
echo "export LD_LIBRARY_PATH=/path/to/this/repo/ext/rtl/model_nvdla/:$LD_LIBRARY_PATH" >> ~/.bashrc
echo >> ~/.bashrc
```
## Step 6: Run a workload invoking NVDLA
The NVDLA workload should be defined in the full system simulation. This is done through cross-compiling an ARM CPU binary that will invoke the NVDLA accelerator. The binary, whose source code is in `bsc-util/*.c`, should be cross-compiled with `aarch64-linux-gnu-g++-7`. In addition, an assembly file (`util/m5/src/abi/arm64/m5op.S`) defining the gem5-specific operations should also be cross-compiled. So one may compile them separately into *.o's and link them together. An example compilation process would be:
```
cd bsc-util/
aarch64-linux-gnu-g++-7 my_validation_nvdla_single_thread.cpp -c -o my_validation_nvdla_single_thread.o -I../include -fPIC -O3 --static -std=c++11
aarch64-linux-gnu-g++-7 ../util/m5/src/abi/arm64/m5op.S -c -o m5op.o -I../include -fPIC -O3 --static -std=c++11
aarch64-linux-gnu-g++-7 my_validation_nvdla_single_thread.o m5op.o -o my_validation_nvdla_single_thread -fPIC -O3 --static -std=c++11
sudo mv my_validation_nvdla_single_thread ../mnt/home/
# must move to ../mnt/home/ because bsc-util/nvdla_utilities/sweep/main.py will look for the binary there
```
To run a simulation, one can use the already prepared `configs/boot/hack_back_ckpt.rcS` and `configs/example/arm/fs_bigLITTLE_RTL.py` configuration scripts to generate checkpoints and execute simulation:
```
# To create a checkpoint
# cd to root directory of this repo
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
In this repo, we have made it feasible to run more tests on NVDLA software simulation flow OTHER THAN the sanity tests provided with NVDLA. For more details, please refer to [`bsc-util/nvdla_utilities/README.md`](https://github.com/suchandler96/gem5-NVDLA/tree/DMAEnable/bsc-util/nvdla_utilities).

## Contributing
Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

## License
[MIT](https://choosealicense.com/licenses/mit/)
