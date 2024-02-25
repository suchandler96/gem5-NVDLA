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
    '''
    parser.add_argument(
        "--prototxt", help="Path to the Caffe prototxt")
    parser.add_argument(
        "--split-at", type=str, metavar="var", nargs='+',
        help="a list of blob names in the provided prototxt so that the upstream "
        "and downstream will be split into separate pipeline stages")
    '''
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
        "--out-dir", default="/home/nvdla/traces/lenet_pipeline/",
        help="directory to put the generated sc.log, register txn and mem traces")
    parser.add_argument(
        "--convert-only", action="store_true", default=False, help="Whether to assume the presence of sc.log "
        "and only do processing only")

    args = parser.parse_args()

    return args


def main():
    options = parse_args()
    num_stages = len(options.prototxts)
    work_dirs = []
    for i in range(num_stages):
        work_dir = os.path.join(os.path.abspath(options.out_dir), "stage_" + str(i + 1))
        work_dirs.append(work_dir)
        os.makedirs(work_dir, exist_ok=True)

        assert os.path.exists(options.prototxts[i])
        os.system("cp " + options.prototxts[i] + " " + work_dir)
        run_cmd = "cd " + os.path.dirname(os.path.abspath(__file__)) + \
                  " && python3.6 caffe2trace.py --model-name " + options.model_name + \
                  "_stage_" + str(i + 1) + " --caffemodel " + os.path.abspath(options.caffemodel) + " --prototxt " + \
                  os.path.abspath(options.prototxts[i]) + \
                  " --nvdla-compiler " + os.path.abspath(options.nvdla_compiler) + " --qemu-bin " + \
                  os.path.abspath(options.qemu_bin) + " --qemu-lua " + os.path.abspath(options.qemu_lua) + \
                  " --out-dir " + work_dir
        if options.convert_only:
            run_cmd += " --convert-only"
        os.system(run_cmd)


if __name__ == "__main__":
    main()
