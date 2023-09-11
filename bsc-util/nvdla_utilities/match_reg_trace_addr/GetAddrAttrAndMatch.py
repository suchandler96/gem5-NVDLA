import sys
import os
import argparse

sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from TxnSegment import *


def get_input_addresses(addr_log):
    input_addresses = []
    for addr, log in addr_log.items():
        if log[0][1] == 'r':
            input_addresses.append(addr)

    return input_addresses


def get_output_addresses(addr_log):
    output_addresses = []
    for addr, log in addr_log.items():
        if log[-1][1] == 'w':
            output_addresses.append(addr)

    return output_addresses


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


def get_txn_addr(txn_segments):
    txn_addr_book = {}
    for segment in txn_segments:
        for key, value in segment.addr_reg_mapping.items():
            if value not in txn_addr_book:
                txn_addr_desc = TxnAddrDesc(value)
                txn_addr_desc.add_txn_seg_ptr(segment)
                txn_addr_book[value] = txn_addr_desc
            else:
                txn_addr_book[value].add_txn_seg_ptr(segment)

    return txn_addr_book


def get_txn_addr_length(txn_addr_book, sorted_addr):
    for i in range(len(sorted_addr)):
        if sorted_addr[i] in txn_addr_book:

            length = 0x0
            last_addr = sorted_addr[i] - 0x40
            for counter_id in range(i, len(sorted_addr)):
                if last_addr == sorted_addr[counter_id] - 0x40:
                    length += 0x40
                    last_addr += 0x40
                else:
                    break

            txn_addr_book[sorted_addr[i]].length = length

    sorted_txn_addr = list(txn_addr_book.keys())
    sorted_txn_addr.sort()
    for i in range(len(sorted_txn_addr) - 1):   # for the largest txn_addr, this is neither necessary nor applicable
        txn_addr_desc = txn_addr_book[sorted_txn_addr[i]]
        txn_addr_desc.length = min(txn_addr_desc.length, sorted_txn_addr[i + 1] - sorted_txn_addr[i])


def get_io_type(txn_addr_book, input_addr_list, output_addr_list):
    for addr in input_addr_list:
        if addr in txn_addr_book:
            txn_addr_desc = txn_addr_book[addr]
            if not txn_addr_desc.is_weight:
                txn_addr_desc.io_type = 1       # only mark data as input (instead of weights)

    for addr in output_addr_list:
        if addr in txn_addr_book:
            txn_addr_book[addr].io_type = 2     # mark as output


def match_pipeline_stages(pipeline_txn_addr_book_list, src_txn_addr_book, src_addr_space_end):
    pipeline_stage_num = len(pipeline_txn_addr_book_list)

    # firstly, match weights
    # only need to give each tensor a unique address space
    src_weight_addr_desc_list = []
    src_data_addr_desc_list = []
    for addr, addr_desc in src_txn_addr_book.items():
        if addr_desc.is_weight:
            src_weight_addr_desc_list.append(addr_desc)
        else:
            src_data_addr_desc_list.append(addr_desc)

    src_weight_addr_desc_list.sort(key=lambda x: x.length)
    weight_matched_tags = [False for _ in range(len(src_weight_addr_desc_list))]

    for stage_id in range(pipeline_stage_num):
        for addr, addr_desc in pipeline_txn_addr_book_list[stage_id].items():
            if addr_desc.is_weight:
                # look for a position in src_weight_addr_desc_list
                found = False
                for i in range(len(src_weight_addr_desc_list)):
                    if addr_desc.length == src_weight_addr_desc_list[i].length and not weight_matched_tags[i]:
                        weight_matched_tags[i] = True
                        addr_desc.suggested_match_addr = src_weight_addr_desc_list[i].addr
                        found = True
                        break
                assert found
    for weight_matched_tag in weight_matched_tags:      # all weights in src should be mapped
        assert weight_matched_tag

    # then, match intra-stage data
    src_data_addr_desc_list.sort(key=lambda x: x.length)
    data_matched_tags = [False for _ in range(len(src_data_addr_desc_list))]

    for stage_id in range(pipeline_stage_num):
        for addr, addr_desc in pipeline_txn_addr_book_list[stage_id].items():
            if addr_desc.io_type == 0:      # intermediate variables
                # look for a position in src_data_addr_desc_list
                found = False
                for i in range(len(src_data_addr_desc_list)):
                    if addr_desc.length == src_data_addr_desc_list[i].length and not data_matched_tags[i]:
                        data_matched_tags[i] = True
                        addr_desc.suggested_match_addr = src_data_addr_desc_list[i].addr
                        found = True
                        break
                assert found

    # at last, match inter-stage input / output
    input_entries = []
    output_entries = []
    for txn_addr_book in pipeline_txn_addr_book_list:
        # each pipeline stage
        input_found = False
        output_found = False
        for txn_addr, txn_addr_desc in txn_addr_book.items():
            if txn_addr_desc.io_type == 1:
                assert not input_found  # for now only accept pipeline stages with single-input-single-output
                input_entries.append(txn_addr_desc)
            elif txn_addr_desc.io_type == 2:
                assert not output_found
                output_entries.append(txn_addr_desc)

    assert len(input_entries) == pipeline_stage_num and len(output_entries) == pipeline_stage_num

    for i in range(pipeline_stage_num - 1):
        input_entries[i + 1].peer_desc_list.append(output_entries[i])
        output_entries[i].peer_desc_list.append(input_entries[i + 1])

    for txn_addr_book in pipeline_txn_addr_book_list:
        for txn_addr, txn_addr_desc in txn_addr_book.items():

            if (txn_addr_desc.io_type == 1 or txn_addr_desc.io_type == 2) \
                    and txn_addr_desc.suggested_match_addr is None:
                # look for a position in src_data_addr_desc_list
                found = False
                for i in range(len(src_data_addr_desc_list)):
                    if txn_addr_desc.length == src_data_addr_desc_list[i].length and not data_matched_tags[i]:
                        data_matched_tags[i] = True
                        txn_addr_desc.suggested_match_addr = src_data_addr_desc_list[i].addr
                        found = True
                        break

                if not found:
                    # assign a new address space for it
                    txn_addr_desc.suggested_match_addr = src_addr_space_end
                    src_addr_space_end = ((src_addr_space_end + txn_addr_desc.length - 0x40) & 0x1000) + 0x1000

                # assign the same addr for peer
                for peer in txn_addr_desc.peer_desc_list:
                    peer.suggested_match_addr = txn_addr_desc.suggested_match_addr

    '''some src_data_addr_desc may not be mapped because I/O between pipeline stages will be mapped to
     only one src_data_addr_desc for convenience'''
    # for data_matched_tag in data_matched_tags:
    # assert data_matched_tag


# todo: current io_type has bugs with output and intermediate variables due to addr reuse with NVDLA compiler
def generate_addr_mappings(options, txn_addr_book_list):
    suggested_addr_mappings = []
    for i in range(len(txn_addr_book_list)):
        suggested_addr_mapping = {}
        print("pipeline stage " + str(i - 1) + ":\n")
        txn_addr_book = txn_addr_book_list[i]
        for addr in sorted(list(txn_addr_book.keys())):
            addr_desc = txn_addr_book[addr]
            # assume we want to distinguish READ_ONLY (weight & input) / OUTPUT / INTERMEDIATE
            suggested = addr_desc.suggested_match_addr if addr_desc.suggested_match_addr is not None else addr
            suggested = ((suggested & 0x0FFFFFFF) | 0x80000000)
            if options.mark_addr_type:
                '''
                if addr_desc.io_type == 0:  # intermediate variables
                    suggested = ((suggested & 0x0FFFFFFF) | 0xA0000000)
                elif addr_desc.io_type == 2:  # output variables
                    suggested = ((suggested & 0x0FFFFFFF) | 0x90000000)
                '''
                if addr_desc.io_type == 0 or addr_desc.io_type == 2:  # intermediate and output
                    suggested = ((suggested & 0x0FFFFFFF) | 0x90000000)
            suggested_addr_mapping[addr] = suggested

            print("addr: " + str(hex(addr)) + "\nlength: " + str(hex(addr_desc.length)) + "\nis_weight: " +
                  str(addr_desc.is_weight) + "\nio_type: " + str(addr_desc.io_type) + "\nsuggested_match_addr: " +
                  str(hex(suggested)) + "\n")

        suggested_addr_mappings.append(suggested_addr_mapping)
    return suggested_addr_mappings


def output_result_txn(options, suggested_addr_mappings):
    for i in range(len(options.src_dirs)):
        txn_path = os.popen("ls " + os.path.join(options.src_dirs[i], "input.txn")).read().strip()
        new_txn_path = txn_path.replace("input", "matched_input")

        with open(txn_path) as fp:
            lines = fp.readlines()

        new_lines = []
        for line in lines:
            if "write_reg" in line:
                stripped_line = line.strip()
                tmp_words = re.split(r'[ \t\s]\s*', stripped_line)
                tmp_words[-1] = tmp_words[-1].strip('#')
                if int(tmp_words[-1], 16) in addr_reg_list:
                    addr = int(tmp_words[-2], 16)
                    assert addr in suggested_addr_mappings[i]
                    new_lines.append(line.replace(tmp_words[-2], str(hex(suggested_addr_mappings[i][addr]))))
                    continue

            new_lines.append(line)

        with open(new_txn_path, 'w') as fp:
            fp.writelines(new_lines)


def output_rd_only_var_log(options, txn_addr_book_list, raw_addr_log_list, suggested_addr_mappings):
    for i in range(len(options.src_dirs)):
        rd_var_log = []     # this list keeps the relative position of reading each variable
        rd_var_log_path = os.path.join(options.src_dirs[i], "rd_only_var_log")
        raw_addr_log = raw_addr_log_list[i]
        txn_addr_book = txn_addr_book_list[i]

        for rw_and_addr in raw_addr_log:
            # rw_and_addr[0] is 'r' or 'w', while rw_and_addr[1] is the raw address
            if 'w' in rw_and_addr[0] or rw_and_addr[1] not in txn_addr_book:        # only care about read variables
                continue

            addr_desc = txn_addr_book[rw_and_addr[1]]
            if addr_desc.is_weight or addr_desc.io_type == 1:                           # weight or input
                # if addr_desc.is_weight:     # for convenience, temporarily only include weights
                rd_var_log.append([suggested_addr_mappings[i][rw_and_addr[1]], addr_desc.length])   # (addr, len)

        # output to file
        with open(rd_var_log_path, "wb") as fp:
            for j in range(len(rd_var_log)):
                fp.write((rd_var_log[j][0]).to_bytes(4, byteorder="little", signed=False))
                fp.write((rd_var_log[j][1]).to_bytes(4, byteorder="little", signed=False))


def main():
    parser = argparse.ArgumentParser(description="GetAddrAttrAndMatch.py options")
    parser.add_argument("--src-dirs", type=str, metavar="dir", nargs='+',
                        help="a list of directories containing *input.txn and VP_mem_rd_wr,"
                             "if --mark-addr-type is also specified, expect the first to be the unpartitioned NN's txn"
                             "while the following to be each pipeline stage in order")
    parser.add_argument("--match-pipeline-stages", action="store_true", default=False,
                        help="to view the first as end2end model and the following as pipeline stages and map them into a unified mem space")
    parser.add_argument("--mark-addr-type", action="store_true", default=False,
                        help="to remap weight, input data, intermediate data, output data into different address spaces."
                             "weight & input addr starts from 0x80000000, output: 0x90000000, intermediate: 0xa0000000;"
                             "but currently since NVDLA compiler will sometimes reuse output addresses, "
                             "all non-read-only variables start from 0x90000000")
    parser.add_argument("--output-result-txn", action="store_true", default=False,
                        help="to output the mapped addresses to a new txn file under the original directories")
    parser.add_argument("--output-rd-only-var-log", action="store_true", default=False,
                        help="to output read-only variables together with lengths, ranked by their time of issue")
    options = parser.parse_args()

    txn_addr_book_list = []
    addr_log_list = []
    raw_addr_log_list = []
    sorted_addr_list = []

    for i in range(len(options.src_dirs)):
        txn_path = os.popen("ls " + os.path.join(options.src_dirs[i], "input.txn")).read().strip()
        mem_trace_path = os.path.join(options.src_dirs[i], "VP_mem_rd_wr")
        assert os.path.exists(txn_path)
        assert os.path.exists(mem_trace_path)

        words = get_words(txn_path)
        txn_segments = get_txn_segments(words, txn_path)
        txn_addr_book = get_txn_addr(txn_segments)

        addr_log, sorted_addr, raw_addr_log = parse_rd_wr_trace(mem_trace_path)

        get_txn_addr_length(txn_addr_book, sorted_addr)

        input_addr_list, output_addr_list = get_input_addresses(addr_log), get_output_addresses(addr_log)
        get_io_type(txn_addr_book, input_addr_list, output_addr_list)

        txn_addr_book_list.append(txn_addr_book)
        addr_log_list.append(addr_log)
        raw_addr_log_list.append(raw_addr_log)
        sorted_addr_list.append(sorted_addr)

    if options.match_pipeline_stages:
        match_pipeline_stages(txn_addr_book_list[1:], txn_addr_book_list[0], (sorted_addr_list[0][-1] & 0x1000) + 0x1000)

    suggested_addr_mappings = generate_addr_mappings(options, txn_addr_book_list)

    if options.output_result_txn:
        output_result_txn(options, suggested_addr_mappings)

    if options.output_rd_only_var_log:
        output_rd_only_var_log(options, txn_addr_book_list, raw_addr_log_list, suggested_addr_mappings)


if __name__ == "__main__":
    main()
