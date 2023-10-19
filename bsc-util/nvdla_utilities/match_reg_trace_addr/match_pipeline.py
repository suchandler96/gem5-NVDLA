import sys
import os
import argparse

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
import parse_qemu_log


class PipelineMultiBatch:
    def __init__(self):
        self.alignment = 0x1000
        self.align_mask = 0xfffff000
        self.weight_base_addr = 0x80000000
        self.activation_base_addr = None
        self.batch_num = 4
        self.pipeline_stages = []
        self.weight_map = {}
        self.activation_map = {}

    def aligned_ceil(self, addr):
        return ((addr - 0x1) & self.align_mask) + self.alignment

    def remap_weights(self):
        all_weights = []
        for i, stage in enumerate(self.pipeline_stages):
            for weight in stage.weights:
                all_weights.append((i, weight[0], weight[1]))
        # all_weights is a list of tuples of (pipeline_stage_id(starting from 0), addr_id, offset)
        all_weights.sort(key=lambda x: self.pipeline_stages[x[0]].data[(x[1], x[2])].size, reverse=True)

        next_avail_aligned = self.weight_base_addr
        for weight_desc in all_weights:
            # weight_desc: (stage_id, addr_id, offset)
            self.weight_map[weight_desc] = next_avail_aligned
            next_avail_aligned = self.aligned_ceil(next_avail_aligned +
                self.pipeline_stages[weight_desc[0]].data[(weight_desc[1], weight_desc[2])].size)

        self.activation_base_addr = next_avail_aligned

    def remap_activations(self):
        all_activations = []
        # for the first stage, put all activations
        # for the other stages, put all activations except inputs
        for i, stage in enumerate(self.pipeline_stages):
            if i == 0:
                for act in stage.activations:
                    for batch in range(self.batch_num):
                        all_activations.append((batch, i, act[0], act[1]))
            else:
                for act in stage.activations:
                    if act not in stage.inputs:
                        for batch in range(self.batch_num):
                            all_activations.append((batch, i, act[0], act[1]))

        all_activations.sort(key=lambda x: self.pipeline_stages[x[1]].data[(x[2], x[3])].size, reverse=True)

        assert self.activation_base_addr is not None
        next_avail_aligned = self.activation_base_addr
        for act_desc in all_activations:
            # act_desc: (batch_id, stage_id, addr_id, offset)
            self.activation_map[act_desc] = next_avail_aligned
            next_avail_aligned = self.aligned_ceil(next_avail_aligned +
                self.pipeline_stages[act_desc[1]].data[(act_desc[2], act_desc[3])].size)

        for stage_id in range(1, len(self.pipeline_stages)):
            for i, ipt in enumerate(self.pipeline_stages[stage_id].inputs):
                for batch_id in range(self.batch_num):
                    key = (batch_id, stage_id, ipt[0], ipt[1])
                    corr_opt = self.pipeline_stages[stage_id - 1].outputs[i]
                    corr_opt_key = (batch_id, stage_id - 1, corr_opt[0], corr_opt[1])
                    assert key not in self.activation_map.keys()
                    self.activation_map[key] = self.activation_map[corr_opt_key]

    def remap(self, in_dirs):
        self.pipeline_stages = []
        for pipeline_log_dir in in_dirs:
            pipeline_stage = parse_qemu_log.Workload(pipeline_log_dir)
            pipeline_stage.print_workload_info()
            self.pipeline_stages.append(pipeline_stage)

        self.remap_weights()
        self.remap_activations()


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument("--in-dir", type=str, metavar="dir", nargs='+',
                        help="a list of directories containing txn, mem traces and qemu log "
                             "of a pipeline stage, with each pipeline stage in order")
    parser.add_argument(
        "--out-dir", default="/home/lactose/nvdla/traces/lenet_auto_pipeline_multibatch/",
        help="directory to put the generated sc.log, register txn and mem traces")

    args = parser.parse_args()
    return args


def main():
    options = parse_args()
    pipeline_mapper = PipelineMultiBatch()
    for i in range(len(options.in_dir)):
        os.makedirs(os.path.join(options.out_dir, "stage_" + str(i + 1)), exist_ok=True)
    pipeline_mapper.remap(options.in_dir)


if __name__ == "__main__":
    main()
