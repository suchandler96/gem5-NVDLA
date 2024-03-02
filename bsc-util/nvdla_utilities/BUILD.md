# How to Build Docker Image `advp` (advanced VP)
## Step 1. Pull `nvdla/vp` Docker Image and Build NVDLA C-model
The `nvdla_hw.patch` used below contains bugfixes from [this pull request](https://github.com/nvdla/hw/pull/312/files) and [this issue](https://github.com/nvdla/hw/issues/219).
```
# Download repo and make sure you are on branch `DMAEnable`. It is suggested to put it in ~/ so that the paths in the commands below wouldn't need to be modified.
$ cd ~/
$ git clone https://github.com/suchandler96/gem5-NVDLA.git
$ mv gem5-NVDLA/ gem5-nvdla     # simply a rename


# switch into nvdla/vp docker container
# because cmod may lack dependencies in host machine
# only use the "docker run" cmd for the first time.
# Remember the ID of this docker container, as it will be reused.
$ docker pull nvdla/vp
$ docker run -it -v /home:/home nvdla/vp    # this step mounts the /home directory of host machine to the docker container
(nvdla/vp)# apt update && apt install python3 expect cpio libncurses5-dev
(nvdla/vp)# cd /usr/local/nvdla/
(nvdla/vp)# git clone https://github.com/nvdla/hw.git
(nvdla/vp)# git clone https://github.com/nvdla/sw.git
(nvdla/vp)# git clone https://github.com/nvdla/vp.git
(nvdla/vp)# cd hw/
(nvdla/vp)# git apply home/user_name/gem5-nvdla/bsc-util/nvdla_utilities/nvdla_hw.patch
(nvdla/vp)# make
     * project name -> nv_full
     * c pre-processor -> /usr/bin/cpp
     * g++ -> /usr/bin/g++
     * perl -> /usr/bin/perl
     * java -> /usr/bin/java
     * SystemC -> (keep as default)
     * verilator -> verilator
     * clang -> clang
(nvdla/vp)# tools/bin/tmake -build cmod_top
```
## Step 2. Rebuild Compiler, Runtime and Driver
This part for compiling NVDLA toolchain is modified from [this post](https://github.com/nvdla/sw/issues/51#issuecomment-404704012). The `buildroot` directory has been removed from our `advp` docker image since it is too large.
```
(nvdla/vp)# cd ../sw/
(nvdla/vp)# git apply home/user_name/gem5-nvdla/bsc-util/nvdla_utilities/nvdla_sw.patch

# Install buildroot to rebuild NVDLA compiler, runtime and driver to output debug log.
# Instead of downloading buildroot from its website,
# now need to download from its repository, and go back to branch 2017.11.

(nvdla/vp)# cd /usr/local/
(nvdla/vp)# git clone git://git.buildroot.net/buildroot
(nvdla/vp)# cd buildroot
(nvdla/vp)# git checkout -t orgin/2017.11.x

(nvdla/vp)# make qemu_aarch64_virt_defconfig
(nvdla/vp)# make menuconfig
     * Target Options -> Target Architecture -> AArch64 (little endian)
     * Target Options -> Target Architecture Variant -> cortex-A57
     * Toolchain -> Custom kernel headers series -> 4.13.x
     * Toolchain -> Toolchain type -> External toolchain
     * Toolchain -> Toolchain -> Linaro AArch64 2017.08
     * Toolchain -> Toolchain origin -> Toolchain to be downloaded and installed
     * Kernel -> () Kernel version -> 4.13.3
     * Kernel -> Kernel configuration -> Use the architecture default configuration
     * System configuration -> Enable root login with password -> Y
     * System configuration -> Root password -> nvdla
     * Target Packages -> Show packages that are also provided by busybox -> Y
     * Target Packages -> Networking applications -> openssh -> Y

(nvdla/vp)# make -j24


# Build KMD (Kernel Mode Driver)

(nvdla/vp)# cd /usr/local/nvdla/sw/kmd
(nvdla/vp)# make KDIR=/usr/local/buildroot/output/build/linux-4.13.3 ARCH=arm64 CROSS_COMPILE=/usr/local/buildroot/output/host/bin/aarch64-linux-gnu-
(nvdla/vp)# cp port/linux/opendla.ko /usr/local/nvdla/
(nvdla/vp)# cd /usr/local/nvdla/
(nvdla/vp)# mkdir backup
(nvdla/vp)# mv opendla_1.ko driver_backup/ && mv opendla_2.ko backup/
(nvdla/vp)# mv opendla.ko opendla_1.ko
# The newly compiled opendla.ko is at `sw/kmd/port/linux/opendla.ko`. We do an additional backup and rename operation in the last several lines.


# Build UMD (User Mode Driver, including compiler and runtime)

(nvdla/vp)# cd /usr/local/nvdla/sw/umd
(nvdla/vp)# export TOP=$(pwd)
(nvdla/vp)# make TOOLCHAIN_PREFIX=/usr/local/buildroot/output/host/opt/ext-toolchain/bin/aarch64-linux-gnu-
(nvdla/vp)# make runtime TOOLCHAIN_PREFIX=/usr/local/buildroot/output/host/opt/ext-toolchain/bin/aarch64-linux-gnu-
(nvdla/vp)# cd /usr/local/nvdla/
(nvdla/vp)# mv nvdla_runtime backup/ && mv libnvdla_runtime.so backup/
(nvdla/vp)# mv nvdla_compiler backup/ && mv libnvdla_compiler.so backup/
(nvdla/vp)# mkdir compiler
(nvdla/vp)# cd /usr/local/nvdla/sw/umd
(nvdla/vp)# cp out/apps/compiler/nvdla_compiler/nvdla_compiler /usr/local/nvdla/compiler/
(nvdla/vp)# cp out/core/src/compiler/libnvdla_compiler/libnvdla_compiler.so /usr/local/nvdla/compiler/
(nvdla/vp)# cp out/apps/runtime/nvdla_runtime/nvdla_runtime /usr/local/nvdla/
(nvdla/vp)# cp out/core/src/runtime/libnvdla_runtime/libnvdla_runtime.so /usr/local/nvdla/
# Here we are still doing a backup and copy after building compiler and runtime.
```

## Step 3. Rebuild Virtual Platform (VP)
Usually, the VP debug info file is SO LARGE that it may overflow the whole hard disk space. To resolve this issue, a patch file is provided in `bsc-util/nvdla_utilities/nvdla_hw.patch`. By far we assume this has already been applied. This patch comments out most of the debugging output except those related to NVDLA register transactions and memory traces. Then we need to rebuild `cmod` and `vp` according to instructions on [NVDLA VP documentation](http://nvdla.org/vp.html#download-the-virtual-simulator). Reducing those IO's not only reduces hard disk usage, but also accelerates simulation process. If users want to try out testcases like ResNet-18/50 for 3x224x224 images, this step is crucial.

First we change the sources to update qemu repos according to [this thread](https://github.com/riscv-collab/riscv-gnu-toolchain/issues/280).
```
(nvdla/vp)# cd /usr/local/nvdla/vp/
(nvdla/vp)# touch ~/.gitconfig
(nvdla/vp)# echo "[url \"https://git.qemu.org/git\"]" >> ~/.gitconfig
(nvdla/vp)# echo "insteadOf = git://git.qemu-project.org" >> ~/.gitconfig
(nvdla/vp)# git submodule update --init --recursive

# If some submodules are still not able to be pulled, users may need to pull mannually.
# For example, in my case, `QemuMacDrivers`, `qemu-palcode`, `skiboot`, `tlm2c` and `memory` failed, so I did the following:
(nvdla/vp)# cd /usr/local/nvdla/vp/libs/qbox/roms
(nvdla/vp)# git clone https://git.qemu.org/QemuMacDrivers.git
(nvdla/vp)# cd QemuMacDrivers/
(nvdla/vp)# git checkout d4e7d7a

(nvdla/vp)# cd ../
(nvdla/vp)# git clone https://github.com/rth7680/qemu-palcode
(nvdla/vp)# cd qemu-palcode/
(nvdla/vp)# git checkout f3c7e44

(nvdla/vp)# cd ../
(nvdla/vp)# git clone https://git.qemu.org/skiboot.git
(nvdla/vp)# cd skiboot/
(nvdla/vp)# git checkout 762d008

# re-download everything in tlm2c/
(nvdla/vp)# cd /usr/local/nvdla/vp/libs/
(nvdla/vp)# rm -rf tlm2c/
(nvdla/vp)# git clone git@github.com:nvdla/tlm2c.git
(nvdla/vp)# cd tlm2c/
(nvdla/vp)# git checkout c54ade0

# re-download everything in memory/
(nvdla/vp)# cd ../../models/
(nvdla/vp)# rm -rf memory/
(nvdla/vp)# git clone git@github.com:nvdla/simple_memory.git
(nvdla/vp)# mv simple_memory/ memory/
(nvdla/vp)# cd memory/
(nvdla/vp)# git checkout 1018f8

# below we rebuild VP, using the cmod built in step 1.
(nvdla/vp)# cd ../vp
(nvdla/vp)# cmake -DCMAKE_INSTALL_PREFIX=build -DSYSTEMC_PREFIX=/usr/local/systemc-2.3.0/ -DNVDLA_HW_PREFIX=/usr/local/nvdla/hw/ -DNVDLA_HW_PROJECT=nv_full -DCMAKE_BUILD_TYPE=Debug
(nvdla/vp)# make -j24
(nvdla/vp)# make install
```
And an executable named `aarch64_toplevel` will appear under `vp/` directory. This `aarch64_toplevel` will also be referred to in our automated script.

# How to Build Docker Image `gem5_nvdla_env`
## Step 1. Pull gem5 Docker Image
```
$ cd ~/
# Pull our gem5-NVDLA repo and rename if you haven't
# git clone https://github.com/suchandler96/gem5-NVDLA.git && mv gem5-NVDLA/ gem5-nvdla

$ docker pull gcr.io/gem5-test/ubuntu-18.04_all-dependencies:v22-1
$ docker run -it -v /home:/home gcr.io/gem5-test/ubuntu-18.04_all-dependencies:v22-1
# below we call this image "gem5" for short

(gem5)# cd /usr/local/ && mkdir nvdla && cd nvdla/
(gem5)# git clone https://github.com/nvdla/hw.git
(gem5)# cd hw/
(gem5)# git apply /home/user_name/gem5-nvdla/bsc-util/nvdla_utilities/nvdla_hw.patch
```
## Step 2. Prepare Other Dependencies in the Docker Container
The dependencies below are required:
- Install through website (prebuilt binaries):
    - jdk 1.7 (tested with jdk 1.7.0_80)

- Install through command line (git, apt, etc.):
    - clang-10 & clang++-10
    - perl with IO-Tee, yaml packages
    - Verilator v4.040

For those to be installed from the websites (jdk 1.7), a final file structure is shown:
```
/usr/local/
|-- jdk1.7.0_80/
    |-- bin/
    |-- db/
    ......
```
And the lines to append to `/root/.bashrc` in the docker container are pasted here:
```
# for jdk 1.7
export JAVA_HOME=/usr/local/jdk1.7.0_80
export JRE_HOME=${JAVA_HOME}/jre
export CLASSPATH=.:${JAVA_HOME}/lib:${JRE_HOME}/lib
export PATH=${JAVA_HOME}/bin:$PATH
```
Do remember to `source /root/.bashrc` after modification.

Below we show the codes to install dependencies from the command line.
```
(gem5)# apt update && apt install clang-10 clang++-10
(gem5)# apt install libio-tee-perl libyaml-perl
(gem5)# ln -s /usr/bin/clang-10 /usr/bin/clang
(gem5)# ln -s /usr/bin/clang++-10 /usr/bin/clang++

(gem5)# cd /home/user_name/     # this is a temp dir to hold src and build files. Putting it elsewhere is also ok.
(gem5)# git clone https://github.com/verilator/verilator && cd verilator/
(gem5)# unset VERILATOR_ROOT    # For bash
(gem5)# git checkout stable     # Use most recent stable release
(gem5)# git checkout v4.040     # Switch to specified release version

# ensure the clang and clang++ are v10 just installed
(gem5)# export CC=clang-10
(gem5)# export CXX=clang++-10

(gem5)# autoconf
(gem5)# ./configure --prefix /usr/local/verilator_4.040

(gem5)# make -j 24
(gem5)# make install

# create symbolic link for include directory
(gem5)# ln -s /usr/local/verilator_4.040/share/verilator/include/ /usr/local/verilator_4.040/include
```
And append the following lines to `/root/.bashrc` of the docker container:
```
# for verilator 4.040 (gem5-NVDLA uses verilator 4.040)
export PATH=/usr/local/verilator_4.040/bin:$PATH
export VERILATOR_ROOT=/usr/local/verilator_4.040
export C_INCLUDE_PATH=/usr/local/verilator_4.040/include/:$C_INCLUDE_PATH
export CPLUS_INCLUDE_PATH=/usr/local/verilator_4.040/include/:$CPLUS_INCLUDE_PATH
```
Finally, remember to `source /root/.bashrc` after modification.

## Step 3. Build NVDLA VMOD and Verilator Verification Flow
```
(gem5)# cd /usr/local/nvdla/hw/
(gem5)# make
    * project name -> nv_full
    * c pre-processor -> /usr/bin/cpp
    * g++ -> /usr/bin/g++
    * perl -> /usr/bin/perl
    * java -> /usr/local/jdk1.7.0_80/bin/java
    * SystemC -> (keep as default)
    * verilator -> verilator
    * clang -> clang
(gem5)# ./tools/bin/tmake -build vmod
(gem5)# export CC=clang
(gem5)# export CXX=clang++

# The whole compilation requires more than 45GB of memory.
# If you check out what's happening in nvdla/hw/verif/verilator/Makefile,
# You will see we are doing heavy optimizations including:
#   (1) verilator -O3 to the verilog -> cpp code generation phase;
#   (2) clang -O3 -Ofast to the cpp -> .o compilation;
#   (3) profiling-guided optimization (PGO) to the cpp -> .o compilation.
# Please note in our case PGO can bring 40% performance improvement.
# We have also fixed some of the UNOPTFLAT warnings in the original NVDLA
# vmod/, which is included in nvdla_hw.patch. If you don't want these
# modifications, simply do `git checkout` for files under nvdla/hw/vmod/.
(gem5)# ./tools/bin/tmake -build verilator
```

## Step 4. Add paths of the user's own gem5-nvdla directory into `/root/.bashrc`
```
# for disk image
export M5_PATH=/home/gem5_linux_images/:/home/gem5_linux_images/aarch-system-20220707:/home/gem5_linux_images/aarch-system-20220707/binaries/:$M5_PATH
```
So we suggest the users put `gem5-nvdla/` and `gem5_linux_images/` right under the `~/` directory of the host machine.
