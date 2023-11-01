import sys
import os
import argparse
import re

sys.path.append(os.path.dirname(os.path.abspath(__file__)))


class Data:
    def __init__(self):
        """ compilation-time info """
        self.attr = None        # weight, activation, unknown
        self.addr_id = None
        self.offset = None
        self.addr = None        # offset 0x80000000 by default
        self.size = None

        """runtime info"""
        self.num_access = 0     # calculated by the number of accesses of last addr aligned to 0x40
        self.liveness = [None, None]    # (rw_id of first access of first element, rw_id of last access pf last element)

    def valid(self):
        if self.addr_id <= 0 or (self.addr_id == 0 and self.offset == 0 and self.size == 0):
            return False
        return True


class Surface:
    def __init__(self):
        self.weights = []
        self.activations = []
        self.unknowns = []


class Workload:
    def __init__(self, in_dir):
        self.in_dir = in_dir        # each workload corresponds to a directory of log files
        self.data = {}              # {(addr_id, offset): class Data}
        self.surfaces = []          # [class Surface, ...]
        self.inputs = []            # once got, the inputs and outputs are sorted in reverse order of size
        self.outputs = []           # [(addr_id, offset), ...]
        self.activations = []
        self.intermediate_act = []  # [(addr_id, offset), ...]
        self.weights = []

        self.addr_base_map = {}     # {addr_base_id: addr_base_val}; addr_base_val starts from 0xc0000000

        self.addr_log = None        # = {addr: [[rw_id, 'r' or 'w'], ...]}
        self.sorted_addr = None     # = [addr, ...]
        self.raw_addr_log = None    # = [['r' or 'w', addr], ...]
        self.rd_only_vars = []      # = [[addr, len], ...]

        with open(os.path.join(self.in_dir, "qemu_log")) as fp:
            lines = fp.readlines()

        self.surfaces, self.data = construct_surfaces(lines)
        self.addr_base_map = get_addr_mapping(lines)

        # figure out the attributes of "unknown"
        mem_trace_path = os.path.join(self.in_dir, "VP_mem_rd_wr")
        assert os.path.exists(mem_trace_path)

        self.addr_log, self.sorted_addr, self.raw_addr_log = parse_rd_wr_trace(mem_trace_path)

        # set data_blk.addr and determine "unknown"
        for _, data_blk in self.data.items():
            if data_blk.addr_id != -1:
                data_blk.addr = ((self.addr_base_map[data_blk.addr_id] + data_blk.offset) & 0x0fffffff) | 0x80000000
                if data_blk.attr == "unknown":
                    addr_log_entry = self.addr_log[data_blk.addr]
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

        self.inputs, self.outputs = get_inout_data_blks(self.data, self.addr_log)

        for act in self.activations:
            if act not in self.inputs and act not in self.outputs:
                self.intermediate_act.append(act)

        rd_only_addr2desc = {}
        for i in self.inputs:
            rd_only_addr2desc[self.data[i].addr] = i
        for w in self.weights:
            rd_only_addr2desc[self.data[w].addr] = w

        for rw_and_addr in self.raw_addr_log:
            # rw_and_addr[0] is 'r' or 'w', while rw_and_addr[1] is the raw address
            if 'r' in rw_and_addr[0] and rw_and_addr[1] in rd_only_addr2desc.keys():
                self.rd_only_vars.append(rd_only_addr2desc[rw_and_addr[1]])

        # do liveness analysis for each Data object
        for _, data_blk in self.data.items():
            first = data_blk.addr
            last = last_aligned(data_blk.addr, data_blk.size, 0x40)
            assert first in self.addr_log
            assert last in self.addr_log
            data_blk.liveness = (self.addr_log[first][0][0], self.addr_log[last][-1][0])
            data_blk.num_access = len(self.addr_log[last])

    def write_rd_only_var_log(self, rd_var_log_path):
        with open(rd_var_log_path, "wb") as fp:
            for rd_only_var in self.rd_only_vars:
                data_blk = self.data[rd_only_var]
                fp.write(data_blk.addr.to_bytes(4, byteorder="little", signed=False))
                fp.write(data_blk.size.to_bytes(4, byteorder="little", signed=False))

    # read compile_log and read ALLOC and DEALLOC info
    def read_compile_log(self):
        with open(os.path.join(self.in_dir, "compile_log")) as fp:
            lines = fp.readlines()

        desc2uid = {}   # {(addr_id, offset, size): (tsd_str, tb_str)}, to examine the mapping is a bijection
        uid2desc = {}
        # first build the above two mappings
        for line_id, line in enumerate(lines):
            if "(Surface) Address list entry for" in line:
                name_match = re.search(r"tsd=(tsd-\d+)/(tb-\d+):\d+ -> (\d+) offset=(\d+) size=(\d+)", line)
                uid = (name_match.group(1), name_match.group(2))
                desc = (int(name_match.group(3)), int(name_match.group(4)))
                if desc in self.data:
                    print(uid, desc, hex(self.data[desc].addr))
                    assert desc not in desc2uid
                    desc2uid[desc] = uid
                    uid2desc[uid] = desc
                    if desc in self.data:
                        data_blk = self.data[desc]
                        assert data_blk.uid is None     # assume each tensor is reported once
                        data_blk.uid = uid

        # then get ALLOC and DEALLOC time info
        for line_id, line in enumerate(lines):
            if "[MEMTOOL]" in line:
                time_match = re.search(r"t = (\d)+", line)
                time_stamp = int(time_match.group(1))
                tb_match = re.search(r"tb-\d+", line)
                if "DEALLOC" in line:
                    assert "deallocating " in lines[line_id - 1]
                    assert tb_match.group(0) in lines[line_id - 1]
                    uid_match = re.search(r"(tsd-\d+)/(tb-\d+)", lines[line_id - 1])
                    assert uid_match.group(2) == tb_match.group(0)
                    uid = (uid_match.group(1), uid_match.group(2))
                    desc = uid2desc[uid]
                    if desc in self.data:
                        self.data[desc].liveness[1] = time_stamp
                elif "ALLOC" in line:
                    assert "resolve placement/alloc for" in lines[line_id - 1]
                    assert tb_match.group(0) in lines[line_id - 1]
                    uid_match = re.search(r"(tsd-\d+)/(tb-\d+)", lines[line_id - 1])
                    # hw_match = re.search(r"\s+[a-z-]+-\d+'s", line)
                    assert uid_match.group(2) == tb_match.group(0)
                    uid = (uid_match.group(1), uid_match.group(2))
                    desc = uid2desc[uid]
                    if desc in self.data:
                        self.data[desc].liveness[0] = time_stamp
                else:
                    assert False

    def print_workload_info(self):
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


def last_aligned(start_addr, size, alignment):
    return ((start_addr + size - 0x1) // alignment) * alignment


def get_addr_mapping(lines):
    addr_base_map = {}
    for line in lines:
        addr_base_match = re.search(r", got dst_ptr = (c[0-9a-f]+) \(index = (\d+)\)", line)
        if addr_base_match is not None:
            key = int(addr_base_match.group(2))
            val = int(addr_base_match.group(1), 16)
            if key not in addr_base_map.keys():
                addr_base_map[key] = val
            else:
                assert addr_base_map[key] == val    # the log file should agree with itself
    return addr_base_map


# based on combining compilation info and runtime info
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


# @output addr_log = {addr: [[rw_id, 'r' or 'w'], ...]}
# @output sorted_addr = [addr, ...]
# @output addresses = [['r' or 'w', addr], ...]
def parse_rd_wr_trace(file_name):
    with open(file_name) as fp:
        lines = fp.readlines()
    addresses = [line.split() for line in lines]
    sorted_addr = []
    for i in range(len(addresses)):
        addr_val = int(addresses[i][1], 16)
        addresses[i][1] = addr_val
        sorted_addr.append(addr_val)
    sorted_addr = list(set(sorted_addr))
    sorted_addr.sort()

    addr_log = {}
    for i in range(len(addresses)):
        addr = addresses[i][1]
        if addr in addr_log:
            addr_log[addr].append([i, addresses[i][0]])
        else:
            addr_log[addr] = [[i, addresses[i][0]]]

    return addr_log, sorted_addr, addresses


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--work-dir", default="/home/lactose/nvdla/traces/lenet_auto/",
        help="directory to put the generated sc.log, register txn and mem traces")

    args = parser.parse_args()
    return args


def main():
    options = parse_args()
    workload = Workload(options.work_dir)
    workload.print_workload_info()


if __name__ == "__main__":
    main()
