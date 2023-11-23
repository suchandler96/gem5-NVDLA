import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import sys


def parse_one_type_trace(lines):
    addresses = [int(line, 16) for line in lines]

    x_coords = np.arange(0, len(addresses))

    fig = plt.figure()
    ax = fig.gca()
    plt.scatter(x_coords, addresses, s=1)

    ylabels = map(lambda t: '0x%08x' % int(t), ax.get_yticks())
    ax.set_yticklabels(ylabels)

    plt.show()


def parse_mixed_type_trace(lines):
    rd_address_x_coords = []
    rd_address_y_coords = []
    wr_address_x_coords = []
    wr_address_y_coords = []
    for i, line in enumerate(lines):
        words_in_line = line.split()
        if 'r' in words_in_line[0]:
            rd_address_x_coords.append(i)
            rd_address_y_coords.append(int(words_in_line[1], 16))
        elif 'w' in words_in_line[0]:
            wr_address_x_coords.append(i)
            wr_address_y_coords.append(int(words_in_line[1], 16))
        else:
            print("unknown line: %s" % line)
            exit(1)

    fig = plt.figure()
    ax = fig.gca()
    plt.scatter(rd_address_x_coords, rd_address_y_coords, s=1, c='red', label='read trace')
    plt.scatter(wr_address_x_coords, wr_address_y_coords, s=1, c='blue', label='write_trace')

    ylabels = map(lambda t: '0x%08x' % int(t), ax.get_yticks())
    ax.set_yticklabels(ylabels)
    ax.legend()

    plt.show()


if __name__ == '__main__':

    with open(sys.argv[1]) as f:
        lines = f.readlines()

    # detect type of trace file:
    # contains only one type of read / write or mixed read-write
    if ' ' in lines[0]:
        parse_mixed_type_trace(lines)
    else:
        parse_one_type_trace(lines)
