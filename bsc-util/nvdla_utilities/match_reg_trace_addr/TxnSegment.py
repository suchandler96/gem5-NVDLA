import re

pivot_addr_reg_name_list = ["0x5034", "0xb048", "0xc01c"]
addr_reg_list = [0x5034, 0x503c, 0x507c, 0xa02c, 0xa044, 0xb048, 0xc01c, 0xd070]
corr_map_table = {0x5000: [0x5000, 0x6000], 0xb000: [0xa000, 0xb000], 0xc000: [0xc000, 0xd000]}

data_addr_regs = [0x5034, 0xa044, 0xb048, 0xc01c, 0xd070]
weight_addr_regs = [0x507c, 0xa02c]
input_addr_regs = [0x5034, 0x507c, 0xa02c, 0xa044, 0xc01c]
# omitting 0x503c because we assume it will always be the same as 0x5034

output_addr_regs = [0xb048, 0xd070]
reg_to_neglect = [0x500c, 0x5010, 0x6008, 0xa008, 0xb038]


class TxnSegment:
    def __init__(self, core_reg, core_reg_val, core_line_num, file_name):
        # use numbers instead of strings
        # line num starts from 0
        self.core_reg = core_reg
        self.core_reg_val = core_reg_val
        self.addr_reg_mapping = {core_reg: core_reg_val}
        self.other_reg_mapping = {}
        self.core_line_num = core_line_num
        self.file_name = file_name
        self.match_candidates = []   # the indices to its possible matched txn segments in the unpartitioned txn file
        self.matched_target = None  # the index to its matched target txn segment

    def desc_str(self):
        return self.file_name + " line " + str(self.core_line_num) + " (" + str(hex(self.core_reg)) + " = " + \
               str(hex(self.core_reg_val)) + ")"


class TxnAddrDesc:
    def __init__(self, addr):
        self.addr = addr
        self.length = None
        self.is_weight = None
        self.txn_seg_ptrs = []
        self.io_type = None     # unknown: 0; input: 1; output: 2
        self.suggested_match_addr = None
        self.peer_desc_list = []

    def add_txn_seg_ptr(self, segment_ptr):
        if segment_ptr not in self.txn_seg_ptrs:
            self.txn_seg_ptrs.append(segment_ptr)

            for key, value in segment_ptr.addr_reg_mapping.items():
                if value == self.addr:
                    if key in data_addr_regs:
                        assert ((not self.is_weight) or (self.is_weight is None))
                        self.is_weight = False
                        self.io_type = 0

                    if key in weight_addr_regs:
                        assert (self.is_weight or (self.is_weight is None))
                        self.is_weight = True


def expand_line_range(txn_words, pivot_line_idx, txn_seg):
    patience_limit = 10
    patience = 10
    pivot_reg_base = int(txn_words[pivot_line_idx][3], 16) & 0xff000
    lower_line_idx = pivot_line_idx
    upper_line_idx = pivot_line_idx

    while True:
        lower_line_idx -= 1

        if lower_line_idx < 0:      # to prevent segfault
            break

        reg_addr = int(txn_words[lower_line_idx][-1], 16)

        if txn_words[lower_line_idx][0] != "write_reg" or reg_addr & 0xff000 not in corr_map_table[pivot_reg_base] or (
                (reg_addr & 0xfff == 0x004) or (reg_addr & 0xfff == 0x000)) or reg_addr in reg_to_neglect:
            patience -= 1
            if patience <= 0:
                break
            else:
                continue

        if reg_addr in addr_reg_list:
            # earlier configuration should be overwritten by a later one
            if reg_addr not in txn_seg.addr_reg_mapping:
                txn_seg.addr_reg_mapping[reg_addr] = int(txn_words[lower_line_idx][2], 16)
        else:
            if reg_addr not in txn_seg.other_reg_mapping:
                txn_seg.other_reg_mapping[reg_addr] = int(txn_words[lower_line_idx][2], 16)

    patience = patience_limit

    while True:
        upper_line_idx += 1

        if upper_line_idx >= len(txn_words):    # to prevent segfault
            break

        reg_addr = int(txn_words[upper_line_idx][-1], 16)

        if txn_words[upper_line_idx][0] != "write_reg" or reg_addr & 0xff000 not in corr_map_table[pivot_reg_base] or (
                (reg_addr & 0xfff == 0x004) or (reg_addr & 0xfff == 0x000)) or reg_addr in reg_to_neglect:
            patience -= 1
            if patience <= 0:
                break
            else:
                continue

        if reg_addr in addr_reg_list:
            txn_seg.addr_reg_mapping[reg_addr] = int(txn_words[upper_line_idx][2], 16)
        else:
            txn_seg.other_reg_mapping[reg_addr] = int(txn_words[upper_line_idx][2], 16)


# assume all 0x5034 configs will appear right after a 0x6xxx config
# assume all 0xc01c and 0xd070 configs will come together
# assume all 0xaxxx and 0xbxxx configs will come together
def get_txn_segments(txn_words, file_name):
    line_num = len(txn_words)

    txn_segments = []

    for i in range(line_num):
        if len(txn_words[i]) < 4:
            continue

        # words[0]: write/read_reg, words[1]: 0xffffxxxx, words[2]: value, words[3]: #+reg
        if txn_words[i][3] in pivot_addr_reg_name_list:
            txn_segment = TxnSegment(int(txn_words[i][3], 16), int(txn_words[i][2], 16), i, file_name)
            expand_line_range(txn_words, i, txn_segment)
            txn_segments.append(txn_segment)

    return txn_segments


def get_words(file_name):
    words = []
    with open(file_name) as fp:
        lines = fp.readlines()

    for line in lines:
        stripped_line = line.strip()
        tmp_words = re.split(r'[ \t\s]\s*', stripped_line)  # use spaces and tabs to split the string
        if tmp_words[0] == "read_reg" or tmp_words[0] == "write_reg":
            tmp_words[-1] = tmp_words[-1].strip('#')  # uncomment the reg address
        words.append(tmp_words)

    return words
