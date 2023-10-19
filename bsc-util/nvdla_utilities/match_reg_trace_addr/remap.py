import sys
import shutil
import os
import argparse
import re

sys.path.append(os.path.dirname(__file__))
from parse_qemu_log import Workload


class BaseRemapper:
    def __init__(self, in_dir, nvdla_hw_path, model_name):
        """ paths """
        self.in_dir = in_dir
        self.nvdla_hw_path = nvdla_hw_path
        self.model_name = model_name

        """ workload-related info """
        self.out_dir = None
        self.sim_dir_host = None
        self.testcase_str = None

        """ mapping parameters """
        self.alignment = 0x1000
        self.align_mask = 0xfffffffffffff000

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        self.out_dir = out_dir
        os.makedirs(out_dir, exist_ok=True)
        self.sim_dir_host = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../mnt" + sim_dir))
        os.system("sudo mkdir -p " + self.sim_dir_host)
        self.testcase_str = testcase_str

    def aligned_ceil(self, addr):
        return ((addr - 0x1) & self.align_mask) + self.alignment

    def get_remap_decision(self):
        pass

    def write_to_files(self):
        pass

    def copy_output_to_img(self):
        files = os.listdir(self.out_dir)
        for file in files:
            if "rd_only_var_log" in file or ".bin" in file:
                os.system("sudo cp " + os.path.join(self.out_dir, file) + " " + self.sim_dir_host)


class IdentityRemapper(BaseRemapper):
    def __init__(self, in_dir, nvdla_hw_path, model_name):
        super(IdentityRemapper, self).__init__(in_dir, nvdla_hw_path, model_name)

    def testcase_init(self, out_dir, sim_dir, testcase_str=""):
        assert os.path.abspath(out_dir) == os.path.abspath(self.in_dir)
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)

    def get_remap_decision(self):
        pass

    def write_to_files(self):
        pass


class CVSRAMRemapper(BaseRemapper):
    def __init__(self, in_dir, nvdla_hw_path, model_name):
        super(CVSRAMRemapper, self).__init__(in_dir, nvdla_hw_path, model_name)

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
        reg_match = re.search(r'#(0x[0-9a-f]{1,2}0[0-9a-f]{2})', line)
        reg = int(reg_match.group(1), 16)
        ram_type_reg, ram_type_bit = self.assoc_reg_bits[reg]

        explore_id = line_id - 1
        while explore_id >= 0:
            ram_reg_match = re.search(r'#(0x[0-9a-f]{1,2}0[0-9a-f]{2})', raw_lines[explore_id])
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
            ram_reg_match = re.search(r'#(0x[0-9a-f]{1,2}0[0-9a-f]{2})', raw_lines[explore_id])
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
    def __init__(self, in_dir, nvdla_hw_path, model_name):
        super(SingleAccelCVSRAMRemapper, self).__init__(in_dir, nvdla_hw_path, model_name)

        """ workload-related info """
        self.workload = Workload(in_dir)

        """ testcase-related decisions """
        self.mapping = {}   # {addr_in_dram: addr_in_cvsram}

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)
        self.mapping.clear()

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
        script_path = os.path.join(os.path.abspath(self.nvdla_hw_path), "verif/verilator/input_txn_to_verilator.pl")
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
    def __init__(self, in_dir, nvdla_hw_path, model_name):
        super(WeightPinRemapper, self).__init__(in_dir, nvdla_hw_path, model_name)

        """ workload-related info """
        # sort all the weights in workload
        self.weights_cp = [weight for weight in self.workload.weights]
        self.weights_cp.sort(key=lambda x: self.workload.data[x].size, reverse=True)

    def get_remap_decision(self):
        assert self.num_cvsram == 1
        cvsram_size = self.cvsram_sizes[0]
        cvsram_base_addr = self.cvsram_base_addrs[0]

        # assign one by one greedily. If enough space, then mark to remap
        space_left = cvsram_size
        for weight in self.weights_cp:
            aligned_size = self.aligned_ceil(self.workload.data[weight].size)
            if aligned_size <= space_left:
                self.mapping[self.workload.data[weight].addr] = cvsram_base_addr + cvsram_size - space_left
                space_left -= aligned_size


class PipelineRemapper(BaseRemapper):
    def __init__(self, in_dir, nvdla_hw_path, model_name):
        super(PipelineRemapper, self).__init__(in_dir, nvdla_hw_path, model_name)

        """ paths """
        self.in_dirs = []
        compile_out_subdirs = os.listdir(self.in_dir)
        found_any = False
        for subdir in compile_out_subdirs:
            if "stage_" in subdir:
                self.in_dirs.append(subdir)
                found_any = True
        self.num_stages = len(self.in_dirs)
        assert found_any

        """ mapping parameters """
        self.weight_base_addr = 0x80000000
        self.activation_base_addr = None
        self.batch_num = 4

        """ workload-related info """
        self.pipeline_stages = []
        for pipeline_log_dir in self.in_dirs:
            pipeline_stage = Workload(pipeline_log_dir)
            pipeline_stage.print_workload_info()
            self.pipeline_stages.append(pipeline_stage)

        # weights
        self.all_weights = []
        for i, stage in enumerate(self.pipeline_stages):
            for weight in stage.weights:
                self.all_weights.append((i, weight[0], weight[1]))
                # all_weights = [(pipeline_stage_id(starting from 0), addr_id, offset)]
        self.all_weights.sort(key=lambda x: self.pipeline_stages[x[0]].data[(x[1], x[2])].size, reverse=True)

        # activations
        self.all_activations = []
        # all_activations = [(batch_id, stage_id, addr_id, offset)]
        # for the first stage, put all activations
        # for the other stages, put all activations except inputs
        for i, stage in enumerate(self.pipeline_stages):
            if i == 0:
                for act in stage.activations:
                    for batch in range(self.batch_num):
                        self.all_activations.append((batch, i, act[0], act[1]))
            else:
                for act in stage.activations:
                    if act not in stage.inputs:
                        for batch in range(self.batch_num):
                            self.all_activations.append((batch, i, act[0], act[1]))

        self.all_activations.sort(key=lambda x: self.pipeline_stages[x[1]].data[(x[2], x[3])].size, reverse=True)

        """ testcase-related decisions """
        self.weight_map = {}        # {(stage_id, addr_id, offset): (mapped_addr, is_cvsram)}
        self.activation_map = {}    # {(batch_id, stage_id, addr_id, offset): (mapped_addr, is_cvsram)}

    def testcase_init(self, out_dir, sim_dir, testcase_str):
        BaseRemapper.testcase_init(self, out_dir, sim_dir, testcase_str)
        self.weight_map.clear()
        self.activation_map.clear()

    def remap_weights(self):
        next_avail_aligned = self.weight_base_addr
        for weight_desc in self.all_weights:
            # weight_desc: (stage_id, addr_id, offset)
            self.weight_map[weight_desc] = next_avail_aligned
            al_sz = self.aligned_ceil(self.pipeline_stages[weight_desc[0]].data[(weight_desc[1], weight_desc[2])].size)
            next_avail_aligned += al_sz

        self.activation_base_addr = next_avail_aligned

    def remap_activations(self):
        assert self.activation_base_addr is not None
        next_avail_aligned = self.activation_base_addr
        for act_desc in self.all_activations:
            # for all the activations (outputs of the previous stage and inputs of the next stage are counted ONCE)
            # act_desc: (batch_id, stage_id, addr_id, offset)
            self.activation_map[act_desc] = (next_avail_aligned, False)     # False means it isn't mapped to CVSRAM
            aligned_size = self.aligned_ceil(self.pipeline_stages[act_desc[1]].data[(act_desc[2], act_desc[3])].size)
            next_avail_aligned += aligned_size

        # match the outputs of the previous stage with the inputs of the next stage
        for stage_id in range(1, len(self.pipeline_stages)):
            for i, ipt in enumerate(self.pipeline_stages[stage_id].inputs):
                for batch_id in range(self.batch_num):
                    key = (batch_id, stage_id, ipt[0], ipt[1])
                    corr_opt = self.pipeline_stages[stage_id - 1].outputs[i]    # corresponding output
                    corr_opt_key = (batch_id, stage_id - 1, corr_opt[0], corr_opt[1])
                    assert key not in self.activation_map.keys()
                    self.activation_map[key] = self.activation_map[corr_opt_key]

    def get_remap_decision(self):
        self.remap_weights()
        self.remap_activations()

    def write_to_files(self):
        for i in range(self.num_stages):
            with open(os.path.join(self.in_dirs[i], "input.txn")) as fp:
                txn_lines = fp.readlines()
            for batch_id in range(self.batch_num):
                new_lines = [str(line) for line in txn_lines]
                modify_status = []
                for j in range(len(txn_lines)):
                    modify_status.append(False)     # False means not modified by remapping yet

                for w_desc, w_map_info in self.weight_map.items():
                    # w_desc: pipeline_stage, addr_id, offset
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

                for a_desc, a_map_info in self.activation_map.items():
                    addr_mapped, is_cvsram = a_map_info
                    # a_desc: batch_id, stage_id, addr_id, offset
                    if a_desc[0] == batch_id and a_desc[1] == i:
                        orig_addr = self.pipeline_stages[i].data[(a_desc[2], a_desc[3])].addr
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

                with open(out_txn_path, "w") as fp:
                    fp.writelines(new_lines)

                # also need to call the perl script to convert them into binary format
                # the nvdla/vp docker image has perl v5.22.1 installed, ok
                perl_script_path = os.path.join(os.path.abspath(self.nvdla_hw_path),
                                                "verif/verilator/input_txn_to_verilator.pl")
                os.system("perl " + perl_script_path + " " + out_txn_path + " " + out_bin_path)


class PipelineWeightPinRemapper(CVSRAMRemapper, PipelineRemapper):
    def __init__(self, in_dir, nvdla_hw_path, model_name):
        PipelineRemapper.__init__(self, in_dir, nvdla_hw_path, model_name)
        CVSRAMRemapper.__init__(self, in_dir, nvdla_hw_path, model_name)

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

    def get_remap_decision(self):
        self.remap_weights()
        self.remap_activations()

    def write_to_files(self):
        PipelineRemapper.write_to_files(self)
