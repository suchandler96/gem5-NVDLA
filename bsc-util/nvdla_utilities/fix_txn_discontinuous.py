import os
import argparse
import re


def check_discontinuity(vp_out_dir, out_name):
    file = os.path.join(vp_out_dir, "input.txn")
    assert os.path.exists(file)
    with open(file) as fp:
        lines = fp.readlines()

    interrupt_regs = {0x0004, 0x0008, 0x000c}
    status_regs = {0x4040, 0x5000, 0x6000, 0x7000, 0x8000, 0x9000, 0xa000, 0xb000, 0xc000, 0xd000, 0xe000, 0xf000, 0x10000}
    pingpong_regs = {0x5004, 0x6004, 0x7004, 0x8004, 0x9004, 0xa004, 0xb004, 0xc004, 0xd004, 0xe004, 0xf004, 0x10004}
    kickoff_regs = {0x4034, 0x4038, 0x5010, 0x6008, 0x7008, 0x8008, 0x9008, 0xa008, 0xb038, 0xc008, 0xd008, 0xe008, 0xf048, 0x10008}

    new_lines = [str(line) for line in lines]

    for line_id, line in enumerate(lines):
        if "until" in line:
            of_concern = True
            # read the lines before:
            component_offset = None
            to_insert_pos = None
            for explore_id in range(line_id - 1, -1, -1):
                # search for the first register instruction that does not involve global interrupt registers
                reg_match = re.search(r'#(0x[0-9a-f]{1,2}0[0-9a-f]{2})', lines[explore_id])
                if reg_match is None or int(reg_match.group(1), 16) in interrupt_regs:
                    explore_id -= 1
                    continue
                else:
                    reg_addr = int(reg_match.group(1), 16)
                    if reg_addr in status_regs or reg_addr in pingpong_regs or reg_addr in kickoff_regs:
                        of_concern = False
                    else:
                        component_offset = ((reg_addr >> 12) << 12)
                        to_insert_pos = explore_id + 1
                    break

            if not of_concern:
                continue
            assert component_offset is not None
            assert to_insert_pos is not None

            logging = False
            to_move_id_start = None
            to_move_id_end = None
            for explore_id in range(line_id + 1, len(lines)):
                # search for the first register instruction that does not involve global interrupt registers
                reg_match = re.search(r'#(0x[0-9a-f]{1,2}0[0-9a-f]{2})', lines[explore_id])
                if reg_match is None or int(reg_match.group(1), 16) in interrupt_regs:
                    if not logging:
                        continue
                    else:
                        # logging completed.
                        logging = False
                        to_move_id_end = explore_id
                        break
                else:
                    reg_addr = int(reg_match.group(1), 16)
                    offset = ((reg_addr >> 12) << 12)
                    if reg_addr in status_regs or reg_addr in pingpong_regs or reg_addr in kickoff_regs or \
                            offset != component_offset:
                        if not logging:
                            # this is not something that we need to modify. Go to next 'until'
                            pass
                        else:
                            # logging completed
                            logging = False
                            to_move_id_end = explore_id
                        break
                    else:
                        if not logging:
                            # start logging
                            logging = True
                            to_move_id_start = explore_id
                        else:
                            # keep logging
                            pass
            assert not logging
            if to_move_id_start is not None:
                assert to_move_id_end is not None
                del new_lines[to_move_id_start: to_move_id_end]
                for i in range(to_move_id_end - to_move_id_start):
                    new_lines.insert(to_insert_pos + i, lines[to_move_id_start + i])

    with open(os.path.join(vp_out_dir, out_name), "w") as fp:
        fp.writelines(new_lines)


def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--vp-out-dir", help="should be the same with '--out-dir' for caffe2trace.py "
        " This directory holds input.txn, sc.log mem trace, etc. This script should only be called by caffe2trace.py,"
        " instead of pipeline_compile.py, because it only handles one input.txn file.", required=True)
    parser.add_argument(
        "--name", help="output .txn file name", default="try_input")
    args = parser.parse_args()

    check_discontinuity(args.vp_out_dir, args.name + ".txn")


if __name__ == "__main__":
    main()
