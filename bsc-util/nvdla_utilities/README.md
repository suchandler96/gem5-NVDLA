# NVDLA VP Debug Info Conversion and Verification Utility

This is a preprocessing program to convert the debugging log of running [NVDLA Virtual Platform](http://nvdla.org/vp.html) to NVDLA register transaction traces, which can be fed into the NVDLA hardware verification process as introduced [in NVDLA documentation](http://nvdla.org/hw/v1/integration_guide.html#running-a-test-using-make), or with the verilator verification flow on the bottom of that webpage. Below will show how to use this utility to:
1. convert a piece of sample VP debug info to a NVDLA trace file;
2. verify the converted register traces with the verilator verification flow by comparing their read and write memory traces with the VP memory traces respectively.

## Terminologies
**VP**: NVDLA virtual platform

**NVDLA register (transaction) traces / register control sequence**: A sequence of commands composed of "read_reg", "write_reg", "wait" that configures NVDLA by writing values to its registers directly. It also involves some other CPU activities like waiting for interrupts from NVDLA. More details about this format can be found [here](http://nvdla.org/hw/v1/integration_guide.html#test-format).

**Memory traces**: A sequence of memory addresses that NVDLA accesses. These accesses include input data, NN model parameters, output data of some NN layers, etc. NVDLA accesses the memory in a 64-byte-aligned and 64-byte-long manner, so keeping track of the address of the first byte of each 64-byte request would be enough. Can be further divided into memory reads and writes.

## Features & known problems
1. This utility is coded for NVDLA hardware at branch `nvdlav1` with revision number 8e06b1b9d85aab65b40d43d08eec5ea4681ff715.
2. Currently, the exact data layout is not supported in this conversion utility, i.e., the converted register trace can only be run with fake data. But this could be done by picking out the read-only addresses with `get_input_addresses()` and `get_output_addresses()` in `GetAddrAttrAndMatch.py` and writing a script to capture their values by going through the output debug info of VP.
3. The verilator verification flow and VP may have different interrupt handling mechanisms. For the former one, one example can be these [lines of codes from line 323 to 334](https://github.com/nvdla/hw/blob/nvdlav1/verif/traces/traceplayer/googlenet_conv2_3x3_int16/input.txn#L323), where 4 interrupts are captured and cleared in a **one-by-one** fashion. Whereas for VP, one will see multiple bits of ones in the interrupt status register (NVDLA `0x000c`) detected and cleared **as a whole** in the cropped NVDLA traces in the LeNet example provided with this utility. To handle this discrepancy, a special NVDLA register transaction command other than `read_reg`, `write_reg`, or `wait` is added, which is named `until`. `until $reg $val` means the simulation flow will continuously read register `$reg` until it gives a read result of `$val`. The command is added to replace the original `wait` command to handle interrupts, because NVDLA interrupts can be raised by multiple hardware components, and each component is responsible for a certain bit in NVDLA register `0x000c`. So the `until` command is designed to match the interrupt handling mechanism of VP. To interpret this new command in the original NVDLA verilator verification flow, patch files of [`nvdla/hw/verif/verilator/input_txn_to_verilator.pl`](https://github.com/nvdla/hw/blob/nvdlav1/verif/verilator/input_txn_to_verilator.pl) and [`nvdla.cpp`](https://github.com/nvdla/hw/blob/nvdlav1/verif/verilator/nvdla.cpp) are provided, so that users can apply the patches to the files and run the converted NVDLA register traces in the original flow. The modified commands are STILL COMPATIBLE with the original interrupt handling mechanism, i.e., the `wait` command and the traces provided in `nvdla/hw/verif/` can still be run after applying the patch.
4. For NVDLA register `0xa004`, which is a control register related to the "ping-pong register group" working mechanism, may have some read mismatches in the verilator verification process after conversion. Although not 100% sure, this may be caused by some discrepancies between NVDLA SystemC model and verilog model, because the counterpart registers for other computing components do not see such mismatches. But ignoring the mismatches will produce exactly the same memory traces in all the inference tasks that this utility has been tested against.
5. This utility has been tested against LeNet with 1x28x28 input images (e.g., MNIST), ResNet-18 with input size 3x32x32 (e.g., CIFAR-10), ResNet-18 with input size 3x224x224 (e.g., IMAGENET), and ResNet-50 with input size 3x224x224. The converted NVDLA register traces exhibit the same memory traces with the VP counterpart.
6. We have provided a script to automate the conversion from Caffe model to *.txn register control sequences and also memory traces. Go to section "Automated Script" for details.

## How to get VP debug info
1. Get a sample caffe NN model. Here we provide a pretrained LeNet inference model(`example_usage/LeNet/LeNet_caffe_model/`);

2. Compile the NN model with [prebuilt NVDLA compiler](https://github.com/nvdla/sw/tree/master/prebuilt/x86-ubuntu) using the following command:
```
    cd /path/to/nvdla/sw/prebuilt/x86/
    ./nvdla_compiler -o lenet --cprecision fp16 --configtarget nv_full --informat nchw --prototxt /path/to/LeNet_caffe_model/Lenet.prototxt --caffemodel /path/to/LeNet_caffe_model/lenet_iter_10000.caffemodel
```
The output file with a suffix of ".nvdla" (e.g., fast-math.nvdla) is the "loadable" to be loaded by NVDLA_runtime;

3. Follow the instructions at [NVDLA Virtual Platform documentation](http://nvdla.org/vp.html#running-the-virtual-simulator-from-docker) to run the docker version of VP. Note the commands in NVDLA documentation let the user mount the host machine's `/home` directory to the docker's, making it possible to transfer files between the host machine and docker container;
```
    # if run for the first time:
    docker pull nvdla/vp
    docker run -it -v /home:/home nvdla/vp

    # if one has run before, use docker exec instead of docker run to avoid another container to be instantiated
    cd /usr/local/nvdla
    export SC_LOG="outfile:sc.log;verbosity_level:sc_debug;csb_adaptor:enable;dbb_adaptor:enable;sram_adaptor:enable"
    mv /path/to/nvdla/sw/prebuilt/x86-ubuntu/fast-math.nvdla ./
    mv fast-math.nvdla lenet.nvdla      # simply a rename
    cp /path/to/gem5-NVDLA/bsc-util/nvdla_utilities/example_usage/LeNet/LeNet_caffe_model/eight_invert.pgm ./
    aarch64_toplevel -c aarch64_nvdla.lua
    # usr: root; passwd: nvdla
```
4. Inside the QEMU simulator, do the following;
```
    mount -t 9p -o trans=virtio r /mnt
    cd /mnt
    insmod drm.ko
    insmod opendla_1.ko
    ./nvdla_runtime --loadable lenet.nvdla --rawdump --image eight_invert.pgm
```
5. After the runtime program finishes, exit QEMU and move the `sc.log` file to somewhere in **the host computer's** file system. The sc.log file for the provided LeNet test case is around 2.9GB. But if the patch file in tip 7 below is applied, it can be reduced to ~100MB.
```
    # Press Ctrl+A and then press 'X' to exit QEMU
    mv sc.log /path/to/sc.log   # move somewhere on host machine
```
6. Users may refer to [this blog](https://medium.com/@anakin1028/run-lenet-on-nvdla-837a6fac6f8b) for more details to run a testcase on VP.

7. Usually, the VP debug info file is SO LARGE that it may overflow the whole hard disk space. To resolve this issue, a patch file is provided in `bsc-util/nvdla_utilities/modify_cmod`. This patch comments out most of the debugging output except those related to NVDLA register transactions and memory traces. Users may apply that patch with `git apply /path/to/modify_cmod` under the `nvdla/hw` path, and then rebuild `cmod` and `vp` according to instructions on [NVDLA VP documentation](http://nvdla.org/vp.html#download-the-virtual-simulator) (As for the building environment, the docker provided with VP would be enough). Reducing those IO's not only reduces hard disk usage, but also accelerates simulation process. If users want to try out testcases like ResNet-18/50 for 3x224x224 images, this step is crucial. The detailed process would be:
```
    # in the host machine, cd to nvdla/vp path
    # change the sources to update qemu repos according to [this thread](https://github.com/riscv-collab/riscv-gnu-toolchain/issues/280).
    touch ~/.gitconfig
    echo "[url \"https://git.qemu.org/git\"]" >> ~/.gitconfig
    echo "insteadOf = git://git.qemu-project.org" >> ~/.gitconfig
    git submodule update --init --recursive

    # If some submodules are still not able to be pulled, users may need to pull mannually.
    # For example, in my case, `QemuMacDrivers`, `qemu-palcode`, `skiboot`, `tlm2c` and `memory` failed, so I did the following:
    cd /path/to/nvdla/vp/libs/qbox/roms
    git clone https://git.qemu.org/QemuMacDrivers.git
    cd QemuMacDrivers/
    git checkout d4e7d7a

    cd ../
    git clone https://github.com/rth7680/qemu-palcode
    cd qemu-palcode/
    git checkout f3c7e44

    cd ../
    git clone https://git.qemu.org/skiboot.git
    cd skiboot/
    git checkout 762d008

    # re-download everything in tlm2c/
    cd /path/to/nvdla/vp/libs/
    rm -rf tlm2c/
    git clone git@github.com:nvdla/tlm2c.git
    cd tlm2c/
    git checkout c54ade0

    # re-download everything in memory/
    cd ../models/
    rm -rf memory/
    git clone git@github.com:nvdla/simple_memory.git
    mv simple_memory/ memory/
    cd memory/
    git checkout 1018f8

    
    # switch into nvdla/vp docker container
    # because cmod may lack dependencies in host machine
    cd /path/to/nvdla/hw
    git apply path/to/this/repo/bsc-util/nvdla_utilities/modify_cmod
    tools/bin/tmake -build cmod_top
    cmake -DCMAKE_INSTALL_PREFIX=build -DSYSTEMC_PREFIX=/usr/local/systemc-2.3.0/ -DNVDLA_HW_PREFIX=/home/YOUR_USR_NAME/path/to/nvdla/hw/ -DNVDLA_HW_PROJECT=nv_full -DCMAKE_BUILD_TYPE=Debug
    make
    make install
```
And an executable named `aarch64_toplevel` will appear under `vp/` directory. Use the path to the newly-built executable to replace the one used in [NVDLA VP documentation](http://nvdla.org/vp.html#running-the-virtual-simulator-from-docker) will work, i.e.,
```
    cd /usr/local/nvdla
    /path/to/nvdla/vp/aarch64_toplevel -c aarch64_nvdla.lua
```

## Convert VP debug info to NVDLA trace file and memory traces with the utility
1. Compile NVDLAUtil.cpp with any c++ compiler with c++11:
   ```
   g++ -std=c++11 NVDLAUtil.cpp -o NVDLAUtil
   ```
2. Type the following command to convert `sc.log` to NVDLA register traces:
    ```
    mkdir /path/to/nvdla/hw/verif/traces/traceplayer/lenet/
    ./NVDLAUtil -i /path/to/sc.log --print-reg-txn --function parse-vp-log --change-addr > /path/to/nvdla/hw/verif/traces/traceplayer/lenet/input.txn
    # Do not modify the output file names.
    ```
3. Type the following command to convert sc.log to NVDLA memory traces:
    ```
    ./NVDLAUtil -i /path/to/sc.log --print-mem-rd --function parse-vp-log --change-addr > /path/to/nvdla/hw/verif/traces/traceplayer/lenet/VP_mem_rd
    ./NVDLAUtil -i /path/to/sc.log --print-mem-wr --function parse-vp-log --change-addr > /path/to/nvdla/hw/verif/traces/traceplayer/lenet/VP_mem_wr
    ./NVDLAUtil -i /path/to/sc.log --print-mem-rd --print-mem-wr --function parse-vp-log --change-addr > /path/to/nvdla/hw/verif/traces/traceplayer/lenet/VP_mem_rd_wr
    ```
4. Generate memory trace description file for read-only variables, which will be used by a prefetching mechanism in the simulator
    ```
    python3 nvdla_utilities/match_reg_trace_addr/GetAddrAttrAndMatch.py --src-dirs /path/to/nvdla/hw/verif/traces/traceplayer/lenet/ --output-rd-only-var-log
    ```
5. Use '-h' option to get help for all the options for NVDLAUtil.

## Test generated register trace file and memory traces in gem5-NVDLA framework
1. Mount the disk image if not mounted
    ```
    cd /path/to/gem5-NVDLA
    python3 util/gem5img.py mount /path/to/gem5_linux_images/ubuntu-18.04-arm64-docker.img ./mnt
    sudo mkdir ./mnt/home/lenet     # doesn't matter if exists
    ```
2. Convert the NVDLA register trace file into binary format:
    ```
    cd /path/to/nvdla/hw/verif/verilator/
    git apply /path/to/gem5-NVDLA/bsc-util/nvdla_utilities/input_txn_to_verilator.pl.patch    # apply patch if haven't
    perl input_txn_to_verilator.pl ../traces/traceplayer/lenet/ ../traceplayer/lenet/trace.bin
    ```
3. Copy files needed by the scheduler to the disk image:
    ```
    sudo cp /path/to/nvdla/hw/verif/traces/traceplayer/lenet/rd_only_var_log ./mnt/home/lenet
    sudo cp /path/to/nvdla/hw/verif/traces/traceplayer/lenet/trace.bin ./mnt/home/lenet
    ```
4. Create checkpoint for simulation:
    ```
    # Suppose the user has already got the `my_validation_nvdla_single_thread` scheduler
    # under `./mnt/home/`, following README.md at root directory of this repo
    build/ARM/gem5.opt configs/example/arm/fs_bigLITTLE_RTL.py \
 	--big-cpus 0 --little-cpus 1 \
  	--cpu-type atomic --bootscript=configs/boot/hack_back_ckpt.rcS
    ```
5. Run simulation with various memory subsystem configurations (or so called parameter sweep):
    ```
    cd /path/to/gem5-NVDLA/bsc-util/nvdla_utilities/sweep
    python3 main.py --jsons-dir ../example_usage/experiments/jsons --output-dir ../example_usage/experiments/logs/ --cpt-dir ../../../m5out/cpt.xxxxxxxxxxxx/ --run-points --scheduler my_validation_nvdla_single_thread --params /home/lenet/trace.bin /home/lenet/rd_only_var_log --num-threads 8
    ```
6. Get a csv file summarizing the results of simulation under `~/`
    ```
    python3 get_sweep_stats.py --get-root-dir ../example_usage/experiments/logs/ --out-dir ~/ --out-prefix lenet_demo_sweep_
    ```

## Automated Script
This part provides a script to automate the process from "How to get VP debug info" to "Convert VP debug info to NVDLA trace file and memory traces with the utility". The script expects Caffe NN models as inputs, and will output the .txn file corresponding to the NN model, together with memory traces.

The script, `caffe2trace.py`, should be run in a `nvdla/vp` docker container. Before running, users should:
1. Run `apt update && apt install python3 expect` in the docker container for dependencies.
2. If users want to build the VP on themselves (section 7 in "How to get VP debug info"), the building process should be finished before running this script.
3. Mount the disk image to gem5-NVDLA/mnt.
4. An example usage would be:
    ```
    python3 caffe2trace.py --model-name lenet --caffemodel example_usage/LeNet/LeNet_caffe_model/lenet_iter_10000.caffemodel --prototxt example_usage/LeNet/LeNet_caffe_model/Lenet.prototxt
    ```
please provide absolute path if possible, and make sure to go through the argparse part of the script.

## Verify the converted NVDLA register trace file with verilator verification flow and get its memory traces
This step is to ensure the users that the NVDLA register trace file is extracted correctly, so that using the verification tools in `nvdla/hw` will lead to the same memory access behaviors. Several memory accesses very close to each other could have some minor differences in access order, but the number of times each address is accessed should preserve the same.
1. Verify the above converted register trace file like [other traces](https://github.com/nvdla/hw/tree/nvdlav1/verif/traces/traceplayer) (put the converted register trace in a standalone directory just beside other test cases) with the verilator flow, and capture the output on the terminal (e.g., with tee) with a command like
    ```
    cd /path/to/nvdla/hw
    git apply /path/to/gem5-NVDLA/bsc_util/nvdla_utilities/nvdla.cpp.patch

    cd /path/to/nvdla/hw/verif/verilator
    make run TEST=lenet | tee /path/to/put/lenet_nvdla_cpp_term_log
    ```
2. Use the utility to crop out read / write memory traces of this converted register trace. The command could be like:
    ```
    ./NVDLAUtil -i /path/to/put/lenet_nvdla_cpp_term_log --print-mem-rd --function nvdla-cpp-log2mem-trace > /path/to/lenet/converted/rd/mem/trace
    ./NVDLAUtil -i /path/to/put/lenet_nvdla_cpp_term_log --print-mem-wr --function nvdla-cpp-log2mem-trace > /path/to/lenet/converted/wr/mem/trace
    ```

3. Compare the read (and write) memory traces of the two versions. For example,
    ```
    ./NVDLAUtil -i1 /path/to/nvdla/hw/verif/traces/traceplayer/lenet/VP_mem_rd -i2 /path/to/lenet/converted/rd/mem/trace --function comp-mem-trace
    ./NVDLAUtil -i1 /path/to/nvdla/hw/verif/traces/traceplayer/lenet/VP_mem_wr -i2 /path/to/lenet/converted/wr/mem/trace --function comp-mem-trace
    ```
   The result should be "No differences are found". Users may also plot the memory traces to visualize the comparison.