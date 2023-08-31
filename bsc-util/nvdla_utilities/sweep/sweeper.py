import os
import sys
import json
import errno
import six
import shutil
import subprocess
import multiprocessing as mp
from params import *

param_types = {
    "ddr-type": DDRTypeParam,
    "numNVDLA": NumNVDLAParam,
    "dma-enable": DMAEnableParam,
    "embed-spm-size": EmbedSPMSizeParam,
    "accel-embed-spm-lat": AccelEmbedSPMLatParam,
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
    "shared-spm": SharedSPMParam
}


class Sweeper:
    def __init__(self, cpt_dir, output_dir, params_dir, gem5_binary, scheduler, scheduler_params):
        self._cpt_dir = cpt_dir
        self._output_dir = os.path.abspath(output_dir)
        if not os.path.isdir(self._output_dir):
            os.mkdir(self._output_dir)

        self._template_dir = os.path.dirname(os.path.abspath(__file__))

        self._gem5_binary = gem5_binary
        self._run_cmd = scheduler + " " + " ".join(scheduler_params)
        
        self._num_data_points = 0
        self._params_list = []
        self.pt_dirs = []
        
        for root, dirs, files in os.walk(params_dir):
            is_valid_dir = False
            for file in files:
                if file.endswith(".json"):
                    is_valid_dir = True
                    break
            if not is_valid_dir:
                continue

            rel_path = os.path.relpath(root, params_dir)
            sweep_output_dir = os.path.abspath(os.path.join(output_dir, rel_path))

            for file in files:
                if file.endswith(".json"):
                    file_name = "".join(file.rsplit(".json", 1))    # get the file name without file extension
                    this_sweep_out_dir = os.path.join(sweep_output_dir, file_name)
                    os.makedirs(this_sweep_out_dir, exist_ok=True)

                    with open(os.path.join(root, file)) as fp:
                        json_map = json.load(fp)
                    self._params_list.append((self._init_params(json_map), this_sweep_out_dir))

    def _init_params(self, params):
        params_ls = []
        for param_name, param_type in param_types.items():
            if param_name in params:
                params_ls.append(param_type(param_name, params[param_name]))
            else:
                params_ls.append(param_type(param_name, param_type.default_value()))
        return params_ls

    def _create_point(self, json_id):
        point_dir = os.path.join(self._params_list[json_id][1], str(self._num_data_points))

        # Copy configuration files to the simulation directory of this data point.
        if not os.path.isdir(point_dir):
            os.mkdir(point_dir)
        for f in ["run.sh", "bootscript.rcS"]:
            shutil.copyfile(
                os.path.join(self._template_dir, f),
                os.path.join(point_dir, f))

        change_config_file(point_dir, "run.sh", {"gem5-binary": self._gem5_binary})
        change_config_file(point_dir, "run.sh", {"cpt-dir": self._cpt_dir})
        change_config_file(point_dir, "run.sh", {"output-dir": os.path.abspath(point_dir)})
        change_config_file(point_dir, "bootscript.rcS", {"run-cmd": self._run_cmd})
        change_config_file(point_dir, "run.sh", {"config-dir":
            os.path.abspath(os.path.join(os.path.dirname(__file__), "../../../configs/example/arm/fs_bigLITTLE_RTL.py"))})

        # Apply every sweep parameter for this data point.
        for p in self._params_list[json_id][0]:
            p.apply(point_dir)

        self.pt_dirs.append(point_dir)
        print("---Created data point: %d.---" % self._num_data_points)

    def enumerate(self, param_idx, json_id):
        if param_idx < len(self._params_list[json_id][0]) - 1:
            while self._params_list[json_id][0][param_idx].next() == True:
                self.enumerate(param_idx + 1, json_id)
            return
        else:
            while self._params_list[json_id][0][param_idx].next() == True:
                self._create_point(json_id)
                self._num_data_points += 1
            return

    def enumerate_all(self):
        """Create configurations for all data points.  """
        print("Creating all data points...")
        for json_id in range(len(self._params_list)):
            self.enumerate(0, json_id)

    def run_all(self, threads):
        """Run simulations for all data points.

        Args:
        Number of threads used to run the simulations.
        """
        print("Running all data points...")
        counter = mp.Value('i', 0)
        sims = []
        pool = mp.Pool(
            initializer=_init_counter, initargs=(counter, ), processes=threads)
        for p in range(self._num_data_points):
            cmd = os.path.join(self.pt_dirs[p], "run.sh")
            sims.append(pool.apply_async(_run_simulation, args=(cmd, )))
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
        print("Running simulation returned nonzero exit code! Contents of output:\n "
        "%s\n%s" % (six.ensure_text(stdout), six.ensure_text(stderr)))

    with counter.get_lock():
        counter.value += 1
    print("---Finished running points: %d.---" % counter.value)
