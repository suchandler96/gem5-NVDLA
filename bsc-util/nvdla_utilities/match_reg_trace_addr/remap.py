import sys
import shutil
import os
import argparse
import re
import matplotlib.pyplot as plt
import matplotlib.patches as patches

sys.path.append(os.path.dirname(__file__))
from parse_qemu_log import *


class BaseRemapper:
    def __init__(self, in_dir, model_name):
        """ paths """
        self.in_dir = in_dir
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
        with open(rd_var_log_path, "wb") as fp:
            for rd_only_var in self.workload.rd_only_vars:
                data_blk = self.workload.data[rd_only_var]
                fp.write(data_blk.addr.to_bytes(4, byteorder="little", signed=False))
                fp.write(data_blk.size.to_bytes(4, byteorder="little", signed=False))


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
                    new_lines[line_id] = line.replace(hex(orig_addr), hex(mapped_addr))
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
            for rd_only_var in self.workload.rd_only_vars:
                data_blk = self.workload.data[rd_only_var]
                if data_blk.addr not in self.mapping.keys():
                    fp.write(data_blk.addr.to_bytes(4, byteorder="little", signed=False))
                    fp.write(data_blk.size.to_bytes(4, byteorder="little", signed=False))


class WeightPinRemapper(SingleAccelCVSRAMRemapper):
    def __init__(self, in_dir, model_name):
        super(WeightPinRemapper, self).__init__(in_dir, model_name)

        """ workload-related info """
        # sort all the weights in workload
        self.weights_cp = [weight for weight in self.workload.weights]
        self.weights_cp.sort(key=lambda x: self.workload.data[x].size, reverse=True)

    def compute_remap_decision(self):
        cvsram_size = self.cvsram_sizes[0]
        cvsram_base_addr = self.cvsram_base_addrs[0]

        # assign one by one greedily. If enough space, then mark to remap
        space_left = cvsram_size
        for weight in self.weights_cp:
            aligned_size = self.aligned_ceil(self.workload.data[weight].size)
            if aligned_size <= space_left:
                self.mapping[self.workload.data[weight].addr] = cvsram_base_addr + cvsram_size - space_left
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
        for desc, rel_addr in relative_map.items():
            self.mapping[self.workload.data[desc].addr] = self.cvsram_base_addrs[0] + rel_addr


class MixPinRemapper(SingleAccelCVSRAMRemapper):
    def __init__(self, in_dir, model_name):
        super(MixPinRemapper, self).__init__(in_dir, model_name)

        """ workload-related info """
        self.last_tick = len(self.workload.raw_addr_log) - 1

    def compute_remap_decision(self):
        w_and_acts_file = os.path.join(self.in_dir, "weight_and_im_acts")
        write_solver_input(w_and_acts_file, self.workload, log_weights=True)

        # call the gurobi solver
        gurobi_out_path = os.path.join(self.out_dir, self.testcase_str + "_alloc_result")
        os.system("cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && make ActAlloc")
        return ["cd " + os.path.join(os.path.dirname(__file__), "CVSRAMAlloc") + " && ./ActAlloc " + w_and_acts_file +
                " " + gurobi_out_path + " " + str(self.cvsram_sizes[0]) + " > " +
                os.path.join(self.out_dir, self.testcase_str + "_gurobi_stdout")]

    def collect_remap_decision(self):
        gurobi_out_path = os.path.join(self.out_dir, self.testcase_str + "_alloc_result")
        gurobi_in_path = os.path.join(self.in_dir, "weight_and_im_acts")
        relative_map = collect_gurobi_results(self.workload, self.cvsram_sizes[0], gurobi_out_path, gurobi_in_path)
        for desc, rel_addr in relative_map.items():
            self.mapping[self.workload.data[desc].addr] = self.cvsram_base_addrs[0] + rel_addr


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
            # pipeline_stage.print_workload_info()
            self.pipeline_stages.append(pipeline_stage)

        # weights
        self.all_weights = []
        for i, stage in enumerate(self.pipeline_stages):
            for weight in stage.weights:
                self.all_weights.append((i, weight[0], weight[1]))
                # all_weights = [(pipeline_stage_id(starting from 0), addr_id, offset)]
        self.all_weights.sort(key=lambda x: self.pipeline_stages[x[0]].data[(x[1], x[2])].size, reverse=True)

        # activations
        self.all_activations = []   # = [(batch_id, stage_id, addr_id, offset)]
        self.intra_act = []         # = [(stage_id, addr_id, offset)]
        self.inter_act = []         # = [(batch_id, stage_id, addr_id, offset)]

        """ testcase-related decisions """
        self.weight_map = {}        # {(stage_id, addr_id, offset): (mapped_addr, is_cvsram)}
        self.inter_act_map = {}     # {(batch_id, stage_id, addr_id, offset): (mapped_addr, is_cvsram)}
        self.intra_act_map = {}     # {(stage_id, addr_id, offset): (mapped_addr, is_cvsram)}

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)
        self.weight_map.clear()
        self.inter_act_map.clear()
        self.intra_act.clear()

    def set_pipeline_params(self, num_batches):
        self.batch_num = num_batches
        # self.all_activations = [(batch_id, stage_id, addr_id, offset)]
        # for the first stage, put all activations
        # for the other stages, put all activations except inputs
        # for input / output activations, include their batch_id
        # but for intermediate ones, set their batch_id to a default 0
        # since inside a pipeline stage, the intermediate acts will not be overwritten by data from another batch
        for i, stage in enumerate(self.pipeline_stages):
            for act in stage.intermediate_act:
                self.intra_act.append((i, act[0], act[1]))
            for act in stage.outputs:
                for batch in range(self.batch_num):
                    self.inter_act.append((batch, i, act[0], act[1]))
        for act in self.pipeline_stages[0].inputs:
            for batch in range(self.batch_num):
                self.inter_act.append((batch, 0, act[0], act[1]))

        self.all_activations = self.inter_act + self.intra_act
        self.all_activations.sort(key=lambda x: self.pipeline_stages[x[-3]].data[(x[-2], x[-1])].size, reverse=True)

    def remap_weights(self):
        # weights are not strided tensors
        next_avail_aligned = self.weight_base_addr
        for weight_desc in self.all_weights:
            # weight_desc: (stage_id, addr_id, offset)
            self.weight_map[weight_desc] = (next_avail_aligned, False)  # False means it isn't mapped to CVSRAM
            # aligned size
            al_sz = self.aligned_ceil(self.pipeline_stages[weight_desc[0]].data[(weight_desc[1], weight_desc[2])].size)
            next_avail_aligned += al_sz

        self.activation_base_addr = next_avail_aligned

    def remap_activations(self):
        assert self.activation_base_addr is not None
        next_avail_aligned = self.activation_base_addr
        activation_map = {}
        # {(batch_id, stage_id, addr_id, offset) or (stage_id, addr_id, offset): (mapped_addr, is_cvsram)}

        mapped = [False for _ in self.all_activations]
        for act_id, act_desc in enumerate(self.all_activations):
            if mapped[act_id]:
                continue
            # for all the activations (outputs of the previous stage and inputs of the next stage are counted ONCE)
            # act_desc: (batch_id, stage_id, addr_id, offset)   (for input / output activations)
            #           (          stage_id, addr_id, offset)   (for intermediate activations)
            stage = self.pipeline_stages[act_desc[-3]]
            data_blk = stage.data[(act_desc[-2], act_desc[-1])]
            if data_blk.hyper_data_addr is not None:
                for strided_data_key in stage.hyper_data[data_blk.hyper_data_addr].bundled_data_blk_keys:
                    strided_data_blk = stage.data[strided_data_key]
                    if len(act_desc) == 3:
                        global_key = (act_desc[0], strided_data_key[0], strided_data_key[1])
                    elif len(act_desc) == 4:
                        global_key = (act_desc[0], act_desc[1], strided_data_key[0], strided_data_key[1])
                    else:
                        assert False
                    activation_map[global_key] = (next_avail_aligned + strided_data_blk.addr -
                                                  data_blk.hyper_data_addr, False)
                    mapped[self.all_activations.index(global_key)] = True
                assert mapped[act_id]
                next_avail_aligned += self.aligned_ceil(stage.hyper_data[data_blk.hyper_data_addr].bundled_data_size)
            else:
                activation_map[act_desc] = (next_avail_aligned, False)     # False means it isn't mapped to CVSRAM
                aligned_size = self.aligned_ceil(data_blk.size)
                next_avail_aligned += aligned_size
                mapped[act_id] = True

        # match the outputs of the previous stage with the inputs of the next stage
        # for inter-stage tensors, only their output-side were added to self.all_activations
        # and are thus mapped in the previous loop,
        # so we assert (input_)key not in activation_map
        for stage_id in range(1, len(self.pipeline_stages)):
            for i, ipt in enumerate(self.pipeline_stages[stage_id].inputs):
                for batch_id in range(self.batch_num):
                    key = (batch_id, stage_id, ipt[0], ipt[1])
                    corr_opt = self.pipeline_stages[stage_id - 1].outputs[i]    # corresponding output
                    corr_opt_key = (batch_id, stage_id - 1, corr_opt[0], corr_opt[1])
                    assert key not in activation_map.keys()
                    activation_map[key] = activation_map[corr_opt_key]

        for key, val in activation_map.items():
            if len(key) == 3:   # intermediate activation
                self.intra_act_map[key] = val
            else:
                assert len(key) == 4
                self.inter_act_map[key] = val

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

                for w_desc, w_map_info in self.weight_map.items():
                    # w_desc: pipeline_stage, addr_id, offset
                    assert len(w_desc) == 3
                    addr_mapped, is_cvsram = w_map_info
                    if w_desc[0] == i:
                        orig_addr = self.pipeline_stages[i].data[(w_desc[1], w_desc[2])].addr
                        for line_id, line in enumerate(txn_lines):
                            if hex(orig_addr) in line and not modify_status[line_id]:
                                new_lines[line_id] = line.replace(hex(orig_addr), hex(addr_mapped))
                                modify_status[line_id] = True
                                if is_cvsram:
                                    # should not go into this branch for class PipelineRemapper
                                    # only intended for PipelineWeightPinRemapper
                                    self.change_ram_type_to_cvsram(txn_lines, new_lines, line_id, modify_status)

                # intermediate activation / intra_act
                for ia_desc, ia_map_info in self.intra_act_map.items():
                    # ia_desc: (stage_id, addr_id, offset)
                    assert len(ia_desc) == 3
                    addr_mapped, is_cvsram = ia_map_info
                    if ia_desc[0] == i:
                        orig_addr = self.pipeline_stages[i].data[(ia_desc[-2], ia_desc[-1])].addr
                        for line_id, line in enumerate(txn_lines):
                            if hex(orig_addr) in line and not modify_status[line_id]:
                                new_lines[line_id] = line.replace(hex(orig_addr), hex(addr_mapped))
                                modify_status[line_id] = True
                                if is_cvsram:
                                    # should not go into this branch for class PipelineRemapper
                                    # only intended for PipelineWeightPinRemapper
                                    self.change_ram_type_to_cvsram(txn_lines, new_lines, line_id, modify_status)

                # input & output activation / inter_act
                for io_desc, io_map_info in self.inter_act_map.items():
                    addr_mapped, is_cvsram = io_map_info
                    # act_desc: (batch_id, stage_id, addr_id, offset)
                    assert len(io_desc) == 4
                    if io_desc[0] == batch_id and io_desc[1] == i:
                        orig_addr = self.pipeline_stages[i].data[(io_desc[2], io_desc[3])].addr
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
                    for rd_only_var in self.pipeline_stages[i].rd_only_vars:
                        data_blk = self.pipeline_stages[i].data[rd_only_var]

                        # we have disabled input prefetching in current rd_only_var_log
                        assert data_blk.attr == "weight"
                        key = (i, rd_only_var[0], rd_only_var[1])
                        mapped_to_cvsram = self.weight_map[key][1]
                        if not mapped_to_cvsram:
                            mapped_addr = self.weight_map[key][0]
                            fp.write(mapped_addr.to_bytes(4, byteorder="little", signed=False))
                            fp.write(data_blk.size.to_bytes(4, byteorder="little", signed=False))


class PipelineWeightPinRemapper(CVSRAMRemapper, PipelineRemapper):
    def __init__(self, in_dir, model_name):
        PipelineRemapper.__init__(self, in_dir, model_name)
        CVSRAMRemapper.__init__(self, in_dir, model_name)

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)

    def remap_weights(self):
        next_avail_aligned = self.weight_base_addr                              # next available addr in DRAM
        spaces_left = [self.cvsram_sizes[i] for i in range(self.num_cvsram)]    # available space in all CVSRAMs
        for weight_desc in self.all_weights:
            # weight_desc: (stage_id, addr_id, offset)
            stage_id = weight_desc[0]
            this_size = self.pipeline_stages[stage_id].data[(weight_desc[1], weight_desc[2])].size
            aligned_size = self.aligned_ceil(this_size)
            if aligned_size <= spaces_left[stage_id]:   # like WeightPinRemapper, assign weights greedily
                self.weight_map[weight_desc] = (self.cvsram_base_addrs[stage_id] + self.cvsram_sizes[stage_id] -
                                                spaces_left[stage_id], True)
                # True means assigned to CVSRAM; False means assigned in DRAM
                spaces_left[stage_id] -= aligned_size
            else:   # map to DRAM
                self.weight_map[weight_desc] = (next_avail_aligned, False)
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
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)

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
            for desc, rel_addr in relative_map.items():
                assert len(desc) == 2
                global_key = (stage_id, desc[0], desc[1])
                self.intra_act_map[global_key] = (self.cvsram_base_addrs[stage_id] + rel_addr, True)

    def write_to_files(self):
        PipelineRemapper.write_to_files(self)


def write_solver_input(file_path, workload, log_weights):
    with open(file_path, "w") as fp:
        for act in workload.intermediate_act:
            data_blk = workload.data[act]
            fp.write(str(data_blk.liveness[0]) + " " + str(data_blk.liveness[1]) + " " + str(data_blk.size) + " " +
                     str(data_blk.line_stride * data_blk.height) + " " + str(data_blk.surf_stride) + " " +
                     str(data_blk.num_access) + " " + hex(data_blk.addr) + " ")
            last_aligned_addr = last_aligned(data_blk.addr, data_blk.true_occupy_space, workload.axi_width)
            for id_rw in workload.addr_log[last_aligned_addr]:
                fp.write(id_rw[1] + " ")
            fp.write("\n")

        if log_weights:
            last_tick = len(workload.raw_addr_log) - 1
            for w in workload.weights:
                data_blk = workload.data[w]
                fp.write("0 " + str(last_tick) + " " + str(data_blk.size) +
                         " 0 " + "0 " + "1 " + hex(data_blk.addr) + " r\n")

        fp.write("-----\n")

        for hyp_tensor_addr, hyp_tensor in workload.hyper_data.items():
            fp.write(str(hyp_tensor.bundled_data_size) + " ")
            for related_data_key in hyp_tensor.bundled_data_blk_keys:
                try:
                    idx = workload.intermediate_act.index(related_data_key)
                    fp.write(str(idx) + " ")
                except ValueError as e:
                    print("did not find element ", related_data_key, " in self.workload.intermediate_act")
            fp.write("\n")


def collect_gurobi_results(workload, cvsram_size, gurobi_out_path, gurobi_in_path):
    # read results from the solver and draw the CVSRAM occupation figure
    occ_fig = plt.figure()
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
            desc = workload.weights[collect_w_cnt] if is_w else workload.intermediate_act[collect_a_cnt]
            data_blk = workload.data[desc]
            relative_mapping[desc] = int(words[1])
            if is_w:
                ax1.add_patch(patches.Rectangle((0, int(words[1])), len(workload.raw_addr_log) - 1, data_blk.size,
                                                linewidth=1, edgecolor='black'))
            else:
                if not data_blk.is_strided():
                    ax1.add_patch(patches.Rectangle((data_blk.liveness[0], int(words[1])),
                                                    data_blk.liveness[1] - data_blk.liveness[0], data_blk.size,
                                                    linewidth=1, edgecolor='black'))
                else:
                    # we need to draw several rectangles for strided tensors
                    num_segment = (data_blk.size - 1) // (data_blk.height * data_blk.line_stride) + 1
                    for stride in range(num_segment):
                        ax1.add_patch(patches.Rectangle((data_blk.liveness[0], int(words[1]) +
                                                         stride * data_blk.surf_stride),
                                                        data_blk.liveness[1] - data_blk.liveness[0],
                                                        data_blk.height * data_blk.line_stride,
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
    plt.tight_layout()
    occ_fig.savefig(gurobi_out_path + "_vis.png", dpi=300)

    return relative_mapping
