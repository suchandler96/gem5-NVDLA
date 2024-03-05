import sys
import os
import re

sys.path.append(os.path.dirname(os.path.abspath(__file__)))

"""
tsd: tensor surface descriptor
tb(d): tensor buffer (descriptor)
"""


class Data:
    def __init__(self):
        """ compilation-time info """
        self.attr = None        # weight, activation, unknown
        self.addr_id = None
        self.offset = None
        self.size = None
        self.width = None
        self.height = None
        self.channel = None
        self.line_stride = None
        self.surf_stride = None
        self.plane_stride = None

    def valid(self):
        if self.addr_id <= 0 or (self.addr_id == 0 and self.offset == 0 and self.size == 0):
            return False
        return True


class TensorSurface:
    def __init__(self, tsd, tb, addr_id, offset, addr_base_map):
        """ compilation-time info """
        self.tsd_name = tsd     # each Data has a unique tsd_name
        self.tb_name = tb       # but multiple Data objects may belong to the same tb
        self.addr_id = addr_id
        self.offset = offset

        self.addr = None        # offset 0xc0000000 by default
        self.size = None        # will only be correct for input/output/weight tensor surfaces

        if self.addr_id in addr_base_map:
            self.addr = addr_base_map[self.addr_id] + self.offset


class TensorBuffer:
    def __init__(self, tb, addr_id, size, addr_base_map):
        self.tb_name = tb
        self.addr_id = addr_id if addr_id in addr_base_map else None
        # generally 1 are weights and 2 are activations (w/o inputs and outputs)

        self.offset = None
        self.size = size
        self.tsd_list = []

        self.addr = None
        self.liveness = None
        self.num_access = None

    def add_tensor_surface(self, ts_name, ts, addr_base_map):
        assert ts_name not in self.tsd_list
        if ts.addr_id in addr_base_map:     # otherwise this TensorBuffer will be deleted. Don't care
            if self.offset is None:
                self.addr_id = ts.addr_id   # self.addr_id can be None during construction
                self.offset = ts.offset
                self.addr = addr_base_map[self.addr_id] + self.offset
            elif ts.addr < self.addr:     # update to a new addr_id and offset
                self.offset = ts.offset
                self.addr_id = ts.addr_id
                self.addr = ts.addr
            self.tsd_list.append(ts_name)


class Surface:
    def __init__(self):
        self.weights = []
        self.activations = []
        self.unknowns = []


class Workload:
    def __init__(self, in_dir, in_compilation=False, use_real_data=False, dump_results=False, axi_width=0x40):
        self.in_dir = in_dir        # each workload corresponds to a directory of log files
        self.tb = {}                # tensor buffers = {tb_name: TensorBuffer}
        self.ts = {}                # tensor surfaces = {ts_name: TensorSurface}

        self.in_tb = []             # input tensor surfaces: [class TensorBuffer, ...]
        self.out_tb = []            # output tensor surfaces: [class TensorBuffer, ...]
        self.act_tb = []            # activation tensor buffers
        self.itm_act_tb = []        # intermediate activation tensor buffers
        self.w_tb = []              # weight tensor buffers

        self.rd_only_tbs = []       # = [tb_name]

        self.axi_width = axi_width  # in bytes

        self.addr_base_map = {}     # {addr_base_id: addr_base_val}; addr_base_val starts from 0xc0000000

        self.addr_log = None        # = {addr: [[rw_id, 'r' or 'w'], ...]}
        self.sorted_addr = None     # = [addr, ...]
        self.raw_addr_log = None    # = [['r' or 'w', addr], ...]

        self.txn_lines = []         # for self.sclog2traces() to store translated contents of input.txn
        self.in_compilation = in_compilation        # constructor called in compilation or remapping phase
        self.use_real_data = use_real_data  # (does not contain dump_mem instructions)
        self.dump_results = dump_results    # whether to dump results for checking correctness

        """sanity check"""
        if self.dump_results:
            assert self.use_real_data
        assert self.axi_width == 0x40 or self.axi_width == 0x20

        with open(os.path.join(self.in_dir, "qemu_log")) as fp:
            qemu_log_lines = fp.readlines()
        self.addr_base_map = get_addr_mapping(qemu_log_lines)

        """acquire self.ts and self.tb"""
        self.read_compile_log(self.addr_base_map, qemu_log_lines)

        if self.in_compilation:     # memory trace files and input.txn not yet generated
            self.sclog2traces()           # not yet prepended load_mem & dump instructions

        """construct self.in_tb, self.out_tb, self.w_tb, self.act_tb, self.itm_act_tb"""
        mem_trace_path = os.path.join(self.in_dir, "VP_mem_rd_wr" if self.in_compilation else "rtl_mem_rd_wr")
        if not self.in_compilation:
            # check validity of rtl_mem_rd_wr and nvdla_cpp.log
            nvdla_cpp_log = os.path.abspath(os.path.join(self.in_dir, "nvdla_cpp.log"))
            legal = (os.path.exists(nvdla_cpp_log) and os.path.exists(mem_trace_path) and
                     os.stat(nvdla_cpp_log).st_size != 0 and os.stat(mem_trace_path).st_size != 0)
            log_tail_lines = "".join(os.popen("tail -n 3 " + nvdla_cpp_log).readlines()) if legal else ""
            legal = legal and ("done at" in log_tail_lines) and ("PASS" in log_tail_lines or "FAIL" in log_tail_lines)
            if not legal:
                usr_pfx = os.popen("cd ~/ && pwd").readlines()[0].strip().rstrip('/')
                bin_dir_in_docker = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                    "../../../ext/rtl/model_nvdla")).replace(usr_pfx, "/home")
                trace_in_docker = os.path.abspath(os.path.join(self.in_dir, "trace.bin").replace(usr_pfx, "/home"))
                nvdla_cpp_log_in_docker = nvdla_cpp_log.replace(usr_pfx, "/home")
                cmd = "cat /root/.bashrc | grep export | grep verilator > /root/envs && source /root/envs && cd " + \
                      bin_dir_in_docker + " && make VNV_nvdla && ./VNV_nvdla " + trace_in_docker + " > " + \
                      nvdla_cpp_log_in_docker + " && exit"
                found_gem5_nvdla_env = False
                for line in os.popen("docker images").readlines():
                    words = line.split()
                    if "edwinlai99/gem5_nvdla_env" in words[0] and "v3" in words[1]:
                        found_gem5_nvdla_env = True
                        break
                assert found_gem5_nvdla_env

                cid = os.popen("docker run -it -d --rm -v ~/:/home edwinlai99/gem5_nvdla_env:v3").readlines()[0].strip()
                os.system('docker exec -it ' + cid + ' /bin/bash -c ' + '"' + cmd + '" && docker stop ' + cid)

                with open(nvdla_cpp_log) as fp:
                    nvdla_cpp_log_lines = fp.readlines()
                rd_log_lines, wr_log_lines, rw_log_lines = [], [], []
                for line in nvdla_cpp_log_lines:
                    rw_m = re.search("([a-z]+) request from dla, addr ([0-9a-zA-Z]+)", line)
                    if rw_m is not None:
                        is_read = (rw_m.group(1) == "read")
                        addr = int(rw_m.group(2), 16)
                        if is_read:
                            burst_match = re.search("burst ([0-9]+)", line)
                            assert burst_match is not None
                            burst_len = int(burst_match.group(1)) + 1
                            for burst_id in range(burst_len):
                                this_addr = addr + burst_id * self.axi_width
                                rd_log_lines.append(hex(this_addr) + "\n")
                                rw_log_lines.append("r " + hex(this_addr) + "\n")
                        else:
                            wr_log_lines.append(hex(addr) + "\n")
                            rw_log_lines.append("w " + hex(addr) + "\n")
                with open(os.path.join(self.in_dir, "rtl_mem_rd_wr"), "w") as fp:
                    fp.writelines(rw_log_lines)
                with open(os.path.join(self.in_dir, "rtl_mem_rd"), "w") as fp:
                    fp.writelines(rd_log_lines)
                with open(os.path.join(self.in_dir, "rtl_mem_wr"), "w") as fp:
                    fp.writelines(wr_log_lines)

        self.addr_log, self.sorted_addr, self.raw_addr_log = parse_rd_wr_trace(mem_trace_path)
        self.get_various_tensor_buffers()   # in VP_mem_rd_wr, only the life cycles are inaccurate

        if len(self.in_tb) > 1:
            print("Critical situation: len(self.in_tb) > 1")
        if len(self.out_tb) > 1:
            print("Critical situation: len(self.out_tb) > 1")

        """get self.rd_only_tbs"""
        rd_only_addr2tb_name = {}
        for w in self.w_tb:
            to_query_addr = self.get_to_query_addr(self.tb[w])
            rd_only_addr2tb_name[to_query_addr] = w

        for rw_and_addr in self.raw_addr_log:
            # rw_and_addr[0] is 'r' or 'w', while rw_and_addr[1] is the raw address
            if 'r' in rw_and_addr[0] and rw_and_addr[1] in rd_only_addr2tb_name.keys():
                self.rd_only_tbs.append(rd_only_addr2tb_name[rw_and_addr[1]])

        if self.in_compilation and self.use_real_data:
            # care about self.w_tb and self.in_tb
            memory = {}     # {axi-aligned addr, [uint32_t]}
            to_prepend_txn_lines = []
            to_append_txn_lines = []
            with open(os.path.join(self.in_dir, "sc.log")) as fp:
                sclog_lines = fp.readlines()
            for line in sclog_lines:
                if "Info: nvdla.dbb_adaptor: GP: iswrite" in line:
                    info_match = re.search("addr=([0-9a-zA-Z]+) len=([0-9]+) data=0x ([0-9a-fA-FX_ ]+) resp", line)
                    assert info_match is not None
                    addr = int(info_match.group(1), 16)
                    data_len = int(info_match.group(2))
                    assert data_len == self.axi_width
                    assert info_match.group(3).count(' ') == data_len // 4 - 1
                    uint32_ts = info_match.group(3).replace("X", "0").replace("_", "").split()
                    contents = [int(uint32_t, 16) for uint32_t in uint32_ts]

                    memory[addr] = contents

            to_get_tb = self.w_tb + self.in_tb + self.out_tb if self.dump_results else self.w_tb + self.in_tb
            for tb_name in to_get_tb:
                tb = self.tb[tb_name]
                file_name = "0x%08x" % tb.addr + ".dat"
                if tb_name in self.out_tb:
                    file_name = "golden_" + file_name
                file_lines = []
                bytes_in_this_line = 0
                file_line = ""
                aligned_start = (tb.addr // self.axi_width) * self.axi_width
                aligned_end_ceil = ((tb.addr + tb.size - 1) // self.axi_width + 1) * self.axi_width
                # true_start = data_blk.addr
                # true_end = data_blk.addr + data_blk.size
                for addr in range(aligned_start, aligned_end_ceil, self.axi_width):
                    # each entry has length self.axi_width
                    contents = memory[addr]
                    this_txn_st = max(tb.addr, addr)
                    this_txn_ed = min(addr + self.axi_width, tb.addr + tb.size)
                    for byte_id in range(this_txn_st, this_txn_ed):
                        int_id = (byte_id % self.axi_width) // 4
                        offset = ((byte_id % self.axi_width) % 4) * 8
                        byte = (contents[int_id] >> offset) & 0xff
                        file_line += "0x%02x " % byte
                        if bytes_in_this_line == 31:
                            bytes_in_this_line = 0
                            file_lines.append(file_line.strip() + "\n")
                            file_line = ""
                        else:
                            bytes_in_this_line += 1
                if bytes_in_this_line != 0:     # have some remaining
                    file_lines.append(file_line.strip() + "\n")
                    bytes_in_this_line = 0
                    file_line = ""
                with open(os.path.join(self.in_dir, file_name), "w") as fp:
                    fp.writelines(file_lines)
                if tb_name in self.out_tb:
                    to_append_txn_lines.append("dump_mem " + "0x%08x" % tb.addr + " " + hex(tb.size) +
                                               " " + file_name + "\t#actual_len = " + hex(tb.size) + "\n")
                else:
                    to_prepend_txn_lines.append("load_mem " + "0x%08x" % tb.addr + " " + hex(tb.size) +
                                                " " + file_name + "\t#actual_len = " + hex(tb.size) + "\n")
            self.txn_lines = to_prepend_txn_lines + self.txn_lines + to_append_txn_lines

        if self.in_compilation:
            self.txn_lines = fix_txn_lines_discontinuity(self.txn_lines)
            with open(os.path.join(self.in_dir, "input.txn"), "w") as fp:
                fp.writelines(self.txn_lines)
        else:
            """do liveness analysis for each intermediate activation TensorBuffer object"""
            for tb_name in self.itm_act_tb:
                tb = self.tb[tb_name]
                first = self.get_to_query_addr(tb)
                last = last_aligned(tb.addr, tb.size, self.axi_width)
                assert first in self.addr_log
                assert last in self.addr_log
                tb.liveness = (self.addr_log[first][0][0], self.addr_log[last][-1][0])
                tb.num_access = len(self.addr_log[last])

    def get_to_query_addr(self, ts_or_tb):      # it accepts either tensor surface or tensor buffer
        if (ts_or_tb.addr // self.axi_width) * self.axi_width == ts_or_tb.addr:
            to_query_addr = ts_or_tb.addr
        elif ts_or_tb.size > self.axi_width:
            to_query_addr = ((ts_or_tb.addr // self.axi_width) + 1) * self.axi_width
        else:
            print("critical unaligned data_blk")
            to_query_addr = (ts_or_tb.addr // self.axi_width) * self.axi_width
        return to_query_addr

    # read compile_log
    def read_compile_log(self, addr_base_map, qemu_log_lines):
        with open(os.path.join(self.in_dir, "compile_log")) as fp:
            lines = fp.readlines()

        for line_id, line in enumerate(lines):
            if "(Surface) Address list entry for" in line:
                name_match = re.search(r"tsd=(tsd-\d+)/(tb-\d+):\d+ -> (\d+) offset=(\d+) size=(\d+)", line)
                tsd = name_match.group(1)
                tb = name_match.group(2)
                addr_id = int(name_match.group(3))
                offset = int(name_match.group(4))
                tb_size = int(name_match.group(5))  # this tb_size is not 100% correct, as observed in resnet50 inputs

                assert tsd not in self.ts
                new_ts = TensorSurface(tsd, tb, addr_id, offset, addr_base_map)
                self.ts[tsd] = new_ts

                if tb not in self.tb:
                    curr_tb = TensorBuffer(tb, addr_id, tb_size, addr_base_map)
                    curr_tb.add_tensor_surface(tsd, new_ts, addr_base_map)
                    self.tb[tb] = curr_tb
                else:
                    prev_tb = self.tb[tb]
                    assert prev_tb.size == tb_size
                    prev_tb.add_tensor_surface(tsd, new_ts, addr_base_map)

        # remove redundant ts and tb
        to_pop = []
        for ts_name, ts in self.ts.items():
            if ts.addr is None:
                to_pop.append(ts_name)
        for pop_item in to_pop:
            self.ts.pop(pop_item)
        to_pop = []
        for tb_name, tb in self.tb.items():
            if tb.addr is None:
                to_pop.append(tb_name)
        for pop_item in to_pop:
            self.tb.pop(pop_item)

        """acquire size info of tsd (only for input/out/weight are correct)"""
        surfaces, data = construct_surfaces(qemu_log_lines)
        for ts_name, ts in self.ts.items():
            desc = (ts.addr_id, ts.offset)
            ts.size = data[desc].size

    def sclog2traces(self):
        txn_lines = []
        rd_lines = []
        wr_lines = []
        rw_lines = []
        with open(os.path.join(self.in_dir, "sc.log")) as fp:
            sclog_lines = fp.readlines()

        csb_inputting = False
        csb_reg = 0x0
        csb_data = 0
        csb_is_write = 0x0
        for sclog_line in sclog_lines:
            if len(sclog_line) < 2:     # the line is empty
                continue
            if "NV_NVDLA_csb_master::nvdla2csb_b_transport, base_addr:" in sclog_line:
                continue
            if "NV_NVDLA_csb_master::nvdla2csb_b_transport, csb req to" in sclog_line:
                assert not csb_inputting
                csb_inputting = True
            elif "NV_NVDLA_csb_master.cpp:" in sclog_line:
                assert csb_inputting
                info = re.search("NV_NVDLA_csb_master.cpp: [0-9]+:([A-Za-z ]+): ([0-9a-zA-Z]+)", sclog_line)
                if info is not None:
                    key = info.group(1)
                    val = info.group(2)
                    if key == "Addr":
                        csb_reg = int(val, 16)
                    elif key == "Data":
                        csb_data = int(val, 16)
                    elif key == "Is write":
                        csb_is_write = int(val, 16)
                    elif key == "nposted":
                        csb_nposted = int(val, 16)
                        assert csb_nposted == 0x0
                        if csb_is_write != 0:   # the end for csb write_reg log
                            csb_inputting = False
                            out_addr = 0xffff0000 + (0x0000ffff & ((csb_reg - 0) >> 2))
                            # write bit will be corrected by txn2verilator
                            txn_lines.append("write_reg 0x%x 0x%08x\t\t\t#0x%04x\n" % (out_addr, csb_data, csb_reg))
                else:
                    exp = re.search("Err bit: ([0-9a-zA-Z]+) Data: ([0-9a-zA-Z]+)", sclog_line)
                    # unique for csb read_reg response
                    assert exp is not None, "Unresolved csb line: " + sclog_line
                    assert int(exp.group(1), 16) == 0
                    csb_exp_data = int(exp.group(2), 16)
                    csb_inputting = False
                    out_addr = 0xffff0000 + (0x0000ffff & ((csb_reg - 0) >> 2))
                    if csb_reg == 0x000c and csb_exp_data != 0:
                        txn_lines.append("until 0xffff0003 0x%08x\n" % csb_exp_data)
                    elif csb_reg == 0xa004:
                        txn_lines.append("read_reg 0x%08x 0x00000000 0x%08x\t#0x%04x\n"
                                         % (out_addr, csb_exp_data, csb_reg))
                    else:
                        txn_lines.append("read_reg 0x%08x 0xffffffff 0x%08x\t#0x%04x\n"
                                         % (out_addr, csb_exp_data, csb_reg))

            elif "NvdlaAxiAdaptor::axi_rd_wr_thread, send" in sclog_line:
                if "done" in sclog_line:
                    continue
                if "read request" in sclog_line:
                    axi_is_write = False
                elif "write request" in sclog_line:
                    axi_is_write = True
                else:
                    assert False, "Unresolved axi line: " + sclog_line
                addr_match = re.search("address=([0-9a-zA-Z]+)", sclog_line)
                assert addr_match is not None
                axi_addr = int(addr_match.group(1), 16)
                if axi_is_write:
                    wr_lines.append(hex(axi_addr) + "\n")
                    rw_lines.append("w " + hex(axi_addr) + "\n")
                else:
                    rd_lines.append(hex(axi_addr) + "\n")
                    rw_lines.append("r " + hex(axi_addr) + "\n")

        self.txn_lines = txn_lines  # don't write to files at this point. Write after getting load|dump_mem insts.
        with open(os.path.join(self.in_dir, "VP_mem_rd_wr"), "w") as fp:
            fp.writelines(rw_lines)
        with open(os.path.join(self.in_dir, "VP_mem_rd"), "w") as fp:
            fp.writelines(rd_lines)
        with open(os.path.join(self.in_dir, "VP_mem_wr"), "w") as fp:
            fp.writelines(wr_lines)

    # based on combining compilation info and runtime info
    def get_various_tensor_buffers(self):
        assert len(self.in_tb) == 0 and len(self.out_tb) == 0
        for tb_name, tb in self.tb.items():
            if tb.addr_id != 1:
                self.act_tb.append(tb_name)
                to_query_addr = self.get_to_query_addr(tb)
                addr_entry = self.addr_log[to_query_addr]
                if addr_entry[0][1] == "r":
                    self.in_tb.append(tb_name)
                elif addr_entry[-1][1] == "w":
                    self.out_tb.append(tb_name)
                else:
                    self.itm_act_tb.append(tb_name)
            else:
                self.w_tb.append(tb_name)


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
                    else:
                        prev_data = data[key]
                        if temp_data.size < prev_data.size:
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
                    elif kv_match.group(1) == "width":
                        temp_data.width = int(kv_match.group(2), 16)
                    elif kv_match.group(1) == "height":
                        temp_data.height = int(kv_match.group(2), 16)
                    elif kv_match.group(1) == "channel":
                        temp_data.channel = int(kv_match.group(2), 16)
                    elif kv_match.group(1) == "line_stride":
                        temp_data.line_stride = int(kv_match.group(2))
                    elif kv_match.group(1) == "surf_stride":
                        temp_data.surf_stride = int(kv_match.group(2))
                    elif kv_match.group(1) == "plane_stride":
                        temp_data.plane_stride = int(kv_match.group(2))

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
        addr_base_match = re.search(r", got dst_ptr = ([0-9a-f]+) \(index = (\d+)\)", line)
        if addr_base_match is not None:
            key = int(addr_base_match.group(2))
            val = int(addr_base_match.group(1), 16)
            if key not in addr_base_map.keys():
                addr_base_map[key] = val
            else:
                assert addr_base_map[key] == val    # the log file should agree with itself
    return addr_base_map


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


def fix_txn_lines_discontinuity(txn_lines):
    interrupt_regs = {0x0004, 0x0008, 0x000c}
    status_regs = {0x4040, 0x5000, 0x6000, 0x7000, 0x8000, 0x9000, 0xa000, 0xb000, 0xc000, 0xd000, 0xe000, 0xf000, 0x10000}
    pingpong_regs = {0x5004, 0x6004, 0x7004, 0x8004, 0x9004, 0xa004, 0xb004, 0xc004, 0xd004, 0xe004, 0xf004, 0x10004}
    kickoff_regs = {0x4034, 0x4038, 0x5010, 0x6008, 0x7008, 0x8008, 0x9008, 0xa008, 0xb038, 0xc008, 0xd008, 0xe008, 0xf048, 0x10008}

    new_lines = [str(line) for line in txn_lines]

    for line_id, line in enumerate(txn_lines):
        if "until" in line:
            of_concern = True
            # read the lines before:
            component_offset = None
            to_insert_pos = None
            for explore_id in range(line_id - 1, -1, -1):
                # search for the first register instruction that does not involve global interrupt registers
                reg_match = re.search(r'#(0x[0-9a-f]{1,2}0[0-9a-f]{2})', txn_lines[explore_id])
                if reg_match is None or int(reg_match.group(1), 16) in interrupt_regs:
                    explore_id -= 1
                    continue
                else:
                    reg_addr = int(reg_match.group(1), 16)
                    if reg_addr in status_regs or reg_addr in pingpong_regs or reg_addr in kickoff_regs:
                        of_concern = False
                    else:
                        component_offset = ((reg_addr >> 12) << 12)
                        to_insert_pos = explore_id + 1
                    break

            if not of_concern:
                continue
            assert component_offset is not None
            assert to_insert_pos is not None

            logging = False
            to_move_id_start = None
            to_move_id_end = None
            for explore_id in range(line_id + 1, len(txn_lines)):
                # search for the first register instruction that does not involve global interrupt registers
                reg_match = re.search(r'#(0x[0-9a-f]{1,2}0[0-9a-f]{2})', txn_lines[explore_id])
                if reg_match is None or int(reg_match.group(1), 16) in interrupt_regs:
                    if not logging:
                        continue
                    else:
                        # logging completed.
                        logging = False
                        to_move_id_end = explore_id
                        break
                else:
                    reg_addr = int(reg_match.group(1), 16)
                    offset = ((reg_addr >> 12) << 12)
                    if reg_addr in status_regs or reg_addr in pingpong_regs or reg_addr in kickoff_regs or \
                            offset != component_offset:
                        if not logging:
                            # this is not something that we need to modify. Go to next 'until'
                            pass
                        else:
                            # logging completed
                            logging = False
                            to_move_id_end = explore_id
                        break
                    else:
                        if not logging:
                            # start logging
                            logging = True
                            to_move_id_start = explore_id
                        else:
                            # keep logging
                            pass
            assert not logging
            if to_move_id_start is not None:
                assert to_move_id_end is not None
                del new_lines[to_move_id_start: to_move_id_end]
                for i in range(to_move_id_end - to_move_id_start):
                    new_lines.insert(to_insert_pos + i, txn_lines[to_move_id_start + i])

    return new_lines
