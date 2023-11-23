import os
import argparse
import subprocess
import sys
sys.path.append(os.path.dirname(__file__))
from match_reg_trace_addr.parse_qemu_log import *


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--model-name", help="name of the NN model", required=True)
    parser.add_argument(
        "--caffemodel", help="Path to the Caffe model file", required=True)
    parser.add_argument(
        "--prototxt", help="Path to the Caffe prototxt", required=True)
    parser.add_argument(
        "--nvdla-hw", default="/home/lactose/nvdla/hw/",
        help="Path to NVDLA hw repo")
    parser.add_argument(
        "--nvdla-compiler", default="/home/lactose/nvdla/sw/prebuilt/x86-ubuntu/nvdla_compiler",
        help="Path to NVDLA compiler")
    parser.add_argument(
        "--qemu-bin", default="/home/lactose/nvdla/vp/aarch64_toplevel",
        help="Path to qemu binary. By default it is pointing to the self-built qemu binary. Set to "
             "/usr/local/nvdla/aarch64_toplevel if one wants to use the one provided in docker image")
    parser.add_argument(
        "--qemu-lua", default="/usr/local/nvdla/aarch64_nvdla.lua",
        help="Path to configuration file of running qemu. Using the one in docker image is ok.")
    parser.add_argument(
        "--out-dir", default="/home/lactose/nvdla/traces/lenet_auto/",
        help="directory to put the generated sc.log, register txn and mem traces")
    parser.add_argument(
        "--disk-image", default="/home/lactose/gem5_linux_images/ubuntu-18.04-arm64-docker.img",
        help="path to the disk image for full system simulation")
    parser.add_argument(
        "--convert-only", action="store_true", default=False, help="Whether to assume the presence of sc.log "
        "and only do processing only"
    )
    parser.add_argument(
        "--no-print-hints", action="store_true", default=False, help="Whether to print the steps afterwards"
    )

    args = parser.parse_args()
    return args


def check_dependence(options):
    assert os.path.exists(options.caffemodel)
    assert os.path.exists(options.prototxt)
    assert os.path.exists(options.nvdla_hw)
    assert os.path.exists(options.nvdla_compiler)
    assert os.path.exists(options.qemu_bin)
    assert os.path.exists(options.qemu_lua)
    assert os.path.exists(options.disk_image)


def get_loadable(options):
    os.makedirs(options.out_dir, exist_ok=True)
    compiler_path, compiler_name = os.path.split(options.nvdla_compiler)
    assert os.path.exists(options.nvdla_compiler)
    compile_log_path = os.path.join(options.out_dir, "compile_log")
    cmd = "cd " + compiler_path + " && ./" + compiler_name + " -o " + options.model_name + \
          " --cprecision fp16 --configtarget nv_full --informat nchw --prototxt " + options.prototxt + \
          " --caffemodel " + options.caffemodel + " > " + compile_log_path + " 2>&1 && mv fast-math.nvdla " + \
          options.model_name + ".nvdla && mv " + options.model_name + ".nvdla /usr/local/nvdla"

    try:
        os.system(cmd)
    except:
        print("error in generating loadable file")
        exit(1)


def set_environ_var():
    # export SC_LOG="outfile:sc.log;verbosity_level:sc_debug;csb_adaptor:enable;dbb_adaptor:enable;sram_adaptor:enable"
    os.environ["SC_LOG"] = \
        "outfile:sc.log;verbosity_level:sc_debug;csb_adaptor:enable;dbb_adaptor:enable;sram_adaptor:enable"

    # export SC_SIGNAL_WRITE_CHECK=DISABLE
    os.environ["SC_SIGNAL_WRITE_CHECK"] = "DISABLE"


def run_qemu(options):
    if not os.path.exists("/usr/bin/expect"):
        # install expect
        os.system("apt update")
        os.system("apt install expect")
    qemu_run_template = '''#!/usr/bin/expect

# starts guest vm, run benchmarks, poweroff
set timeout -1

# Assign a variable to the log file
log_file /usr/local/nvdla/qemu_log

# Start the guest VM
spawn %(bin)s -c %(lua)s

# Login process
expect "login: "
# Enter username
send "root\\r"

# Enter Password
expect "Password: "
send "nvdla\\r"

expect "# "
send "mount -t 9p -o trans=virtio r /mnt && cd /mnt\\r"
expect "# "
send "insmod drm.ko && insmod opendla_1.ko\\r"
expect "# "
send "./nvdla_runtime --loadable %(loadable)s\\r"

# poweroff the Guest VM
expect "# "
send "shutdown -h now\\r"
''' % {"bin": options.qemu_bin, "lua": options.qemu_lua, "loadable": options.model_name + ".nvdla"}
    qemu_run_template.format(options.qemu_bin, options.qemu_lua)
    with open("/usr/local/nvdla/qemu_run.exp", "w") as f:
        f.write(qemu_run_template)
    cmd = "cd /usr/local/nvdla/ && chmod +x qemu_run.exp && ./qemu_run.exp"
    if os.path.exists("/usr/local/nvdla/qemu_log"):
        os.system("mv /usr/local/nvdla/qemu_log /usr/local/nvdla/qemu_log_bkp")
    qemu_proc = subprocess.Popen(cmd, shell=True)
    qemu_proc.wait()


def process_log(options):
    if not options.convert_only:
        os.system("cd /usr/local/nvdla && mv sc.log " + options.out_dir)
        os.system("cd /usr/local/nvdla && mv qemu_log " + options.out_dir)

    nvdla_utilities_dir = os.path.dirname(os.path.abspath(__file__))
    if not os.path.exists(os.path.join(nvdla_utilities_dir, "NVDLAUtil")):
        os.system("cd " + nvdla_utilities_dir + " && g++ -std=c++11 NVDLAUtil.cpp -o NVDLAUtil")
    os.system("cd " + nvdla_utilities_dir + " && ./NVDLAUtil -i " + os.path.join(options.out_dir, "sc.log") +
              " --print-reg-txn -f parse-vp-log --change-addr > " + os.path.join(options.out_dir, "input.txn"))
    os.system("cd " + nvdla_utilities_dir + " && ./NVDLAUtil -i " + os.path.join(options.out_dir, "sc.log") +
              " --print-mem-rd -f parse-vp-log --change-addr > " + os.path.join(options.out_dir, "VP_mem_rd"))
    os.system("cd " + nvdla_utilities_dir + " && ./NVDLAUtil -i " + os.path.join(options.out_dir, "sc.log") +
              " --print-mem-wr -f parse-vp-log --change-addr > " + os.path.join(options.out_dir, "VP_mem_wr"))
    os.system("cd " + nvdla_utilities_dir + " && ./NVDLAUtil -i " + os.path.join(options.out_dir, "sc.log") +
              " --print-mem-rd --print-mem-wr -f parse-vp-log --change-addr > " + os.path.join(options.out_dir,
                                                                                               "VP_mem_rd_wr"))
    os.system("cd " + nvdla_utilities_dir + " && python3 fix_txn_discontinuous.py --vp-out-dir " + options.out_dir +
              " --name try_input")
    os.system("cd " + options.out_dir + " && mv input.txn bkp_input.txn")
    os.system("cd " + options.out_dir + " && mv try_input.txn input.txn")
    workload = Workload(options.out_dir)
    workload.write_rd_only_var_log(os.path.join(options.out_dir, "rd_only_var_log"))

    # the nvdla/vp docker image has perl v5.22.1 installed, ok
    perl_script_path = os.path.join(os.path.abspath(options.nvdla_hw), "verif/verilator/input_txn_to_verilator.pl")

    # check contents of perl script. If it does not contain keyword `until`, abort and remind the user
    with open(perl_script_path, "r") as f:
        perl_script_lines = f.readlines()
    found = False
    for line in perl_script_lines:
        if "until" in line:
            found = True
            break
    if not found:
        print("Patch file " + os.path.join(os.path.dirname(__file__), "nvdla_hw.patch") +
              " should be applied to " + perl_script_path)
        exit(1)
    os.system("perl " + perl_script_path + " " + os.path.join(options.out_dir, "input.txn") + " " +
              os.path.join(options.out_dir, "trace.bin"))


def main():
    options = parse_args()

    if not options.convert_only:
        check_dependence(options)

        # compile the caffe model to loadable and move to /usr/local/nvdla
        get_loadable(options)

        # set environment variable
        set_environ_var()

        # run qemu
        run_qemu(options)

    # process the log file
    process_log(options)

    # move to disk image for full system simulation
    gem5_nvdla_dir = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../"))
    work_dir_in_image = os.path.join(gem5_nvdla_dir, "mnt/home/" + options.model_name)

    if not options.no_print_hints:
        print("""\n\n
For the next steps, one needs to do the following ON THE HOST MACHINE (OUTSIDE THE DOCKER CONTAINER):\n
1. Mount the disk image for simulation (ignore if have mounted, sudo may be required)
```
cd """ + gem5_nvdla_dir + " && mkdir mnt && sudo python3 util/gem5img.py mount " +
          options.disk_image + """ ./mnt
```
2. Cross-compile schedulers like `my_validation_nvdla_single_thread.cpp` and `pipeline_execute.cpp` and move the \
binaries to """ + os.path.join(os.path.abspath(gem5_nvdla_dir), "mnt/home/") + """

3. Refer to """ + os.path.join(gem5_nvdla_dir, "bsc-util/nvdla_utilities/sweep/main.py") + """ for the options to \
create simulation points and checkpoints""")


if __name__ == "__main__":
    main()
