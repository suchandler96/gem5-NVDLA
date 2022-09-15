import sys
import os
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

    return addr_log, sorted_addr


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


def match_pipeline_stages(txn_addr_book_list, src_txn_addr_book, src_addr_space_end):
    pipeline_stage_num = len(txn_addr_book_list)

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
        for addr, addr_desc in txn_addr_book_list[stage_id].items():
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
        for addr, addr_desc in txn_addr_book_list[stage_id].items():
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
    for txn_addr_book in txn_addr_book_list:
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

    for txn_addr_book in txn_addr_book_list:
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
                    src_addr_space_end += txn_addr_desc.length

                # assign the same addr for peer
                for peer in txn_addr_desc.peer_desc_list:
                    peer.suggested_match_addr = txn_addr_desc.suggested_match_addr

    '''some src_data_addr_desc may not be mapped because I/O between pipeline stages will be mapped to
     only one src_data_addr_desc for convenience'''
    # for data_matched_tag in data_matched_tags:
        # assert data_matched_tag


def main():
    txn_segments_list = []
    txn_addr_book_list = []
    addr_log_list = []
    sorted_addr_list = []

    for i in range(1, len(sys.argv)):
        txn_path = os.popen("ls " + os.path.join(sys.argv[i], "*_raw_input.txn")).read().strip()
        mem_trace_path = os.path.join(sys.argv[i], "VP_mem_rd_wr")

        words = get_words(txn_path)
        txn_segments = get_txn_segments(words, txn_path)
        txn_addr_book = get_txn_addr(txn_segments)

        addr_log, sorted_addr = parse_rd_wr_trace(mem_trace_path)

        get_txn_addr_length(txn_addr_book, sorted_addr)

        input_addr_list, output_addr_list = get_input_addresses(addr_log), get_output_addresses(addr_log)
        get_io_type(txn_addr_book, input_addr_list, output_addr_list)

        txn_segments_list.append(txn_segments)
        txn_addr_book_list.append(txn_addr_book)
        addr_log_list.append(addr_log)
        sorted_addr_list.append(sorted_addr)

    match_pipeline_stages(txn_addr_book_list[1:], txn_addr_book_list[0], sorted_addr_list[0][-1] + 0x40)

    suggested_addr_mappings = {}
    for i in range(1, len(txn_addr_book_list)):
        print("pipeline stage " + str(i - 1) + ":\n")
        txn_addr_book = txn_addr_book_list[i]
        for addr in sorted(list(txn_addr_book.keys())):
            addr_desc = txn_addr_book[addr]
            suggested_addr_mappings[addr] = addr_desc.suggested_match_addr
            print("addr: " + str(hex(addr)) + "\nlength: " + str(hex(addr_desc.length)) + "\nis_weight: " +
                  str(addr_desc.is_weight) + "\nio_type: " + str(addr_desc.io_type) + "\nsuggested_match_addr: " +
                  str(hex(addr_desc.suggested_match_addr)) + "\n")

    # output mapping result to new txn files, can be commented if one doesn't want it
    for i in range(2, len(sys.argv)):
        txn_path = os.popen("ls " + os.path.join(sys.argv[i], "*_raw_input.txn")).read().strip()
        new_txn_path = txn_path.replace("raw_input", "matched_input")

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
                    assert addr in suggested_addr_mappings
                    new_lines.append(line.replace(tmp_words[-2], str(hex(suggested_addr_mappings[addr]))))
                    continue

            new_lines.append(line)

        with open(new_txn_path, 'w') as fp:
            fp.writelines(new_lines)


if __name__ == "__main__":
    main()
