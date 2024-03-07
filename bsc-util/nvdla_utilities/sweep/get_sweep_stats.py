import os
import re
import collections
import argparse
import json
from os.path import exists
from params import *
from sweeper import param_types


class PerformanceCollector:
    def __init__(self, get_root_dir, jsons_dir="", pr_stats=None, sh_stats=None):
        if get_root_dir == "":      # invalid input
            return

        self.get_root_dir = get_root_dir
        self.sweep_header = ["sweep_name", "sweep_id"]
        self.compulsory_header_fields = ["ddr-type", "dma-enable", "add-accel-private-cache", "pft-enable",
                                         "cvsram-enable", "cvsram-size"]
        self.stats_pr_header = ["nvdla_cycles", "memory_cycles"] if pr_stats is None else pr_stats
        self.stats_sh_header = ["num_dma_prefetch", "num_dma", "host_seconds", "simulated_seconds"] \
            if sh_stats is None else sh_stats
        # `pr`(private) stuffs may have multiple instances when using multiple NVDLAs
        # `sh`(shared) stuffs have only one instance

        # headers to be retrieved from sweep_dir, to be updated below
        self.var_header = []
        self.stats_header = []

        self.stat_header2identifier = {   # including both private and shared stats' headers
            "nvdla_cycles": "nvdla_cycles",
            "memory_cycles": "memory_cycles",
            "num_dma_prefetch": "num_dma_prefetch",
            "num_dma": "num_dma",
            "host_seconds": "host_seconds",
            "simulated_seconds": "simulated_seconds"
        }
        self.max_num_nvdla = 0
        for root, dirs, files in sorted(os.walk(get_root_dir), key=natural_keys):
            if "run.sh" in files:
                with open(os.path.join(root, "run.sh")) as fp:
                    run_sh_lines = fp.readlines()
                num_nvdla = int(NumNVDLAParam.get(root, run_sh_lines))
                self.max_num_nvdla = max(self.max_num_nvdla, num_nvdla)

        # per-testcase buffers and statistics
        self.sweep_dir = None
        self.axilog = None
        self.num_dma_prefetch = 0
        self.num_dma = 0
        self.dbb_inflight = []     # [[(cycle_id, num), ...], ...]
        self.cvsram_inflight = []

        # get self.var_header
        jsons_dir = os.path.join(get_root_dir, "../jsons") if jsons_dir == "" else jsons_dir
        assert os.path.exists(jsons_dir), f"path '{jsons_dir}' does not exist!\n"

        swept_vars = set(self.compulsory_header_fields)
        for root, dirs, files in os.walk(jsons_dir):
            for file in files:
                if ".json" in file:
                    with open(os.path.join(root, file), "r") as fp:
                        data = json.load(fp)
                    for var, to_sweep_values in data.items():
                        assert type(to_sweep_values) == list, f"weird type in json file.\n"
                        if len(to_sweep_values) >= 2:
                            swept_vars.add(var)
        self.var_header = list(swept_vars)
        self.var_header.sort()

        # get self.stats_header
        for item in self.stats_pr_header:
            for accel_id in range(self.max_num_nvdla):
                self.stats_header += [item + "[" + str(accel_id) + "]"]
        self.stats_header += self.stats_sh_header

    def init_testcase(self, sweep_dir):
        self.sweep_dir = sweep_dir
        self.num_dma_prefetch = 0
        self.num_dma = 0

        self.dbb_inflight = [[(0, 0)] for _ in range(self.max_num_nvdla)]     # [[(cycle_id, num), ...], ...]
        self.cvsram_inflight = [[(0, 0)] for _ in range(self.max_num_nvdla)]

        axi_path = os.path.join(sweep_dir, "axilog")
        if os.path.exists(axi_path):
            with open(axi_path, "rb") as fp:
                self.axilog = fp.read()
        else:
            self.axilog = bytes()

        byte_id = 0
        while byte_id < len(self.axilog):
            int64_0 = int.from_bytes(self.axilog[byte_id: byte_id + 8], byteorder="little")
            byte_id += 8
            int64_1 = int.from_bytes(self.axilog[byte_id: byte_id + 8], byteorder="little")
            byte_id += 8
            print_cmd = (int64_1 >> 56) & 0xf
            if print_cmd == 0x8:        # dma_prefetch
                self.num_dma_prefetch += 1
            elif print_cmd == 0x3:      # dma
                self.num_dma += 1
            elif print_cmd == 0x0:      # read request from dla
                time = int64_1 & 0xffffffff
                nvdla = (int64_1 >> 40) & 0xff
                name = (int64_1 >> 48) & 0xff
                burst = (int64_1 >> 60) & 0xf
                if name == ord('D'):
                    to_append = (time, self.dbb_inflight[nvdla][-1][-1] + burst + 1)
                    if self.dbb_inflight[nvdla][-1][0] == time:
                        self.dbb_inflight[nvdla].pop(-1)
                    self.dbb_inflight[nvdla].append(to_append)
                elif name == ord('C'):
                    to_append = (time, self.cvsram_inflight[nvdla][-1][-1] + burst + 1)
                    if self.cvsram_inflight[nvdla][-1][0] == time:
                        self.cvsram_inflight[nvdla].pop(-1)
                    self.cvsram_inflight[nvdla].append(to_append)
                else:
                    assert False
            elif print_cmd == 0x4:      # read data used by nvdla
                time = int64_1 & 0xffffffff
                nvdla = (int64_1 >> 40) & 0xff
                name = (int64_1 >> 48) & 0xff
                if name == ord('D'):
                    to_append = (time, self.dbb_inflight[nvdla][-1][-1] - 1)
                    if self.dbb_inflight[nvdla][-1][0] == time:
                        self.dbb_inflight[nvdla].pop(-1)
                    self.dbb_inflight[nvdla].append(to_append)
                elif name == ord('C'):
                    to_append = (time, self.cvsram_inflight[nvdla][-1][-1] - 1)
                    if self.cvsram_inflight[nvdla][-1][0] == time:
                        self.cvsram_inflight[nvdla].pop(-1)
                    self.cvsram_inflight[nvdla].append(to_append)
                else:
                    assert False

    def get_num_dma_prefetch(self):
        return self.num_dma_prefetch

    def get_num_dma(self):
        return self.num_dma

    def get_host_seconds(self):
        lines = os.popen("cd " + self.sweep_dir + ' && cat stdout | grep -E "simulation time: [0-9]+ seconds"').readlines()
        time = 0
        for line in lines:
            time_match = re.search(r"simulation time: ([0-9]+) seconds", line)
            time += int(time_match.group(1))
        return str(time)

    def get_simulated_seconds(self):
        if not os.path.exists(os.path.join(self.sweep_dir, "system.terminal")):
            return "0"

        st_num = os.popen("cd " + self.sweep_dir + ' && cat system.terminal | grep "start_time =" -c').readlines()[0].strip('\n')
        ed_num = os.popen("cd " + self.sweep_dir + ' && cat system.terminal | grep "end_time =" -c').readlines()[0].strip('\n')
        pipeline_sign = os.popen("cd " + self.sweep_dir + ' && cat system.terminal | grep "finished all ops" -c').readlines()[0].strip('\n')

        if int(st_num) != 0 and int(ed_num) != 0:   # single core pattern
            st = os.popen("cd " + self.sweep_dir + ' && cat system.terminal | grep "start_time ="').readlines()[0].strip('\n')
            st_time_match = re.search(r"start_time = ([0-9\.]+)", st)
            start_time = float(st_time_match.group(1))

            ed = os.popen("cd " + self.sweep_dir + ' && cat system.terminal | grep "end_time ="').readlines()[0].strip('\n')
            ed_time_match = re.search(r"end_time = ([0-9\.]+)", ed)
            end_time = float(ed_time_match.group(1))
            return str(end_time - start_time)
        elif int(pipeline_sign) != 0:               # pipeline pattern
            time_records = os.popen("cd " + self.sweep_dir + ' && cat system.terminal | grep " at t = "').readlines()
            st_time_match = re.search(r"NVDLA [0-9]+ launched batch [0-9]+ at t = ([0-9\.]+)", time_records[0])
            ed_time_match = re.search(r"NVDLA [0-9]+ finished batch [0-9]+ at t = ([0-9\.]+)", time_records[-1])
            if st_time_match is None or ed_time_match is None:
                return "0"
            return str(float(ed_time_match.group(1)) - float(st_time_match.group(1)))
        else:
            return "0"

    def get_nvdla_cycles(self):
        item_vals_of_nvdlas = ["0" for _ in range(self.max_num_nvdla)]

        stats_txt_path = os.path.abspath(os.path.join(self.sweep_dir, "stats.txt"))
        if exists(stats_txt_path):
            stat_lines = os.popen("cat " + stats_txt_path + " | grep nvdla_cycles").readlines()
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
                if int(accel_id) < self.max_num_nvdla:
                    item_vals_of_nvdlas[int(accel_id)] = stat_val

        return item_vals_of_nvdlas

    def get_memory_cycles(self):
        item_vals_of_nvdlas = ["0" for _ in range(self.max_num_nvdla)]

        for nvdla in range(self.max_num_nvdla):
            begin_time = min(self.dbb_inflight[nvdla][0][0], self.cvsram_inflight[nvdla][0][0])
            end_time = max(self.dbb_inflight[nvdla][-1][0], self.cvsram_inflight[nvdla][-1][0])

            if begin_time != self.dbb_inflight[nvdla][0][0]:
                self.dbb_inflight[nvdla].insert(-1, (begin_time, 0))
            if begin_time != self.cvsram_inflight[nvdla][0][0]:
                self.cvsram_inflight[nvdla].insert(-1, (begin_time, 0))
            if end_time != self.dbb_inflight[nvdla][-1][0]:
                self.dbb_inflight[nvdla].append((end_time, 1))
            if end_time != self.cvsram_inflight[nvdla][-1][0]:
                self.cvsram_inflight[nvdla].append((end_time, 1))

            zero_dbb_intervals, zero_cvsram_intervals = [], []

            for i in range(len(self.dbb_inflight[nvdla]) - 1):
                if self.dbb_inflight[nvdla][i][1] == 0 and \
                    (self.dbb_inflight[nvdla][i + 1][1] != 0 or
                     self.dbb_inflight[nvdla][i + 1][0] - self.dbb_inflight[nvdla][i][0] > 10):
                    # normally interval between a series of DBB read for a sequence of continuous addresses
                    # should be 2 cycles, but we use 10 to better make out those pauses for computation
                    zero_dbb_intervals.append((self.dbb_inflight[nvdla][i][0], self.dbb_inflight[nvdla][i + 1][0]))

            for i in range(len(self.cvsram_inflight[nvdla]) - 1):
                if self.cvsram_inflight[nvdla][i][1] == 0 and \
                    (self.cvsram_inflight[nvdla][i + 1][1] != 0 or
                     self.cvsram_inflight[nvdla][i + 1][0] - self.cvsram_inflight[nvdla][i][0] > 10):
                    zero_cvsram_intervals.append((self.cvsram_inflight[nvdla][i][0], self.cvsram_inflight[nvdla][i + 1][0]))

            # get intersect
            last_cvsram_int_id = 0
            zero_time = 0
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
                            zero_time += (min_right - max_left)
                            try_cvsram_id += 1
                        break

            item_vals_of_nvdlas[nvdla] = str(end_time - begin_time - zero_time)
        return item_vals_of_nvdlas

    def get_var_val(self):
        ret = []
        run_sh_path = os.path.join(self.sweep_dir, "run.sh")
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()
        for var in self.var_header:
            var_value_str = param_types[var].get(self.sweep_dir, run_sh_lines)
            ret.append(var_value_str)

        return ret

    def get_stat_val(self):
        sweep_stats = []
        for item in self.stats_pr_header:
            identifier = self.stat_header2identifier[item]
            sweep_stats += eval("self.get_" + identifier + "()")

        for item in self.stats_sh_header:
            identifier = self.stat_header2identifier[item]
            sweep_stats.append(eval("self.get_" + identifier + "()"))
        return sweep_stats

    def write_to_sweep_dir(self):
        dir_level = os.path.abspath(self.sweep_dir).split(os.sep)
        sweep_id = dir_level[-1]
        sweep_name = os.path.relpath(os.path.abspath(os.path.join(self.sweep_dir, "..")), self.get_root_dir)
        this_pt_list = [sweep_name, sweep_id] + self.get_var_val() + self.get_stat_val()
        this_pt_list = [str(e) for e in this_pt_list]
        this_pt_table = [self.sweep_header + self.var_header + self.stats_header, this_pt_list]
        with open(os.path.join(self.sweep_dir, sweep_id + "_summary.csv"), "w") as fp:
            for line_list in this_pt_table:
                fp.write(",".join(line_list))
                fp.write("\n")


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


def summarize(options):
    perf = PerformanceCollector(options.get_root_dir, options.jsons_dir, options.pr_stats, options.sh_stats)
    # first hold all the headers in a 2D list
    whole_table = [perf.sweep_header + perf.var_header + perf.stats_header]

    for root, dirs, files in sorted(os.walk(options.get_root_dir), key=natural_keys):
        if "run.sh" in files:
            # this is a directory that holds all data for an experiment
            dir_level = os.path.abspath(root).split(os.sep)
            sweep_id = dir_level[-1]
            if sweep_id + "_summary.csv" in files:  # statistics have been generated right after simulation
                with open(os.path.join(root, sweep_id + "_summary.csv")) as fp:
                    this_pt_line = fp.readlines()[1].strip()
                    this_pt_list = this_pt_line.split(',')
            else:
                perf.init_testcase(root)
                sweep_name = os.path.relpath(os.path.abspath(os.path.join(root, "..")), options.get_root_dir)
                this_pt_list = [sweep_name, sweep_id] + perf.get_var_val() + perf.get_stat_val()
                this_pt_list = [str(e) for e in this_pt_list]
            whole_table.append(this_pt_list)

    with open(os.path.join(options.out_dir, options.out_prefix + "_summary.csv"), "w") as fp:
        for line_list in whole_table:
            fp.write(",".join(line_list))
            fp.write("\n")


def main():
    options = parse_args()
    summarize(options)


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