#!/bin/bash
# cpt dir: /home/lactose/gem5-rtl/m5out/cpt.348616490000/
%(gem5-binary)s \
-d %(output-dir)s \
%(config-dir)s --big-cpus 0 --little-cpus 1 --last-cache-level 2 --caches --accelerators \
--numNVDLA %(numNVDLA)s \
--maxReqNVDLA 1000 --enableTimingAXI \
--restore-from %(cpt-dir)s \
--bootscript=bootscript.rcS \
--ddr-type %(ddr-type)s \
%(dma-enable)s \
--embed-spm-size %(embed-spm-size)s \
--accel-embed-spm-lat %(accel-embed-spm-lat)s \
%(add-accel-private-cache)s \
--accel-pr-cache-size %(accel-pr-cache-size)s \
--accel-pr-cache-assoc %(accel-pr-cache-assoc)s \
--accel-pr-cache-tag-lat %(accel-pr-cache-tag-lat)s \
--accel-pr-cache-dat-lat %(accel-pr-cache-dat-lat)s \
--accel-pr-cache-resp-lat %(accel-pr-cache-resp-lat)s \
--accel-pr-cache-mshr %(accel-pr-cache-mshr)s \
--accel-pr-cache-tgts-per-mshr %(accel-pr-cache-tgts-per-mshr)s \
--accel-pr-cache-wr-buf %(accel-pr-cache-wr-buf)s \
--accel-pr-cache-clus %(accel-pr-cache-clus)s \
%(add-accel-shared-cache)s \
--accel-sh-cache-size %(accel-sh-cache-size)s \
--accel-sh-cache-assoc %(accel-sh-cache-assoc)s \
--accel-sh-cache-tag-lat %(accel-sh-cache-tag-lat)s \
--accel-sh-cache-dat-lat %(accel-sh-cache-dat-lat)s \
--accel-sh-cache-resp-lat %(accel-sh-cache-resp-lat)s \
--accel-sh-cache-mshr %(accel-sh-cache-mshr)s \
--accel-sh-cache-tgts-per-mshr %(accel-sh-cache-tgts-per-mshr)s \
--accel-sh-cache-wr-buf %(accel-sh-cache-wr-buf)s \
--accel-sh-cache-clus %(accel-sh-cache-clus)s \
%(pft-enable)s \
--pft-threshold %(pft-threshold)s \
%(use-fake-mem)s \
> stdout 2> stderr