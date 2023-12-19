import os
import sys
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../")))
from match_reg_trace_addr.remap import *
import json
import errno
import time
import six
import shutil
import math
import subprocess
import pickle
import multiprocessing as mp
from params import *

param_types = {
    "little-cpu-clock": LittleCPUClockParam,
    "freq-ratio": FreqRatioParam,
    "ddr-type": DDRTypeParam,
    "numNVDLA": NumNVDLAParam,
    "buffer-mode": BufferModeParam,
    "dma-enable": DMAEnableParam,
    "shared-spm": SharedSPMParam,
    "embed-spm-size": EmbedSPMSizeParam,
    "embed-spm-assoc": EmbedSPMAssocParam,
    "embed-spm-lat": EmbedSPMLatParam,
    "add-accel-private-cache": AddAccelPrivateCacheParam,
    "accel-pr-cache-size": AccelPrCacheSizeParam,
    "accel-pr-cache-assoc": AccelPrCacheAssocParam,
    "accel-pr-cache-tag-lat": AccelPrCacheTagLatParam,
    "accel-pr-cache-dat-lat": AccelPrCacheDatLatParam,
    "accel-pr-cache-resp-lat": AccelPrCacheRespLatParam,
    "accel-pr-cache-mshr": AccelPrCacheMshrParam,
    "accel-pr-cache-tgts-per-mshr": AccelPrCacheTgtsPerMshrParam,
    "accel-pr-cache-wr-buf": AccelPrCacheWrBufParam,
    "accel-pr-cache-clus": AccelPrCacheClusParam,
    "add-accel-shared-cache": AddAccelSharedCacheParam,
    "accel-sh-cache-size": AccelShCacheSizeParam,
    "accel-sh-cache-assoc": AccelShCacheAssocParam,
    "accel-sh-cache-tag-lat": AccelShCacheTagLatParam,
    "accel-sh-cache-dat-lat": AccelShCacheDatLatParam,
    "accel-sh-cache-resp-lat": AccelShCacheRespLatParam,
    "accel-sh-cache-mshr": AccelShCacheMshrParam,
    "accel-sh-cache-tgts-per-mshr": AccelShCacheTgtsPerMshrParam,
    "accel-sh-cache-wr-buf": AccelShCacheWrBufParam,
    "accel-sh-cache-clus": AccelShCacheClusParam,
    "pft-enable": PftEnableParam,
    "pft-threshold": PftThresholdParam,
    "use-fake-mem": UseFakeMemParam,
    "cvsram-enable": CVSRAMEnableParam,
    "cvsram-size": CVSRAMSizeParam,
    "cvsram-bandwidth": CVSRAMBandwidthParam,
    "remapper": RemapperParam
}


class Sweeper:
    def __init__(self, args):
        self.home_path = os.popen("cd ~/ && pwd").readlines()[0].strip('\n')
        self.new_home = args.home.rstrip("/")
        self.gem5_nvdla_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../"))
        self.gen_points = args.gen_points
        self.cpt_dir = None
        self.vp_out_dir = args.vp_out_dir
        self.out_dir = os.path.abspath(args.out_dir)
        self.model_name = args.model_name
        self.num_batches = args.pipeline_batches    # only for pipeline
        os.makedirs(args.out_dir, exist_ok=True)
        # create subdirectory 'traces' in case CVSRAM Remapper changes trace.bin

        self.disk_image = args.disk_image
        self.template_dir = os.path.dirname(os.path.abspath(__file__))
        self.gem5_binary = args.gem5_binary
        self.sim_dir = args.sim_dir
        self.scheduler = args.scheduler

        self.num_data_points = 0
        self.params_list = []   # [(params_list, this_sweep_out_dir)], each tuple is for one sweeping json file
        self.pt_dirs = []   # a list of working directories of all sweeping points
        self.trace_id_prefixes = [""]     # keep track of all traces generated

        self.mappers = {}
        self.mapper_comps = []  # [(mapper_path, [shell_cmd])]: each is a testcase that requires remapping computation

        for root, dirs, files in os.walk(args.jsons_dir):
            is_valid_dir = False
            for file in files:
                if file.endswith(".json"):
                    is_valid_dir = True
                    break
            if not is_valid_dir:
                continue

            rel_path = os.path.relpath(root, args.jsons_dir)
            sweep_output_dir = os.path.abspath(os.path.join(self.out_dir, rel_path))

            for file in files:
                if file.endswith(".json"):
                    file_name = "".join(file.rsplit(".json", 1))    # get the file name without file extension
                    this_sweep_out_dir = os.path.join(sweep_output_dir, file_name)
                    os.makedirs(this_sweep_out_dir, exist_ok=True)

                    with open(os.path.join(root, file)) as fp:
                        json_map = json.load(fp)
                    self.params_list.append((self._init_params(json_map), this_sweep_out_dir))

    def _init_params(self, params):
        params_ls = []
        for param_name, param_type in param_types.items():
            if param_name in params:
                params_ls.append(param_type(param_name, params[param_name]))
            else:
                params_ls.append(param_type(param_name, param_type.default_value()))
        return params_ls

    def _create_point(self, json_id):
        point_dir = os.path.join(self.params_list[json_id][1], str(self.num_data_points))
        self.pt_dirs.append(point_dir)
        if not self.gen_points:
            return

        # Copy configuration files to the simulation directory of this data point.
        if not os.path.isdir(point_dir):
            os.mkdir(point_dir)
        for f in ["run.sh", "bootscript.rcS"]:
            shutil.copyfile(
                os.path.join(self.template_dir, f),
                os.path.join(point_dir, f))

        trace_id = ""
        key_params = {}     # temporarily store the values of trace-changing sweeping parameters
        for p in self.params_list[json_id][0]:
            if p._changes_trace_bin:
                trace_id = "%s_%s" % (trace_id, str(p)) if trace_id != "" else str(p)
                if isinstance(p, RemapperParam):
                    key_params["Remapper"] = p.curr_sweep_value()
                elif isinstance(p, CVSRAMSizeParam):
                    size_match = re.search(r"(\d+)([kKmMgG])B", p.curr_sweep_value())
                    prefix = size_match.group(2).lower()
                    if "k" in prefix:
                        size = int(size_match.group(1)) * 1024
                    elif "m" in prefix:
                        size = int(size_match.group(1)) * 1024 * 1024
                    elif "g" in prefix:
                        size = int(size_match.group(1)) * 1024 * 1024 * 1024
                    else:
                        assert False
                    key_params["CVSRAMSize"] = size
        mapper_pfx = key_params["Remapper"]
        if eval("issubclass(" + mapper_pfx + "Remapper, PipelineRemapper)") and mapper_pfx != "Pipeline":
            new_sim_dir = os.path.join(self.sim_dir, trace_id)
        else:
            new_sim_dir = self.sim_dir

        # Only when this is a new trace id, do remapping
        # Some remapping process requires parallel computation. These tasks will be stored in self.remap_comps
        if trace_id not in self.trace_id_prefixes:  # initialized with [""], so should not accept empty trace_id
            self.trace_id_prefixes.append(trace_id)
            if mapper_pfx not in self.mappers.keys():   # if a new remapper, record it
                # for a certain workload and a certain class of remapper, only one remapper instance is needed
                self.mappers[mapper_pfx] = eval(mapper_pfx + "Remapper(self.vp_out_dir, self.model_name)")
            mapper = self.mappers[mapper_pfx]
            if mapper_pfx == "Identity":
                remap_subdir = "."
            elif mapper_pfx == "WeightPin" or mapper_pfx == "ActPin" or mapper_pfx == "MixPin":
                remap_subdir = "cvsram"
            elif mapper_pfx == "Pipeline":
                remap_subdir = "multibatch"
            elif mapper_pfx == "PipelineWeightPin" or mapper_pfx == "PipelineActPin":
                remap_subdir = "multibatch_cvsram"
            else:
                assert False

            remap_out_dir = os.path.abspath(os.path.join(self.vp_out_dir, remap_subdir))
            mapper.testcase_init(remap_out_dir, new_sim_dir, trace_id)
            if eval("issubclass(" + mapper_pfx + "Remapper, CVSRAMRemapper)"):
                if eval("issubclass(" + mapper_pfx + "Remapper, SingleAccelCVSRAMRemapper)"):
                    num_cvsram = 1
                elif eval("issubclass(" + mapper_pfx + "Remapper, PipelineRemapper)") and mapper_pfx != "Pipeline":
                    num_cvsram = mapper.num_stages
                else:
                    assert False
                mapper.set_cvsram_param(num_cvsram, [0x50000000 for _ in range(num_cvsram)],
                                        [key_params["CVSRAMSize"] for _ in range(num_cvsram)])
            if eval("issubclass(" + mapper_pfx + "Remapper, PipelineRemapper)"):
                mapper.set_pipeline_params(self.num_batches)

            exe_cmds = mapper.compute_remap_decision()
            dump_mapper_path = os.path.abspath(os.path.join(mapper.out_dir, trace_id + "_mapper"))
            self.mapper_comps.append((dump_mapper_path, exe_cmds if exe_cmds is not None and exe_cmds != [] else []))
            with open(dump_mapper_path, 'wb') as mapper_file:
                pickle.dump(mapper, mapper_file)

        else:
            mapper = self.mappers[mapper_pfx]

        if "single_thread" in self.scheduler:
            trace_bin = "trace.bin" if mapper_pfx == "Identity" else trace_id + "_trace.bin"
            rd_only_var_log = "rd_only_var_log" if mapper_pfx == "Identity" else trace_id + "_rd_only_var_log"
            run_cmd = "/home/" + self.scheduler + " " + os.path.join(new_sim_dir, trace_bin) + " " + \
                      os.path.join(self.sim_dir, rd_only_var_log)
        elif "pipeline" in self.scheduler:
            if mapper_pfx == "Pipeline" or mapper_pfx == "PipelineWeightPin" or mapper_pfx == "PipelineActPin":
                run_cmd = "/home/" + self.scheduler + " " + \
                          os.path.join(new_sim_dir, self.model_name + "_" + trace_id + "_") + \
                          " " + str(self.num_batches) + " " + str(mapper.num_stages)
            else:
                assert False
        else:
            assert False

        # cpt-dir should be changed after regenerating a checkpoint
        change_config_file(point_dir, "run.sh",
                           {"gem5-binary": self.gem5_binary.replace(self.home_path, self.new_home)})

        change_config_file(point_dir, "run.sh",
                           {"output-dir": os.path.abspath(point_dir).replace(self.home_path, self.new_home)})
        change_config_file(point_dir, "bootscript.rcS", {"run-cmd": run_cmd})
        change_config_file(point_dir, "run.sh", {"config-dir":
            os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../configs/example/arm/fs_bigLITTLE_RTL.py"))
                                                 .replace(self.home_path, self.new_home)})

        # Apply every sweep parameter for this data point.
        for p in self.params_list[json_id][0]:
            p.apply(point_dir)

        print("---Created data point: %d.---" % self.num_data_points)

    def parallel_remap_compute(self):
        pool = mp.Pool(mp.cpu_count())
        for _, cmds in self.mapper_comps:
            for cmd in cmds:
                pool.apply_async(shell_run_cmd, args=(cmd, ))
        pool.close()
        pool.join()

    def resume_create_point(self):
        if not os.path.exists(os.path.join(self.gem5_nvdla_dir, "mnt/mnt")):
            os.system("cd " + os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../")) +
                      " && sudo python3 util/gem5img.py mount " + self.disk_image + " ./mnt")

        for dump_mapper_path, _ in self.mapper_comps:
            with open(dump_mapper_path, 'rb') as mapper_file:
                mapper = pickle.load(mapper_file)
            os.system("rm " + dump_mapper_path)
            mapper.collect_remap_decision()
            mapper.write_to_files()
            mapper.copy_output_to_img()

    def enumerate(self, param_idx, json_id):
        if param_idx < len(self.params_list[json_id][0]) - 1:
            while self.params_list[json_id][0][param_idx].next():
                self.enumerate(param_idx + 1, json_id)
            return
        else:
            # check the legality and meaningfulness of the combination of parameters
            while self.params_list[json_id][0][param_idx].next():
                type_val_pairs = {}
                for p in self.params_list[json_id][0]:
                    type_val_pairs[type(p)] = p.curr_sweep_value()
                meaningful = True
                for p in self.params_list[json_id][0]:
                    meaningful = meaningful and p.is_meaningful(type_val_pairs)
                    if not meaningful:
                        break
                if not meaningful:
                    continue
                self._create_point(json_id)
                self.num_data_points += 1
            return

    def enumerate_all(self):
        """Create configurations for all data points.  """
        print("Creating all data points...")
        for json_id in range(len(self.params_list)):
            self.enumerate(0, json_id)

        # run compilation in parallel for some data points
        self.parallel_remap_compute()
        self.resume_create_point()

    def run_all(self, args):
        """Run simulations for all data points.

        Args:
        Number of threads used to run the simulations.
        """
        # get self.pt_dirs if not args.gen_points
        if not args.gen_points:
            for root, dirs, files in os.walk(self.out_dir):
                if "run.sh" in files:
                    self.pt_dirs.append(root)
            self.pt_dirs.sort()

        assert 0 <= args.machine_id < args.num_machines
        if args.machine_id == 0 and not args.skip_checkpoint:
            print("Generating checkpoint...")
            os.makedirs(os.path.join(self.gem5_nvdla_dir, "m5out"), exist_ok=True)  # in case m5out doesn't exist
            bin_path = "build/ARM/gem5.opt" if args.gem5_binary.endswith("opt") else "build/ARM/gem5.fast"
            lg = os.popen("cd " + self.gem5_nvdla_dir + " && " + bin_path + " configs/example/arm/fs_bigLITTLE_RTL.py"
                          " --big-cpus 0 --little-cpus 1 --cpu-type atomic"
                          " --bootscript=configs/boot/hack_back_ckpt.rcS").readlines()
            # get the exact directory of the checkpoint just generated
            tick_match = re.search("at tick ([0-9]+)", lg[-4])
            assert tick_match is not None
            cpt_dir_name = "cpt." + tick_match.group(1)
            self.cpt_dir = os.path.join(self.gem5_nvdla_dir, "m5out", cpt_dir_name)

            # after generating the checkpoint, we can apply it to the scripts
            for pt_dir in self.pt_dirs:
                with open(os.path.join(pt_dir, "run.sh")) as fp:
                    run_sh_lines = fp.readlines()
                for i, line in enumerate(run_sh_lines):
                    if "restore-from" in line:
                        if "cpt-dir" in line:   # placeholder found
                            change_config_file(pt_dir, "run.sh", {"cpt-dir": self.cpt_dir})
                        else:
                            cpt_match = re.search(r"(cpt\.[0-9]+)", line)
                            assert cpt_match is not None
                            run_sh_lines[i] = run_sh_lines[i].replace(cpt_match.group(1), cpt_dir_name)
                            with open(os.path.join(pt_dir, "run.sh"), "w") as fp:
                                fp.writelines(run_sh_lines)
                        break

            esc_home_path = self.home_path.replace('/', '\\/')
            esc_new_home = self.new_home.replace('/', '\\/')
            esc_out_dir = self.out_dir.replace('/', '\\/')
            os.system('sed -i "s/' + esc_home_path + '/' + esc_new_home + '/g" `grep "' +
                      esc_home_path + '" -rl ' + esc_out_dir + '`')

        print("machine_id = %d, running all data points..." % args.machine_id)
        assert args.num_machines > 0
        this_machine_pt_dirs = []
        num_groups = math.ceil(len(self.pt_dirs) / args.num_threads)
        for grp_id in range(num_groups):
            if grp_id % args.num_machines == args.machine_id:
                for pt_id in range(grp_id * args.num_threads, min((grp_id + 1) * args.num_threads, len(self.pt_dirs))):
                    this_machine_pt_dirs.append(self.pt_dirs[pt_id])

        counter = mp.Value('i', 0)
        sims = []
        pool = mp.Pool(
            initializer=_init_counter, initargs=(counter, ), processes=args.num_threads)
        for p in range(len(this_machine_pt_dirs)):
            cmd = os.path.join(this_machine_pt_dirs[p], "run.sh")
            sims.append(pool.apply_async(_run_simulation, args=(cmd, )))
            time.sleep(0.5)     # sleep for a while before launching next to avoid a gem5 bug (socket bind() failed)
            # see https://gem5-users.gem5.narkive.com/tvnOFKtP/panic-listensocket-listen-listen-failed
        for sim in sims:
            sim.get()
        pool.close()
        pool.join()


counter = 0


def _init_counter(args):
    global counter
    counter = args


def _run_simulation(cmd):
    global counter
    process = subprocess.Popen(["bash", cmd], cwd=os.path.dirname(cmd),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = process.communicate()
    if process.returncode != 0:
        print("Running simulation returned nonzero exit code!\ncoming from cmd:\n%s\nContents of output:\n "
              "%s\n%s" % (cmd, six.ensure_text(stdout), six.ensure_text(stderr)))

    with counter.get_lock():
        counter.value += 1
    print("---Finished running points: %d.---" % counter.value)


def shell_run_cmd(cmd):
    os.system(cmd)
