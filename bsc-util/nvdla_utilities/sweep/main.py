import argparse
import os
from sweeper import Sweeper

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--jsons-dir", help="Path to the directory including sweep parameter JSONs.", required=True)
    parser.add_argument(
        "--output-dir", "-o", help="Output directory for generating the data points.", required=True)
    parser.add_argument(
        "--cpt-dir", help="checkpoint directory as simulation starting point.",
        required=True)
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
    parser.add_argument(
        "--params", type=str, metavar="param", nargs='+',
        help="parameters to be passed to the scheduler")

    args = parser.parse_args()

    if not args.gem5_binary:
        args.gem5_binary = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "../../../build/ARM/gem5.opt"))

    sweeper = Sweeper(os.path.abspath(args.cpt_dir), os.path.abspath(args.output_dir), os.path.abspath(args.jsons_dir),
        args.gem5_binary, "/home/" + args.scheduler, args.params)

    # Start enumerating all the data points.
    sweeper.enumerate_all()

    # Start running simulations for all the generated data points.
    if args.run_points:
        sweeper.run_all(threads=args.num_threads)


if __name__ == "__main__":
    main()
