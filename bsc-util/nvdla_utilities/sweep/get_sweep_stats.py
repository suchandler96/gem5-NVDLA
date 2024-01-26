import os
import re
import collections
import argparse
import json
from os.path import exists
from params import *
from sweeper import param_types


sweep_header = ["sweep_name", "sweep_id"]
compulsory_header_fields = ["ddr-type", "dma-enable", "add-accel-private-cache", "pft-enable", "cvsram-enable",
                            "cvsram-size"]
stats_pr_header = ["nvdla_cycles", "memory_cycles"]
stats_sh_header = ["num_dma_prefetch", "num_dma", "host_seconds", "simulated_seconds"]
# `pr`(private) stuffs may have multiple instances when using multiple NVDLAs
# `sh`(shared) stuffs have only one instance

stat_header2identifier = {   # including both private and shared stats' headers
    "nvdla_cycles": "nvdla_cycles",
    "memory_cycles": "memory_cycles",
    "num_dma_prefetch": "num_dma_prefetch",
    "num_dma": "num_dma",
    "host_seconds": "host_seconds",
    "simulated_seconds": "simulated_seconds"
}

max_num_nvdla = 0


def get_max_num_nvdla(options):
    global max_num_nvdla
    for root, dirs, files in sorted(os.walk(options.get_root_dir), key=natural_keys):
        if "run.sh" in files:
            num_nvdla = int(NumNVDLAParam.get(root))
            max_num_nvdla = max(max_num_nvdla, num_nvdla)


def get_num_dma_prefetch(sweep_dir):
    counter = 0
    axi_path = os.path.join(sweep_dir, "axilog")
    if os.path.exists(axi_path):
        fp = open(axi_path, "rb")

        while True:
            int64_0 = int.from_bytes(fp.read(8), byteorder="little")
            if not int64_0:
                break
            int64_1 = int.from_bytes(fp.read(8), byteorder="little")
            if (int(int64_1) >> 56) & 0xf == 0x8:
                counter += 1
        fp.close()
    return counter


def get_num_dma(sweep_dir):
    counter = 0
    axi_path = os.path.join(sweep_dir, "axilog")
    if os.path.exists(axi_path):
        fp = open(axi_path, "rb")

        while True:
            int64_0 = int.from_bytes(fp.read(8), byteorder="little")
            if not int64_0:
                break
            int64_1 = int.from_bytes(fp.read(8), byteorder="little")
            if (int(int64_1) >> 56) & 0xf == 0x3:
                counter += 1
        fp.close()
    return counter


def get_host_seconds(sweep_dir):
    lines = os.popen("cd " + sweep_dir + ' && cat stdout | grep -E "simulation time: [0-9]+ seconds"').readlines()
    time = 0
    for line in lines:
        time_match = re.search(r"simulation time: ([0-9]+) seconds", line)
        time += int(time_match.group(1))
    return time


def get_simulated_seconds(sweep_dir):
    if not os.path.exists(os.path.join(sweep_dir, "system.terminal")):
        return "0"

    st_num = os.popen("cd " + sweep_dir + ' && cat system.terminal | grep "start_time =" -c').readlines()[0].strip('\n')
    ed_num = os.popen("cd " + sweep_dir + ' && cat system.terminal | grep "end_time =" -c').readlines()[0].strip('\n')
    pipeline_sign = os.popen("cd " + sweep_dir + ' && cat system.terminal | grep "finished all ops" -c').readlines()[0].strip('\n')

    if int(st_num) != 0 and int(ed_num) != 0:   # single core pattern
        st = os.popen("cd " + sweep_dir + ' && cat system.terminal | grep "start_time ="').readlines()[0].strip('\n')
        st_time_match = re.search(r"start_time = ([0-9\.]+)", st)
        start_time = float(st_time_match.group(1))

        ed = os.popen("cd " + sweep_dir + ' && cat system.terminal | grep "end_time ="').readlines()[0].strip('\n')
        ed_time_match = re.search(r"end_time = ([0-9\.]+)", ed)
        end_time = float(ed_time_match.group(1))
        return str(end_time - start_time)
    elif int(pipeline_sign) != 0:               # pipeline pattern
        time_records = os.popen("cd " + sweep_dir + ' && cat system.terminal | grep " at t = "').readlines()
        st_time_match = re.search(r"NVDLA [0-9]+ launched batch [0-9]+ at t = ([0-9\.]+)", time_records[0])
        ed_time_match = re.search(r"NVDLA [0-9]+ finished batch [0-9]+ at t = ([0-9\.]+)", time_records[-1])
        if st_time_match is None or ed_time_match is None:
            return "0"
        return str(float(ed_time_match.group(1)) - float(st_time_match.group(1)))
    else:
        return "0"


def get_nvdla_cycles(sweep_dir):
    global max_num_nvdla
    item_vals_of_nvdlas = ["0" for _ in range(max_num_nvdla)]

    stats_txt_path = os.path.abspath(os.path.join(sweep_dir, "stats.txt"))
    sweep_stats = []
    if exists(stats_txt_path):
        with open(stats_txt_path, "r") as fp:
            stat_lines = fp.readlines()
    else:
        stat_lines = []

    for line in stat_lines:
        if "nvdla_cycles" in line:
            accel_id_match = re.search(r"accel_([0-9]+)", line)
            if accel_id_match:
                accel_id = accel_id_match.group(1)
            else:
                assert False

            stat_val_match = re.search(r"%s\s+([0-9]+)\s+" % "nvdla_cycles", line)

            assert stat_val_match
            stat_val = stat_val_match.group(1)
            if int(accel_id) < max_num_nvdla:
                item_vals_of_nvdlas[int(accel_id)] = stat_val

    return item_vals_of_nvdlas


def get_memory_cycles(sweep_dir):
    global max_num_nvdla
    item_vals_of_nvdlas = ["0" for _ in range(max_num_nvdla)]

    print_insts = []
    axi_path = os.path.join(sweep_dir, "axilog")
    if os.path.exists(axi_path):
        fp = open(os.path.join(sweep_dir, "axilog"), "rb")
        while True:
            int64_0 = int.from_bytes(fp.read(8), byteorder="little")
            if not int64_0:
                break
            int64_1 = int.from_bytes(fp.read(8), byteorder="little")
            assert int64_1
            print_insts.append((int(int64_0), int(int64_1)))
        fp.close()

    dbb_inflight = [[(0, 0)] for _ in range(max_num_nvdla)]     # [[(cycle_id, num), ...], ...]
    cvsram_inflight = [[(0, 0)] for _ in range(max_num_nvdla)]

    for print_inst in print_insts:
        print_cmd = (print_inst[1] >> 56) & 0xf
        if print_cmd == 0x0:      # read request from dla
            time = print_inst[1] & 0xffffffff
            nvdla = (print_inst[1] >> 40) & 0xff
            name = (print_inst[1] >> 48) & 0xff
            burst = (print_inst[1] >> 60) & 0xf
            if name == ord('D'):
                to_append = (time, dbb_inflight[nvdla][-1][-1] + burst + 1)
                if dbb_inflight[nvdla][-1][0] == time:
                    dbb_inflight[nvdla].pop(-1)
                dbb_inflight[nvdla].append(to_append)
            elif name == ord('C'):
                to_append = (time, cvsram_inflight[nvdla][-1][-1] + burst + 1)
                if cvsram_inflight[nvdla][-1][0] == time:
                    cvsram_inflight[nvdla].pop(-1)
                cvsram_inflight[nvdla].append(to_append)
                nvdla = (print_inst[1] >> 40) & 0xff
            else:
                assert False
        elif print_cmd == 0x4:      # read data used by nvdla
            time = print_inst[1] & 0xffffffff
            nvdla = (print_inst[1] >> 40) & 0xff
            name = (print_inst[1] >> 48) & 0xff
            if name == ord('D'):
                to_append = (time, dbb_inflight[nvdla][-1][-1] - 1)
                if dbb_inflight[nvdla][-1][0] == time:
                    dbb_inflight[nvdla].pop(-1)
                dbb_inflight[nvdla].append(to_append)
            elif name == ord('C'):
                to_append = (time, cvsram_inflight[nvdla][-1][-1] - 1)
                if cvsram_inflight[nvdla][-1][0] == time:
                    cvsram_inflight[nvdla].pop(-1)
                cvsram_inflight[nvdla].append(to_append)
            else:
                assert False

    for nvdla in range(max_num_nvdla):
        zero_inflight = []
        begin_time = min(dbb_inflight[nvdla][0][0], cvsram_inflight[nvdla][0][0])
        end_time = max(dbb_inflight[nvdla][-1][0], cvsram_inflight[nvdla][-1][0])

        if begin_time != dbb_inflight[nvdla][0][0]:
            dbb_inflight[nvdla].insert(-1, (begin_time, 0))
        if begin_time != cvsram_inflight[nvdla][0][0]:
            cvsram_inflight[nvdla].insert(-1, (begin_time, 0))
        if end_time != dbb_inflight[nvdla][-1][0]:
            dbb_inflight[nvdla].append((end_time, 1))
        if end_time != cvsram_inflight[nvdla][-1][0]:
            cvsram_inflight[nvdla].append((end_time, 1))

        zero_dbb_intervals, zero_cvsram_intervals = [], []

        for i in range(len(dbb_inflight[nvdla]) - 1):
            if dbb_inflight[nvdla][i][1] == 0 and (dbb_inflight[nvdla][i + 1][1] != 0 or
                                                   dbb_inflight[nvdla][i + 1][0] - dbb_inflight[nvdla][i][0] > 10):
                # normally interval between a series of DBB read for a sequence of continuous addresses
                # should be 2 cycles, but we use 10 to better make out those pauses for computation
                zero_dbb_intervals.append((dbb_inflight[nvdla][i][0], dbb_inflight[nvdla][i + 1][0]))

        for i in range(len(cvsram_inflight[nvdla]) - 1):
            if cvsram_inflight[nvdla][i][1] == 0 and (cvsram_inflight[nvdla][i + 1][1] != 0 or cvsram_inflight[nvdla][i + 1][0] - cvsram_inflight[nvdla][i][0] > 10):
                zero_cvsram_intervals.append((cvsram_inflight[nvdla][i][0], cvsram_inflight[nvdla][i + 1][0]))

        # get intersect
        last_cvsram_int_id = 0
        for interval in zero_dbb_intervals:
            while last_cvsram_int_id < len(zero_cvsram_intervals):
                cvsram_int = zero_cvsram_intervals[last_cvsram_int_id]
                if cvsram_int[1] <= interval[0]:    # cvsram_interval is to the left of dbb_interval
                    last_cvsram_int_id += 1
                    continue
                elif cvsram_int[0] >= interval[1]:  # cvsram_interval is to the right of dbb_interval
                    break                           # go to next dbb_interval
                else:                               # must have an intersection
                    try_cvsram_id = last_cvsram_int_id
                    while try_cvsram_id < len(zero_cvsram_intervals):
                        try_cvsram_int = zero_cvsram_intervals[try_cvsram_id]
                        if try_cvsram_int[0] >= interval[1]:    # try_cvsram_interval is to the right of dbb_interval
                            break
                        max_left = max(interval[0], try_cvsram_int[0])
                        min_right = min(interval[1], try_cvsram_int[1])
                        assert max_left < min_right
                        zero_inflight.append((max_left, min_right))
                        try_cvsram_id += 1
                    break

        zero_time = 0
        for interval in zero_inflight:
            zero_time += interval[1] - interval[0]

        item_vals_of_nvdlas[nvdla] = str(end_time - begin_time - zero_time)
    return item_vals_of_nvdlas


def get_var_header(options):
    global compulsory_header_fields
    jsons_dir = os.path.join(options.get_root_dir, "../jsons") if options.jsons_dir == "" else options.jsons_dir
    assert os.path.exists(jsons_dir), f"path '{jsons_dir}' does not exist!\n"
    header = get_swept_vars(jsons_dir)
    for entry in compulsory_header_fields:
        if entry not in header:
            header.append(entry)
    return header


def get_swept_vars(json_dir):
    swept_vars = []
    for root, dirs, files in os.walk(json_dir):
        for file in files:
            if ".json" in file:
                with open(os.path.join(root, file), "r") as fp:
                    data = json.load(fp)
                for var, to_sweep_values in data.items():
                    assert type(to_sweep_values) == list, f"weird type in json file.\n"
                    if len(to_sweep_values) >= 2 and var not in swept_vars:
                        swept_vars.append(var)
    assert len(swept_vars) != 0
    return swept_vars


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

    sweep_stats = []
    for item in stats_pr_header:
        identifier = stat_header2identifier[item]
        sweep_stats += eval("get_" + identifier + "(sweep_dir)")

    for item in stats_sh_header:
        identifier = stat_header2identifier[item]
        sweep_stats.append(eval("get_" + identifier + "(sweep_dir)"))
    return sweep_stats


def atoi(text):
    return int(text) if text.isdigit() else text


def natural_keys(tuple_of_root_dirs_files):
    text = tuple_of_root_dirs_files[0].replace("MB", "000000").replace("kB", "000").replace("KB", "000")
    return [atoi(c) for c in re.split('(\d+)', text)]


def parse_args():
    parser = argparse.ArgumentParser(description="get_sweep_stats.py options")
    parser.add_argument('--pr-stats', nargs='+', type=str, help="private (of a certain NVDLA) stat metrics of concern")
    parser.add_argument('--sh-stats', nargs='+', type=str, help="shared (among NVDLAs) stat metrics of concern")
    parser.add_argument("--jsons-dir", "-j", default="", type=str,
                        help="path to the directory containing json files")
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



# put the encoded output here:
# cmd_id    |<--------------------- pamrams ----------------------->|

# read request
# printf("(%lu) nvdla#%d %s: read request from dla, addr 0x%08lx burst %d id %d\n", wrapper->tickcount, wrapper->id_nvdla, name, *dla.ar_araddr, *dla.ar_arlen, *dla.ar_arid);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick    stream  dla name    0   burst


# write request from dla
# printf("(%lu) nvdla#%d %s: write request from dla, addr 0x%08lx id %d\n", wrapper->tickcount, wrapper->id_nvdla, name, *dla.aw_awaddr, *dla.aw_awid);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick    stream  dla name    1


# spm hit
# printf("(%lu) this spm_line_addr exists in spm, addr 0x%08lx\n", wrapper->tickcount, start_addr);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla         2


# DMA read issue
# printf("(%lu) DMA read req issued from NVDLA side, addr 0x%08lx\n", wrapper->tickcount, spm_line_addr);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla         3


# read data back to NVDLA
# printf("(%lu) read data used by nvdla#%d (returned by gem5 or already in spm), addr 0x%08lx\n", wrapper->tickcount, wrapper->id_nvdla, addr_front);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla name    4


# PREFETCH AXI back
# printf("(%lu) nvdla#%d read data returned by gem5 PREFETCH, addr 0x%08lx\n", wrapper->tickcount, wrapper->id_nvdla, addr);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla         5


# normal AXI back
# printf("(%lu) nvdla#%d read data returned by gem5, addr 0x%08lx\n", wrapper->tickcount, wrapper->id_nvdla, addr);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla         6


# DMA return (including both prefetch and normal)
# printf("(%lu) nvdla#%d AXIResponder handling DMA return data addr 0x%08lx with length %d.\n", wrapper->tickcount, wrapper->id_nvdla, addr, len);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla         7


# DMA PREFETCH issue
# printf("(%lu) nvdla#%d PREFETCH (DMA) request addr 0x%08lx issued.\n", wrapper->tickcount, wrapper->id_nvdla, spm_line_addr);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla         8


# AXI PREFETCH issue
# printf("(%lu) nvdla#%d PREFETCH request addr 0x%08lx issued.\n", wrapper->tickcount, wrapper->id_nvdla, to_issue_addr);
# 64,   (32,    8,      8,  8,      4,  4)
# addr  tick            dla         9