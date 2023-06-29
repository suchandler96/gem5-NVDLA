import os
from configparser import RawConfigParser
import fileinput
import re

def change_config_file(point_dir, config_file, kv_map):
    f = fileinput.input(os.path.join(point_dir, config_file), inplace=True)
    for line in f:
        for k in kv_map:
            if k in line:
                line = line % kv_map
        print(line, end="")
    f.close()

class BaseParam:
    def __init__(self, name, sweep_vals):
        self._name = name
        self._sweep_vals = sweep_vals
        self._curr_sweep_idx = -1

    def __str__(self):
        return "%s_%s" % (self._name, str(self.curr_sweep_value()))

    def curr_sweep_value(self):
        return self._sweep_vals[self._curr_sweep_idx]

    def apply(self, point_dir):
        raise NotImplementedError

    @classmethod
    def get(self, point_dir):
        raise NotImplementedError

    @classmethod
    def default_value(cls):
        raise NotImplementedError

    def next(self):
        self._curr_sweep_idx += 1
        if self._curr_sweep_idx == len(self._sweep_vals):
            self._curr_sweep_idx = -1
            return False
        return True


class DDRTypeParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"ddr-type": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--ddr-type")
            if pos == -1:
                continue
            return re.search(r"--ddr-type\s+([0-9a-zA-Z\_]+)").group(1)

    @classmethod
    def default_value(cls):
      return ["DDR3_1600_8x8"]


class NumNVDLAParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"numNVDLA": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--numNVDLA")
            if pos == -1:
                continue
            return re.search(r"--numNVDLA\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [1]


class DMAEnableParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)
        
    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"dma-enable": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--dma-enable")
            if pos != -1:
                return True
        return False
            

    @classmethod
    def default_value(cls):
        return [""]


class EmbedSPMSizeParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"embed-spm-size": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--embed-spm-size")
            if pos == -1:
                continue
            return re.search(r"--embed-spm-size\s+([0-9a-zA-Z]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return ["64kB"]


class AccelEmbedSPMLatParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-embed-spm-lat": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-embed-spm-lat")
            if pos == -1:
                continue
            return re.search(r"--accel-embed-spm-lat\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [12]


class AddAccelPrivateCacheParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)
        
    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"add-accel-private-cache": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--add-accel-private-cache")
            if pos != -1:
                return True
        return False

    @classmethod
    def default_value(cls):
        return [""]


class AccelPrCacheSizeParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-size": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-size")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-size\s+([0-9a-zA-Z]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return ["1MB"]


class AccelPrCacheAssocParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-assoc": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-assoc")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-assoc\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [16]


class AccelPrCacheTagLatParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-tag-lat": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-tag-lat")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-tag-lat\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [12]


class AccelPrCacheDatLatParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-dat-lat": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-dat-lat")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-dat-lat\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [12]


class AccelPrCacheRespLatParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-resp-lat": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-resp-lat")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-resp-lat\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [5]


class AccelPrCacheMshrParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-mshr": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-mshr")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-mshr\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [32]


class AccelPrCacheTgtsPerMshrParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-tgts-per-mshr": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-tgts-per-mshr")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-tgts-per-mshr\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [8]


class AccelPrCacheWrBufParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-wr-buf": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-wr-buf")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-wr-buf\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [8]


class AccelPrCacheClusParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-pr-cache-clus": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-pr-cache-clus")
            if pos == -1:
                continue
            return re.search(r"--accel-pr-cache-clus\s+([a-zA-Z\_]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return ["mostly_incl"]


class AddAccelSharedCacheParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)
        
    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"add-accel-shared-cache": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--add-accel-shared-cache")
            if pos != -1:
                return True
        return False

    @classmethod
    def default_value(cls):
        return [""]


class AccelShCacheSizeParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-size": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-size")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-size\s+([0-9a-zA-Z]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return ["4MB"]


class AccelShCacheAssocParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-assoc": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-assoc")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-assoc\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [16]


class AccelShCacheTagLatParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-tag-lat": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-tag-lat")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-tag-lat\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [12]


class AccelShCacheDatLatParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-dat-lat": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-dat-lat")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-dat-lat\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [12]


class AccelShCacheRespLatParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-resp-lat": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-resp-lat")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-resp-lat\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [5]


class AccelShCacheMshrParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-mshr": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-mshr")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-mshr\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [128]


class AccelShCacheTgtsPerMshrParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-tgts-per-mshr": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-tgts-per-mshr")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-tgts-per-mshr\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [8]


class AccelShCacheWrBufParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-wr-buf": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-wr-buf")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-wr-buf\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [32]


class AccelShCacheClusParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"accel-sh-cache-clus": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--accel-sh-cache-clus")
            if pos == -1:
                continue
            return re.search(r"--accel-sh-cache-clus\s+([a-zA-Z\_]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return ["mostly_excl"]


class PftEnableParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)
        
    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"pft-enable": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--pft-enable")
            if pos != -1:
                return True
        return False

    @classmethod
    def default_value(cls):
        return [""]


class PftThresholdParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)

    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"pft-threshold": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--pft-threshold")
            if pos == -1:
                continue
            return re.search(r"--pft-threshold\s+([0-9]+)", line).group(1)

    @classmethod
    def default_value(cls):
        return [16]


class UseFakeMemParam(BaseParam):
    def __init__(self, name, sweep_vals):
        BaseParam.__init__(self, name, sweep_vals)
        
    def apply(self, point_dir):
        change_config_file(
            point_dir, "run.sh", {"use-fake-mem": self.curr_sweep_value()})

    @classmethod
    def get(self, point_dir):
        run_sh_path = os.path.join(point_dir, "run.sh")
        assert os.path.exists(run_sh_path)
        with open(run_sh_path, "r") as fp:
            run_sh_lines = fp.readlines()

        for line in run_sh_lines:
            pos = line.find("--use-fake-mem")
            if pos != -1:
                return True
        return False

    @classmethod
    def default_value(cls):
        return [""]
