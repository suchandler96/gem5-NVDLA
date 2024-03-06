# gem5-NVDLA
gem5-NVDLA is a specialized version of gem5-RTL that is designed to be used with the NVDLA verilog model. For gem5+RTL framework that this project is based on, see its original repo [gem5-RTL](https://gitlab.bsc.es/glopez/gem5-rtl.git). Here we are showing citation for gem5+RTL, but we have rewritten the installation process to make it more newcomer-friendly.

Apart from specialized adaption of gem5 memory system for NVDLA, this project also enables the conversion from any NVDLA-supported caffe NN model to NVDLA register transaction traces (i.e., something like input.txn [here](https://github.com/nvdla/hw/tree/nvdlav1/verif/traces/traceplayer)). The code and detailed usage of this conversion utility can be found in `bsc-util/nvdla_utilities`.

The NVDLA (NVIDIA Deep Learning Accelerator) is a full-stack utility demonstrating how a industry-level accelerator works. With its maintenance stopped in 2018, the public are on their own to make it work from compilation to runtime. Previously the only way to run NVDLA is to instantiate on an FPGA and run NNs physically on board, using the compiler and runtime provided with NVDLA. This, however, leaves memory architects unable to explore memory subsystem in AI accelerator systems, since they require **a simulator** to get statistics with different memory configuration. The aim of this repo is thus to bridge this gap. On one hand, it integrates solutions to all the undocumented bugs and usages required to run NVDLA compiler, runtime, and virtual platform. On the other hand, it provides new capabilities to run NVDLA in a simulator, i.e., to apply scheduling algorithms, SPM allocation and prefetching mechanisms, which enables further exploration with NVDLA.

# Code Structure
Here we list the structure of codes apart from the gem5 skeleton:
1. `ext/rtl/model_nvdla/` includes the wrapper class of NVDLA and embedded SPM (`wrapper_nvdla.cc`) and the logic of converting between gem5 packets and NVDLA AXI requests (`axiResponder.cc`);
2. `src/rtl/rtlNVDLA.cc` puts the behavior of NVDLA as a gem5 object, e.g., sending and receiving memory requests;
3. `src/dev/dma_nvdla.cc` describes the behavior of DMA engine;
4. `bsc-util/` puts all the schedulers for invoking NVDLA in simulation;
5. `bsc-util/nvdla_utilities/` puts all the compilation-related stuffs for NVDLA, including single-NVDLA and multi-NVDLA compilation scripts.
6. `bsc-util/nvdla_utilities/sweep/` includes scripts for parameter sweeps and data collection.
7. `bsc-util/nvdla_utilities/example_usage/` includes caffe models, compiled register traces & other log files for multiple testcases, and example sweeping parameter configuration json files.

# Installation and Sample Tests
Running simulation in a docker environment is strongly recommended as it saves time and effort to install dependencies. However, due to the following environment requirements, we are faced with difficulties to integrate the whole flow (i.e., compiling Caffe NNs into NVDLA register trace, simulation point creation, and simulation) into one docker image, so that commands have to be run from 2 different docker environments as well as a host machine with sudo privilege:
1. Running NVDLA Virtual Platform requires a `edwinlai99/advp` container;
2. Running full-system simulation involves moving testcase files to a disk image, which requires sudo privilege to the host machine;
3. Building and running gem5 requires a specialized environment (in a `edwinlai99/gem5_nvdla_env` container).

Fortunately, we provide docker images with all dependencies installed and one-step scripts for each phase (and also steps to build these docker images in `bsc-util/nvdla_utilities/BUILD.md`), so that manual efforts can be minimized. Throughout the whole repo, each example command will be prefixed with `(docker_image_name)#` or `$` to distinguish the running environment. Lines with only a `#` symbol are comments.

## Dependencies
Gurobi needs to be used on the host machine, and then modify the paths in `bsc-util/nvdla_utilities/match_reg_trace_addr/CVSRAMAlloc/Makefile`. If not installed, the activation-pinning and mixed-pinning strategies cannot run properly.


## Step 1: Clone Our Repository
We strongly suggest putting `gem5-nvdla/` and `gem5_linux_images/` right under the `~/` directory so that the paths in the commands below and path variables in our prebuilt docker images wouldn't need extra modification. If not, please check the `root/.bashrc` file in a `gem5_nvdla_env` container.
```
$ cd ~/
$ git clone https://github.com/suchandler96/gem5-NVDLA.git
$ mv gem5-NVDLA/ gem5-nvdla     # simply a rename
$ mkdir nvdla                   # to put testcases in the steps afterwards
$ cd gem5-nvdla/
$ mkdir ext/rtl/model_nvdla/verilator_nvdla
$ mkdir mnt
$ git apply ban_git_hook_install_Ofast.patch      # to prevent a git_hook bug in case of dubious ownership of the repo
$ docker pull edwinlai99/advp:v1
$ docker pull edwinlai99/gem5_nvdla_env:v3
```

## Step 2: Prepare gem5 Disk Image and Kernel
Since this framework runs in full system mode of gem5, one needs to prepare a linux kernel and disk image. One choice is to use those provided in [gem5 doc ARM fs binaries](https://www.gem5.org/documentation/general_docs/fullsystem/guest_binaries). Organize the files downloaded in a hierarchy below:
```
~/
|-- gem5_linux_images/
    |-- ubuntu-18.04-arm64-docker.img
    |-- aarch-system-20220707/
        |-- binaries/
        |-- disks/
        ......
```
If `gem5_linux_images` is not put right under the `~/` directory:
- the global variables `default_kernel` and `default_disk` in `configs/example/arm/fs_bigLITTLE_RTL.py` has also to change correspondingly.
- change the `M5_PATH` variable in `/root/.bashrc` in the `gem5_nvdla_env` docker image. Each time a new container is instantiated, this variable should be modified, or keep using the same container.
- The default values for Argparse argument "disk_image" in `nvdla_utilities/sweep/main.py` should be changed.

## Step 3: Build Schedulers for Full-system Simulation
NVDLA workloads are simulated in the full-system mode of gem5, so there should be a CPU binary that invokes the accelerators. The architecture used for simulation is ARM-based, so cross-compiling is necessary. The binaries, whose source codes are in `bsc-util/*.c`, should be cross-compiled with `aarch64-linux-gnu-g++-9`. In addition, an assembly file (`util/m5/src/abi/arm64/m5op.S`) defining the gem5-specific operations should also be cross-compiled. So one may compile them separately into *.o's and link them together. An example compilation process would be:
```
# The version of cross-compiling toolchain does not matter very much.
It depends on the user's OS version. Since we are tested on Ubuntu 18.04, gcc-7 is used. But gcc-9 may be more convenient on systems >= 20.04
$ sudo apt install gcc-9-aarch64-linux-gnu g++-9-aarch64-linux-gnu
$ cd gem5-nvdla/bsc-util/
$ aarch64-linux-gnu-g++-9 my_validation_nvdla_single_thread.cpp -c -o my_validation_nvdla_single_thread.o -I../include -fPIC -O3 --static -std=c++11
$ aarch64-linux-gnu-g++-9 ../util/m5/src/abi/arm64/m5op.S -c -o m5op.o -I../include -fPIC -O3 --static -std=c++11
$ aarch64-linux-gnu-g++-9 my_validation_nvdla_single_thread.o m5op.o -o my_validation_nvdla_single_thread -fPIC -O3 --static -std=c++11
$ python3 ../util/gem5img.py mount ~/gem5_linux_images/ubuntu-18.04-arm64-docker.img ../mnt
$ sudo mv my_validation_nvdla_single_thread ../mnt/home/
# ... (do the same thing to pipeline_execute scheduler)
# must move to ../mnt/home/ because bsc-util/nvdla_utilities/sweep/main.py will look for the binaries there
```

## Step 4: Build gem5 in a `gem5_nvdla_env` Docker Container
```
$ docker run --net=host -v ~/:/home -it --rm edwinlai99/gem5_nvdla_env:v3
(gem5_nvdla_env)# cp /usr/local/nvdla/hw/outdir/nv_full/verilator/VNV_nvdla__ALL.a /home/gem5-nvdla/ext/rtl/model_nvdla/verilator_nvdla/
(gem5_nvdla_env)# cp /usr/local/nvdla/hw/outdir/nv_full/verilator/VNV_nvdla.h /home/gem5-nvdla/ext/rtl/model_nvdla/verilator_nvdla/
(gem5_nvdla_env)# cd /home/gem5-nvdla/ext/rtl/model_nvdla/verilator_nvdla/
(gem5_nvdla_env)# mv VNV_nvdla__ALL.a libVNV_nvdla__ALL.a       # rename
(gem5_nvdla_env)# cd /home/gem5-nvdla/
# check Makefile to see the number of threads options before compilation
(gem5_nvdla_env)# make nvdla
(gem5_nvdla_env)# exit
```

## Step 5: Generate Data Points for Simulation with an Example Testcase
Note the commands below will write files into the disk image in `gem5_linux_images/`.
```
$ mkdir -p ~/nvdla/traces   # make a directory to store the simulation files
$ cd ~/gem5-nvdla/bsc-util/nvdla_utilities/sweep/
$ cp -r ../example_usage/traces/lenet ~/nvdla/traces/
$ cp -r ../example_usage/experiments/jsons_tiny/ ~/nvdla/traces/lenet/
$ python3 main.py --jsons-dir ~/nvdla/traces/lenet/jsons_tiny/ --out-dir ~/nvdla/traces/lenet/logs/ --vp-out-dir ~/nvdla/traces/lenet/ --sim-dir /home/lenet/ --model-name lenet --gen-points --num-threads 24 --scheduler my_validation_nvdla_single_thread --home /home
```

## Step 6: Run Simulation in a `gem5_nvdla_env` Docker Container and Gather Results
```
$ docker run --net=host -v ~/:/home -it --rm edwinlai99/gem5_nvdla_env:v3
(gem5_nvdla_env)# cd /home/gem5-nvdla/bsc-util/nvdla_utilities/sweep/
(gem5_nvdla_env)# python3 main.py --jsons-dir /home/nvdla/traces/lenet/jsons_tiny/ --out-dir /home/nvdla/traces/lenet/logs/ --vp-out-dir /home/nvdla/traces/lenet/ --sim-dir /home/lenet/ --model-name lenet --run-points --num-threads 24 --scheduler my_validation_nvdla_single_thread
# wait until the simulation ends... It may take roughly 30-60 seconds depending on the computer's performance
(gem5_nvdla_env)# exit

$ cd ~/gem5-nvdla/bsc-util/nvdla_utilities/sweep/
$ python3 get_sweep_stats.py -d ~/nvdla/traces/lenet/logs/ -j ~/nvdla/traces/lenet/jsons_tiny/ -p lenet_test --out-dir ~/
# Then a file named "lenet_test_summary.csv" would appear in ~/
```
Some results close to the table below are desired:

| sweep_id | dma-enable | add-accel-private-cache | use-fake-mem | pft-enable | nvdla_cycles[0] | memory_cycles[0] |
|----------|------------|-------------------------|--------------|------------|-----------------|------------------|
| 0        | FALSE      | FALSE                   | FALSE        | TRUE       | 113221          | 76946            |
| 1        | FALSE      | FALSE                   | TRUE         | TRUE       | 71137           | 0                |
| 2        | FALSE      | TRUE                    | FALSE        | TRUE       | 89407           | 48231            |  
| 3        | TRUE       | FALSE                   | FALSE        | TRUE       | 73459           | 31553            |

For other workloads provided in our examples, steps 5-6 should be done again for that workload. If a new NN is to be compiled, Please also refer to the "Compile a Single NN" and "Compile a Pipelined Multibatch NN" sections. For users who want to customize our toolchain, please refer to `bsc-util/nvdla_utilities/BUILD.md`.

Our framework provides far better simulation performance (18x-22x simulation speed) than gem5-rtl and the original NVDLA verilator verification flow, approaching the performance of its C-model. Under an ideal memory setting, Resnet-50 can be simulated within 2 hours. Our optimizations include:
- Reduce IO using a compressed log format and IO buffer (`ext/rtl/model_nvdla/verilator_nvdla/axiResponder.hh`);
- Adopt later version of clang and verilator to compile NVDLA RTL model;
- Reduce some UNOPTFLAT warnings at the verilog code level;
- Apply `-O3` to `.v` -> `.cpp` compilation using verilator;
- Apply `-O3 -Ofast` to `.cpp` -> `.a` compilation using clang;
- Apply profiling-guided optimization (PGO) to `.cpp` -> `.a` compilation using clang (extra 40% improvement);
- Apply `-Ofast` to the compilation of `gem5.fast`;
- Apply PGO to gem5 compilation (extra 10% improvement).

# Compile a Single NN
This section provides the process to generate the files in `bsc-util/nvdla_utilities/example_usage/lenet/`.
```
$ docker run -it --rm -v ~/:/home edwinlai99/advp:v1
(advp)# cd /home/gem5-nvdla/bsc-util/nvdla_utilities/
(advp)# python3.6 caffe2trace.py --model-name lenet --caffemodel example_usage/caffe_models/lenet/lenet_iter_10000.caffemodel --prototxt example_usage/caffe_models/lenet/Lenet.prototxt --out-dir /home/nvdla/traces/lenet/
```
Then the log files and `*.txn` register trace will appear in `/home/nvdla/traces/lenet/`.

# Compile a Pipelined Multibatch NN
Our repo provides a scheduler that can map multiple batches of NN inference task of a single model onto multiple simulated NVDLAs. This script, `pipeline_compile.py`, thus helps to compile multiple prototxt files at the same time. This script expects the user to manually split a Caffe NN into multiple `*.prototxt` files (the `*.caffemodel` does not need to be modified), each of which corresponds to a pipeline stage. These `.prototxt` files should be provided to the script in the order of pipeline stages. Users are expected to use a subclass of `PipelineRemapper` in `match_reg_trace_addr/remap.py` when doing parameter sweeps for pipelined workloads. See below for usage:
```
$ docker run -it --rm -v ~/:/home edwinlai99/advp:v1
(advp)# cd /home/gem5-nvdla/bsc-util/nvdla_utilities/
(advp)# python3.6 pipeline_compile.py --model-name lenet --caffemodel example_usage/caffe_models/lenet/lenet_iter_10000.caffemodel --prototxts /home/gem5-nvdla/bsc-util/nvdla_utilities/example_usage/traces/lenet_pipeline/stage_1/lenet_stage1.prototxt /home/gem5-nvdla/bsc-util/nvdla_utilities/example_usage/traces/lenet_pipeline/stage_2/lenet_stage2.prototxt --out-dir /home/nvdla/traces/lenet_pipeline/
```

## Developer Tips
If you use CLion as your IDE, you may:
- Include the CMakeLists.txt [here](https://github.com/suchandler96/NVDLAUtil/blob/master/miscellaneous/CMakeLists.txt) in the root of this project so that code indexing works as if it were a normal CMake project.
- Right click on `configs/` -> mark directory as -> Python Namespace Package.
- Go to "settings" -> Python Interpreter -> Expand the menu bar of Python Interpreter -> Show all -> (Select your python interpreter) -> Click on the "Show Interpreter Path" button -> Add `~/gem5-nvdla/configs/` and `~/gem5-nvdla/src/python/` to PYTHONPATH.

# License
[MIT](https://choosealicense.com/licenses/mit/)

