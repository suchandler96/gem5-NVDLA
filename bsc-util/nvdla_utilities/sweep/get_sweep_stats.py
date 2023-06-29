import os
import re
import collections
import argparse
from os.path import exists
from params import *
from sweeper import param_types


sweep_header = ["sweep_name", "sweep_id"]
default_var_header = ["numNVDLA", "dma-enable", "embed-spm-size", "add-accel-private-cache",
                      "accel-pr-cache-size", "add-accel-shared-cache", "accel-sh-cache-size",
                      "pft-enable"]
stats_pr_header = ["nvdla_cycles"]
stats_sh_header = []
# `pr`(private) stuffs may have multiple instances when using multiple NVDLAs
# `sh`(shared) stuffs have only one instance

stat_header2identifier = {   # including both private and shared stats' headers
    "nvdla_cycles": "nvdla_cycles"
}

max_num_nvdla = 0


def get_max_num_nvdla(options):
    global max_num_nvdla
    for root, dirs, files in sorted(os.walk(options.get_root_dir), key=natural_keys):
        if "run.sh" in files:
            num_nvdla = int(NumNVDLAParam.get(root))
            max_num_nvdla = max(max_num_nvdla, num_nvdla)


def get_var_header(options):
    global default_var_header
    if (options.var is None):
        return default_var_header
    else:
        return options.var


def get_sweep_var(sweep_dir, var_header):
    global max_num_nvdla
    ret = []
    for var in var_header:
        var_value_str = param_types[var].get(sweep_dir)
        if var == "numNVDLA":
            max_num_nvdla = max(max_num_nvdla, int(var_value_str))
        ret.append(var_value_str)
    
    return ret


def get_stats_header(options):
    global stats_pr_header
    global stats_sh_header
    global max_num_nvdla
    ret = []
    if options.pr_stats is not None:
        stats_pr_header = options.pr_stats

    for item in stats_pr_header:
        for accel_id in range(max_num_nvdla):
            ret += [item + "[" + str(accel_id) + "]"]

    if options.sh_stats is None:
        ret += stats_sh_header
    else:
        ret += options.sh_stats
        stats_sh_header = options.sh_stats
    return ret


def get_sweep_stats(options, sweep_dir):
    global stat_header2identifier
    global stats_pr_header
    global stats_sh_header
    global max_num_nvdla

    stats_txt_path = os.path.abspath(os.path.join(sweep_dir, "stats.txt"))
    assert exists(stats_txt_path)

    sweep_stats = []

    with open(stats_txt_path, "r") as fp:
        stat_lines = fp.readlines()

    for item in stats_pr_header:
        item_vals_of_nvdlas = [0 for _ in range(max_num_nvdla)]
        identifier = stat_header2identifier[item]

        for line in stat_lines:

            if identifier in line:
                accel_id_match = re.search(r"accel_([0-9]+)", line)
                if accel_id_match:
                    accel_id = accel_id_match.group(1)
                else:
                    assert False

                stat_val_match = re.search(r"%s\s+([0-9]+)\s+#" % identifier, line)
                # suppose each line must end with a comment

                assert stat_val_match
                stat_val = stat_val_match.group(1)
                if int(accel_id) < max_num_nvdla:
                    item_vals_of_nvdlas[int(accel_id)] = stat_val

        sweep_stats += item_vals_of_nvdlas

    for item in stats_sh_header:
        identifier = stat_header2identifier[item]
        for line in stat_lines:
            if identifier in line:
                stat_val_match = re.search(r"%s\s+([0-9]+)\s+#" % identifier, line)
                if stat_val_match is None:
                    continue
                stat_val = stat_val_match.group(1)
                sweep_stats += stat_val
                break
                # assume the identifier is strong enough that it only matches the regex throughout the whole stats.txt file ONCE
    return sweep_stats


def atoi(text):
    return int(text) if text.isdigit() else text


def natural_keys(tuple_of_root_dirs_files):
    text = tuple_of_root_dirs_files[0].replace("MB", "000000").replace("kB", "000").replace("KB", "000")
    return [atoi(c) for c in re.split('(\d+)', text)]


def parse_args():
    parser = argparse.ArgumentParser(description="get_sweep_stats.py options")
    parser.add_argument('--var', nargs='+', type=str, help="Variables of concern")
    parser.add_argument('--pr-stats', nargs='+', type=str, help="private (of a certain NVDLA) stat metrics of concern")
    parser.add_argument('--sh-stats', nargs='+', type=str, help="shared (among NVDLAs) stat metrics of concern")
    parser.add_argument("--get-root-dir", "-d", type=str, required=True,
                        help="path to the root dir containing a set of experiments")
    parser.add_argument("--out-dir", "-o", type=str, default=".",
                        help="path to the directory to store the summary of a set of experiments")
    parser.add_argument("--out-prefix", "-p", type=str, default="", required=True,
                        help="the prefix to the output file name")

    return parser.parse_args()


def summary(options):
    global sweep_header

    get_max_num_nvdla(options)
    # first hold all the headers in a 2D list
    var_header = get_var_header(options)
    stats_header = get_stats_header(options)
    whole_table = [sweep_header + var_header + stats_header]

    for root, dirs, files in sorted(os.walk(options.get_root_dir), key=natural_keys):
        if "run.sh" in files:
            # this is a directory that holds all data for an experiment
            dir_level = os.path.abspath(root).split(os.sep)
            sweep_id = dir_level[-1]
            sweep_name = os.path.relpath(os.path.abspath(os.path.join(root, "..")), options.get_root_dir)
            
            this_pt_list = [sweep_name, sweep_id] + get_sweep_var(root, var_header) + get_sweep_stats(options, root)
            this_pt_list = [str(e) for e in this_pt_list]
            whole_table.append(this_pt_list)

    with open(os.path.join(options.out_dir, options.out_prefix + "summary.csv"), "w") as fp:
        for line_list in whole_table:
            fp.write(",".join(line_list))
            fp.write("\n")


def main():
    options = parse_args()
    summary(options)


if __name__ == "__main__":
    main()
