import sys
import shutil
import os
import argparse
import re
import errno
import matplotlib.pyplot as plt
import matplotlib.patches as patches

sys.path.append(os.path.dirname(__file__))
from parse_qemu_log import *


class BaseRemapper:
    def __init__(self, in_dir, model_name):
        """ paths """
        self.in_dir = in_dir    # expect in_dir to be VP out dir
        self.model_name = model_name

        """ workload-related info """
        self.out_dir = None
        self.sim_dir_host = None
        self.testcase_str = None

        """ mapping parameters """
        self.alignment = 0x1000

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        self.out_dir = out_dir
        os.makedirs(out_dir, exist_ok=True)
        self.sim_dir_host = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../mnt" + sim_dir))
        self.testcase_str = testcase_str

    def aligned_ceil(self, addr):
        return ((addr - 0x1) // self.alignment + 1) * self.alignment

    """
    @output: if not None, it's the shell command to be executed to do some heavy math during remap decision.
    """
    def compute_remap_decision(self):
        pass

    # for more complex CVSRAM utilizing methods, heavy computation is needed. We need to let multiple testcases
    # do the computation in parallel and then collect the results one-by-one.
    def collect_remap_decision(self):
        pass

    def write_to_files(self):
        pass

    def copy_output_to_img(self):
        os.system("sudo mkdir -p " + self.sim_dir_host)
        files = os.listdir(self.out_dir)
        for file in files:
            if "rd_only_var_log" in file or ".bin" in file:
                os.system("sudo cp " + os.path.join(self.out_dir, file) + " " + self.sim_dir_host)


class IdentityRemapper(BaseRemapper):
    def __init__(self, in_dir, model_name):
        super(IdentityRemapper, self).__init__(in_dir, model_name)

        """ workload-related info """
        self.workload = Workload(in_dir)

    def testcase_init(self, out_dir, sim_dir, testcase_str=""):
        assert os.path.abspath(out_dir) == os.path.abspath(self.in_dir)
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)

    def compute_remap_decision(self):
        pass

    def write_to_files(self):
        script_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../input_txn_to_verilator.pl")
        os.system("perl " + script_path + " " + os.path.join(self.out_dir, "input.txn") + " " +
                  os.path.join(self.out_dir, "trace.bin"))
        rd_var_log_path = os.path.join(self.out_dir, "rd_only_var_log")
        """ generate rd_only_var_log """
        if not os.path.exists(rd_var_log_path):
            with open(rd_var_log_path, "wb") as fp:
                for rd_only_var in self.workload.rd_only_tbs:
                    tb = self.workload.tb[rd_only_var]
                    fp.write(tb.addr.to_bytes(4, byteorder="little", signed=False))
                    fp.write(tb.size.to_bytes(4, byteorder="little", signed=False))


class CVSRAMRemapper(BaseRemapper):
    def __init__(self, in_dir, model_name):
        super(CVSRAMRemapper, self).__init__(in_dir, model_name)

        """ mapping parameters """
        self.num_cvsram = 0
        self.cvsram_base_addrs = []
        self.cvsram_sizes = []
        self.assoc_reg_bits = {    # {reg: (associated_reg, bit)} -> &= ~(1<<bit); bit value: 0: CVSRAM, 1: DRAM
            0x4000: (0x4014, 0), 0x4004: (0x4014, 0),
            0x4008: (0x4014, 1), 0x400c: (0x4014, 1),
            0x5030: (0x502c, 0), 0x5034: (0x502c, 0),
            0x5038: (0x502c, 0), 0x503c: (0x502c, 0),
            0x5078: (0x5074, 0), 0x507c: (0x5074, 0),
            0xa018: (0xa074, 0), 0xa01c: (0xa074, 0),
            0xa02c: (0xa028, 5), 0xa030: (0xa028, 5),
            0xa044: (0xa040, 5), 0xa048: (0xa040, 5),
            0xa05c: (0xa058, 5), 0xa060: (0xa058, 5),
            0xb048: (0xb0b4, 0), 0xb04c: (0xb0b4, 0),
            0xc01c: (0xc02c, 0), 0xc020: (0xc02c, 0),
            # 0xd060: (), 0xd064: (),
            0xd070: (0xd080, 0), 0xd074: (0xd080, 0),
            0xe018: (0xe028, 0), 0xe01c: (0xe028, 0),
            0xf050: (0xf060, 0), 0xf054: (0xf060, 0),
            0x1001c: (0x10010, 0), 0x10020: (0x10010, 0),
            0x10038: (0x10030, 0), 0x1003c: (0x10030, 0)
        }   # these are from nvdla/hw/cmod/include/arnvdla.h

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)

    def set_cvsram_param(self, num_cvsram, cvsram_base_addrs, cvsram_sizes):
        self.num_cvsram = num_cvsram
        self.cvsram_base_addrs = cvsram_base_addrs
        self.cvsram_sizes = cvsram_sizes

    def change_ram_type_to_cvsram(self, raw_lines, modified_lines, line_id, modify_status):
        line = raw_lines[line_id]
        # use regex to get the register
        reg_match = re.search(r'#\s?(0x[0-9a-f]{1,2}0[0-9a-f]{2})', line)
        if reg_match is None:       # not register txns. May be {load|dump}_mem
            return
        reg = int(reg_match.group(1), 16)
        ram_type_reg, ram_type_bit = self.assoc_reg_bits[reg]

        explore_id = line_id - 1
        while explore_id >= 0:
            ram_reg_match = re.search(r'#\s?(0x[0-9a-f]{1,2}0[0-9a-f]{2})', raw_lines[explore_id])
            if ram_reg_match is None:
                break
            if ram_reg_match.group(0)[2] != reg_match.group(0)[2] or \
                    ram_reg_match.group(0)[3] != reg_match.group(0)[3]:
                # only explore registers in the same reg group and continuous lines
                break
            if int(ram_reg_match.group(1), 16) == ram_type_reg and not modify_status[explore_id]:
                reg_val_match = re.search(r'_reg\s0xffff[0-9a-f]{4}\s(0x[0-9a-f]{8})', raw_lines[explore_id])
                old_val_str = reg_val_match.group(1)
                new_val = int(old_val_str, 16) & ~(1 << ram_type_bit)   # change to CVSRAM
                new_val_str = f"{new_val:#0{10}x}"
                modified_lines[explore_id] = new_val_str.join(raw_lines[explore_id].rsplit(old_val_str, 1)) # reverse_replace
                modify_status[explore_id] = True
            explore_id -= 1

        explore_id = line_id + 1
        while explore_id < len(raw_lines):
            ram_reg_match = re.search(r'#\s?(0x[0-9a-f]{1,2}0[0-9a-f]{2})', raw_lines[explore_id])
            if ram_reg_match is None:
                break
            if ram_reg_match.group(0)[2] != reg_match.group(0)[2] or \
                    ram_reg_match.group(0)[3] != reg_match.group(0)[3]:
                # only explore registers in the same reg group and continuous lines
                break
            if int(ram_reg_match.group(1), 16) == ram_type_reg and not modify_status[explore_id]:
                reg_val_match = re.search(r'_reg\s0xffff[0-9a-f]{4}\s(0x[0-9a-f]{8})', raw_lines[explore_id])
                old_val_str = reg_val_match.group(1)
                new_val = int(old_val_str, 16) & ~(1 << ram_type_bit)   # change to CVSRAM
                new_val_str = f"{new_val:#0{10}x}"
                modified_lines[explore_id] = new_val_str.join(raw_lines[explore_id].rsplit(old_val_str, 1)) # reverse_replace
                modify_status[explore_id] = True
            explore_id += 1


class SingleAccelCVSRAMRemapper(CVSRAMRemapper):
    def __init__(self, in_dir, model_name):
        super(SingleAccelCVSRAMRemapper, self).__init__(in_dir, model_name)

        """ workload-related info """
        self.workload = Workload(in_dir)

        """ testcase-related decisions """
        self.mapping = {}   # {addr_in_dram: addr_in_cvsram}

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)
        self.mapping.clear()

        for root, dirs, files in os.walk(self.in_dir):
            if os.path.abspath(root) == os.path.abspath(self.in_dir):
                # create symbolic links of *.dat files under root in out_dir
                for file in files:
                    if file.endswith(".dat"):
                        src = os.path.abspath(os.path.join(root, file))
                        link = os.path.abspath(os.path.join(out_dir, file))
                        try:
                            os.symlink(src, link)
                        except OSError as e:
                            if e.errno == errno.EEXIST:
                                os.remove(link)
                                os.symlink(src, link)
                            else:
                                raise e

    def set_cvsram_param(self, num_cvsram, cvsram_base_addrs, cvsram_sizes):
        CVSRAMRemapper.set_cvsram_param(self, num_cvsram, cvsram_base_addrs, cvsram_sizes)
        assert num_cvsram == 1

    def write_to_files(self):
        """ modify input.txn """
        with open(os.path.join(self.in_dir, "input.txn")) as fp:
            raw_txn_lines = fp.readlines()
        new_lines = [str(line) for line in raw_txn_lines]
        modify_status = [False for _ in range(len(raw_txn_lines))]  # False means not modified by remapping yet

        for orig_addr, mapped_addr in self.mapping.items():
            for line_id, line in enumerate(raw_txn_lines):
                if hex(orig_addr) in line and not modify_status[line_id]:
                    new_lines[line_id] = line.replace(hex(orig_addr), hex(mapped_addr), 1)
                    # the parameter "1" is crucial since in {load|dump}_mem, files are named with addresses
                    # we want to keep the file name unchanged after remapping
                    modify_status[line_id] = True

                    self.change_ram_type_to_cvsram(raw_txn_lines, new_lines, line_id, modify_status)

        out_txn_path = os.path.join(self.out_dir, self.testcase_str + "_input.txn")
        rd_var_log_path = os.path.join(self.out_dir, self.testcase_str + "_rd_only_var_log")
        with open(out_txn_path, "w") as fp:
            fp.writelines(new_lines)
        script_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../input_txn_to_verilator.pl")
        os.system("perl " + script_path + " " + out_txn_path + " " +
                  os.path.join(self.out_dir, self.testcase_str + "_trace.bin"))

        """ modify rd_only_var_log """
        with open(rd_var_log_path, "wb") as fp:
            for rd_only_var in self.workload.rd_only_tbs:
                tb = self.workload.tb[rd_only_var]
                if tb.addr not in self.mapping.keys():
                    fp.write(tb.addr.to_bytes(4, byteorder="little", signed=False))
                    fp.write(tb.size.to_bytes(4, byteorder="little", signed=False))


class WeightPinRemapper(SingleAccelCVSRAMRemapper):
    def __init__(self, in_dir, model_name):
        super(WeightPinRemapper, self).__init__(in_dir, model_name)

        """ workload-related info """
        # sort all the weights in workload
        self.weights_cp = [weight for weight in self.workload.w_tb]
        self.weights_cp.sort(key=lambda x: self.workload.tb[x].size, reverse=True)

    def compute_remap_decision(self):
        cvsram_size = self.cvsram_sizes[0]
        cvsram_base_addr = self.cvsram_base_addrs[0]

        # assign one by one greedily. If enough space, then mark to remap
        space_left = cvsram_size
        for weight in self.weights_cp:
            aligned_size = self.aligned_ceil(self.workload.tb[weight].size)
            if aligned_size <= space_left:
                self.mapping[self.workload.tb[weight].addr] = cvsram_base_addr + cvsram_size - space_left
                space_left -= aligned_size


class ActPinRemapper(SingleAccelCVSRAMRemapper):
    def __init__(self, in_dir, model_name):
        super(ActPinRemapper, self).__init__(in_dir, model_name)

        """ workload-related info """
        self.last_tick = len(self.workload.raw_addr_log) - 1

    def compute_remap_decision(self):
        itm_acts_file = os.path.join(self.in_dir, "intermediate_acts")
        write_solver_input(itm_acts_file, self.workload, log_weights=False)

        # call the gurobi solver
        gurobi_out_path = os.path.join(self.out_dir, self.testcase_str + "_alloc_result")
        os.system("cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && make ActAlloc")
        return ["cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && ./ActAlloc " + itm_acts_file +
                " " + gurobi_out_path + " " + str(self.cvsram_sizes[0]) + " > " +
                os.path.join(self.out_dir, self.testcase_str + "_gurobi_stdout")]

    def collect_remap_decision(self):
        gurobi_in_path = os.path.join(self.in_dir, "intermediate_acts")
        gurobi_out_path = os.path.join(self.out_dir, self.testcase_str + "_alloc_result")
        relative_map = collect_gurobi_results(self.workload, self.cvsram_sizes[0], gurobi_out_path, gurobi_in_path)
        for tb_name, rel_addr in relative_map.items():
            buffer = self.workload.tb[tb_name]
            for ts_name in buffer.tsd_list:
                ts = self.workload.ts[ts_name]
                to_map_addr = self.cvsram_base_addrs[0] + rel_addr + ts.addr - buffer.addr
                if ts.addr in self.mapping:
                    assert to_map_addr == self.mapping[ts.addr]
                else:
                    self.mapping[ts.addr] = to_map_addr


class MixPinRemapper(SingleAccelCVSRAMRemapper):
    def __init__(self, in_dir, model_name):
        super(MixPinRemapper, self).__init__(in_dir, model_name)

        """ workload-related info """
        self.last_tick = len(self.workload.raw_addr_log) - 1
        # sort all the weights in workload
        self.weights_cp = [weight for weight in self.workload.w_tb]
        self.weights_cp.sort(key=lambda x: self.workload.tb[x].size, reverse=True)

    def compute_remap_decision(self):
        # do the same thing as ActPin Remappers, except for replacing the keyword names
        itm_acts_file = os.path.join(self.in_dir, "intermediate_acts")
        write_solver_input(itm_acts_file, self.workload, log_weights=False)

        # call the gurobi solver
        gurobi_out_path = os.path.join(self.out_dir, self.testcase_str.replace("MixPin", "ActPin") + "_alloc_result")
        os.system("cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && make ActAlloc")
        return ["cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && ./ActAlloc " + itm_acts_file +
                " " + gurobi_out_path + " " + str(self.cvsram_sizes[0]) + " > " +
                os.path.join(self.out_dir, self.testcase_str.replace("MixPin", "ActPin") + "_gurobi_stdout")]

    def collect_remap_decision(self):
        gurobi_out_path = os.path.join(self.out_dir, self.testcase_str.replace("MixPin", "ActPin") + "_alloc_result")
        gurobi_in_path = os.path.join(self.in_dir, "intermediate_acts")
        relative_map = collect_gurobi_results(self.workload, self.cvsram_sizes[0], gurobi_out_path, gurobi_in_path)
        cvsram_space_left = self.cvsram_sizes[0]
        for tb_name, rel_addr in relative_map.items():
            buffer = self.workload.tb[tb_name]
            for ts_name in buffer.tsd_list:
                ts = self.workload.ts[ts_name]
                to_map_to_addr = self.cvsram_base_addrs[0] + rel_addr + ts.addr - buffer.addr
                if ts.addr in self.mapping:
                    assert to_map_to_addr == self.mapping[ts.addr]
                else:
                    self.mapping[ts.addr] = to_map_to_addr
            cvsram_space_left = min(cvsram_space_left, self.cvsram_sizes[0] - rel_addr - buffer.size)
            assert cvsram_space_left >= 0

        for weight_desc in self.weights_cp:
            weight_blk = self.workload.tb[weight_desc]
            if weight_blk.size <= cvsram_space_left:
                self.mapping[weight_blk.addr] = self.cvsram_base_addrs[0] + self.cvsram_sizes[0] - cvsram_space_left
                cvsram_space_left -= weight_blk.size


class PipelineRemapper(BaseRemapper):
    def __init__(self, in_dir, model_name):
        super(PipelineRemapper, self).__init__(in_dir, model_name)

        """ paths """
        self.in_dirs = []
        compile_out_subdirs = os.listdir(self.in_dir)
        found_any = False
        for subdir in compile_out_subdirs:
            if "stage_" in subdir:
                self.in_dirs.append(os.path.join(self.in_dir, subdir))
                found_any = True
        self.num_stages = len(self.in_dirs)
        assert found_any

        """ mapping parameters """
        self.weight_base_addr = 0xc0000000
        self.activation_base_addr = None
        self.batch_num = None

        """ workload-related info """
        self.pipeline_stages = []
        for pipeline_log_dir in self.in_dirs:
            pipeline_stage = Workload(pipeline_log_dir)
            self.pipeline_stages.append(pipeline_stage)

        # weights
        self.all_w_tb = []
        for i, stage in enumerate(self.pipeline_stages):
            for weight in stage.w_tb:
                self.all_w_tb.append((i, weight))
                # self.all_w_tb = [(pipeline_stage_id(starting from 0), weight_tb_name)]
        self.all_w_tb.sort(key=lambda x: self.pipeline_stages[x[0]].tb[x[1]].size, reverse=True)

        # activations
        self.intra_act_tb = []          # = [(stage_id, tb_name)]
        self.inter_act_tb = []          # = [(batch_id, stage_id, tb_name)]

        """ testcase-related decisions """
        self.weight_map = {}            # {(stage_id, tb_name): (mapped_addr, is_cvsram)}
        self.inter_act_map = {}         # {(batch_id, stage_id, tb_name): (mapped_addr, is_cvsram)}
        self.intra_act_map = {}         # {(stage_id, tb_name): (mapped_addr, is_cvsram)}

        self.weight_addr_map = {}       # {(stage_id, orig_addr): (mapped_addr, is_cvsram)}
        self.inter_act_addr_map = {}    # {(batch_id, stage_id, orig_addr): (mapped_addr, is_cvsram)}
        self.intra_act_addr_map = {}    # {(stage_id, orig_addr): (mapped_addr, is_cvsram)}

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)
        self.weight_map.clear()
        self.inter_act_map.clear()
        self.intra_act_map.clear()
        self.weight_addr_map.clear()
        self.inter_act_addr_map.clear()
        self.intra_act_addr_map.clear()

    def set_pipeline_params(self, num_batches):
        self.batch_num = num_batches
        # for the first stage, put all activations
        # for the other stages, put all activations except inputs
        # for input / output activations, include their batch_id
        # but for intermediate ones, ignore their batch_id
        # since inside a pipeline stage, the intermediate acts will not be overwritten by data from another batch
        for i, stage in enumerate(self.pipeline_stages):
            for act in stage.itm_act_tb:
                self.intra_act_tb.append((i, act))
            for act in stage.out_tb:
                for batch in range(self.batch_num):
                    self.inter_act_tb.append((batch, i, act))
        for act in self.pipeline_stages[0].in_tb:
            for batch in range(self.batch_num):
                self.inter_act_tb.append((batch, 0, act))

    def remap_weights(self):
        next_avail_aligned = self.weight_base_addr
        for w_desc in self.all_w_tb:
            # w_desc: (stage_id, tb_name)
            w_tb = self.pipeline_stages[w_desc[0]].tb[w_desc[1]]
            self.weight_map[w_desc] = (next_avail_aligned, False)  # False means it isn't mapped to CVSRAM
            self.weight_addr_map[(w_desc[0], w_tb.addr)] = (next_avail_aligned, False)
            # aligned size
            al_sz = self.aligned_ceil(w_tb.size)
            next_avail_aligned += al_sz

        self.activation_base_addr = next_avail_aligned

    def remap_activations(self):
        assert self.activation_base_addr is not None
        next_avail_aligned = self.activation_base_addr
        activation_map = {}
        # {(batch_id, stage_id, tb_name) or (stage_id, tb_name): (mapped_addr, is_cvsram)}
        for act_id, act_desc in enumerate(self.inter_act_tb + self.intra_act_tb):
            # for all the activations (outputs of the previous stage and inputs of the next stage are counted ONCE)
            # act_desc: (batch_id, stage_id, tb_name)   (for input / output activations)
            #           (          stage_id, tb_name)   (for intermediate activations)
            activation_map[act_desc] = (next_avail_aligned, False)     # False means it isn't mapped to CVSRAM
            aligned_size = self.aligned_ceil(self.pipeline_stages[act_desc[-2]].tb[act_desc[-1]].size)
            next_avail_aligned += aligned_size

        # match the outputs of the previous stage with the inputs of the next stage
        # for inter-stage tensors, only their output-side were added to self.all_activations
        # and are thus mapped in the previous loop,
        # so we assert (input_)key not in activation_map
        for stage_id in range(1, len(self.pipeline_stages)):
            for i, ipt in enumerate(self.pipeline_stages[stage_id].in_tb):
                for batch_id in range(self.batch_num):
                    key = (batch_id, stage_id, ipt)
                    corr_opt = self.pipeline_stages[stage_id - 1].out_tb[i]     # corresponding output
                    corr_opt_key = (batch_id, stage_id - 1, corr_opt)
                    assert key not in activation_map.keys()
                    activation_map[key] = activation_map[corr_opt_key]

        for key, val in activation_map.items():
            stage = self.pipeline_stages[key[-2]]
            tb = stage.tb[key[-1]]
            if len(key) == 2:   # intermediate activation
                self.intra_act_map[key] = val
                for tsd_name in tb.tsd_list:
                    tsd_addr = stage.ts[tsd_name].addr
                    self.intra_act_addr_map[(key[-2], tsd_addr)] = (val[0] + tsd_addr - tb.addr, val[1])
            else:
                assert len(key) == 3
                self.inter_act_map[key] = val
                for tsd_name in tb.tsd_list:
                    tsd_addr = stage.ts[tsd_name].addr
                    self.inter_act_addr_map[(key[-3], key[-2], tsd_addr)] = (val[0] + tsd_addr - tb.addr, val[1])

    def compute_remap_decision(self):
        self.remap_weights()
        self.remap_activations()

    def write_to_files(self):
        for i in range(self.num_stages):
            with open(os.path.join(self.in_dirs[i], "input.txn")) as fp:
                txn_lines = fp.readlines()
            for batch_id in range(self.batch_num):
                new_lines = [str(line) for line in txn_lines]
                modify_status = [False for _ in txn_lines]

                for w_addr_desc, w_map_info in self.weight_addr_map.items():
                    # w_desc: pipeline_stage, weight_addr
                    assert len(w_addr_desc) == 2
                    addr_mapped, is_cvsram = w_map_info
                    if w_addr_desc[0] == i:
                        orig_addr = w_addr_desc[-1]
                        for line_id, line in enumerate(txn_lines):
                            if hex(orig_addr) in line and not modify_status[line_id]:
                                new_lines[line_id] = line.replace(hex(orig_addr), hex(addr_mapped))
                                modify_status[line_id] = True
                                if is_cvsram:
                                    # should not go into this branch for class PipelineRemapper
                                    # only intended for PipelineWeightPinRemapper
                                    self.change_ram_type_to_cvsram(txn_lines, new_lines, line_id, modify_status)

                # intermediate activation / intra_act
                for ia_addr_desc, ia_map_info in self.intra_act_addr_map.items():
                    # ia_addr_desc: (stage_id, ia_addr)
                    assert len(ia_addr_desc) == 2
                    addr_mapped, is_cvsram = ia_map_info
                    if ia_addr_desc[0] == i:
                        orig_addr = ia_addr_desc[-1]
                        for line_id, line in enumerate(txn_lines):
                            if hex(orig_addr) in line and not modify_status[line_id]:
                                new_lines[line_id] = line.replace(hex(orig_addr), hex(addr_mapped))
                                modify_status[line_id] = True
                                if is_cvsram:
                                    # should not go into this branch for class PipelineRemapper
                                    # only intended for PipelineWeightPinRemapper
                                    self.change_ram_type_to_cvsram(txn_lines, new_lines, line_id, modify_status)

                # input & output activation / inter_act
                for io_addr_desc, io_map_info in self.inter_act_addr_map.items():
                    addr_mapped, is_cvsram = io_map_info
                    # act_desc: (batch_id, stage_id, io_addr)
                    assert len(io_addr_desc) == 3
                    if io_addr_desc[0] == batch_id and io_addr_desc[1] == i:
                        orig_addr = io_addr_desc[-1]
                        for line_id, line in enumerate(txn_lines):
                            if hex(orig_addr) in line and not modify_status[line_id]:
                                new_lines[line_id] = line.replace(hex(orig_addr), hex(addr_mapped))
                                modify_status[line_id] = True
                                if is_cvsram:
                                    self.change_ram_type_to_cvsram(txn_lines, new_lines, line_id, modify_status)

                # write lines to file
                out_pfx = self.model_name + "_" + self.testcase_str + "_" + str(batch_id + 1) + "_" + str(i + 1) + "_"
                out_txn_path = os.path.join(os.path.abspath(self.out_dir), out_pfx + "input.txn")
                out_bin_path = os.path.join(os.path.abspath(self.out_dir), out_pfx + "trace.bin")
                rd_var_log_path = os.path.join(os.path.abspath(self.out_dir), out_pfx + "rd_only_var_log")
                # the "pipeline_execute" scheduler expects rd_only_var_log to be dependent on batchID
                # so that it may be compatible with future prefetch policies

                with open(out_txn_path, "w") as fp:
                    fp.writelines(new_lines)

                # also need to call the perl script to convert them into binary format
                # the nvdla/vp docker image has perl v5.22.1 installed, ok
                script_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../input_txn_to_verilator.pl")
                os.system("perl " + script_path + " " + out_txn_path + " " + out_bin_path)

                """ modify rd_only_var_log """
                with open(rd_var_log_path, "wb") as fp:
                    for rd_only_tb in self.pipeline_stages[i].rd_only_tbs:
                        tb = self.pipeline_stages[i].tb[rd_only_tb]

                        # we have disabled input prefetching in current rd_only_var_log
                        assert tb.addr_id == 1
                        key = (i, rd_only_tb)
                        mapped_to_cvsram = self.weight_map[key][1]
                        if not mapped_to_cvsram:
                            mapped_addr = self.weight_map[key][0]
                            fp.write(mapped_addr.to_bytes(4, byteorder="little", signed=False))
                            fp.write(tb.size.to_bytes(4, byteorder="little", signed=False))


class PipelineWeightPinRemapper(CVSRAMRemapper, PipelineRemapper):
    def __init__(self, in_dir, model_name):
        PipelineRemapper.__init__(self, in_dir, model_name)
        CVSRAMRemapper.__init__(self, in_dir, model_name)

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        PipelineRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)

    def remap_weights(self):
        next_avail_aligned = self.weight_base_addr                              # next available addr in DRAM
        spaces_left = [self.cvsram_sizes[i] for i in range(self.num_cvsram)]    # available space in all CVSRAMs
        for weight_desc in self.all_w_tb:
            # weight_desc: (stage_id, tb_name)
            stage_id = weight_desc[0]
            w_tb = self.pipeline_stages[stage_id].tb[weight_desc[-1]]
            aligned_size = self.aligned_ceil(w_tb.size)
            if aligned_size <= spaces_left[stage_id]:   # like WeightPinRemapper, assign weights greedily
                map_to_addr = self.cvsram_base_addrs[stage_id] + self.cvsram_sizes[stage_id] - spaces_left[stage_id]
                self.weight_map[weight_desc] = (map_to_addr, True)
                self.weight_addr_map[(stage_id, w_tb.addr)] = (map_to_addr, True)
                # True means assigned to CVSRAM; False means assigned in DRAM
                spaces_left[stage_id] -= aligned_size
            else:   # map to DRAM
                self.weight_map[weight_desc] = (next_avail_aligned, False)
                self.weight_addr_map[(stage_id, w_tb.addr)] = (next_avail_aligned, False)
                next_avail_aligned += aligned_size

        self.activation_base_addr = next_avail_aligned

    def compute_remap_decision(self):
        self.remap_weights()
        self.remap_activations()

    def write_to_files(self):
        PipelineRemapper.write_to_files(self)


class PipelineActPinRemapper(CVSRAMRemapper, PipelineRemapper):
    def __init__(self, in_dir, model_name):
        PipelineRemapper.__init__(self, in_dir, model_name)
        CVSRAMRemapper.__init__(self, in_dir, model_name)

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        PipelineRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)

    def remap_activations(self):
        PipelineRemapper.remap_activations(self)

        cmds = []
        for stage_id in range(len(self.in_dirs)):
            itm_acts_file = os.path.join(self.in_dirs[stage_id], "intermediate_acts")
            write_solver_input(itm_acts_file, self.pipeline_stages[stage_id], log_weights=False)

            # call the gurobi solver
            stage_out_dir = os.path.join(self.out_dir, "stage_" + str(stage_id + 1))
            os.makedirs(stage_out_dir, exist_ok=True)
            gurobi_out_path = os.path.join(stage_out_dir, self.testcase_str + "_alloc_result")
            os.system("cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && make ActAlloc")
            cmds.append("cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && ./ActAlloc " +
                        itm_acts_file + " " + gurobi_out_path + " " + str(self.cvsram_sizes[stage_id]) + " > " +
                        os.path.join(stage_out_dir, self.testcase_str + "_gurobi_stdout"))
        return cmds

    def compute_remap_decision(self):
        self.remap_weights()
        cmds = self.remap_activations()
        return cmds

    def collect_remap_decision(self):
        # read results from the solver and draw the CVSRAM occupation figure
        for stage_id in range(self.num_stages):
            stage = self.pipeline_stages[stage_id]
            stage_out_dir = os.path.join(self.out_dir, "stage_" + str(stage_id + 1))
            gurobi_out_path = os.path.join(stage_out_dir, self.testcase_str + "_alloc_result")
            gurobi_in_path = os.path.join(self.in_dirs[stage_id], "intermediate_acts")
            relative_map = collect_gurobi_results(stage, self.cvsram_sizes[stage_id], gurobi_out_path, gurobi_in_path)
            for tb_name, rel_addr in relative_map.items():
                global_key = (stage_id, tb_name)
                self.intra_act_map[global_key] = (self.cvsram_base_addrs[stage_id] + rel_addr, True)
                tb = stage.tb[tb_name]
                for tsd_name in tb.tsd_list:
                    tsd_addr = stage.ts[tsd_name].addr
                    to_map_to_addr = self.cvsram_base_addrs[stage_id] + rel_addr + tsd_addr - tb.addr, True
                    self.intra_act_addr_map[(stage_id, tsd_addr)] = to_map_to_addr

    def write_to_files(self):
        PipelineRemapper.write_to_files(self)


def write_solver_input(file_path, workload, log_weights):
    with open(file_path, "w") as fp:
        for act in workload.itm_act_tb:
            buffer = workload.tb[act]
            fp.write(str(buffer.liveness[0]) + " " + str(buffer.liveness[1]) + " " + str(buffer.size) + " " +
                     str(buffer.num_access) + " " + hex(buffer.addr) + " ")
            last_aligned_addr = last_aligned(buffer.addr, buffer.size, workload.axi_width)
            for id_rw in workload.addr_log[last_aligned_addr]:
                fp.write(id_rw[1] + " ")
            fp.write("\n")

        if log_weights:
            last_tick = len(workload.raw_addr_log) - 1
            for w in workload.w_tb:
                weight_tb = workload.tb[w]
                assert len(weight_tb.tsd_list) == 1
                fp.write("0 " + str(last_tick) + " " + str(weight_tb.size) +
                         "1 " + hex(weight_tb.addr) + " r\n")


def collect_gurobi_results(workload, cvsram_size, gurobi_out_path, gurobi_in_path):
    # read results from the solver and draw the CVSRAM occupation figure
    occ_fig = plt.figure(figsize=(12.8, 9.6))
    ax1 = occ_fig.add_subplot("111")
    plt.xlim(xmin=0, xmax=len(workload.raw_addr_log))
    plt.ylim(ymin=0, ymax=cvsram_size)

    collect_w_cnt, collect_a_cnt = 0, 0

    with open(gurobi_in_path) as fp:
        in_lines = fp.readlines()

    with open(gurobi_out_path) as fp:
        out_lines = fp.readlines()

    relative_mapping = {}   # {data_desc(inside a pipeline stage): mapping position in a CVSRAM}
    for line_id, line in enumerate(out_lines):
        words = line.split()
        attrs = in_lines[line_id].strip().split()
        is_w = attrs[5] == "1"      # weights will only be used once
        if words[0] == '1':
            tb_name = workload.tb[workload.w_tb[collect_w_cnt]].tb_name if is_w else workload.itm_act_tb[collect_a_cnt]
            buffer = workload.tb[tb_name]
            relative_mapping[tb_name] = int(words[1])
            if is_w:
                ax1.add_patch(patches.Rectangle((0, int(words[1])), len(workload.raw_addr_log) - 1, buffer.size,
                                                linewidth=1, edgecolor='black'))
            else:
                ax1.add_patch(patches.Rectangle((buffer.liveness[0], int(words[1])),
                                                buffer.liveness[1] - buffer.liveness[0], buffer.size,
                                                linewidth=1, edgecolor='black'))
        if is_w:
            collect_w_cnt += 1
        else:
            collect_a_cnt += 1

    ylabels = map(lambda t: '0x%x' % int(t), ax1.get_yticks())
    ax1.set_yticklabels(ylabels)
    ax1.ticklabel_format(style='sci', scilimits=(-1, 2), axis='x')
    plt.title("Buffer Allocation Result on CVSRAM size = 0x%x Bytes" % cvsram_size)
    plt.xlabel("Logical Order")
    plt.ylabel("CVSRAM Address")
    plt.rcParams.update({'font.size': 22})
    plt.tight_layout()
    occ_fig.savefig(gurobi_out_path + "_vis.png", dpi=400)

    return relative_mapping
