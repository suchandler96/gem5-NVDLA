import argparse
import os
from sweeper import Sweeper


def main():
    parser = argparse.ArgumentParser()

    # working directories
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

    # dependencies
    parser.add_argument(
        "--home", default="~", help="Absolute path to the home directory. "
                                    "Useful when running in docker. The script will replace all ~'s with this path. "
                                    "So if data points are generated on the host machine and experiments are to be "
                                    "run in the docker environment, one may use --home /home")
    parser.add_argument(
        "--disk-image", default="~/gem5_linux_images/ubuntu-18.04-arm64-docker.img",
        help="path to the disk image for full system simulation")
    parser.add_argument(
        "--gem5-binary", help="Path to the gem5 binary.")

    # workload-related info
    parser.add_argument(
        "--model-name", help="name of the NN model.", required=True)
    parser.add_argument(
        "--scheduler", help="The name of binary to run on simulated system,"
                            "scheduler=xx means path_to_scheduler_in_disk_image=/home/xx", required=True)
    parser.add_argument(
        "--pipeline-batches", type=int, default=4,
        help="Number of batches in pipeline scheduling.")

    # run options
    parser.add_argument(
        "--gen-points", action="store_true", default=False,
        help="Whether to generate all the simulation points and regenerate a gem5 checkpoint.")
    parser.add_argument(
        "--run-points", action="store_true", default=False,
        help="Whether to run the generated data points.")
    parser.add_argument(
        "--skip-checkpoint", action="store_true", default=False,
        help="By default in the 'run_points' phase, machine_id=0 will do checkpointing first and "
             "modify the checkpoint paths in the directories of all simulation points. "
             "If '--skip-checkpoint' is enabled, it will assume the checkpointing step "
             "has been done and thus will skip this step.")
    parser.add_argument(
        "--num-threads", type=int, default=8,
        help="Number of threads used in each machine to run the data points.")
    parser.add_argument(
        "--machine-id", type=int, default=0,
        help="The ID of the current node. Useful when running in clusters")
    parser.add_argument(
        "--num-machines", type=int, default=1,
        help="Total numbers of nodes / machines in use. Useful when running in clusters")
    parser.add_argument(
        '--pr-stats', nargs='+', type=str,
        help="private (of a certain NVDLA) stat metrics of concern")
    parser.add_argument(
        '--sh-stats', nargs='+', type=str,
        help="shared (among NVDLAs) stat metrics of concern")

    args = parser.parse_args()

    if not args.gem5_binary:
        fast_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../build/ARM/gem5.fast"))
        if os.path.exists(fast_path):
            args.gem5_binary = fast_path
        else:
            opt_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../build/ARM/gem5.opt"))
            assert os.path.exists(opt_path), f"gem5.fast/.opt binary does not exist!"
            args.gem5_binary = opt_path

    sweeper = Sweeper(args)

    # Start enumerating all the data points.
    if args.gen_points:
        sweeper.enumerate_all()

    # Start running simulations for all the generated data points.
    if args.run_points:
        sweeper.run_all(args=args)


if __name__ == "__main__":
    main()
