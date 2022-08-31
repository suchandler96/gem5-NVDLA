#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>

enum FuncType {
    no_func,
    parse_vp_log,
    comp_mem_trace,
    nvdla_cpp_log2mem_trace
};

class AXI_Txn {
public:
    int is_write{};
    uint64_t address{};
    int print_rd_flag;
    int print_wr_flag;

    AXI_Txn(int _print_rd, int _print_wr) : print_rd_flag(_print_rd), print_wr_flag(_print_wr) {}

    void print_axi_txn() const {
        if(is_write) {
            if(print_wr_flag)
                printf("%lx\n", address);
        } else {
            if(print_rd_flag)
                printf("%lx\n", address);
        }
    }

    void clear() {
        is_write = 0;
        address = 0;
    }
};


class CSB_Txn {
public:
    uint32_t addr;
    uint32_t data;
    uint32_t is_write;
    uint32_t n_posted;

    // for read response only
    uint32_t err_bit;
    uint32_t exp_data;

    int print_reg_txn_flag;
    int change_addr_flag;

    bool inputting;

    CSB_Txn(int flag, int addr_convert_flag) : addr(0), data(0), is_write(0), n_posted(0), err_bit(0), exp_data(0),
                                               inputting(false), print_reg_txn_flag(flag), change_addr_flag(addr_convert_flag) {}

    void clear() {
        inputting = false;
        addr = 0;
        data = 0;
        is_write = 0;
        n_posted = 0;

        err_bit = 0;
        exp_data = 0;
    }

    void print_csb_txn() const {
        if(print_reg_txn_flag) {
            uint32_t out_addr =
                    0xffff0000 + (0x0000ffff & ((addr - 0) >> 2));  // write bit will be corrected by txn2verilator

            if(is_write) {
                if(change_addr_flag && ((data & 0xf0000000) == 0xc0000000)) {
                    printf("write_reg 0x%x 0x%08x\t\t\t#0x%04x\n", out_addr, (data & 0x0fffffff) | 0x80000000, addr);
                } else {
                    printf("write_reg 0x%x 0x%08x\t\t\t#0x%04x\n", out_addr, data, addr);
                }
            } else {
                assert(data == 0x0);

                if(addr == 0x000c && exp_data != 0) {
                    printf("until 0xffff0003 0x%08x\n", exp_data);
                } else if(addr == 0xa004) {
                    printf("read_reg 0x%08x 0x0 0x%08x\t#0x%04x\n", out_addr, exp_data, addr);
                } else
                    printf("read_reg 0x%08x 0xffffffff 0x%08x\t#0x%04x\n", out_addr, exp_data, addr);
            }
        }
    }

    void mark_using() {
        inputting = true;
    }
};


void parse_args(int argc, char** argv, int* flags, std::vector<std::string>& in_files) {
    bool func_determined = false;
    for(int i = 0; i < 16; i++)
        flags[i] = 0;

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "--input1") == 0 || strcmp(argv[i], "-i1") == 0 || strcmp(argv[i], "--input2") == 0 || strcmp(argv[i], "-i2") == 0 || strcmp(argv[i], "-i") == 0) {
            // todo: handle exception when argv[i+1] == nullptr or NULL
            in_files.emplace_back(argv[i + 1]);
            i++;
        } else if(strcmp(argv[i], "--print_mem_rd") == 0) {
            // print AXI data read addresses (aligned to 0x40)
            flags[0] = 1;
        } else if(strcmp(argv[i], "--print_mem_wr") == 0) {
            // print AXI data write addresses (aligned to 0x40)
            flags[1] = 1;
        } else if(strcmp(argv[i], "--print_reg_txn") == 0) {
            // print CSB register transactions
            flags[2] = 1;
        } else if(strcmp(argv[i], "--print_wait") == 0) {
            // print interrupt signals to inform CPU to wait
            flags[3] = 1;
        } else if(strcmp(argv[i], "--function") == 0 || strcmp(argv[i], "-f") == 0) {
            // select the function to operate on
            i++;
            // todo: handle exception when argv[i] == nullptr or NULL
            if(strcmp(argv[i], "parse_vp_log") == 0) {
                flags[4] = parse_vp_log;
            } else if(strcmp(argv[i], "comp_mem_trace") == 0) {
                flags[4] = comp_mem_trace;
            } else if(strcmp(argv[i], "nvdla_cpp_log2mem_trace") == 0) {
                flags[4] = nvdla_cpp_log2mem_trace;
            } else {
                std::cerr << "Error: invalid function type: " << argv[i] << "\n\nUse \"-h\" option to print help message.\n";
                exit(1);
            }

            func_determined = true;
        } else if(strcmp(argv[i], "--change_addr") == 0) {
            // change data address from 0xcxxxxxxxx to 0x8xxxxxxxx
            flags[5] = 1;
        } else if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: ./NVDLAUtil [-i1] in_file_1 ([-i2] in_file_2) [--function <function>] [--print_options]\n\n\n");
            printf("\t--function | -f: select the function to operate on\n");
            printf("\t <function>: parse_vp_log (convert VP debug info to NVDLA register traces (.txn), memory traces or both)\n");
            printf("\t             comp_mem_trace (compare memory trace files)\n");
            printf("\t             nvdla_cpp_log2mem_trace (convert terminal output of nvdla.cpp to memory traces)\n\n");
            printf("\t[--print_options]:\n");
            printf("\t\t--print_mem_rd: print AXI data read addresses (aligned to 0x40)\n");
            printf("\t\t--print_mem_wr: print AXI data write addresses (aligned to 0x40)\n");
            printf("\t\t--print_reg_txn: print CSB register transactions\n");
            printf("\t\t--print_wait: print CPU wait instructions for NVDLA interrupts\n");

            exit(0);
        } else {
            std::cerr << "Error: invalid argument: "<< argv[i] << "\n\nUse \"-h\" option to print help message.\n";
            exit(1);
        }
    }

    if(!func_determined) {
        std::cerr << "Error: no function specified\n";
        exit(1);
    }
}


void VPLog2Txn(const std::string& vp_trace_file_name, const int* print_flags) {
    std::ifstream vp_trace_file;
    AXI_Txn axi_txn(print_flags[0], print_flags[1]);
    CSB_Txn csb_txn(print_flags[2], print_flags[5]);


    vp_trace_file.open(vp_trace_file_name, std::ios::in);


    std::string vp_file_line;
    while(getline(vp_trace_file, vp_file_line)) {

        if(vp_file_line.length() < 2)   // the line is empty
            continue;

        //! csb transactions
        if(vp_file_line.find("NV_NVDLA_csb_master::nvdla2csb_b_transport, base_addr:") != std::string::npos)
            continue;
        if(vp_file_line.find("NV_NVDLA_csb_master::nvdla2csb_b_transport, csb req to") != std::string::npos) {
            assert(!csb_txn.inputting);
            csb_txn.mark_using();
            continue;
        }

        if(vp_file_line.find("NV_NVDLA_csb_master.cpp:") != std::string::npos) {
            assert(csb_txn.inputting);
            if(vp_file_line.find(":Addr:") != std::string::npos) {
                size_t pos_of_num = vp_file_line.find_last_of(' ');
                std::stringstream ss(vp_file_line.substr(pos_of_num + 1));
                ss >> std::hex >> csb_txn.addr;
                continue;
            }

            if(vp_file_line.find(":Data:") != std::string::npos) {
                size_t pos_of_num = vp_file_line.find_last_of(' ');
                std::stringstream ss(vp_file_line.substr(pos_of_num + 1));
                ss >> std::hex >> csb_txn.data;
                continue;
            }

            if(vp_file_line.find(":Is write:") != std::string::npos) {
                size_t pos_of_num = vp_file_line.find_last_of(' ');
                std::stringstream ss(vp_file_line.substr(pos_of_num + 1));
                ss >> std::hex >> csb_txn.is_write;
                continue;
            }

            if(vp_file_line.find(":nposted:") != std::string::npos) {
                size_t pos_of_num = vp_file_line.find_last_of(' ');
                std::stringstream ss(vp_file_line.substr(pos_of_num + 1));
                ss >> std::hex >> csb_txn.n_posted;
                if(csb_txn.n_posted != 0) {
                    printf("n_posted != 0.\n");
                    exit(1);
                }

                if(csb_txn.is_write) {
                    //! the end for csb write_reg log
                    csb_txn.inputting = false;
                    csb_txn.print_csb_txn();
                    csb_txn.clear();
                }
                continue;
            }

            if(vp_file_line.find("Err bit:") != std::string::npos) {
                //! unique for csb read_reg response
                size_t pos_of_err_num = vp_file_line.find("Err bit:") + 9;
                std::string sub_string = vp_file_line.substr(pos_of_err_num);
                size_t pos_space = sub_string.find_first_of(' ');
                std::stringstream ss(sub_string.substr(0, pos_space));
                ss >> std::hex >> csb_txn.err_bit;

                if(csb_txn.err_bit != 0) {
                    printf("csb read response error bit is not 0\n");
                    exit(1);
                }

                size_t pos_of_exp_data = vp_file_line.find_last_of('x') - 1;
                std::stringstream new_ss(vp_file_line.substr(pos_of_exp_data));
                new_ss >> std::hex >> csb_txn.exp_data;


                //! the end for csb read_reg log
                csb_txn.inputting = false;
                csb_txn.print_csb_txn();
                csb_txn.clear();
                continue;
            }

            std::cerr << "Unresolved csb line:\n" << vp_file_line << "\n";
            exit(1);
        }
        /*
        if(vp_file_line.find("interrupt") != std::string::npos) {
            size_t pos_of_interrupt = vp_file_line.find_last_of(' ');
            std::string last_word = vp_file_line.substr(pos_of_interrupt + 1);
            if(last_word.find("interrupt") != std::string::npos)
                intr_mgr.print_interrupt(vp_file_line);

            continue;
        }
         */

        if(vp_file_line.find("NvdlaAxiAdaptor::axi_rd_wr_thread, send") != std::string::npos) {
            if(vp_file_line.find("done") != std::string::npos)
                continue;

            int temp_is_write;
            if(vp_file_line.find("read request") != std::string::npos)
                temp_is_write = 0;
            else if(vp_file_line.find("write request") != std::string::npos)
                temp_is_write = 1;
            else {
                std::cerr << "Unresolved axi line:\n" << vp_file_line << "\n";
                exit(1);
            }

            uint64_t temp_addr;
            size_t pos_of_addr = vp_file_line.find_last_of('x') - 1;
            std::stringstream new_ss(vp_file_line.substr(pos_of_addr));
            new_ss >> std::hex >> temp_addr;

            axi_txn.is_write = temp_is_write;
            axi_txn.address = temp_addr;
            axi_txn.print_axi_txn();
            axi_txn.clear();

            continue;
        }
    }

    vp_trace_file.close();
}


void CompMemTrace(const std::string& file_1_name, const std::string& file_2_name) {
    std::ifstream file_1, file_2;
    file_1.open(file_1_name, std::ios::in);
    file_2.open(file_2_name, std::ios::in);

    std::vector<uint64_t> file_1_addr, file_2_addr;

    std::string file_line;
    while(getline(file_1, file_line)) {
        std::stringstream line_ss(file_line);

        uint64_t addr;
        line_ss >> std::hex >> addr;
        file_1_addr.push_back(addr);
    }
    std::sort(file_1_addr.begin(), file_1_addr.end());

    while(getline(file_2, file_line)) {
        std::stringstream line_ss(file_line);

        uint64_t addr;
        line_ss >> std::hex >> addr;
        file_2_addr.push_back(addr);
    }
    std::sort(file_2_addr.begin(), file_2_addr.end());

    if(file_1_addr.empty() || file_2_addr.empty()) {
        printf("At least one of the files is empty\n");
        exit(0);
    }

    if(file_1_addr.size() != file_2_addr.size()) {
        printf("file size not equal, file_1_addr_num = %zu, file_2_addr_num = %zu\n", file_1_addr.size(), file_2_addr.size());
        exit(0);
    }

    bool differ_found = false;
    for(size_t i = 0; i < file_1_addr.size(); i++) {
        if(file_1_addr[i] != file_2_addr[i]) {
            differ_found = true;
            printf("%lx\n", file_1_addr[i]);
        }
    }

    if(!differ_found)
        printf("No differences found\n");
}


void NVDLA_CPP_Log2MemTrace(const std::string& file_name, const int* print_flags) {
    std::ifstream log_file;
    log_file.open(file_name, std::ios::in);

    std::string file_line;
    while(getline(log_file, file_line)) {

        if(file_line.find("DBB: read request from dla, addr") != std::string::npos) {
            size_t pos_of_addr = file_line.find("addr") + 5;
            // printf("rd: 0x");

            if(print_flags[0]) {
                for(size_t i = pos_of_addr; i < pos_of_addr + 8; i++)
                    printf("%c", file_line[i]);
                printf("\n");
            }
            continue;
        }

        if(file_line.find("DBB: write request from dla, addr") != std::string::npos) {
            size_t pos_of_addr = file_line.find("addr") + 5;
            if(print_flags[1]) {
                for(size_t i = pos_of_addr; i < pos_of_addr + 8; i++)
                    printf("%c", file_line[i]);
                printf("\n");
            }

            continue;
        }
    }
}


int main(int argc, char** argv) {
    int print_flags[16];
    std::vector<std::string> in_files;
    parse_args(argc, argv, print_flags, in_files);

    switch(print_flags[4]) {
        case parse_vp_log:
            VPLog2Txn(in_files[0], print_flags);
            break;
        case comp_mem_trace:
            CompMemTrace(in_files[0], in_files[1]);
            break;
        case nvdla_cpp_log2mem_trace:
            NVDLA_CPP_Log2MemTrace(in_files[0], print_flags);
            break;
        default:
            printf("Error: invalid function type\n");
            exit(1);
    }

    return 0;
}
