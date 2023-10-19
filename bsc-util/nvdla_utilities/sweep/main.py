import argparse
import os
from sweeper import Sweeper


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--jsons-dir", help="Path to the directory including sweep parameter JSONs.", required=True)
    parser.add_argument(
        "--out-dir", help="Output directory for storing simulation logs.", required=True)
    parser.add_argument(
        "--vp-out-dir", help="should be the same with '--out-dir' for caffe2trace.py "
        "and pipeline_compile.py. This directory holds input.txn, sc.log mem trace, etc.", required=True)
    parser.add_argument(
        "--sim-dir", help="The directory inside gem5 disk image for the simulated system to read data from. "
        "e.g., /home/lenet/", required=True)
    parser.add_argument(
        "--nvdla-hw", default="/home/lactose/nvdla/hw/",
        help="Path to NVDLA hw repo")
    parser.add_argument(
        "--model-name", help="name of the NN model.", required=True)
    parser.add_argument(
        "--rerun-cpt", action="store_true", default=False, help="Whether to regenerate a checkpoint.")
    parser.add_argument(
        "--disk-image", default="/home/lactose/gem5_linux_images/ubuntu-18.04-arm64-docker.img",
        help="path to the disk image for full system simulation")
    parser.add_argument(
        "--gem5-binary", help="Path to the gem5 binary.")
    parser.add_argument(
        "--run-points", action="store_true", default=False,
        help="Option to run the generated data points.")
    parser.add_argument(
        "--num-threads", type=int, default=8,
        help="Number of threads used to run the data points.")
    parser.add_argument(
        "--scheduler", help="The name of binary to run on simulated system,"
        "scheduler=xx means path_to_scheduler_in_disk_image=/home/xx", required=True)

    args = parser.parse_args()

    if not args.gem5_binary:
        args.gem5_binary = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "../../../build/ARM/gem5.opt"))

    sweeper = Sweeper(args)

    # Start enumerating all the data points.
    sweeper.enumerate_all()

    # Start running simulations for all the generated data points.
    if args.run_points:
        sweeper.run_all(args=args)


if __name__ == "__main__":
    main()
