import sys
import os
import argparse
import re

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import GetAddrAttrAndMatch


class Data:
    def __init__(self):
        self.attr = None        # weight, activation, unknown
        self.addr_id = None
        self.offset = None
        self.addr = None
        self.size = None

    def valid(self):
        if self.addr_id == 0 and self.offset == 0 and self.size == 0:
            return False
        return True


class Surface:
    def __init__(self):
        self.weights = []
        self.activations = []
        self.unknowns = []


class Workload:
    def __init__(self):
        self.data = {}
        self.surfaces = []
        self.inputs = []    # once got, the inputs and outputs are sorted in reverse order of size
        self.outputs = []
        self.activations = []
        self.weights = []

        self.addr_base_map = {}

    def get_workload_info(self, in_dir):
        with open(os.path.join(in_dir, "qemu_log")) as fp:
            lines = fp.readlines()

        self.surfaces, self.data = construct_surfaces(lines)
        self.addr_base_map = get_addr_mapping(lines)

        # figure out the attributes of "unknown"
        mem_trace_path = os.path.join(in_dir, "VP_mem_rd_wr")
        assert os.path.exists(mem_trace_path)

        addr_log, sorted_addr, raw_addr_log = GetAddrAttrAndMatch.parse_rd_wr_trace(mem_trace_path)

        # set data_blk.addr and determine "unknown"
        for _, data_blk in self.data.items():
            if data_blk.addr_id != -1:
                data_blk.addr = ((self.addr_base_map[data_blk.addr_id] + data_blk.offset) & 0x0fffffff) | 0x80000000
                if data_blk.attr == "unknown":
                    addr_log_entry = addr_log[data_blk.addr]
                    read_only = True
                    for visit_pair in addr_log_entry:
                        if visit_pair[1] == 'w':
                            read_only = False
                            break
                    if read_only:
                        data_blk.attr = "weight"
                    else:
                        data_blk.attr = "activation"

        # update self.activations and self.weights
        for data_blk_key, data_blk in self.data.items():
            if data_blk.addr_id != -1:
                if data_blk.attr == "weight":
                    self.weights.append(data_blk_key)
                elif data_blk.attr == "activation":
                    self.activations.append(data_blk_key)
                else:
                    assert False

        # update unknown in self.surfaces
        for surface in self.surfaces:
            for unknown in surface.unknowns:
                if self.data[unknown].attr == "weight":
                    surface.weights.append(unknown)
                elif self.data[unknown].attr == "activation":
                    surface.activations.append(unknown)
                else:
                    assert False
            surface.unknowns = []

        self.inputs, self.outputs = get_inout_data_blks(self.data, addr_log)

        for key, val in self.data.items():
            print(val.attr, val.addr_id, hex(val.offset), hex(val.addr) if val.addr is not None else None, hex(val.size))
        print("-----------------------")

        print("inputs:")
        for ipt in self.inputs:
            print(hex(self.data[ipt].addr))
        print("outputs:")
        for opt in self.outputs:
            print(hex(self.data[opt].addr))

        print("-----------------------")
        for i, surface in enumerate(self.surfaces):
            print("surface[" + str(i) + "]:\n")
            print("weights = ", surface.weights)
            print("activations = ", surface.activations)
            print("unknowns = ", surface.unknowns)
            print("\n")


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--work-dir", default="/home/lactose/nvdla/traces/lenet_auto/",
        help="directory to put the generated sc.log, register txn and mem traces")

    args = parser.parse_args()
    return args


# @output0: a list of surfaces, each is an object of Surface class
# @output1: a dict of data objects, where key is (addr_id, offset) pair, value is an object of Data class
def construct_surfaces(lines):
    logging_surf = False    # surf = surface
    logging_data = False
    surfaces = []
    data = {}
    temp_surf = None
    temp_data = None
    for i, line in enumerate(lines):
        op_start_mark = re.findall(r"NVDLA FW ROI\[\d+\]: dla_[a-zA-Z]+_surface_desc", line)
        data_start_mark = re.findall(r"[a-zA-Z0-9]+_data\s+=\s+\[\s+dla_data_cube =>", line)

        if logging_surf:
            assert len(op_start_mark) == 0
            if "*******************************" in line:
                logging_surf = False
                surfaces.append(temp_surf)
        if logging_data:
            assert logging_surf
            assert len(data_start_mark) == 0
            if " ]" in line:    # the space before "]" differentiates from the [] for reporting time
                logging_data = False
                if temp_data.valid():
                    key = (temp_data.addr_id, temp_data.offset)
                    if key not in data.keys():
                        data[key] = temp_data

                    if temp_data.attr == "weight":
                        temp_surf.weights.append(key)
                    elif temp_data.attr == "activation":
                        temp_surf.activations.append(key)
                    elif temp_data.attr == "unknown":
                        temp_surf.unknowns.append(key)
                    else:
                        assert False
            else:
                kv_match = re.search(r'\[.+\]?\s+([a-z_]+)\s+=\s+([0-9a-f\-]+)$', line)
                if kv_match is not None:
                    if kv_match.group(1) == "address":
                        temp_data.addr_id = int(kv_match.group(2))
                    elif kv_match.group(1) == "offset":
                        temp_data.offset = int(kv_match.group(2), 16)
                    elif kv_match.group(1) == "size":
                        temp_data.size = int(kv_match.group(2))

        if len(op_start_mark) != 0:
            logging_surf = True
            temp_surf = Surface()
        if len(data_start_mark) != 0:
            logging_data = True
            temp_data = Data()
            if "weight" in line or "wmb" in line or "wgs" in line:
                temp_data.attr = "weight"
            elif "src" in line or "dst" in line:
                temp_data.attr = "activation"
            else:
                temp_data.attr = "unknown"
    return surfaces, data


def get_addr_mapping(lines):
    addr_base_map = {}
    for line in lines:
        addr_base_match = re.search(r", got dst_ptr = (c\d+) \(index = (\d+)\)", line)
        if addr_base_match is not None:
            key = int(addr_base_match.group(2))
            val = int(addr_base_match.group(1), 16)
            if key not in addr_base_map.keys():
                addr_base_map[key] = val
            else:
                assert addr_base_map[key] == val    # the log file should agree with itself
    return addr_base_map


# different from get_input/output_addresses in GetAddrAttrAndMatch.py,
# this one is based on combining compilation info and runtime info
def get_inout_data_blks(data, addr_log):
    inputs = []
    outputs = []
    for data_blk_key, data_blk in data.items():
        if data_blk.attr == "activation" and data_blk.addr_id != -1:
            addr_entry = addr_log[data_blk.addr]
            if addr_entry[0][1] == "r":
                inputs.append(data_blk_key)
            if addr_entry[-1][1] == "w":
                outputs.append(data_blk_key)
    inputs.sort(key=lambda x: data[x].size, reverse=True)
    outputs.sort(key=lambda x: data[x].size, reverse=True)
    return inputs, outputs


def main():
    options = parse_args()
    workload = Workload()
    workload.get_workload_info(options.work_dir)


if __name__ == "__main__":
    main()
