import os
import argparse
import subprocess
import sys
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
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
        "--nvdla-compiler", default="/usr/local/nvdla/compiler/nvdla_compiler",
        help="Path to NVDLA compiler")
    parser.add_argument(
        "--qemu-bin", default="/usr/local/nvdla/vp/aarch64_toplevel",
        help="Path to qemu binary. By default it is pointing to the self-built qemu binary. Set to "
             "/usr/local/nvdla/aarch64_toplevel if one wants to use the one provided in docker image")
    parser.add_argument(
        "--qemu-lua", default="/usr/local/nvdla/aarch64_nvdla.lua",
        help="Path to configuration file of running qemu. Using the one in docker image is ok.")
    parser.add_argument(
        "--out-dir", default="/home/nvdla/traces/lenet/",
        help="directory to put the generated sc.log, register txn and mem traces")
    parser.add_argument(
        "--image", default="",
        help="Path to the image for nvdla_runtime to perform inference.")
    parser.add_argument(
        "--true-data", action="store_true", default=False, help="Whether to get true data from sc.log")
    parser.add_argument(
        "--dump-results", action="store_true", default=False,
        help="Whether to dump results to check results in translated input.txn")
    parser.add_argument(
        "--convert-only", action="store_true", default=False, help="Whether to assume the presence of sc.log "
        "and only do processing only")

    args = parser.parse_args()
    return args


def check_dependence(options):
    assert os.path.exists(options.caffemodel)
    assert os.path.exists(options.prototxt)
    assert os.path.exists(options.nvdla_compiler)
    assert os.path.exists(options.qemu_bin)
    assert os.path.exists(options.qemu_lua)

    if options.true_data:
        assert options.image != "" and os.path.exists(options.image)

    if options.dump_results:
        assert options.true_data


def get_loadable(options):
    os.makedirs(options.out_dir, exist_ok=True)
    compiler_path, compiler_name = os.path.split(options.nvdla_compiler)
    assert os.path.exists(options.nvdla_compiler)
    compile_log_path = os.path.abspath(os.path.join(options.out_dir, "compile_log"))
    cmd = ("cd " + compiler_path + " && ./" + compiler_name + " -o " + options.model_name +
           " --cprecision fp16 --configtarget nv_full --informat nchw --prototxt " + os.path.abspath(options.prototxt) +
           " --caffemodel " + os.path.abspath(options.caffemodel) + " > " + compile_log_path +
           " 2>&1 && mv fast-math.nvdla " + options.model_name + ".nvdla && mv " +
           options.model_name + ".nvdla /usr/local/nvdla")

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
    image_str = ""
    if options.true_data:   # prepare real input data if necessary
        path_name, file_name = os.path.split(os.path.abspath(options.image))
        image_str = " --image " + file_name + " "
        os.system("cp " + os.path.abspath(options.image) + " /usr/local/nvdla/")

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
send "./nvdla_runtime --loadable %(loadable)s %(image)s\\r"

# poweroff the Guest VM
expect "# "
send "shutdown -h now\\r"
''' % {"bin": options.qemu_bin, "lua": options.qemu_lua, "loadable": options.model_name + ".nvdla", "image": image_str}
    qemu_run_template.format(options.qemu_bin, options.qemu_lua)
    with open("/usr/local/nvdla/qemu_run.exp", "w") as f:
        f.write(qemu_run_template)
    cmd = "cd /usr/local/nvdla/ && chmod +x qemu_run.exp && ./qemu_run.exp"
    if os.path.exists("/usr/local/nvdla/qemu_log"):
        os.system("mv /usr/local/nvdla/qemu_log /usr/local/nvdla/qemu_log_bkp")
    qemu_proc = subprocess.Popen(cmd, shell=True)
    qemu_proc.wait()


def parse_mixed_type_trace(rd_wr_trace_file):
    rd_address_x_coords = []
    rd_address_y_coords = []
    wr_address_x_coords = []
    wr_address_y_coords = []
    with open(rd_wr_trace_file) as fp:
        lines = fp.readlines()
    for i, line in enumerate(lines):
        words_in_line = line.split()
        if 'r' in words_in_line[0]:
            rd_address_x_coords.append(i)
            rd_address_y_coords.append(int(words_in_line[1], 16))
        elif 'w' in words_in_line[0]:
            wr_address_x_coords.append(i)
            wr_address_y_coords.append(int(words_in_line[1], 16))
        else:
            print("unknown line: %s" % line)
            exit(1)

    fig = plt.figure(figsize=(16, 9))
    ax = fig.gca()
    plt.scatter(rd_address_x_coords, rd_address_y_coords, s=1, c='red', label='read trace')
    plt.scatter(wr_address_x_coords, wr_address_y_coords, s=1, c='blue', label='write_trace')

    ylabels = map(lambda t: '0x%08x' % int(t), ax.get_yticks())
    ax.set_yticklabels(ylabels)
    ax.legend()
    plt.tight_layout()
    fig.savefig(os.path.join(os.path.dirname(rd_wr_trace_file), rd_wr_trace_file + ".png"), dpi=240)


def process_log(options):
    if not options.convert_only:
        os.system("cd /usr/local/nvdla && mv sc.log " + options.out_dir)
        os.system("cd /usr/local/nvdla && mv qemu_log " + options.out_dir)

    nvdla_utilities_dir = os.path.dirname(os.path.abspath(__file__))
    workload = Workload(options.out_dir, in_compilation=True, use_real_data=options.true_data,
                        dump_results=options.dump_results)
    assert os.path.exists(os.path.join(options.out_dir, "VP_mem_rd_wr"))
    # rtl_mem_rd_wr is generated during the remapping phase
    parse_mixed_type_trace(os.path.join(options.out_dir, "VP_mem_rd_wr"))
    os.system("cd " + nvdla_utilities_dir + " && python3.6 fix_txn_discontinuous.py --vp-out-dir " + options.out_dir +
              " --name try_input")
    os.system("cd " + options.out_dir + " && mv input.txn bkp_input.txn")
    os.system("cd " + options.out_dir + " && mv try_input.txn input.txn")


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


if __name__ == "__main__":
    main()
