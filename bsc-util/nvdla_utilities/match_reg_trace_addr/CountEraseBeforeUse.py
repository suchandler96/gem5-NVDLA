import re
import argparse
from GetAddrAttrAndMatch import parse_rd_wr_trace


def parse_args():
    parser = argparse.ArgumentParser(description="GetAddrAttrAndMatch.py options")
    parser.add_argument("--rw-trace-path", type=str, default="",
                        help="path to VP_mem_rd_wr of a specific testcase")
    parser.add_argument("--log-path", type=str, default="",
                        help="path to logged output of a specific testcase run with rtlNVDLA with gem5")
    parser.add_argument("--spm-line-size", type=int, default=1024,
                        help="SPM line size of embedded private SPM of NVDLA")

    return parser.parse_args()


def get_spm_line_attribute(addr_log):
    spm_line_attributes = {}
    for addr_val, rw_list in addr_log.items():
        aligned_addr = ((addr_val & ~(options.spm_line_size - 1)) & 0x0fffffff) | 0x80000000
        # first get the entry in spm_line_attributes
        line_attr = spm_line_attributes.get(aligned_addr)
        if line_attr is None:
            spm_line_attributes[aligned_addr] = 0
        for line_and_attr in rw_list:
            attr = line_and_attr[1]
            if attr == 'r':
                spm_line_attributes[aligned_addr] |= 0x1
            elif attr == 'w':
                spm_line_attributes[aligned_addr] |= 0x2
    return spm_line_attributes


if __name__ == "__main__":
    options = parse_args()

    # todo: get write-only SPM lines. Don't count these unused erases

    # first read all read and write traces
    addr_log, sorted_addr, raw_addr_log = parse_rd_wr_trace(options.rw_trace_path)
    spm_line_attributes = get_spm_line_attribute(addr_log)
    # sorted_addr has been deduplicated

    # build data structure according to log
    """Note the following stat only work for aligned addresses"""
    addr_gem5_log = {}

    num_dla_rd_req = 0
    num_dla_wr_req = 0
    num_dla_use = 0     # the piece of data is fed into NVDLA at this log's tick
    num_sys_dma_rd_req = 0
    num_sys_dma_rd_resp = 0
    num_sys_dma_wr_req = 0
    num_sys_pft_dma_rd_req = 0
    num_sys_rd_req = 0
    num_sys_rd_resp = 0
    num_sys_pft_req = 0
    num_sys_pft_resp = 0

    num_spm_rd_hit = 0
    num_spm_erase = 0

    num_all_unused_erase = 0
    num_unused_erase = 0    # erase of SPM lines that COULD have been reused
    num_unused_pft_erase = 0    # erases of SPM lines caused by prefetches (i.e., the prefetch has been wasted)

    with open(options.log_path) as fp:
        log_lines = fp.readlines()

    # perform two steps:
    # step 1: count the number of events
    # count all NVDLA- and system-related AXI_WIDTH-long txn and SPM events
    for log_line in log_lines:
        addr_0x_pos = log_line.find("addr 0x")
        if addr_0x_pos == -1:
            continue

        if "read request from dla" in log_line:
            num_dla_rd_req += 1
        elif "write request from dla, addr" in log_line:
            num_dla_wr_req += 1
        elif "read data used by nvdla" in log_line:
            num_dla_use += 1
        elif "DMA read req is issued:" in log_line:
            # we filter out this at the system side (instead of NVDLA side)
            # to include both prefetch DMA read and normal DMA read
            num_sys_dma_rd_req += 1
        elif "handling DMA return data" in log_line:
            num_sys_dma_rd_resp += 1
        elif "DMA write req is issued:" in log_line:
            num_sys_dma_wr_req += 1
        elif "PREFETCH (DMA) request" in log_line:
            num_sys_pft_dma_rd_req += 1
        elif "Read AXI Variable addr" in log_line:
            num_sys_rd_req += 1     # todo: this is currently in DPRINTF
        elif "read data returned by gem5, addr 0x" in log_line:
            num_sys_rd_resp += 1
        elif "PREFETCH request addr" in log_line:
            num_sys_pft_req += 1
        elif "read data returned by gem5 PREFETCH" in log_line:
            num_sys_pft_resp += 1
        elif "this spm_line_addr exists in spm" in log_line:
            num_spm_rd_hit += 1
        elif "is erased, dirty" in log_line:
            num_spm_erase += 1

    # step 2: log events for SPM lines instead of AXI_WIDTH transactions
    for log_line in log_lines:
        addr_0x_pos = log_line.find("addr 0x")
        if addr_0x_pos == -1:
            continue

        addr = (int(log_line[addr_0x_pos + 7: addr_0x_pos + 15], 16) & 0x0fffffff) | 0x80000000
        wr_cycle = re.findall(r"\(\s*\+?(-?\d+)\s*\)", log_line)[0]
        # find the first number in brackets

        aligned_addr = addr & ~(options.spm_line_size - 1)

        event = ""
        # No need to distinguish spm_line_size-aligned access or not
        # because we are focusing on a specific SPM line
        # Since each request has a request itself and a response, to avoid repetition,
        # we are only recording events that indicates the [moment] of SPM access happens

        # Not recording read_hit as an event, because each piece of data will eventually be used.
        if "read data used by nvdla" in log_line:
            event = "read_push"     # This piece of data is fed into NVDLA at this time step
        elif "write request from dla, addr" in log_line:
            event = "write_req"
        elif "is erased, dirty" in log_line:
            event = "erase"
        elif "handling DMA return data" in log_line:
            event = "dma_resp"
        elif "PREFETCH (DMA) request" in log_line:
            event = "pft_dma_req"
        elif "DMA read req issued from NVDLA side" in log_line:
            event = "dma_read_req"

        if event != "":
            addr_event_list = addr_gem5_log.get(aligned_addr)
            if addr_event_list is None:
                addr_gem5_log[aligned_addr] = [(wr_cycle, event)]
            else:
                addr_event_list.append((wr_cycle, event))

    for addr, addr_event_list in addr_gem5_log.items():
        erase_indices = [-1]    # in case multiple erases on one addr
        for i, cycle_and_event in enumerate(addr_event_list):
            if cycle_and_event[1] == "erase":
                erase_indices.append(i)

        # asserts loaded and count num_unused_erase (including all write-only, RW and read-only variables)
        for i in range(len(erase_indices) - 1):     # automatically avoids scenarios where no erases happen
            loaded = False
            used = False
            is_pft = False
            for j in range(erase_indices[i] + 1, erase_indices[i + 1]):
                if addr_event_list[j][1] == "pft_resp" or addr_event_list[j][1] == "dma_resp" \
                        or addr_event_list[j][1] == "write_req":
                    loaded = True
                elif addr_event_list[j][1] == "pft_dma_req":
                    is_pft = True
                elif addr_event_list[j][1] == "read_push":
                    used = True
            assert loaded
            if not used:
                num_all_unused_erase += 1
                if spm_line_attributes[addr] & 0x1 == 0:    # this is a write-only variable
                    num_unused_erase += 1
                elif is_pft:
                    num_unused_pft_erase += 1

    print("num_dla_rd_req = ", num_dla_rd_req, "\nnum_dla_wr_req = ", num_dla_wr_req, "\nnum_dla_use = ", num_dla_use,
          "\nnum_sys_dma_rd_req = ", num_sys_dma_rd_req, "\nnum_sys_dma_rd_resp = ", num_sys_dma_rd_resp,
          "\nnum_sys_dma_wr_req = ", num_sys_dma_wr_req, "\nnum_sys_pft_dma_rd_req = ", num_sys_pft_dma_rd_req,
          "\nnum_sys_rd_req = ", num_sys_rd_req,
          "\nnum_sys_rd_resp = ", num_sys_rd_resp, "\nnum_sys_pft_req = ", num_sys_pft_req,
          "\nnum_sys_pft_resp = ", num_sys_pft_resp, "\nnum_spm_rd_hit = ", num_spm_rd_hit,
          "\nnum_spm_erase = ", num_spm_erase, "\nnum_all_unused_erase = ", num_all_unused_erase,
          "\nnum_unused_erase = ", num_unused_erase, "\nnum_unused_pft_erase = ", num_unused_pft_erase)
