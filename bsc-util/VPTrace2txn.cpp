#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <cassert>


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
                printf("wr: 0x%lx\n", address);
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


class InterruptManager {
public:
    bool is_interrupt_cleared;
    int num_CDMA_interrupts;
    int print_intr_flag;

    explicit InterruptManager(int flag) : is_interrupt_cleared(true), num_CDMA_interrupts(0), print_intr_flag(flag) {}

    void print_interrupt(const std::string& vp_line) {
        bool is_this_line_CDMA = false;
        if(vp_line.find("CDMA") != std::string::npos) {
            num_CDMA_interrupts++;
            is_this_line_CDMA = true;
        }

        if(print_intr_flag && (!(is_this_line_CDMA && num_CDMA_interrupts % 2 == 0))) {
            if(print_intr_flag) {
                if(!is_interrupt_cleared) {
                    printf("write_reg 0xffff0003 0xffffffff\t\t\t#NVDLA_GLB.S_INTR_STATUS_0\n"
                           "read_reg 0xffff0003 0xffffffff 0x0\t\t#NVDLA_GLB.S_INTR_STATUS_0\nwait\t\t\t\t\t\t# (%s)\n",
                           vp_line.c_str());
                } else {
                    printf("wait\t\t\t\t\t\t# (%s)\n", vp_line.c_str());
                }
            }

            is_interrupt_cleared = false;
        }
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

    bool inputting;

    InterruptManager* intr_mgr;

    CSB_Txn(int flag, InterruptManager* _int_mgr) : addr(0), data(0), is_write(0), n_posted(0), err_bit(0), exp_data(0),
    inputting(false), print_reg_txn_flag(flag), intr_mgr(_int_mgr) {}

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
        if(is_write && addr == 0x000c && data != 0)
            intr_mgr->is_interrupt_cleared = true;

        if(print_reg_txn_flag) {
            uint32_t out_addr =
                    0xffff0000 + (0x0000ffff & ((addr - 0) >> 2));  // write bit will be corrected by txn2verilator

            if (is_write) {
                printf("write_reg 0x%x 0x%08x\t\t\t#0x%04x\n", out_addr, data, addr);
                // printf("read_reg 0x%x 0xffffffff 0x%08x\t#0x%04x (write_verify)\n", out_addr, data, addr);
            } else {
                assert(data == 0x0);
                printf("read_reg 0x%x 0xffffffff 0x%08x\t#0x%04x\n", out_addr, exp_data,
                       addr);  // todo: the expected data and mask is to be verified
                // printf("read_reg 0x%x 0x00000000 0x%08x\t#0x%04x\n", out_addr, 0, addr);
            }
        }
    }

    void mark_using() {
        inputting = true;
    }
};


void parse_args(int argc, char** argv, int* flags) {
    for(int i = 0; i < 16; i++)
        flags[i] = 0;

    for(int i = 0; i < argc; i++) {
        if(strcmp(argv[i], "--print_read_data") == 0) {
            // print AXI data read addresses (aligned to 0x40)
            flags[0] = 1;
        } else if(strcmp(argv[i], "--print_write_data") == 0) {
            // print AXI data write addresses (aligned to 0x40)
            flags[1] = 1;
        } else if(strcmp(argv[i], "--print_reg_txn") == 0) {
            // print CSB register transactions
            flags[2] = 1;
        } else if(strcmp(argv[i], "--print_wait") == 0) {
            // print interrupt signals to inform CPU to wait
            flags[3] = 1;
        }
    }
}


int main(int argc, char** argv) {
    std::ifstream vp_trace_file;
    int print_flags[16];
    parse_args(argc, argv, print_flags);
    InterruptManager intr_mgr(print_flags[3]);
    AXI_Txn axi_txn(print_flags[0], print_flags[1]);
    CSB_Txn csb_txn(print_flags[2], &intr_mgr);


    vp_trace_file.open(argv[1], std::ios::in);


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

            std::cout << "Unresolved csb line:\n" << vp_file_line << "\n";
            exit(1);
        }

        if(vp_file_line.find("interrupt") != std::string::npos) {
            size_t pos_of_interrupt = vp_file_line.find_last_of(' ');
            std::string last_word = vp_file_line.substr(pos_of_interrupt + 1);
            if(last_word.find("interrupt") != std::string::npos)
                intr_mgr.print_interrupt(vp_file_line);

            continue;
        }

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

    return 0;
}
