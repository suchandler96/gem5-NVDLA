import sys
import os
import argparse


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--model-name", help="name of the NN model", required=True)
    parser.add_argument(
        "--caffemodel", help="Path to the Caffe model file", required=True)
    parser.add_argument(
        "--prototxts", type=str, metavar="dir", nargs='+',
        help="a list of paths to prototxt files in pipeline stage order")
    parser.add_argument(
        "--prototxt", help="Path to the Caffe prototxt")
    parser.add_argument(
        "--split-at", type=str, metavar="var", nargs='+',
        help="a list of blob names in the provided prototxt so that the upstream "
        "and downstream will be split into separate pipeline stages")
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
        "--out-dir", default="/home/lactose/nvdla/traces/lenet_auto_pipeline/",
        help="directory to put the generated sc.log, register txn and mem traces")
    # parser.add_argument(
    #     "--out-dir-multibatch", default="/home/lactose/nvdla/traces/lenet_auto_pipeline/multibatch/",
    #     help="directory to put converted input.txn and trace.bin files across all pipeline stages and batches")
    parser.add_argument(
        "--gem5-nvdla-dir", default="/home/lactose/gem5-nvdla/",
        help="directory to gem5-NVDLA repo")
    parser.add_argument(
        "--disk-image", default="/home/lactose/gem5_linux_images/ubuntu-18.04-arm64-docker.img",
        help="path to the disk image for full system simulation")

    args = parser.parse_args()

    # check legal
    if len(args.prototxts) != 0:
        assert args.prototxt is None and args.split_at is None
    if args.prototxt is not None or args.split_at is not None:
        assert args.prototxt is not None and args.split_at is not None
    return args


def main():
    options = parse_args()
    num_stages = len(options.prototxts)
    work_dirs = []
    # os.makedirs(os.path.abspath(options.out_dir_multibatch), exist_ok=True)
    for i in range(num_stages):
        work_dir = os.path.join(os.path.abspath(options.out_dir), "stage_" + str(i + 1))
        work_dirs.append(work_dir)
        os.makedirs(work_dir, exist_ok=True)

        assert os.path.exists(options.prototxts[i])
        os.system("cp " + options.prototxts[i] + " " + work_dir)
        os.system("cd " + os.path.join(options.gem5_nvdla_dir, "bsc-util/nvdla_utilities") +
                  " && python3 caffe2trace.py --model-name " + options.model_name +
                  "_stage_" + str(i + 1) + " --caffemodel " + os.path.abspath(options.caffemodel) + " --prototxt " +
                  os.path.abspath(options.prototxts[i]) + " --nvdla-hw " + os.path.abspath(options.nvdla_hw) +
                  " --nvdla-compiler " + os.path.abspath(options.nvdla_compiler) + " --qemu-bin " +
                  os.path.abspath(options.qemu_bin) + " --qemu-lua " + os.path.abspath(options.qemu_lua) +
                  " --out-dir " + work_dir + " --gem5-nvdla-dir " + os.path.abspath(options.gem5_nvdla_dir) +
                  " --disk-image " + os.path.abspath(options.disk_image) + " --no-print-hints")

    '''
    sys.path.append(os.path.join(os.path.abspath(options.gem5_nvdla_dir), "bsc-util/nvdla_utilities"))

    import match_reg_trace_addr.match_pipeline as match_pipeline
    mapper = match_pipeline.PipelineMultiBatch()
    mapper.remap(work_dirs)

    for i in range(num_stages):
        print("reading " + os.path.join(work_dirs[i], "input.txn"))
        with open(os.path.join(work_dirs[i], "input.txn")) as fp:
            txn_lines = fp.readlines()
        for batch_id in range(mapper.batch_num):
            new_lines = [str(line) for line in txn_lines]
            modify_status = []
            for j in range(len(txn_lines)):
                modify_status.append(False)     # False means not modified by remapping yet

            for w_desc, addr_mapped in mapper.weight_map.items():
                # w_desc: pipeline_stage, addr_id, offset
                if w_desc[0] == i:
                    orig_addr = mapper.pipeline_stages[i].data[(w_desc[1], w_desc[2])].addr
                    for line_id, line in enumerate(txn_lines):
                        if hex(orig_addr) in line and not modify_status[line_id]:
                            new_lines[line_id] = line.replace(hex(orig_addr), hex(addr_mapped))
                            modify_status[line_id] = True

            for a_desc, addr_mapped in mapper.activation_map.items():
                # a_desc: batch_id, stage_id, addr_id, offset
                if a_desc[0] == batch_id and a_desc[1] == i:
                    orig_addr = mapper.pipeline_stages[i].data[(a_desc[2], a_desc[3])].addr
                    for line_id, line in enumerate(txn_lines):
                        if hex(orig_addr) in line and not modify_status[line_id]:
                            new_lines[line_id] = line.replace(hex(orig_addr), hex(addr_mapped))
                            modify_status[line_id] = True

            # write lines to file
            out_trace_prefix = options.model_name + "_" + str(batch_id + 1) + "_" + str(i + 1) + "_"
            out_txn_path = os.path.join(os.path.abspath(options.out_dir_multibatch), out_trace_prefix + "input.txn")
            out_bin_path = os.path.join(os.path.abspath(options.out_dir_multibatch), out_trace_prefix + "trace.bin")

            with open(out_txn_path, "w") as fp:
                fp.writelines(new_lines)

            # also need to call the perl script to convert them into binary format
            # the nvdla/vp docker image has perl v5.22.1 installed, ok
            perl_script_path = os.path.join(os.path.abspath(options.nvdla_hw),
                                            "verif/verilator/input_txn_to_verilator.pl")
            os.system("perl " + perl_script_path + " " + out_txn_path + " " + out_bin_path)
    '''


if __name__ == "__main__":
    main()
