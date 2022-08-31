# NVDLA VP Debug Info Conversion and Verification Utility

This is a preprocessing program to convert the debugging log of running [NVDLA Virtual Platform](http://nvdla.org/vp.html) to NVDLA register transaction traces, which can be fed into the NVDLA hardware verification process as introduced [in NVDLA documentation](http://nvdla.org/hw/v1/integration_guide.html#running-a-test-using-make), or with the verilator verification flow on the bottom of that webpage. Below will show how to use this utility to:
1. convert a piece of sample VP debug info to a NVDLA trace file;
2. verify the converted register traces with the verilator verification flow by comparing their read and write memory traces with the VP memory traces respectively.

## Terminologies
**VP**: NVDLA virtual platform

**NVDLA register (transaction) traces**: A sequence of commands composed of "read_reg", "write_reg", "wait" that configures NVDLA by writing values to its registers directly. It also involves some other CPU activities like waiting for interrupts from NVDLA. More details about this format can be found [here](http://nvdla.org/hw/v1/integration_guide.html#test-format).

**Memory traces**: A sequence of memory addresses that NVDLA accesses. These accesses include input data, NN model parameters, output data of some NN layers, etc. NVDLA accesses the memory in a 64-byte-aligned and 64-byte-long manner, so keeping track of the address of the first byte of each 64-byte request would be enough. Can be further divided into memory reads and writes.

## Features & known problems
1. This utility is coded for NVDLA hardware at branch `nvdlav1` with revision number 8e06b1b9d85aab65b40d43d08eec5ea4681ff715.
2. Currently, the exact data layout is not supported in this conversion utility, i.e., the converted register trace can only be run with fake data.
3. The verilator verification flow and VP may have different interrupt handling mechanisms. For the former one, one example can be these [lines of codes from line 323 to 334](https://github.com/nvdla/hw/blob/nvdlav1/verif/traces/traceplayer/googlenet_conv2_3x3_int16/input.txn#L323), where 4 interrupts are captured and cleared in a **one-by-one** fashion. Whereas for VP, one will see multiple bits of ones in the interrupt status register (NVDLA `0x000c`) detected and cleared **as a whole** in the cropped NVDLA traces in the LeNet example provided with this utility. To handle this discrepancy, a special NVDLA register transaction command other than `read_reg`, `write_reg`, or `wait` is added, which is named `until`. `until $reg $val` means the simulation flow will continuously read register `$reg` until it gives a read result of `$val`. The command is added to replace the original `wait` command to handle interrupts, because NVDLA interrupts can be raised by multiple hardware components, and each component is responsible for a certain bit in NVDLA register `0x000c`. So the `until` command is designed to match the interrupt handling mechanism of VP. To interpret this new command in the original NVDLA verilator verification flow, patch files of [`nvdla/hw/verif/verilator/input_txn_to_verilator.pl`](https://github.com/nvdla/hw/blob/nvdlav1/verif/verilator/input_txn_to_verilator.pl) and [`nvdla.cpp`](https://github.com/nvdla/hw/blob/nvdlav1/verif/verilator/nvdla.cpp) are provided, so that users can apply the patches to the files and run the converted NVDLA register traces in the original flow. The nvdla integration of this repo has already been adapted to this new command. The modified commands are STILL COMPATIBLE with the original interrupt handling mechanism, i.e., the `wait` command and the traces provided in `nvdla/hw/verif/`.
4. For NVDLA register `0xa004`, which is a control register related to the "ping-pong register group" working mechanism, may have some read mismatches in the verilator verification process after conversion. Although not 100% sure, this may be caused by some discrepancies between NVDLA SystemC model and verilog model, because the counterpart registers for other computing components do not see such mismatches. But ignoring the mismatches would lead to exactly the same memory traces in all the inference tasks that this utility has been tested against.
5. This utility has been tested against LeNet with 1x28x28 input images (e.g., MNIST), ResNet-18 with input size 3x32x32 (e.g., CIFAR-10), ResNet-18 with input size 3x224x224 (e.g., IMAGENET), and ResNet-50 with input size 3x224x224. The converted NVDLA register traces exhibit the same memory traces with the VP counterpart.

## How to get VP debug info
1. Get a sample caffe NN model. Here we provide a pretrained LeNet inference model(`example_usage/LeNet_caffe_model/`);
2. Compile the NN model with [NVDLA compiler](https://github.com/nvdla/sw/tree/master/prebuilt/x86-ubuntu) using the following command: `./nvdla_compiler -o lenet --cprecision fp16 --configtarget nv_full --informat nchw --prototxt /path/to/LeNet_caffe_model/Lenet.prototxt --caffemodel /path/to/LeNet_caffe_model/lenet_iter_10000.caffemodel`. The output file with a suffix of ".nvdla" (e.g., fast-math.nvdla) is the "loadable" to be loaded by NVDLA_runtime;
3. Follow the instructions at [NVDLA Virtual Platform documentation](http://nvdla.org/vp.html#running-the-virtual-simulator-from-docker) to run the docker version of VP;
4. In the docker, before launching the QEMU simulator, toggle the all the debug flags by typing `export SC_LOG="outfile:sc.log;verbosity_level:sc_debug;csb_adaptor:enable;dbb_adaptor:enable;sram_adaptor:enable"` as shown in section 2.7.1 on NVDLA VP webpage;
5. Still in the docker, copy the loadable file and an image of handwritten digit to `/usr/local/nvdla/`;
6. Inside the QEMU simulator, remember to load the NVDLA driver (`insmod drm.ko && insmod opendla_1.ko`);
7. After the runtime program finishes, exit QEMU and move the `sc.log` file to somewhere in **your computer's** file system. The sc.log file for the provided LeNet test case is around 2.9GB. But if the patch file in tip 9 below is applied, it can be reduced to about 100MB.
8. Users may refer to [this blog](https://medium.com/@anakin1028/run-lenet-on-nvdla-837a6fac6f8b) for more details to run a testcase on VP.
9. Usually, the VP debug info file is SO LARGE that it may overflow the whole hard disk space. To resolve this issue, a patch file is provided in `bsc-util/nvdla_utilities/modify_cmod`. This patch annotates most of the debugging output except those related to NVDLA register transactions and memory traces. Users may need to apply that patch with `git apply /path/to/modify_cmod` under the `nvdla/hw` path, and then rebuild `cmod` and `vp` according to instructions on [NVDLA VP documentation](http://nvdla.org/vp.html#download-the-virtual-simulator) (As for the building environment, the docker provided with VP would be enough). Reducing those IO's not only reduces hard disk usage, but also accelerates simulation process.

## Convert VP debug info to NVDLA trace file and memory traces with the utility
1. Compile NVDLAUtil.cpp with any c++ compiler with c++11:
   ```
   g++ -std=c++11 NVDLAUtil.cpp -o NVDLAUtil
   ```
2. Type the following command to convert `sc.log` to NVDLA register traces:
    ```
    ./NVDLAUtil -i /path/to/sc.log --print_reg_txn --function parse_vp_log > /path/to/output/file.txn
    ```
3. Type the following command to convert sc.log to NVDLA memory traces:
    ```
    ./NVDLAUtil -i /path/to/sc.log --print_mem_rd --function parse_vp_log > /path/to/vp/rd/mem/trace
    ./NVDLAUtil -i /path/to/sc.log --print_mem_wr --function parse_vp_log > /path/to/vp/wr/mem/trace
    ```
4. Use '-h' option to get help for all the options for NVDLAUtil.


## Verify the converted NVDLA register trace file with verilator verification flow and get its memory traces
1. Verify the above converted register trace file like [other traces](https://github.com/nvdla/hw/tree/nvdlav1/verif/traces/traceplayer) (put the converted register trace in a standalone directory just beside other test cases) with the verilator flow, and capture the output on the terminal (e.g., with tee) with a command like
    ```
    nvdla/hw/verif/verilator$ make run TEST=lenet_converted | tee /path/to/put/lenet_nvdla_cpp_term_log
    ```
2. Use the utility to crop out read / write memory traces of this converted register trace. The command could be like:
    ```
    ./NVDLAUtil -i /path/to/put/lenet_nvdla_cpp_term_log --print_mem_rd --function nvdla_cpp_log2mem_trace > /path/to/lenet/converted/rd/mem/trace
    ./NVDLAUtil -i /path/to/put/lenet_nvdla_cpp_term_log --print_mem_wr --function nvdla_cpp_log2mem_trace > /path/to/lenet/converted/wr/mem/trace
    ```

3. Compare the read (and write) memory traces of the two versions. For example,
    ```
    ./NVDLAUtil -i1 /path/to/vp/rd/mem/trace -i2 /path/to/lenet/converted/rd/mem/trace --function comp_mem_trace
    ./NVDLAUtil -i1 /path/to/vp/wr/mem/trace -i2 /path/to/lenet/converted/wr/mem/trace --function comp_mem_trace
    ```
   The result should be "No differences are found". Users may also plot the memory traces to visualize the comparison.